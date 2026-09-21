#include "VolumeDeck.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include "XPLMPlanes.h"
#include "XPLMGraphics.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Initialize static members
VolumeDeck* VolumeDeck::instance = nullptr;
const float VolumeDeck::KNOB_RADIUS = 22.0f;
const float VolumeDeck::GAP_FIVE = 6.0f;
const float VolumeDeck::FIXED_TEXT_SPACE = 70.0f;
const float VolumeDeck::LABEL_FONT_SIZE = 15.0f;
const float VolumeDeck::ICON_SOUND_W   = 40.0f;
const float VolumeDeck::ICON_SAVE_CX   = -57.0f;
const float VolumeDeck::ICON_LAYOUT_CX = -83.0f;
const float VolumeDeck::ICON_DRAG_CX   = -105.0f;
const float VolumeDeck::ICON_HALF      = 8.0f;
const float VolumeDeck::DRAG_RADIUS    = 5.0f;
const float VolumeDeck::DRAG_HALF      = 7.0f;
// The row layout needs more air between knobs than the column does: they sit
// side by side with their labels underneath, so GAP_FIVE alone reads as cramped.
const float VolumeDeck::H_GAP          = 12.0f;

// Knob sweep: a conventional volume knob. The ring gap sits at the bottom, the
// pointer starts at 7:30 for 0.0 and runs CLOCKWISE through 12:00 to 4:30 for 1.0.
// GL angles increase counter-clockwise, so volume SUBTRACTS from the start angle.
const float KNOB_ANGLE_START = 225.0f;   // 7:30
const float KNOB_ANGLE_SWEEP = 270.0f;   // ends at -45 == 315 (4:30)

static float knobAngle(float volume) {
    float a = KNOB_ANGLE_START - volume * KNOB_ANGLE_SWEEP;
    while (a < 0.0f) a += 360.0f;
    return fmodf(a, 360.0f);
}

// Palette: dark + amber (classic avionics)
namespace Palette {
    const float PANEL_BG[4]   = { 34/255.0f,  36/255.0f,  42/255.0f, 1.00f};
    const float KNOB_FACE[4]  = { 64/255.0f,  68/255.0f,  76/255.0f, 1.00f};
    const float KNOB_RING[4]  = { 16/255.0f,  18/255.0f,  22/255.0f, 1.00f};
    const float POINTER[4]    = {255/255.0f, 179/255.0f,  64/255.0f, 1.00f};
    const float TICK_SECOND[4]= {127/255.0f, 196/255.0f, 255/255.0f, 1.00f};
    const float LABEL[4]      = {240/255.0f, 217/255.0f, 168/255.0f, 1.00f};
    const float SAVE_DIRTY[4] = {224/255.0f,  82/255.0f,  82/255.0f, 0.90f};
    const float SAVE_CLEAN[4] = { 76/255.0f, 199/255.0f, 110/255.0f, 0.90f};
    const float ICON_OUTLINE[4]={ 16/255.0f,  18/255.0f,  22/255.0f, 1.00f};
    const float ICON_IDLE[4]  = { 90/255.0f,  95/255.0f, 105/255.0f, 1.00f};
    const float ICON_ACTIVE[4]= {255/255.0f, 179/255.0f,  64/255.0f, 1.00f};
    const float DRAG_OUTER[4] = {240/255.0f, 217/255.0f, 168/255.0f, 0.55f};
    const float DRAG_INNER[4] = {255/255.0f, 179/255.0f,  64/255.0f, 0.85f};
    const float SAVE_SHADOW[4]= { 16/255.0f,  18/255.0f,  22/255.0f, 0.55f};
    const float DISABLED[4]   = {110/255.0f, 115/255.0f, 125/255.0f, 1.00f};
}

VolumeDeck* VolumeDeck::getInstance() {
    if (instance == nullptr) {
        instance = new VolumeDeck();
    }
    return instance;
}

VolumeDeck::VolumeDeck() 
    : mainX(0), mainY(0), snapMainX(0), snapMainY(0)
    , layout(LAYOUT_VERTICAL), font(nullptr), fontAttempts(0), currentColor(0), currentLineWidth(1.0f)
    , screenLeft(0), screenBottom(0), screenRight(0), screenTop(0)
    , screenWidth(0), screenHeight(0)
    , drawControlBox(false), screenSizeChanged(true)
    , saveRequired(true), autoPosition(true)
    , dragging(false), firstDraw(true)
    , prevView(0), initialTestStage(1)
{
    // Initialize knobs with dataref names
    const char* knobNames[] = {"master", "exterior", "interior", "pilot", "copilot", "radio", "enviro", "ui"};
    const char* datarefNames[] = {
        "sim/operation/sound/master_volume_ratio",
        "sim/operation/sound/exterior_volume_ratio",
        "sim/operation/sound/interior_volume_ratio",
        "sim/operation/sound/pilot_volume_ratio",
        "sim/operation/sound/copilot_volume_ratio",
        "sim/operation/sound/radio_volume_ratio",
        "sim/operation/sound/enviro_volume_ratio",
        "sim/operation/sound/ui_volume_ratio"
    };
    
    for (int i = 0; i < NUM_KNOBS; i++) {
        VolumeKnob knob;
        knob.name = knobNames[i];
        // "ui" reads badly as "Ui", so it gets the initialism it deserves.
        if (knob.name == "ui") {
            knob.displayName = "UI";
        } else {
            knob.displayName = knob.name;
            knob.displayName[0] = (char)toupper((unsigned char)knob.displayName[0]);
        }
        knob.x = 0;
        knob.y = 0;
        knob.textX = 0;
        knob.interiorVolume = 1.0f;
        knob.exteriorVolume = KNOB_SINGLE_MARK;
        knob.preMuteVolume = -1.0f;
        knob.dataRef = XPLMFindDataRef(datarefNames[i]);
        knobs.push_back(knob);
    }
    
    viewExternalDataRef = XPLMFindDataRef("sim/graphics/view/view_is_external");
}

VolumeDeck::~VolumeDeck() {
}

