#include "VolumeDeck.h"
#include "VolumeCommands.h"
#include "SettingsWindow.h"
#include "XPLMDisplay.h"
#include "XPLMUtilities.h"
#include "XPLMPlugin.h"
#include "XPLMMenus.h"
#include "XPLMDefs.h"
#include <cstring>
#include <cstdint>   // intptr_t, for the menu item refcons

// Mouse callback state
static XPLMWindowID g_window = nullptr;
static bool g_mouseInWindow = false;

// Plugins menu. It is created in XPluginStart and destroyed in XPluginStop, so it
// stays clickable while the plugin is disabled in Plugin Admin -- hence g_enabled.
static XPLMMenuID g_menu = nullptr;
static int        g_menuItem = -1;
static bool       g_enabled = false;

enum MenuAction {
    MENU_SETTINGS = 0,
    MENU_TOGGLE_PANEL,
    MENU_SAVE
};

static void MenuHandler(void* inMenuRef, void* inItemRef);
static void CreateMenu();
static void DestroyMenu();

// Mouse callback functions
int MouseClickHandler(XPLMWindowID inWindowID, int x, int y, int inMouse, void* inRefcon);
int MouseWheelHandler(XPLMWindowID inWindowID, int x, int y, int wheel, int clicks, void* inRefcon);
XPLMCursorStatus CursorHandler(XPLMWindowID inWindowID, int x, int y, void* inRefcon);
void KeyHandler(XPLMWindowID inWindowID, char inKey, XPLMKeyFlags inFlags, char inVirtualKey,
                void* inRefcon, int losingFocus);
int RightClickHandler(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus inMouse, void* inRefcon);
void DrawWindowCallback(XPLMWindowID inWindowID, void* inRefcon);

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    strcpy(outName, "VolumeDeck");
    strcpy(outSig, "com.volumedeck.xplane");
    strcpy(outDesc, "Per-channel volume mixer with bindable commands");
    
    XPLMDebugString("============================================\n");
    XPLMDebugString("VolumeDeck: Plugin starting...\n");
    XPLMDebugString("VolumeDeck: Version " SOFTWARE_VERSION " (C++ Plugin)\n");
    XPLMDebugString("============================================\n");

    // macOS only, and it must come before anything that touches a path. Without
    // this, XPLMGetSystemPath returns a legacy HFS path ("T7 Shield:X-Plane 12:"),
    // which ensureFont() concatenates and hands straight back to X-Plane -- and a
    // bad directory character there is fatal, not an error return. It takes the
    // whole sim down and blames the plugin.
    //
    // Deliberately NOT enabled on Windows/Linux. Per XPLMPlugin.h, Linux returns
    // native paths either way, but Windows would switch from "C:\X-Plane 12\" to
    // "C:/X-Plane 12/". That spelling works fine, yet those two platforms ship
    // working builds today and this fix does not need them to change.
#if APL
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
#endif

    // Commands are created here rather than in XPluginEnable: they outlive the
    // plugin, and creating them early makes them visible to the joystick/keyboard
    // binding UI and the web API regardless of enable state.
    VolumeCommands::create();

    CreateMenu();

    return 1;
}

// Plugins > VolumeDeck. The note item is deliberately disabled: it is a label, not a
// command, and it answers the question the two save scopes would otherwise raise
// every time somebody changes an add-on level and then switches aircraft.
static void CreateMenu() {
    if (g_menu != nullptr) return;

    g_menuItem = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "VolumeDeck", nullptr, 0);
    if (g_menuItem < 0) {
        XPLMDebugString("VolumeDeck: [ERROR] Could not append plugins menu item\n");
        return;
    }

    g_menu = XPLMCreateMenu("VolumeDeck", XPLMFindPluginsMenu(), g_menuItem,
                            MenuHandler, nullptr);
    if (g_menu == nullptr) {
        XPLMDebugString("VolumeDeck: [ERROR] Could not create plugins menu\n");
        return;
    }

    XPLMAppendMenuItem(g_menu, "Settings...", (void*)MENU_SETTINGS, 0);
    XPLMAppendMenuItem(g_menu, "Show / Hide Panel", (void*)MENU_TOGGLE_PANEL, 0);
    XPLMAppendMenuSeparator(g_menu);
    XPLMAppendMenuItem(g_menu, "Save Now", (void*)MENU_SAVE, 0);

    int note = XPLMAppendMenuItem(
        g_menu, "Saves X-Plane levels for this aircraft, add-on levels globally",
        nullptr, 0);
    if (note >= 0) XPLMEnableMenuItem(g_menu, note, 0);

    XPLMDebugString("VolumeDeck: [INIT] Plugins menu created\n");
}

