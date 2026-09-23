#include "VolumeDeck.h"
#include "Palette.h"
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

// Every loop that used to run 0..7 now runs the whole channel registry. knobs is
// built one-to-one from Channels::DEFS and never grows or shrinks, so an index is
// valid for the life of the process even when the channel is missing or switched off.
static const int NUM_KNOBS = Channels::COUNT;

const float VolumeDeck::KNOB_RADIUS = 22.0f;
const float VolumeDeck::GAP_FIVE = 6.0f;
const float VolumeDeck::FIXED_TEXT_SPACE = 70.0f;
const float VolumeDeck::LABEL_FONT_SIZE = 15.0f;
// Owner-plugin caption over an add-on group ("X-ATC-Chatter"). Smaller and quieter
// than a channel label so it reads as a heading rather than another control.
const float VolumeDeck::CAPTION_FONT_SIZE = 11.0f;
// Space between the sim group and an add-on group, with the divider down the middle.
const float VolumeDeck::GROUP_GAP = 10.0f;
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
    , drawControlBox(false), groupsDirty(true), screenSizeChanged(true)
    , saveRequired(true), autoPosition(true)
    , dragging(false), firstDraw(true)
    , prevView(0), initialTestStage(1)
{
    // One knob per registry entry, in registry order. Display names live in the
    // registry too, so "ui" keeps the initialism it deserves rather than becoming "Ui".
    for (int i = 0; i < NUM_KNOBS; i++) {
        const ChannelDef& def = Channels::get(i);

        VolumeKnob knob;
        knob.def         = &def;
        knob.name        = def.slug;
        knob.displayName = def.display;
        knob.ownerName   = (def.owner != nullptr) ? def.owner : "";
        knob.isAddon     = (def.kind == CH_ADDON);
        knob.x = 0;
        knob.y = 0;
        knob.textX = 0;
        knob.textY = 0;
        knob.hitPadLeft = 0;
        knob.interiorVolume = 1.0f;
        knob.exteriorVolume = KNOB_SINGLE_MARK;
        knob.preMuteVolume = -1.0f;
        knob.probeStage = 0;
        knob.probeStash = 0.0f;
        knob.probed = false;
        knob.hasStoredValue = false;
        // Add-ons default to controlled once detected; Settings is how you opt out.
        knob.enabled = true;

        knob.dataRef = XPLMFindDataRef(def.dataref);
        // A sim channel is always there. An add-on's dataref belongs to another
        // plugin, so a null here means nothing yet -- plugin load order is not
        // guaranteed and resolveAddonDataRefs() keeps looking from the flight loop.
        knob.available = (knob.dataRef != nullptr);

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

        // panelWidth()/panelHeight() read the group list, and updateDragPosition()
        // clamps against them -- so it must not be empty before the first draw.
        buildGroups();
        
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
        
        // A third-party dataref may not exist when we enable -- plugin load order is
        // not guaranteed, and the add-on may even be installed but disabled. Keep
        // looking; it is a handful of string lookups once a second.
        vc->resolveAddonDataRefs();

        // Handle initialization tests. Sim channels only: an add-on channel runs its
        // own two-tick probe from serviceKnobProbes(), because it may not have existed
        // when these stages ran.
        if (vc->initialTestStage == 1) {
            XPLMDebugString("VolumeDeck: [LOOP] Running init test stage 1...\n");
            const float testValue = 0.03125f;
            for (int i = 0; i < NUM_KNOBS; i++) {
                if (vc->knobs[i].isAddon) continue;
                vc->knobs[i].exteriorVolume = vc->getVolume(i);
                vc->setVolume(i, testValue);
            }
            vc->initialTestStage = 2;
        } else if (vc->initialTestStage == 2) {
            XPLMDebugString("VolumeDeck: [LOOP] Running init test stage 2...\n");
            const float testValue = 0.03125f;
            for (int i = 0; i < NUM_KNOBS; i++) {
                if (vc->knobs[i].isAddon) continue;
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

        // Add-on probes run on their own clock: one may be discovered long after the
        // stages above have finished, or re-armed when the user switches it on.
        vc->serviceKnobProbes();
        
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

        // Group furniture: a rule separating each add-on group from what precedes it,
        // and the owner plugin's name over the group so it is obvious whose volume
        // that knob is. Positions come from updateKnobPositions() -- nothing here
        // recomputes geometry.
        for (size_t g = 0; g < groups.size(); g++) {
            const PanelGroup& grp = groups[g];
            if (!grp.hasDivider) continue;

            setColor(Palette::DIVIDER);
            setLineWidth(1.0f);
            if (layout == LAYOUT_HORIZONTAL) {
                drawLine(x1 + GAP_FIVE, grp.dividerPos, mainX - GAP_FIVE, grp.dividerPos);
            } else {
                drawLine(grp.dividerPos, topBoxY - GAP_FIVE, grp.dividerPos, y2 + GAP_FIVE);
            }
        }

        for (size_t g = 0; g < groups.size(); g++) {
            const PanelGroup& grp = groups[g];
            if (grp.caption.empty()) continue;
            drawText(grp.captionX, grp.captionY, grp.caption.c_str(),
                     CAPTION_FONT_SIZE, Palette::MUTED, xplm_JustCenter);
        }

        // Draw the knobs
        setColor(Palette::KNOB_FACE);
        for (size_t g = 0; g < groups.size(); g++) {
            for (size_t k = 0; k < groups[g].knobIndices.size(); k++) {
                drawKnob(groups[g].knobIndices[k]);
            }
        }
        
        firstDraw = false;
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in drawControlPanel!\n");
    }
}

void VolumeDeck::drawKnob(int knobIndex) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;
    if (!isChannelDrawable(knobIndex)) return;

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

// Everything above this pair works in normalised 0..1. The registry's minValue and
// maxValue are the only place a channel's real units are known, so an add-on that
// stores 0..100 needs nothing but its numbers in Channels.cpp.
float VolumeDeck::getVolume(int knobIndex) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return 0.0f;
    const VolumeKnob& knob = knobs[knobIndex];
    if (knob.dataRef == nullptr) return 0.0f;

    float span = knob.def->maxValue - knob.def->minValue;
    if (span <= 0.0f) return 0.0f;
    return clamp((XPLMGetDataf(knob.dataRef) - knob.def->minValue) / span, 0.0f, 1.0f);
}

void VolumeDeck::setVolume(int knobIndex, float value) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;
    VolumeKnob& knob = knobs[knobIndex];
    if (knob.dataRef == nullptr) return;

    // An add-on dataref belongs to another plugin. If the user has not switched that
    // channel on in Settings we never write to it -- not on load, not on a view
    // change, not from a command.
    if (knob.isAddon && !knob.enabled) return;

    writeVolumeRaw(knobIndex, value);
}