void VolumeDeck::initialize() {
    XPLMDebugString("VolumeDeck: [INIT] Starting initialization...\n");
    
    try {
        // Get screen dimensions (boxels, not pixels -- see header)
        XPLMDebugString("VolumeDeck: [INIT] Getting screen bounds...\n");
        readScreenBounds();
        char msg[256];
        snprintf(msg, sizeof(msg), "VolumeDeck: [INIT] Screen bounds (boxels): (%d,%d)-(%d,%d) = %dx%d\n",
                 screenLeft, screenBottom, screenRight, screenTop, screenWidth, screenHeight);
        XPLMDebugString(msg);
        
        snapMainX = screenRight - 10.0f;
        snapMainY = screenTop - 40.0f;
        mainX = snapMainX;
        mainY = snapMainY;
        
        snprintf(msg, sizeof(msg), "VolumeDeck: [INIT] Initial position: (%.1f, %.1f)\n", mainX, mainY);
        XPLMDebugString(msg);
        
        // No XPLMRegisterDrawCallback here: direct drawing is deprecated and runs in
        // pixel coordinates, which cannot agree with the boxel coordinates the mouse
        // handler window uses. Drawing is driven from the window callback in main.cpp.
        
        // Register flight loop callback
        XPLMDebugString("VolumeDeck: [INIT] Creating flight loop...\n");
        XPLMCreateFlightLoop_t flightLoopParams;
        flightLoopParams.structSize = sizeof(XPLMCreateFlightLoop_t);
        flightLoopParams.phase = xplm_FlightLoop_Phase_AfterFlightModel;
        flightLoopParams.callbackFunc = flightLoopCallback;
        flightLoopParams.refcon = this;
        
        XPLMFlightLoopID flightLoopID = XPLMCreateFlightLoop(&flightLoopParams);
        if (flightLoopID == nullptr) {
            XPLMDebugString("VolumeDeck: [ERROR] Failed to create flight loop!\n");
        } else {
            XPLMDebugString("VolumeDeck: [INIT] Scheduling flight loop...\n");
            XPLMScheduleFlightLoop(flightLoopID, 1.0f, 1);
        }
        
        XPLMDebugString("VolumeDeck: [INIT] Updating screen size...\n");
        updateScreenSize();
        
        // Must happen outside any draw callback.
        ensureFont();
        
        XPLMDebugString("VolumeDeck: [INIT] Initialization complete!\n");
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception during initialization!\n");
    }
}

void VolumeDeck::shutdown() {
    XPLMDebugString("VolumeDeck: Shutting down...\n");
    
    if (font != nullptr) {
        XPLMDestroyFont(font);
        font = nullptr;
    }
}

float VolumeDeck::flightLoopCallback(float elapsedSinceLastCall, float elapsedTimeSinceLastFlightLoop, 
                                       int counter, void* inRefcon) {
    try {
        VolumeDeck* vc = getInstance();
        
        // Font creation is illegal inside the draw callback, so retry it here.
        if (vc->font == nullptr) vc->ensureFont();
        
        // Handle screen size changes
        int prevW = vc->screenWidth, prevH = vc->screenHeight;
        vc->readScreenBounds();
        if (prevW != vc->screenWidth || prevH != vc->screenHeight) {
            char msg[256];
            snprintf(msg, sizeof(msg), "VolumeDeck: [LOOP] Screen size changed: %dx%d -> %dx%d\n", 
                    prevW, prevH, vc->screenWidth, vc->screenHeight);
            XPLMDebugString(msg);
            
            vc->snapMainX = vc->screenRight - 10.0f;
            vc->snapMainY = vc->screenTop - 40.0f;
            vc->screenSizeChanged = true;
            
            if (vc->autoPosition) {
                vc->mainX = vc->snapMainX;
                vc->mainY = vc->snapMainY;
            }
        }
        
        // Handle initialization tests
        if (vc->initialTestStage == 1) {
            XPLMDebugString("VolumeDeck: [LOOP] Running init test stage 1...\n");
            const float testValue = 0.03125f;
            for (int i = 0; i < NUM_KNOBS; i++) {
                vc->knobs[i].exteriorVolume = vc->getVolume(i);
                vc->setVolume(i, testValue);
            }
            vc->initialTestStage = 2;
        } else if (vc->initialTestStage == 2) {
            XPLMDebugString("VolumeDeck: [LOOP] Running init test stage 2...\n");
            const float testValue = 0.03125f;
            for (int i = 0; i < NUM_KNOBS; i++) {
                float current = vc->getVolume(i);
                float testResult = (fabs(current - testValue) < 0.001f) ? KNOB_SINGLE_MARK : KNOB_FAILED_TEST;
                float originalValue = vc->knobs[i].exteriorVolume;
                vc->setVolume(i, originalValue);
                vc->knobs[i].exteriorVolume = testResult;
            }
            vc->initialTestStage = 3;
        } else if (vc->initialTestStage == 3) {
            XPLMDebugString("VolumeDeck: [LOOP] Loading config (stage 3)...\n");
            vc->loadConfig();
            vc->initialTestStage = 0;
            XPLMDebugString("VolumeDeck: [LOOP] Initialization complete!\n");
        }
        
        // Commands and X-Plane's own sound menu both move volumes without
        // drawKnob() ever running, so refresh the cache here rather than
        // relying on the panel being open. Must precede the view-change
        // check below, which owns the knob values while a change is pending.
        if (vc->initialTestStage == 0) {
            for (int i = 0; i < NUM_KNOBS; i++) {
                vc->syncKnobFromDataRef(i, true);
            }
        }
        
        // Handle view changes (interior/exterior). Suppressed entirely while the
        // writability probe is in flight. Stage 1 parks each real volume in
        // exteriorVolume, which is >= 0 and therefore indistinguishable from
        // split mode to updateVolumesForViewChange() -- it would write that
        // stashed value back over the 0.03125 test value, so stage 2 would read
        // a mismatch and mark all eight knobs KNOB_FAILED_TEST (drawn grey and
        // refusing mode toggles). prevView is deliberately left untouched so the
        // change is re-detected on the next tick, once stage 3 has loaded the
        // config and there are real per-view volumes to apply.
        if (vc->initialTestStage == 0) {
            int currentView = XPLMGetDatai(vc->viewExternalDataRef);
            if (currentView != vc->prevView) {
                char msg[256];
                snprintf(msg, sizeof(msg), "VolumeDeck: [LOOP] View changed: %d -> %d\n", vc->prevView, currentView);
                XPLMDebugString(msg);
                vc->updateVolumesForViewChange();
                vc->prevView = currentView;
                vc->firstDraw = true;
            }
        }
        
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in flightLoopCallback!\n");
    }
    
    return 1.0f; // Call again in 1 second
}