static void DestroyMenu() {
    if (g_menu != nullptr) {
        XPLMDestroyMenu(g_menu);
        g_menu = nullptr;
    }
    if (g_menuItem >= 0) {
        XPLMRemoveMenuItem(XPLMFindPluginsMenu(), g_menuItem);
        g_menuItem = -1;
    }
}

static void MenuHandler(void* /*inMenuRef*/, void* inItemRef) {
    try {
        // The menu outlives XPluginEnable. Acting while disabled would construct the
        // singleton on demand and operate on state that has never been initialised.
        if (!g_enabled) return;

        VolumeDeck* vc = VolumeDeck::getInstance();

        switch ((MenuAction)(intptr_t)inItemRef) {
            case MENU_SETTINGS:      SettingsWindow::toggle(); break;
            case MENU_TOGGLE_PANEL:  vc->toggleControlBox(); break;
            case MENU_SAVE:
                // Same guard the command handlers apply: during the ~3s startup probe
                // the knobs hold probe scratch, not the user's levels, and saving then
                // writes that scratch over their real config.
                if (vc->isReady()) vc->saveConfig();
                break;
        }
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in MenuHandler!\n");
    }
}

PLUGIN_API void XPluginStop(void) {
    XPLMDebugString("VolumeDeck: Plugin stopping...\n");
    DestroyMenu();
    VolumeDeck::getInstance()->shutdown();
}

PLUGIN_API int XPluginEnable(void) {
    XPLMDebugString("VolumeDeck: [ENABLE] Plugin enabled\n");
    
    try {
        XPLMDebugString("VolumeDeck: [ENABLE] Initializing VolumeDeck instance...\n");
        VolumeDeck::getInstance()->initialize();
        
        // Create an invisible window for mouse handling
        XPLMDebugString("VolumeDeck: [ENABLE] Creating mouse handler window...\n");
        // Boxels, not pixels: a modern window is positioned and receives mouse
        // coordinates in boxels. On a display with UI scaling these differ.
        int left, top, right, bottom;
        XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
        
        char msg[256];
        snprintf(msg, sizeof(msg), "VolumeDeck: [ENABLE] Window bounds (boxels): (%d,%d) to (%d,%d)\n",
                 left, top, right, bottom);
        XPLMDebugString(msg);
        
        // Zero-initialised on purpose. SDK 4.4.0 (X-Plane 12.4.4) adds contentType,
        // browserLoadFinishedFunc and browserLoadErrorFunc to this struct. We do not
        // set them, so if this is ever built against XPLM440 they must read as 0
        // (= xplm_WindowContentTypeOpenGL, null callbacks) rather than stack garbage.
        XPLMCreateWindow_t params = {};
        params.structSize = sizeof(params);
        params.left = left;
        params.top = top;
        params.right = right;
        params.bottom = bottom;
        params.visible = 1;
        params.drawWindowFunc = DrawWindowCallback;
        params.handleMouseClickFunc = MouseClickHandler;
        params.handleKeyFunc = KeyHandler;
        params.handleCursorFunc = CursorHandler;
        params.handleMouseWheelFunc = MouseWheelHandler;
        params.refcon = nullptr;
        params.decorateAsFloatingWindow = xplm_WindowDecorationNone;
        params.layer = xplm_WindowLayerFloatingWindows;
        // Draw with XPLMPanelGraphics rather than the legacy OpenGL bridge.
        params.contentType = xplm_WindowContentTypePanelGraphics;
        params.handleRightClickFunc = RightClickHandler;
        
        g_window = XPLMCreateWindowEx(&params);
        
        if (g_window == nullptr) {
            XPLMDebugString("VolumeDeck: [ERROR] Failed to create mouse handler window!\n");
            return 0;
        }
        
        VolumeCommands::registerHandlers();

        // After the sink window, so it is created in front of it -- both live in the
        // floating layer, and the sink spans the whole screen.
        SettingsWindow::create();

        g_enabled = true;

        XPLMDebugString("VolumeDeck: [ENABLE] Plugin enabled successfully\n");
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception during plugin enable!\n");
        return 0;
    }
    
    return 1;
}

PLUGIN_API void XPluginDisable(void) {
    XPLMDebugString("VolumeDeck: Plugin disabled\n");
    
    g_enabled = false;

    VolumeDeck::getInstance()->disable();

    VolumeCommands::unregisterHandlers();

    SettingsWindow::destroy();

    if (g_window) {
        XPLMDestroyWindow(g_window);
        g_window = nullptr;
    }
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void* inParam) {
    // Handle messages if needed
}