void VolumeDeck::writeVolumeRaw(int knobIndex, float value) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;
    VolumeKnob& knob = knobs[knobIndex];
    if (knob.dataRef == nullptr) return;

    value = clamp(value, 0.0f, 1.0f);
    XPLMSetDataf(knob.dataRef,
                 knob.def->minValue + value * (knob.def->maxValue - knob.def->minValue));
}

// --- channel state, for main.cpp, VolumeCommands and the settings window ---

bool VolumeDeck::isChannelAvailable(int index) const {
    if (index < 0 || index >= NUM_KNOBS) return false;
    return knobs[index].available;
}

bool VolumeDeck::isChannelEnabled(int index) const {
    if (index < 0 || index >= NUM_KNOBS) return false;
    return knobs[index].enabled;
}

bool VolumeDeck::isChannelDrawable(int index) const {
    if (index < 0 || index >= NUM_KNOBS) return false;
    return knobs[index].available && knobs[index].enabled;
}

bool VolumeDeck::isChannelControllable(int index) const {
    if (index < 0 || index >= NUM_KNOBS) return false;
    if (!knobs[index].available) return false;
    return !knobs[index].isAddon || knobs[index].enabled;
}

float VolumeDeck::getChannelVolume(int index) {
    return getVolume(index);
}

void VolumeDeck::setChannelEnabled(int index, bool on) {
    if (index < 0 || index >= NUM_KNOBS) return;
    VolumeKnob& knob = knobs[index];
    if (knob.enabled == on) return;

    knob.enabled = on;
    saveRequired = true;
    groupsDirty = true;
    screenSizeChanged = true;   // the panel changes size when a knob comes or goes

    char msg[160];
    snprintf(msg, sizeof(msg), "VolumeDeck: [SETTINGS] Channel %s %s\n",
             knob.name.c_str(), on ? "enabled" : "disabled");
    XPLMDebugString(msg);

    // Switching an add-on on for the first time is the moment we are allowed to touch
    // its dataref, so that is when its writability probe runs.
    if (on && knob.isAddon && knob.available && knob.exteriorVolume == KNOB_SINGLE_MARK
            && knob.probeStage == 0 && !knob.probed) {
        knob.probeStage = 1;
    }
}

