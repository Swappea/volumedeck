#include "VolumeDeck.h"
#include "VolumeCommands.h"
#include "XPLMDisplay.h"
#include "XPLMUtilities.h"
#include "XPLMPlugin.h"
#include "XPLMDefs.h"
#include <cstring>

// Mouse callback state
static XPLMWindowID g_window = nullptr;
static bool g_mouseInWindow = false;

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
    
    return 1;
}

PLUGIN_API void XPluginStop(void) {
    XPLMDebugString("VolumeDeck: Plugin stopping...\n");
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
        
        XPLMDebugString("VolumeDeck: [ENABLE] Plugin enabled successfully\n");
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception during plugin enable!\n");
        return 0;
    }
    
    return 1;
}

PLUGIN_API void XPluginDisable(void) {
    XPLMDebugString("VolumeDeck: Plugin disabled\n");
    
    VolumeCommands::unregisterHandlers();
    
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
                for (int i = 0; i < 8; i++) {
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
        for (int i = 0; i < 8; i++) {
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