void VolumeDeck::draw() {
    try {
        // Check for screen size changes on every draw
        int prevW = screenWidth, prevH = screenHeight;
        readScreenBounds();
        if (prevW != screenWidth || prevH != screenHeight) {
            snapMainX = screenRight - 10.0f;
            snapMainY = screenTop - 40.0f;
            
            if (autoPosition) {
                mainX = snapMainX;
                mainY = snapMainY;
            }
        }
        

        int mouseX, mouseY;
        XPLMGetMouseLocationGlobal(&mouseX, &mouseY);
        
        // Always draw if control box is visible or mouse is near the icon area
        if (drawControlBox || isMouseNearIcon(mouseX, mouseY)) {
            drawSoundIcon();
            
            if (drawControlBox) {
                drawControlPanel();
            }
        }
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in draw()!\n");
    }
}

void VolumeDeck::drawSoundIcon() {
    // Draw clickable sound icon
    // Outline
    setColor(Palette::ICON_OUTLINE);
    setLineWidth(1.0f);
    drawRectangle(mainX - 40, mainY - 8, mainX - 20, mainY + 8);
    drawTriangle(mainX - 40, mainY, mainX - 20, mainY + 15, mainX - 20, mainY - 15);
    
    // Idle fill
    setColor(Palette::ICON_IDLE);
    drawFilledRectangle(mainX - 39, mainY - 7, mainX - 21, mainY + 7);
    drawFilledTriangle(mainX - 37, mainY, mainX - 21, mainY + 13, mainX - 21, mainY - 13);
    
    if (drawControlBox) {
        drawDragIcon();
        drawLayoutIcon();
        drawSaveIcon();
        
        // Draw active sound icon
        setColor(Palette::ICON_ACTIVE);
        drawFilledRectangle(mainX - 38, mainY - 6, mainX - 22, mainY + 6);
        drawFilledTriangle(mainX - 34, mainY, mainX - 22, mainY + 11, mainX - 21, mainY - 11);
        
        // Draw sound waves
        setColor(Palette::LABEL);
        drawLine(mainX - 15, mainY + 5, mainX - 4, mainY + 10);
        drawLine(mainX - 15, mainY, mainX, mainY);
        drawLine(mainX - 15, mainY - 5, mainX - 4, mainY - 10);
    }
}

void VolumeDeck::drawControlPanel() {
    try {
        // Always update knob positions when drawing control panel
        updateKnobPositions();
        
        // Draw background workspace box
        setColor(Palette::PANEL_BG);
        float topBoxY = mainY - (4 * GAP_FIVE);
        float x1 = mainX - panelWidth();
        float y2 = topBoxY - panelHeight();
        drawFilledRectangle(x1, topBoxY, mainX, y2);
        
        // Draw all knobs
        setColor(Palette::KNOB_FACE);
        for (int i = 0; i < NUM_KNOBS; i++) {
            drawKnob(i);
        }
        
        firstDraw = false;
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in drawControlPanel!\n");
    }
}

void VolumeDeck::drawKnob(int knobIndex) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;

    try {
        VolumeKnob& knob = knobs[knobIndex];
        float x = knob.x;
        float y = knob.y;

        int currentView = XPLMGetDatai(viewExternalDataRef);

        syncKnobFromDataRef(knobIndex, !firstDraw);

        const bool locked = (knob.exteriorVolume == KNOB_FAILED_TEST);
        const bool split  = (knob.exteriorVolume >= 0.0f);

        // The value this knob is actually driving in the current view.
        float liveValue = (split && currentView != 0) ? knob.exteriorVolume : knob.interiorVolume;
        if (liveValue < 0.0f) liveValue = 0.0f;

        // Solid face. The old code filled an arc from the centre, which turned the
        // 90-degree dead zone into a wedge cut out of the disc (a "Pac-Man" knob).
        setColor(Palette::KNOB_FACE);
        drawFilledCircle(x, y, KNOB_RADIUS - 1.0f);

        // Unfilled portion of the travel, as a thin dark track.
        setColor(Palette::KNOB_RING);
        setLineWidth(3.0f);
        drawArc(x, y, KNOB_ANGLE_START - KNOB_ANGLE_SWEEP, KNOB_ANGLE_START, KNOB_RADIUS - 3.0f);

        // Filled portion: from the current position back round to the minimum.
        setColor(locked ? Palette::DISABLED : Palette::POINTER);
        setLineWidth(3.0f);
        drawArc(x, y, knobAngle(liveValue), KNOB_ANGLE_START, KNOB_RADIUS - 3.0f);

        // Outer rim, drawn last so it sits cleanly over the track ends.
        setColor(Palette::KNOB_RING);
        setLineWidth(1.5f);
        drawCircle(x, y, KNOB_RADIUS);

        // Pointer(s).
        if (locked) {
            setColor(Palette::DISABLED);
            drawAngleArrow(x, y, knobAngle(knob.interiorVolume), KNOB_RADIUS - 4.0f, KNOB_RADIUS * 0.35f, 1.5f);
        } else if (!split) {
            setColor(Palette::POINTER);
            drawAngleArrow(x, y, knobAngle(knob.interiorVolume), KNOB_RADIUS - 4.0f, KNOB_RADIUS * 0.30f, 2.5f);
        } else {
            // Separate interior/exterior: the live one is amber and full length, the
            // other is a short blue stub so you can see where it is parked.
            float liveAngle  = knobAngle(liveValue);
            float otherValue = (currentView == 0) ? knob.exteriorVolume : knob.interiorVolume;
            if (otherValue < 0.0f) otherValue = 0.0f;

            setColor(Palette::TICK_SECOND);
            drawTickMark(x, y, knobAngle(otherValue), KNOB_RADIUS - 4.0f, KNOB_RADIUS - 10.0f, 2.5f);

            setColor(Palette::POINTER);
            drawAngleArrow(x, y, liveAngle, KNOB_RADIUS - 4.0f, KNOB_RADIUS * 0.30f, 2.5f);
        }

        // Hub.
        setColor(Palette::KNOB_RING);
        drawFilledCircle(x, y, KNOB_RADIUS * 0.16f);

        setLineWidth(1.0f);

        // Label.
        drawString(knob.textX, knob.textY, knob.displayName.c_str());
    } catch (...) {
        char msg[128];
        snprintf(msg, sizeof(msg), "VolumeDeck: [ERROR] Exception in drawKnob(%d)!\n", knobIndex);
        XPLMDebugString(msg);
    }
}