// Third-party datarefs are owned by another plugin, which may load after we do, may
// not be installed at all, or may be switched off in X-Plane's plugin admin. So the
// lookup is retried from the flight loop rather than done once at startup.
void VolumeDeck::resolveAddonDataRefs() {
    for (int i = 0; i < NUM_KNOBS; i++) {
        VolumeKnob& knob = knobs[i];
        if (!knob.isAddon || knob.available) continue;

        knob.dataRef = XPLMFindDataRef(knob.def->dataref);
        if (knob.dataRef == nullptr) continue;

        knob.available = true;
        groupsDirty = true;

        char msg[400];
        snprintf(msg, sizeof(msg), "VolumeDeck: [ADDON] Found %s (%s) -> %s\n",
                 knob.def->dataref, knob.ownerName.c_str(),
                 knob.enabled ? "controlling" : "not controlling (off in Settings)");
        XPLMDebugString(msg);

        if (knob.enabled && !knob.probed) knob.probeStage = 1;
    }
}

// The two-tick writability probe, per knob. The global one in flightLoopCallback()
// cannot serve add-ons: their datarefs may appear minutes later. Note the separate
// probeStash field -- parking the real volume in exteriorVolume is what once made
// updateVolumesForViewChange() write it back over the test value and lock every knob.
void VolumeDeck::serviceKnobProbes() {
    const float testValue = 0.03125f;

    for (int i = 0; i < NUM_KNOBS; i++) {
        VolumeKnob& knob = knobs[i];
        if (knob.probeStage == 0) continue;

        if (!knob.available || !knob.enabled) {
            // Switched off mid-probe. Stage 2 means the test value is sitting in the
            // dataref right now, so put the real level back before walking away.
            if (knob.probeStage == 2 && knob.available) {
                writeVolumeRaw(i, knob.probeStash);
                knob.interiorVolume = knob.probeStash;
            }
            knob.probeStage = 0;
            continue;
        }

        if (knob.probeStage == 1) {
            knob.probeStash = getVolume(i);
            setVolume(i, testValue);
            knob.probeStage = 2;
            continue;
        }

        // Stage 2: did the write stick?
        bool writable = fabs(getVolume(i) - testValue) < 0.001f;
        knob.probeStage = 0;
        knob.probed = true;

        if (!writable) {
            setVolume(i, knob.probeStash);
            knob.interiorVolume = knob.probeStash;
            knob.exteriorVolume = KNOB_FAILED_TEST;
        } else if (knob.hasStoredValue) {
            // We have a saved level for this channel: apply it now that we finally can.
            int currentView = XPLMGetDatai(viewExternalDataRef);
            float wanted = (knob.exteriorVolume >= 0.0f && currentView != 0)
                         ? knob.exteriorVolume : knob.interiorVolume;
            setVolume(i, wanted);
        } else {
            // Nothing saved -- adopt whatever the add-on is already set to rather than
            // yanking the user's level to our default on first sight.
            setVolume(i, knob.probeStash);
            knob.interiorVolume = knob.probeStash;
            knob.exteriorVolume = KNOB_SINGLE_MARK;
        }

        char msg[200];
        snprintf(msg, sizeof(msg), "VolumeDeck: [ADDON] Probe for %s: %s\n",
                 knob.name.c_str(), writable ? "writable" : "NOT writable (knob locked)");
        XPLMDebugString(msg);
    }
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
    if (!isChannelControllable(knobIndex)) return true;
    return knobs[knobIndex].exteriorVolume == KNOB_FAILED_TEST;
}