void DrawWindowCallback(XPLMWindowID inWindowID, void* inRefcon) {
    // This is where the panel is actually drawn. Drawing here rather than through
    // XPLMRegisterDrawCallback keeps rendering in the same boxel coordinate space as
    // the mouse events, so what is drawn and what is clickable always agree.
    static int lastLeft = 0, lastTop = 0, lastRight = 0, lastBottom = 0;
    
    int left, top, right, bottom;
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
    
    if (left != lastLeft || top != lastTop || right != lastRight || bottom != lastBottom) {
        XPLMSetWindowGeometry(inWindowID, left, top, right, bottom);
        lastLeft = left; lastTop = top; lastRight = right; lastBottom = bottom;
    }
    
    try {
        VolumeDeck::getInstance()->draw();
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in DrawWindowCallback!\n");
    }
}

int MouseClickHandler(XPLMWindowID inWindowID, int x, int y, int inMouse, void* inRefcon) {
    try {
        VolumeDeck* vc = VolumeDeck::getInstance();
        
        XPLMMouseStatus mouseStatus = (XPLMMouseStatus)inMouse;
        
        // Handle mouse up
        if (mouseStatus == xplm_MouseUp) {
            vc->stopDragging();
        }
        
        // Handle mouse down
        if (mouseStatus == xplm_MouseDown) {
            // Hit tests live on VolumeDeck next to the geometry that draws them,
            // so the icon art and its click target cannot drift apart.
            if (vc->isOverSoundIcon(x, y)) {
                vc->toggleControlBox();
                return 1;
            }
            
            if (vc->isOverSaveIcon(x, y)) {
                vc->saveConfig();
                return 1;
            }
            
            if (vc->isOverLayoutIcon(x, y)) {
                vc->toggleLayout();
                return 1;
            }
            
            if (vc->isOverDragHandle(x, y)) {
                vc->startDragging(x, y);
                return 1;
            }
            
            if (vc->isControlBoxVisible()) {
                for (int i = 0; i < vc->channelCount(); i++) {
                    if (vc->isMouseOverKnobPublic(i, x, y)) {
                        vc->toggleKnobMode(i);
                        return 1;
                    }
                }
            }
        }
    
        // Handle dragging
        if (mouseStatus == xplm_MouseDrag) {
            if (vc->isDragging()) {
                vc->updateDragPosition(x, y);
                return 1;
            }
        }
        
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in MouseClickHandler!\n");
    }
    
    return 0;
}

int MouseWheelHandler(XPLMWindowID inWindowID, int x, int y, int wheel, int clicks, void* inRefcon) {
    {
        char dbg[192];
        snprintf(dbg, sizeof(dbg), "VolumeDeck: [WHEEL] x=%d y=%d wheel=%d clicks=%d visible=%d main=(%.0f,%.0f)\n",
                 x, y, wheel, clicks, VolumeDeck::getInstance()->isControlBoxVisible() ? 1 : 0,
                 VolumeDeck::getInstance()->getMainX(), VolumeDeck::getInstance()->getMainY());
        XPLMDebugString(dbg);
    }
    try {
        VolumeDeck* vc = VolumeDeck::getInstance();
        
        if (!vc->isControlBoxVisible()) {
            return 0;
        }
        
        // No outer bounding box here: isMouseOverKnob() already bounds-checks each
        // knob, and a second hand-derived copy of the panel geometry only creates a
        // way for the two to disagree.
        for (int i = 0; i < vc->channelCount(); i++) {
            if (vc->isMouseOverKnobPublic(i, x, y)) {
                vc->adjustKnobVolume(i, clicks);
                return 1;
            }
        }
        
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in MouseWheelHandler!\n");
    }
    
    return 0;
}

XPLMCursorStatus CursorHandler(XPLMWindowID inWindowID, int x, int y, void* inRefcon) {
    // Return default cursor
    return xplm_CursorDefault;
}

// Explicit do-nothing handlers. SDK 4.4.0 documents these as optional (a NULL handler
// simply means the window never sees that event), but naming them makes it obvious
// that ignoring keys and right-clicks is deliberate rather than an oversight.
void KeyHandler(XPLMWindowID inWindowID, char inKey, XPLMKeyFlags inFlags, char inVirtualKey,
                void* inRefcon, int losingFocus) {
    // This window takes no keyboard focus.
}

int RightClickHandler(XPLMWindowID inWindowID, int x, int y, XPLMMouseStatus inMouse, void* inRefcon) {
    return 0;   // not consumed
}