// Four-way move arrow: the conventional "drag me" affordance. Replaces the plain
// dot, which gave no hint that the panel could be repositioned.
void VolumeDeck::drawDragIcon() {
    float cx = mainX + ICON_DRAG_CX;
    float cy = mainY;
    float r  = DRAG_HALF;
    float a  = 3.0f;   // arrowhead half-width
    float t  = r - a;  // where the shafts stop and the heads begin

    setColor(Palette::DRAG_OUTER);
    setLineWidth(1.5f);
    drawLine(cx - t, cy, cx + t, cy);
    drawLine(cx, cy - t, cx, cy + t);

    setColor(Palette::DRAG_INNER);
    drawFilledTriangle(cx + r, cy, cx + t, cy - a, cx + t, cy + a);   // right
    drawFilledTriangle(cx - r, cy, cx - t, cy - a, cx - t, cy + a);   // left
    drawFilledTriangle(cx, cy + r, cx - a, cy + t, cx + a, cy + t);   // up
    drawFilledTriangle(cx, cy - r, cx - a, cy - t, cx + a, cy - t);   // down

    setLineWidth(1.0f);
}

// Floppy-disk save icon, drawn around ICON_SAVE_CX so the art and the hit test in
// isOverSaveIcon() can never drift apart. Red body = unsaved changes, green = saved.
void VolumeDeck::drawSaveIcon() {
    float cx = mainX + ICON_SAVE_CX;
    float cy = mainY;
    float h  = ICON_HALF;

    setColor(saveRequired ? Palette::SAVE_DIRTY : Palette::SAVE_CLEAN);
    drawFilledRectangle(cx - h, cy - h, cx + h, cy + h);

    // Shutter at the top.
    setColor(Palette::SAVE_SHADOW);
    drawFilledRectangle(cx - h * 0.45f, cy + h * 0.15f, cx + h * 0.45f, cy + h * 0.8f);

    // Label plate at the bottom.
    setColor(Palette::LABEL);
    drawFilledRectangle(cx - h * 0.6f, cy - h * 0.8f, cx + h * 0.6f, cy - h * 0.1f);
}

// Two stacked bars for the column layout, two side-by-side for the row layout, so
// the icon previews what you are switching to.
void VolumeDeck::drawLayoutIcon() {
    float cx = mainX + ICON_LAYOUT_CX;
    float cy = mainY;

    setColor(Palette::ICON_OUTLINE);
    drawFilledRectangle(cx - 8, cy - 8, cx + 8, cy + 8);

    setColor(Palette::LABEL);
    if (layout == LAYOUT_VERTICAL) {
        // currently a column -> show a row
        drawFilledRectangle(cx - 6, cy - 5, cx - 1, cy + 5);
        drawFilledRectangle(cx + 1, cy - 5, cx + 6, cy + 5);
    } else {
        // currently a row -> show a column
        drawFilledRectangle(cx - 5, cy + 1, cx + 5, cy + 6);
        drawFilledRectangle(cx - 5, cy - 6, cx + 5, cy - 1);
    }
}

float VolumeDeck::getVolume(int knobIndex) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return 0.0f;
    if (knobs[knobIndex].dataRef == nullptr) return 0.0f;
    return XPLMGetDataf(knobs[knobIndex].dataRef);
}

void VolumeDeck::setVolume(int knobIndex, float value) {
    value = clamp(value, 0.0f, 1.0f);
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;
    if (knobs[knobIndex].dataRef == nullptr) return;
    XPLMSetDataf(knobs[knobIndex].dataRef, value);
}

bool VolumeDeck::isOverSoundIcon(int x, int y) const {
    return x >= (mainX - ICON_SOUND_W) && x <= mainX &&
           y >= (mainY - 15.0f) && y <= (mainY + 15.0f);
}

bool VolumeDeck::isOverSaveIcon(int x, int y) const {
    if (!drawControlBox) return false;
    return x >= (mainX + ICON_SAVE_CX - ICON_HALF) && x <= (mainX + ICON_SAVE_CX + ICON_HALF) &&
           y >= (mainY - ICON_HALF) && y <= (mainY + ICON_HALF);
}

bool VolumeDeck::isOverLayoutIcon(int x, int y) const {
    if (!drawControlBox) return false;
    return x >= (mainX + ICON_LAYOUT_CX - ICON_HALF) && x <= (mainX + ICON_LAYOUT_CX + ICON_HALF) &&
           y >= (mainY - ICON_HALF) && y <= (mainY + ICON_HALF);
}

bool VolumeDeck::isOverDragHandle(int x, int y) const {
    if (!drawControlBox) return false;
    return x >= (mainX + ICON_DRAG_CX - DRAG_HALF) && x <= (mainX + ICON_DRAG_CX + DRAG_HALF) &&
           y >= (mainY - DRAG_HALF) && y <= (mainY + DRAG_HALF);
}