// Pulls the live dataref value back into the cached knob state. This used to live
// inside drawKnob(), which only runs while the panel is on screen -- so a volume
// changed by a command or by X-Plane's own sound menu left the cache stale, and
// updateVolumesForViewChange() would later write that stale value back over it.
void VolumeDeck::syncKnobFromDataRef(int knobIndex, bool flagChanges) {
    if (knobIndex < 0 || knobIndex >= NUM_KNOBS) return;

    VolumeKnob& knob = knobs[knobIndex];

    // Nothing to read from a dataref that does not exist, an add-on we were told not
    // to touch, or a knob whose probe currently has the test value in the dataref.
    if (!knob.available || (knob.isAddon && !knob.enabled) || knob.probeStage != 0) return;

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
    if (!isChannelControllable(knobIndex) || knobs[knobIndex].probeStage != 0) return;

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
        if (!knobs[i].available || knobs[i].probeStage != 0) continue;
        if (knobs[i].isAddon && !knobs[i].enabled) continue;

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

// The visible knobs, split into the sim group and one group per owning add-on
// plugin. Rebuilt on every layout pass, because Settings can switch a channel on or
// off and an add-on can be discovered, at any moment.
void VolumeDeck::buildGroups() {
    groupsDirty = false;
    groups.clear();

    PanelGroup sim;
    sim.labelWidth = FIXED_TEXT_SPACE;
    sim.captionX = sim.captionY = 0.0f;
    sim.dividerPos = 0.0f;
    sim.hasDivider = false;
    for (int i = 0; i < NUM_KNOBS; i++) {
        if (knobs[i].isAddon || !isChannelDrawable(i)) continue;
        sim.knobIndices.push_back(i);
    }
    if (!sim.knobIndices.empty()) groups.push_back(sim);

    // One group per owning plugin rather than per knob, so a plugin that exposes
    // several channels gets a single caption over the lot.
    for (int i = 0; i < NUM_KNOBS; i++) {
        if (!knobs[i].isAddon || !isChannelDrawable(i)) continue;
        if (knobs[i].ownerName.empty()) continue;

        int found = -1;
        for (size_t g = 0; g < groups.size(); g++) {
            if (!groups[g].caption.empty() && groups[g].caption == knobs[i].ownerName) {
                found = (int)g;
                break;
            }
        }
        if (found < 0) {
            PanelGroup fresh;
            fresh.caption = knobs[i].ownerName;
            fresh.labelWidth = 0.0f;
            fresh.captionX = fresh.captionY = 0.0f;
            fresh.dividerPos = 0.0f;
            fresh.hasDivider = true;
            groups.push_back(fresh);
            found = (int)groups.size() - 1;
        }
        groups[found].knobIndices.push_back(i);
    }

    // An add-on column only needs to be as wide as its own labels -- and as its
    // caption, or "X-ATC-Chatter" would hang off the end of a one-knob column.
    for (size_t g = 0; g < groups.size(); g++) {
        if (groups[g].caption.empty()) continue;

        float w = 0.0f;
        for (size_t k = 0; k < groups[g].knobIndices.size(); k++) {
            float m = measureText(knobs[groups[g].knobIndices[k]].displayName.c_str(), LABEL_FONT_SIZE);
            if (m > w) w = m;
        }
        w += GAP_FIVE * 2.0f;

        float capOverhang = measureText(groups[g].caption.c_str(), CAPTION_FONT_SIZE)
                          + GAP_FIVE - KNOB_RADIUS * 2.0f;
        if (capOverhang > w) w = capOverhang;

        groups[g].labelWidth = w;
    }
}

// Vertical space reserved above every column for the owner captions. Reserved on all
// columns at once, including the sim column that has no caption, so the knob rows
// still line up across the panel.
float VolumeDeck::captionHeight() const {
    for (size_t g = 0; g < groups.size(); g++) {
        if (!groups[g].caption.empty()) return CAPTION_FONT_SIZE + GAP_FIVE;
    }
    return 0.0f;
}

// Panel extent measured from the mainX/mainY anchor, per layout.
float VolumeDeck::panelWidth() const {
    if (groups.empty()) return KNOB_RADIUS * 2 + FIXED_TEXT_SPACE;

    if (layout == LAYOUT_HORIZONTAL) {
        // The widest row decides, and rows are right-aligned under the anchor.
        size_t widest = 0;
        for (size_t g = 0; g < groups.size(); g++) {
            if (groups[g].knobIndices.size() > widest) widest = groups[g].knobIndices.size();
        }
        return widest * (horizontalCell() + H_GAP) + H_GAP;
    }

    float w = 0.0f;
    for (size_t g = 0; g < groups.size(); g++) {
        w += KNOB_RADIUS * 2 + groups[g].labelWidth;
        if (g + 1 < groups.size()) w += GROUP_GAP;
    }
    return w;
}

float VolumeDeck::panelHeight() const {
    if (groups.empty()) return KNOB_RADIUS * 2 + (2 * GAP_FIVE);

    if (layout == LAYOUT_HORIZONTAL) {
        // One row of knobs per group, each with a label line underneath and, for an
        // add-on group, a caption line above.
        float h = H_GAP;
        for (size_t g = 0; g < groups.size(); g++) {
            h += KNOB_RADIUS * 2 + GAP_FIVE + LABEL_FONT_SIZE + H_GAP;
            if (!groups[g].caption.empty()) h += captionHeight();
        }
        return h;
    }

    size_t tallest = 0;
    for (size_t g = 0; g < groups.size(); g++) {
        if (groups[g].knobIndices.size() > tallest) tallest = groups[g].knobIndices.size();
    }
    return captionHeight() + tallest * KNOB_RADIUS * 2 + (tallest + 1) * GAP_FIVE;
}

// Width of one knob cell in the row layout. The knobs sit shoulder to shoulder
// with their labels underneath, so a cell has to be as wide as the widest label
// or the text runs together ("InteriorExteriorMaster"). Measured across every
// visible channel so cells stay the same size from row to row.
float VolumeDeck::horizontalCell() const {
    float cell = KNOB_RADIUS * 2.0f;
    for (int i = 0; i < NUM_KNOBS; i++) {
        if (!isChannelDrawable(i)) continue;
        float w = measureText(knobs[i].displayName.c_str(), LABEL_FONT_SIZE);
        if (w > cell) cell = w;
    }
    return cell;
}

void VolumeDeck::toggleLayout() {
    layout = (layout == LAYOUT_VERTICAL) ? LAYOUT_HORIZONTAL : LAYOUT_VERTICAL;
    groupsDirty = true;   // column widths are per layout
    screenSizeChanged = true;
    saveRequired = true;
}

void VolumeDeck::updateKnobPositions() {
    if (groupsDirty) buildGroups();

    float topBoxY = mainY - (4 * GAP_FIVE);
    float capH = captionHeight();

    if (layout == LAYOUT_HORIZONTAL) {
        // One row per group, each running leftwards from the anchor with its labels
        // centred beneath, and an add-on group's owner caption centred above.
        float cell = horizontalCell();
        float rowTop = topBoxY - H_GAP;

        for (size_t g = 0; g < groups.size(); g++) {
            PanelGroup& grp = groups[g];
            bool captioned = !grp.caption.empty();

            if (captioned) {
                grp.hasDivider = true;
                grp.dividerPos = rowTop + H_GAP * 0.5f;
                rowTop -= capH;
            } else {
                grp.hasDivider = false;
            }

            float cy = rowTop - KNOB_RADIUS;
            float cx = mainX - H_GAP - cell * 0.5f;
            float firstCx = cx, lastCx = cx;

            for (size_t k = 0; k < grp.knobIndices.size(); k++) {
                VolumeKnob& knob = knobs[grp.knobIndices[k]];
                knob.x = cx;
                knob.y = cy;
                knob.textX = cx;
                knob.textY = cy - KNOB_RADIUS - GAP_FIVE - LABEL_FONT_SIZE * 0.5f;
                knob.hitPadLeft = KNOB_RADIUS;
                lastCx = cx;
                cx -= (cell + H_GAP);
            }

            grp.captionX = (firstCx + lastCx) * 0.5f;
            grp.captionY = rowTop + GAP_FIVE * 0.5f;

            rowTop -= KNOB_RADIUS * 2 + GAP_FIVE + LABEL_FONT_SIZE + H_GAP;
        }
        return;
    }

    // Vertical: one column per group, laid right to left from the anchor, each with
    // its labels in the strip to the left of its knobs.
    float right = mainX;

    for (size_t g = 0; g < groups.size(); g++) {
        PanelGroup& grp = groups[g];
        float colW = KNOB_RADIUS * 2 + grp.labelWidth;
        float knobCx = right - KNOB_RADIUS - 2;
        float textX  = right - colW + 3;
        float y = topBoxY - capH - KNOB_RADIUS;

        for (size_t k = 0; k < grp.knobIndices.size(); k++) {
            VolumeKnob& knob = knobs[grp.knobIndices[k]];
            knob.x = knobCx;
            knob.y = y - 1;
            knob.textX = textX;
            knob.textY = y - 1 - LABEL_FONT_SIZE * 0.35f;
            knob.hitPadLeft = KNOB_RADIUS + grp.labelWidth;
            y = y - GAP_FIVE - KNOB_RADIUS * 2;
        }

        grp.captionX  = right - colW * 0.5f;
        grp.captionY  = topBoxY - capH + GAP_FIVE * 0.5f;
        grp.hasDivider = !grp.caption.empty();
        grp.dividerPos = right + GROUP_GAP * 0.5f;

        right -= colW + GROUP_GAP;
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
    // A knob that is not on screen has no click target, and its x/y are stale.
    if (!isChannelDrawable(knobIndex)) return false;

    VolumeKnob& knob = knobs[knobIndex];
    // Vertical layout: the label strip to the left of the knob is part of its target,
    // and each column sizes its own strip -- so the pad is whatever the layout pass
    // reserved for this knob, not a panel-wide constant.
    float leftPad = (knob.hitPadLeft > 0.0f) ? knob.hitPadLeft : KNOB_RADIUS;
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

    // Label widths were estimates until now, so the columns need re-measuring.
    groupsDirty = true;
    XPLMDebugString("VolumeDeck: [INIT] Font loaded\n");
}

void VolumeDeck::drawString(float x, float y, const char* text) {
    drawText(x, y, text, LABEL_FONT_SIZE, Palette::LABEL,
             layout == LAYOUT_HORIZONTAL ? xplm_JustCenter : xplm_JustLeft);
}

// The one place text is drawn, by the panel and by the settings window alike.
//
// No ensureFont() here. This runs inside a panel draw callback, and XPLMCreateFont
// is rejected there ("Never call this function from within a panel draw callback")
// -- it is fatal, not a no-op. The font is built in initialize() and retried from
// the flight loop; if it is missing we skip text.
void VolumeDeck::drawText(float x, float y, const char* text, float size,
                          const float rgba[4], XPLMJustification_t justify) const {
    if (font == nullptr || text == nullptr) return;

    XPLMFontDrawString(font, XPLMMakeColor(rgba[0], rgba[1], rgba[2], rgba[3]),
                       size, x, y, text, justify);
}

// Falls back to a rough estimate while the font is still loading, so a layout pass
// on one of those early frames does not collapse every column to zero width.
float VolumeDeck::measureText(const char* text, float size) const {
    if (text == nullptr) return 0.0f;
    if (font == nullptr) return 0.5f * size * (float)strlen(text);
    return XPLMFontMeasureString(font, size, text);
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

    std::vector<int> simChannels;
    for (int i = 0; i < NUM_KNOBS; i++) {
        if (!knobs[i].isAddon) simChannels.push_back(i);
    }
    
    while (std::getline(file, line)) {
        // Parse version
        if (fileVersion == 0 && line.find("VERSION") != std::string::npos) {
            sscanf(line.c_str(), "VERSION %d", &fileVersion);
            continue;
        }
        
        // Which channels the user wants on the panel / wants us to control. Global,
        // not per aircraft. Checked before the ".acf" test below, which would
        // otherwise claim any line that happens to mention an aircraft file.
        if (line.compare(0, 8, "CHANNEL ") == 0) {
            char slug[64] = {0};
            int on = 1;
            if (sscanf(line.c_str(), "CHANNEL %63s %d", slug, &on) == 2) {
                int idx = Channels::findBySlug(slug);
                // An unknown slug is a channel from a newer build. Leave it alone --
                // saveConfig() rewrites these lines from live state, so it will be
                // dropped rather than corrupting anything.
                if (idx >= 0) knobs[idx].enabled = (on != 0);
            }
            continue;
        }

        // Add-on levels are stored once, globally, not per aircraft: chatter volume
        // is a property of the add-on, not of the aeroplane you happen to be flying.
        if (line.compare(0, 6, "ADDON ") == 0) {
            char slug[64] = {0};
            float intVol = 0.0f, extVol = KNOB_SINGLE_MARK;
            if (sscanf(line.c_str(), "ADDON %63s %f %f", slug, &intVol, &extVol) == 3) {
                int idx = Channels::findBySlug(slug);
                if (idx >= 0 && knobs[idx].isAddon) {
                    // An add-on's probe can finish either side of this load, so unlike
                    // the sim channels we cannot assume probe-then-load. If the probe
                    // already said "not writable", that verdict describes reality and
                    // the stored pair must not clear it.
                    bool locked = (knobs[idx].exteriorVolume == KNOB_FAILED_TEST);

                    knobs[idx].interiorVolume = intVol;
                    if (!locked) {
                        knobs[idx].exteriorVolume = (extVol < 0.0f) ? KNOB_SINGLE_MARK : extVol;
                    }
                    knobs[idx].hasStoredValue = true;

                    // If the add-on was already up and probed, apply the stored level
                    // now. If it was not, serviceKnobProbes() applies it the moment
                    // the dataref appears.
                    if (!locked && knobs[idx].probed && isChannelControllable(idx)) {
                        int currentView = XPLMGetDatai(viewExternalDataRef);
                        float wanted = (knobs[idx].exteriorVolume >= 0.0f && currentView != 0)
                                     ? knobs[idx].exteriorVolume : knobs[idx].interiorVolume;
                        setVolume(idx, wanted);
                    }
                }
            }
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
                
                // The aircraft line carries the sim channels only, in registry
                // order -- exactly the eight pairs a v2 file already had, so an old
                // file still loads. Add-on levels live on their own global lines.
                size_t pos = 0;
                float intVol, extVol;
                while (pos < simChannels.size() && (iss >> intVol >> extVol)) {
                    int k = simChannels[pos];
                    knobs[k].interiorVolume = intVol;
                    // Builds before this fix wrote KNOB_FAILED_TEST (-2) to disk.
                    // Collapse any negative sentinel to single-value mode so an
                    // already-poisoned file heals itself on the next load rather
                    // than locking the knob grey forever.
                    knobs[k].exteriorVolume =
                        (extVol < 0.0f) ? KNOB_SINGLE_MARK : extVol;
                    knobs[k].hasStoredValue = true;

                    int currentView = XPLMGetDatai(viewExternalDataRef);
                    if (currentView == 0 || knobs[k].exteriorVolume < 0) {
                        setVolume(k, knobs[k].interiorVolume);
                    } else {
                        setVolume(k, knobs[k].exteriorVolume);
                    }

                    pos++;
                }
                break;
            }
        }
    }
    
    file.close();
    
    // An older file is left flagged dirty so the next save rewrites it in the
    // current format, with the global CHANNEL / ADDON lines this version adds.
    if (foundAircraft && fileVersion >= FILE_FORMAT_VERSION) {
        saveRequired = false;
    }
    
    // CHANNEL lines can have changed which knobs are on the panel, and LAYOUT which
    // shape they are in.
    groupsDirty = true;

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

    // Global, not per aircraft: which channels are on the panel, and what the
    // third-party channels are set to. Written from live state every time, so a
    // slug this build no longer knows about simply disappears.
    for (int i = 0; i < NUM_KNOBS; i++) {
        newContent << "CHANNEL " << knobs[i].name << " " << (knobs[i].enabled ? 1 : 0) << "\n";
    }

    for (int i = 0; i < NUM_KNOBS; i++) {
        if (!knobs[i].isAddon) continue;
        // Keep a level we loaded but could not apply (add-on not running this
        // session) rather than dropping the user's setting on the floor.
        if (!knobs[i].available && !knobs[i].hasStoredValue) continue;

        float ext = knobs[i].exteriorVolume;
        if (ext == KNOB_FAILED_TEST) ext = KNOB_SINGLE_MARK;
        newContent << "ADDON " << knobs[i].name << " "
                   << knobs[i].interiorVolume << " " << ext << "\n";
    }

    // Write current aircraft data: the sim channels only, in registry order.
    newContent << currentAircraft;
    for (int i = 0; i < NUM_KNOBS; i++) {
        if (knobs[i].isAddon) continue;
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
            // Rewritten from live state above; must not be duplicated from the
            // old file, and must be skipped before the ".acf" test below.
            if (line.compare(0, 8, "CHANNEL ") == 0) continue;
            if (line.compare(0, 6, "ADDON ") == 0) continue;
            
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
    if (!isChannelControllable(knobIndex)) return;
    
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
    if (!isChannelControllable(knobIndex) || knobs[knobIndex].probeStage != 0) return;
    
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