bool VolumeDeck::isKnobLocked(int knobIndex) const {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return true;
    return knobs[knobIndex].exteriorVolume == KNOB_FAILED_TEST;
}

// Pulls the live dataref value back into the cached knob state. This used to live
// inside drawKnob(), which only runs while the panel is on screen -- so a volume
// changed by a command or by X-Plane's own sound menu left the cache stale, and
// updateVolumesForViewChange() would later write that stale value back over it.
void VolumeDeck::syncKnobFromDataRef(int knobIndex, bool flagChanges) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;

    VolumeKnob& knob = knobs[knobIndex];
    int currentView = XPLMGetDatai(viewExternalDataRef);

    // A view change is pending: updateVolumesForViewChange() owns the knob values
    // until it runs, and syncing now would latch the outgoing view onto the new one.
    if (prevView != currentView) return;

    float currentVal = getVolume(knobIndex);

    if (knob.exteriorVolume < 0 || currentView == 0) {
        if (flagChanges && fabs(knob.interiorVolume - currentVal) > 0.001f
                && knob.exteriorVolume != KNOB_FAILED_TEST) {
            saveRequired = true;
        }
        knob.interiorVolume = currentVal;
    } else {
        if (flagChanges && fabs(knob.exteriorVolume - currentVal) > 0.001f) {
            saveRequired = true;
        }
        knob.exteriorVolume = currentVal;
    }
}

void VolumeDeck::muteToggle(int knobIndex) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;
    if (!isReady() || isKnobLocked(knobIndex)) return;

    VolumeKnob& knob = knobs[knobIndex];

    if (knob.preMuteVolume >= 0.0f) {
        setVolume(knobIndex, knob.preMuteVolume);
        knob.preMuteVolume = -1.0f;
    } else {
        knob.preMuteVolume = getVolume(knobIndex);
        setVolume(knobIndex, 0.0f);
    }

    syncKnobFromDataRef(knobIndex, false);
    saveRequired = true;
}

void VolumeDeck::updateVolumesForViewChange() {
    int currentView = XPLMGetDatai(viewExternalDataRef);

    for (int i = 0; i < NUM_KNOBS; i++) {
        if (knobs[i].exteriorVolume >= 0) {
            if (currentView == 0) {
                // Interior view
                setVolume(i, knobs[i].interiorVolume);
            } else {
                // Exterior view
                setVolume(i, knobs[i].exteriorVolume);
            }
        }
    }
}

void VolumeDeck::readScreenBounds() {
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    screenWidth  = screenRight - screenLeft;
    screenHeight = screenTop - screenBottom;
}

void VolumeDeck::updateScreenSize() {
    readScreenBounds();
    snapMainX = screenRight - 10.0f;
    snapMainY = screenTop - 40.0f;
    screenSizeChanged = true;
}

// Panel extent measured from the mainX/mainY anchor, per layout.
float VolumeDeck::panelWidth() const {
    if (layout == LAYOUT_HORIZONTAL) {
        return NUM_KNOBS * (horizontalCell() + H_GAP) + H_GAP;
    }
    return KNOB_RADIUS * 2 + FIXED_TEXT_SPACE;
}

float VolumeDeck::panelHeight() const {
    if (layout == LAYOUT_HORIZONTAL) {
        // one row of knobs plus a label line underneath
        return KNOB_RADIUS * 2 + H_GAP * 2 + GAP_FIVE + LABEL_FONT_SIZE;
    }
    return NUM_KNOBS * KNOB_RADIUS * 2 + (NUM_KNOBS + 1) * GAP_FIVE;
}

// Width of one knob cell in the row layout. The knobs sit shoulder to shoulder
// with their labels underneath, so a cell has to be as wide as the widest label
// or the text runs together ("InteriorExteriorMaster").
float VolumeDeck::horizontalCell() const {
    float cell = KNOB_RADIUS * 2.0f;
    if (font != nullptr) {
        for (int i = 0; i < NUM_KNOBS; i++) {
            float w = XPLMFontMeasureString(font, LABEL_FONT_SIZE, knobs[i].displayName.c_str());
            if (w > cell) cell = w;
        }
    }
    return cell;
}

void VolumeDeck::toggleLayout() {
    layout = (layout == LAYOUT_VERTICAL) ? LAYOUT_HORIZONTAL : LAYOUT_VERTICAL;
    screenSizeChanged = true;
    saveRequired = true;
}

void VolumeDeck::updateKnobPositions() {
    float topBoxY = mainY - (4 * GAP_FIVE);

    if (layout == LAYOUT_HORIZONTAL) {
        // A single row running leftwards from the anchor, labels centred beneath.
        float cell = horizontalCell();
        float cy = topBoxY - H_GAP - KNOB_RADIUS;
        float cx = mainX - H_GAP - cell * 0.5f;
        for (int i = 0; i < NUM_KNOBS; i++) {
            knobs[i].x = cx;
            knobs[i].y = cy;
            knobs[i].textX = cx;
            knobs[i].textY = cy - KNOB_RADIUS - GAP_FIVE - LABEL_FONT_SIZE * 0.5f;
            cx -= (cell + H_GAP);
        }
        return;
    }

    // Vertical: a column down the right edge, labels in the strip to the left.
    float y = topBoxY - KNOB_RADIUS;
    float textX = mainX - KNOB_RADIUS * 2 - FIXED_TEXT_SPACE + 3;
    for (int i = 0; i < NUM_KNOBS; i++) {
        knobs[i].x = mainX - KNOB_RADIUS - 2;
        knobs[i].y = y - 1;
        knobs[i].textX = textX;
        knobs[i].textY = y - 1 - LABEL_FONT_SIZE * 0.35f;
        y = y - GAP_FIVE - KNOB_RADIUS * 2;
    }
}

float VolumeDeck::clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

bool VolumeDeck::isMouseNearIcon(int x, int y) {
    return (x >= (mainX - 100) && x <= (mainX + 100) &&
            y >= (mainY - 100) && y <= (mainY + 100));
}

bool VolumeDeck::isMouseOverKnob(int knobIndex, int x, int y) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return false;

    VolumeKnob& knob = knobs[knobIndex];
    // Vertical layout: the label strip to the left of the knob is part of its target.
    float leftPad = (layout == LAYOUT_VERTICAL) ? (KNOB_RADIUS + FIXED_TEXT_SPACE) : KNOB_RADIUS;
    return (x >= (knob.x - leftPad) &&
            x <= (knob.x + KNOB_RADIUS) &&
            y >= (knob.y - KNOB_RADIUS) &&
            y <= (knob.y + KNOB_RADIUS));
}

// Panel-graphics drawing helpers.
//
// XPLMPanelGraphics has no global colour or line-width state the way OpenGL did,
// so setColor()/setLineWidth() stash values here and each primitive passes them
// along. That keeps every call site in this file unchanged from the GL version.
void VolumeDeck::setColor(float r, float g, float b, float a) {
    currentColor = XPLMMakeColor(r, g, b, a);
}

void VolumeDeck::setColor(const float rgba[4]) {
    currentColor = XPLMMakeColor(rgba[0], rgba[1], rgba[2], rgba[3]);
}

void VolumeDeck::setLineWidth(float w) {
    currentLineWidth = w;
}

void VolumeDeck::drawRectangle(float x1, float y1, float x2, float y2) {
    XPLMVertex_t v[4] = { {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} };
    XPLMLineLoopWithWidth(currentColor, currentLineWidth, v, 4);
}

void VolumeDeck::drawFilledRectangle(float x1, float y1, float x2, float y2) {
    XPLMVertex_t v[4] = { {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} };
    XPLMPolygon(currentColor, v, 4);
}

void VolumeDeck::drawLine(float x1, float y1, float x2, float y2) {
    XPLMVertex_t v[2] = { {x1, y1}, {x2, y2} };
    XPLMLinesWithWidth(currentColor, currentLineWidth, v, 2);
}

void VolumeDeck::drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    XPLMVertex_t v[3] = { {x1, y1}, {x2, y2}, {x3, y3} };
    XPLMLineLoopWithWidth(currentColor, currentLineWidth, v, 3);
}

void VolumeDeck::drawFilledTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    XPLMVertex_t v[3] = { {x1, y1}, {x2, y2}, {x3, y3} };
    XPLMPolygon(currentColor, v, 3);
}

void VolumeDeck::drawCircle(float x, float y, float radius) {
    XPLMVertex_t v[CIRCLE_SEGMENTS];
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a = 2.0f * (float)M_PI * i / CIRCLE_SEGMENTS;
        v[i].x = x + cosf(a) * radius;
        v[i].y = y + sinf(a) * radius;
    }
    XPLMLineLoopWithWidth(currentColor, currentLineWidth, v, CIRCLE_SEGMENTS);
}

void VolumeDeck::drawFilledCircle(float x, float y, float radius) {
    XPLMVertex_t v[CIRCLE_SEGMENTS];
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a = 2.0f * (float)M_PI * i / CIRCLE_SEGMENTS;
        v[i].x = x + cosf(a) * radius;
        v[i].y = y + sinf(a) * radius;
    }
    XPLMPolygon(currentColor, v, CIRCLE_SEGMENTS);
}

// Shared by the arc helpers: fills pts with the arc samples and returns the count.
// startAngle/endAngle are degrees; the sweep always runs counter-clockwise from
// start to end, wrapping through 360 if end < start.
static int buildArc(float x, float y, float startAngle, float endAngle, float radius,
                    XPLMVertex_t* pts, int maxPts) {
    float range = endAngle - startAngle;
    if (range < 0.0f) range += 360.0f;

    int n = (int)(VolumeDeck::CIRCLE_SEGMENTS * range / 360.0f);
    if (n < 2) n = 2;
    if (n > maxPts - 1) n = maxPts - 1;

    for (int i = 0; i <= n; i++) {
        float a = (startAngle + range * i / n) * (float)M_PI / 180.0f;
        pts[i].x = x + cosf(a) * radius;
        pts[i].y = y + sinf(a) * radius;
    }
    return n + 1;
}

void VolumeDeck::drawArc(float x, float y, float startAngle, float endAngle, float radius) {
    XPLMVertex_t v[CIRCLE_SEGMENTS + 2];
    int n = buildArc(x, y, startAngle, endAngle, radius, v, CIRCLE_SEGMENTS + 2);
    XPLMLineStripWithWidth(currentColor, currentLineWidth, v, n);
}

void VolumeDeck::drawFilledArc(float x, float y, float startAngle, float endAngle, float radius) {
    XPLMVertex_t v[CIRCLE_SEGMENTS + 3];
    v[0].x = x;
    v[0].y = y;
    int n = buildArc(x, y, startAngle, endAngle, radius, v + 1, CIRCLE_SEGMENTS + 2);
    XPLMPolygon(currentColor, v, n + 1);
}

void VolumeDeck::drawAngleArrow(float x, float y, float angle, float outerRadius, float innerRadius, float width) {
    float a = angle * (float)M_PI / 180.0f;
    XPLMVertex_t v[2] = {
        {x + cosf(a) * innerRadius, y + sinf(a) * innerRadius},
        {x + cosf(a) * outerRadius, y + sinf(a) * outerRadius}
    };
    XPLMLinesWithWidth(currentColor, width, v, 2);
}

void VolumeDeck::drawTickMark(float x, float y, float angle, float outerRadius, float innerRadius, float width) {
    drawAngleArrow(x, y, angle, outerRadius, innerRadius, width);
}

// X-Plane ships Roboto in Resources/fonts, so there is nothing to bundle.
void VolumeDeck::ensureFont() {
    if (font != nullptr || fontAttempts >= 3) return;
    fontAttempts++;

    font = XPLMCreateFont(xplm_CharSetASCII);
    if (font == nullptr) {
        XPLMDebugString("VolumeDeck: [ERROR] XPLMCreateFont failed\n");
        return;
    }

    char sys[512];
    XPLMGetSystemPath(sys);
    std::string ttf = std::string(sys) + "Resources/fonts/Roboto-Regular.ttf";

    if (!XPLMFontAddFace(font, ttf.c_str())) {
        char msg[700];
        snprintf(msg, sizeof(msg), "VolumeDeck: [ERROR] Could not load font %s\n", ttf.c_str());
        XPLMDebugString(msg);
        XPLMDestroyFont(font);
        font = nullptr;
        return;
    }

    XPLMDebugString("VolumeDeck: [INIT] Font loaded\n");
}

void VolumeDeck::drawString(float x, float y, const char* text) {
    // No ensureFont() here. This runs inside the panel draw callback, and
    // XPLMCreateFont is rejected there ("Never call this function from within a
    // panel draw callback") -- it is fatal, not a no-op. The font is built in
    // initialize() and retried from the flight loop; if it is missing we skip text.
    if (font == nullptr) return;

    XPLMFontDrawString(font, XPLMMakeColor(Palette::LABEL[0], Palette::LABEL[1],
                                           Palette::LABEL[2], Palette::LABEL[3]),
                       LABEL_FONT_SIZE, x, y, text,
                       layout == LAYOUT_HORIZONTAL ? xplm_JustCenter : xplm_JustLeft);
}

std::string VolumeDeck::getConfigPath() {
    char path[512];
    XPLMGetSystemPath(path);
    std::string configPath = std::string(path) + "Output/preferences/VolumeDeck.dat";
    return configPath;
}

// Pre-rename settings file. Read-only: the first save writes the new name, so the
// old file is left untouched rather than deleted.
std::string VolumeDeck::getLegacyConfigPath() {
    char path[512];
    XPLMGetSystemPath(path);
    return std::string(path) + "Output/preferences/VolumeControl.dat";
}

std::string VolumeDeck::getAircraftFileName() {
    try {
        XPLMDebugString("VolumeDeck: [CONFIG] Getting aircraft filename...\n");
        
        char acfPath[256];
        char acfFile[256];
        XPLMGetNthAircraftModel(0, acfFile, acfPath);
        
        char msg[600];
        snprintf(msg, sizeof(msg), "VolumeDeck: [CONFIG] Aircraft: %s (path: %s)\n", acfFile, acfPath);
        XPLMDebugString(msg);
        
        // Extract just the filename from the path
        std::string filename = acfFile;
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            filename = filename.substr(lastSlash + 1);
        }
        
        snprintf(msg, sizeof(msg), "VolumeDeck: [CONFIG] Aircraft filename: %s\n", filename.c_str());
        XPLMDebugString(msg);
        
        return filename;
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in getAircraftFileName!\n");
        return "unknown.acf";
    }
}

void VolumeDeck::loadConfig() {
    XPLMDebugString("VolumeDeck: [CONFIG] Loading configuration...\n");
    
    std::string configPath = getConfigPath();
    char msg[600];
    snprintf(msg, sizeof(msg), "VolumeDeck: [CONFIG] Config path: %s\n", configPath.c_str());
    XPLMDebugString(msg);
    
    std::ifstream file(configPath);
    
    if (!file.is_open()) {
        // Fall back to the pre-rename file so existing per-aircraft volumes survive.
        std::string legacy = getLegacyConfigPath();
        file.open(legacy);
        if (file.is_open()) {
            snprintf(msg, sizeof(msg), "VolumeDeck: [CONFIG] Migrating settings from %s\n", legacy.c_str());
            XPLMDebugString(msg);
            saveRequired = true;   // first save rewrites it under the new name
        } else {
            XPLMDebugString("VolumeDeck: [CONFIG] No config file found (this is normal on first run)\n");
            return;
        }
    }
    
    XPLMDebugString("VolumeDeck: [CONFIG] Config file opened successfully\n");
    
    std::string line;
    int fileVersion = 0;
    std::string currentAircraft = getAircraftFileName();
    bool foundAircraft = false;
    
    while (std::getline(file, line)) {
        // Parse version
        if (fileVersion == 0 && line.find("VERSION") != std::string::npos) {
            sscanf(line.c_str(), "VERSION %d", &fileVersion);
            continue;
        }
        
        // Parse layout
        if (line.find("LAYOUT") != std::string::npos) {
            int lay = 0;
            if (sscanf(line.c_str(), "LAYOUT %d", &lay) == 1) {
                layout = (lay == 1) ? LAYOUT_HORIZONTAL : LAYOUT_VERTICAL;
            }
            continue;
        }
        
        // Parse position
        if (line.find("X:") != std::string::npos && line.find("Y:") != std::string::npos) {
            int x, y;
            if (sscanf(line.c_str(), "X:%d Y:%d", &x, &y) == 2) {
                if (x >= screenLeft && x <= screenRight && y >= screenBottom && y <= screenTop) {
                    mainX = x;
                    mainY = y;
                    autoPosition = false;
                    screenSizeChanged = true;
                }
            }
            continue;
        }
        
        // Parse aircraft data
        if (line.find(".acf") != std::string::npos) {
            size_t acfEnd = line.find(".acf") + 4;
            std::string acfName = line.substr(0, acfEnd);
            
            if (acfName == currentAircraft) {
                foundAircraft = true;
                std::string data = line.substr(acfEnd);
                std::istringstream iss(data);
                
                int knobNum = 0;
                float intVol, extVol;
                while (iss >> intVol >> extVol && knobNum < NUM_KNOBS) {
                    knobs[knobNum].interiorVolume = intVol;
                    // Builds before this fix wrote KNOB_FAILED_TEST (-2) to disk.
                    // Collapse any negative sentinel to single-value mode so an
                    // already-poisoned file heals itself on the next load rather
                    // than locking the knob grey forever.
                    knobs[knobNum].exteriorVolume =
                        (extVol < 0.0f) ? KNOB_SINGLE_MARK : extVol;
                    
                    int currentView = XPLMGetDatai(viewExternalDataRef);
                    if (currentView == 0 || knobs[knobNum].exteriorVolume < 0) {
                        setVolume(knobNum, knobs[knobNum].interiorVolume);
                    } else {
                        setVolume(knobNum, knobs[knobNum].exteriorVolume);
                    }
                    
                    knobNum++;
                }
                break;
            }
        }
    }
    
    file.close();
    
    if (foundAircraft && fileVersion > 1) {
        saveRequired = false;
    }
    
    char logMsg[300];
    snprintf(logMsg, sizeof(logMsg), "VolumeDeck: [CONFIG] Config loaded for %s\n", currentAircraft.c_str());
    XPLMDebugString(logMsg);
}

void VolumeDeck::saveConfig() {
    std::string configPath = getConfigPath();
    std::string currentAircraft = getAircraftFileName();
    
    // Read existing config
    std::string oldContent;
    std::ifstream inFile(configPath);
    if (inFile.is_open()) {
        std::stringstream buffer;
        buffer << inFile.rdbuf();
        oldContent = buffer.str();
        inFile.close();
    }
    
    // Build new config
    std::stringstream newContent;
    newContent << "VERSION " << FILE_FORMAT_VERSION << "\n";
    
    if (!autoPosition) {
        newContent << "X:" << (int)mainX << " Y:" << (int)mainY << "\n";
    }
    
    newContent << "LAYOUT " << (layout == LAYOUT_HORIZONTAL ? 1 : 0) << "\n";
    
    // Write current aircraft data
    newContent << currentAircraft;
    for (int i = 0; i < NUM_KNOBS; i++) {
        // KNOB_FAILED_TEST describes THIS run's probe, not a user setting -- the
        // same dataref may be writable under another aircraft or a later X-Plane
        // build. It must never reach the file: config load is stage 3, after the
        // probe, so a persisted -2 would overwrite a perfectly good probe result
        // and lock the knob grey on every future launch. Save it as single-value.
        float ext = knobs[i].exteriorVolume;
        if (ext == KNOB_FAILED_TEST) ext = KNOB_SINGLE_MARK;
        newContent << " " << knobs[i].interiorVolume << " " << ext;
    }
    newContent << "\n";
    
    // Copy other aircraft data from old config
    if (!oldContent.empty()) {
        std::istringstream iss(oldContent);
        std::string line;
        int fileVersion = 0;
        
        while (std::getline(iss, line)) {
            if (line.find("VERSION") != std::string::npos) {
                sscanf(line.c_str(), "VERSION %d", &fileVersion);
                continue;
            }
            
            if (line.find("X:") != std::string::npos) continue;
            if (line.find("LAYOUT") != std::string::npos) continue;
            
            if (line.find(".acf") != std::string::npos) {
                size_t acfEnd = line.find(".acf") + 4;
                std::string acfName = line.substr(0, acfEnd);
                
                if (acfName != currentAircraft) {
                    newContent << line << "\n";
                }
            }
        }
    }
    
    // Write to file
    std::ofstream outFile(configPath);
    if (outFile.is_open()) {
        outFile << newContent.str();
        outFile.close();
        saveRequired = false;
        
        char msg[300];
        snprintf(msg, sizeof(msg), "VolumeDeck: Config saved for %s\n", currentAircraft.c_str());
        XPLMDebugString(msg);
    } else {
        XPLMDebugString("VolumeDeck: Failed to save config\n");
    }
}

void VolumeDeck::toggleKnobMode(int knobIndex) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;
    
    VolumeKnob& knob = knobs[knobIndex];
    
    if (knob.exteriorVolume == KNOB_FAILED_TEST) {
        // Can't change unchangeable knob
        return;
    }
    
    if (knob.exteriorVolume == KNOB_SINGLE_MARK) {
        // Toggle Interior/Exterior enabled by setting exteriorVolume to current shared value
        knob.exteriorVolume = knob.interiorVolume;
    } else if (knob.exteriorVolume >= 0) {
        // Toggle Interior/Exterior disabled by setting exteriorVolume to KNOB_SINGLE_MARK
        int currentView = XPLMGetDatai(viewExternalDataRef);
        if (currentView == 0) {
            // Interior view
            knob.exteriorVolume = KNOB_SINGLE_MARK;
        } else {
            // Exterior view
            knob.interiorVolume = knob.exteriorVolume;
            knob.exteriorVolume = KNOB_SINGLE_MARK;
        }
    }
    
    saveRequired = true;
}

void VolumeDeck::adjustKnobVolume(int knobIndex, int clicks) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;
    if (!isReady()) return;
    
    float currentVolume = getVolume(knobIndex);
    float newVolume = currentVolume + (clicks * 0.02f);
    setVolume(knobIndex, newVolume);
    
    // An explicit adjustment cancels a mute: the stashed level is now stale.
    knobs[knobIndex].preMuteVolume = -1.0f;
    
    if (knobs[knobIndex].exteriorVolume != KNOB_FAILED_TEST) {
        saveRequired = true;
    }
    
    // saveRequired is already set above, so don't let the sync re-flag it.
    syncKnobFromDataRef(knobIndex, false);
}

void VolumeDeck::startDragging(int x, int y) {
    dragging = true;
    autoPosition = false;
}

void VolumeDeck::updateDragPosition(int x, int y) {
    if (!dragging) return;
    
    mainX = x + 95;
    mainY = y;
    screenSizeChanged = true;
    saveRequired = true;
    
    // Check if close enough to snap to default position
    if (mainX > snapMainX - 20 && mainX < snapMainX + 20 &&
        mainY > snapMainY - 15 && mainY < snapMainY + 15) {
        mainX = snapMainX;
        mainY = snapMainY;
        autoPosition = true;
    }
    
    // Keep widget on screen
    if ((mainX - panelWidth()) < screenLeft) {
        mainX = screenLeft + panelWidth();
    } else if (mainX > screenRight) {
        mainX = screenRight;
    }
    
    if (mainY - (4 * GAP_FIVE) - panelHeight() < screenBottom) {
        mainY = screenBottom + (4 * GAP_FIVE) + panelHeight();
    } else if ((mainY + 15) > screenTop) {
        mainY = screenTop - 15;
    }
}
