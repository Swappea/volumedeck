#ifndef VOLUME_DECK_H
#define VOLUME_DECK_H

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "Channels.h"
#include <string>
#include <vector>

// Drawing goes through XPLMPanelGraphics (SDK 4.4.0 / X-Plane 12.4.4+) rather than
// raw OpenGL: it gives real line widths, a proper font API and correct compositing,
// and it is the path Laminar is keeping as the graphics backends move off OpenGL.
#include "XPLMPanelGraphics.h"
#include <stdint.h>

// The one place the plugin version lives. main.cpp logs it, and
// create-package.sh / docker-build-all.sh scrape this line to name the zip.
#define SOFTWARE_VERSION "1.1.0"
#define FILE_FORMAT_VERSION 3

// Knob state constants
#define KNOB_SINGLE_MARK -1.0f
#define KNOB_FAILED_TEST -2.0f

enum PanelLayout {
    LAYOUT_VERTICAL   = 0,   // column down the right edge (original)
    LAYOUT_HORIZONTAL = 1    // single row running left from the icon
};

struct VolumeKnob {
    float x;
    float y;
    std::string name;
    std::string displayName;
    std::string ownerName;   // owning plugin; empty for X-Plane's own channels
    float textX;
    float textY;
    float hitPadLeft;      // label strip width to include in the hit test, per layout
    float interiorVolume;
    float exteriorVolume;
    float preMuteVolume;   // level stashed by mute_toggle; -1 when not muted
    XPLMDataRef dataRef;

    const ChannelDef* def;
    bool  isAddon;
    bool  available;       // dataref resolved; always true for sim channels
    bool  enabled;         // user's Settings choice -- see VolumeDeck::isChannelEnabled

    // Per-knob writability probe, used for add-on channels that are discovered after
    // the global startup probe has already run. Deliberately NOT sharing the
    // exteriorVolume stash the global probe uses: that overload is what once made
    // updateVolumesForViewChange() write the real volume back over the test value and
    // lock every knob grey.
    int   probeStage;      // 0 idle, 1 write test value, 2 read it back
    float probeStash;      // real volume parked across the probe
    bool  probed;          // this run's probe has finished; do not re-run it
    bool  hasStoredValue;  // the config file supplied a level for this channel
};

// One column (vertical layout) or row (horizontal layout) of the panel: the sim
// channels first, then one group per owning add-on plugin. The caption is the owner
// name, drawn small above the group; empty for the sim group.
struct PanelGroup {
    std::vector<int> knobIndices;
    std::string      caption;
    float            labelWidth;   // vertical layout: width reserved for the labels
    // Filled in by updateKnobPositions() so drawControlPanel() only has to draw what
    // the layout pass decided -- same rule as the knobs and the icon strip.
    float            captionX, captionY;
    float            dividerPos;   // vertical: x of the rule; horizontal: y of it
    bool             hasDivider;
};

class VolumeDeck {
public:
    static const int CIRCLE_SEGMENTS = 64;   // used by the arc helpers
    static VolumeDeck* getInstance();

    void initialize();
    void shutdown();

    // Callbacks
    static float flightLoopCallback(float elapsedSinceLastCall, float elapsedTimeSinceLastFlightLoop,
                                   int counter, void* inRefcon);

    // Public interface for mouse handling
    float getMainX() const { return mainX; }
    float getMainY() const { return mainY; }
    bool isControlBoxVisible() const { return drawControlBox; }
    void toggleControlBox() { drawControlBox = !drawControlBox; }
    bool isMouseOverKnobPublic(int knobIndex, int x, int y) { return isMouseOverKnob(knobIndex, x, y); }
    void toggleKnobMode(int knobIndex);
    void adjustKnobVolume(int knobIndex, int clicks);
    void startDragging(int x, int y);
    void updateDragPosition(int x, int y);
    bool isDragging() const { return dragging; }
    void stopDragging() { dragging = false; }
    void saveConfig();
    void draw();   // called from the window draw callback in main.cpp
    void toggleLayout();
    bool isOverSoundIcon(int x, int y) const;
    bool isOverSaveIcon(int x, int y) const;
    bool isOverLayoutIcon(int x, int y) const;
    bool isOverDragHandle(int x, int y) const;
    PanelLayout getLayout() const { return layout; }
    void muteToggle(int knobIndex);
    void syncKnobFromDataRef(int knobIndex, bool flagChanges);
    bool isReady() const { return initialTestStage == 0; }
    bool isKnobLocked(int knobIndex) const;

    // Channel surface, used by main.cpp's hit-test loops, VolumeCommands and the
    // settings window. Indices are Channels::DEFS indices and are stable for the
    // life of the process -- an unavailable or hidden channel keeps its slot.
    int  channelCount() const { return (int)knobs.size(); }
    const ChannelDef& channelDef(int index) const { return Channels::get(index); }
    bool isChannelAvailable(int index) const;
    // A sim channel that is "off" is only hidden from the panel: its commands keep
    // working and its level is still tracked and saved. An add-on channel that is
    // off is not controlled at all -- we never write its dataref and its commands
    // no-op, because that dataref belongs to somebody else's plugin.
    bool isChannelEnabled(int index) const;
    void setChannelEnabled(int index, bool on);
    bool isChannelDrawable(int index) const;   // available && enabled -- on the panel
    // May we write this channel's dataref at all? A hidden sim channel still says
    // yes (hiding it is a panel preference, and its keybindings must keep working);
    // a switched-off add-on says no, because that dataref is not ours to touch.
    bool isChannelControllable(int index) const;
    float getChannelVolume(int index);         // normalised 0..1, for the settings UI
    bool isSaveRequired() const { return saveRequired; }

    XPLMFontHandle getFont() const { return font; }
    // Shared by SettingsWindow so both surfaces measure and draw text identically.
    void drawText(float x, float y, const char* text, float size,
                  const float rgba[4], XPLMJustification_t justify) const;
    float measureText(const char* text, float size) const;

private:
    VolumeDeck();
    ~VolumeDeck();

    static VolumeDeck* instance;

    // Drawing functions
    void drawKnob(int knobIndex);
    void drawSoundIcon();
    void drawLayoutIcon();
    void drawDragIcon();
    void drawSaveIcon();
    float horizontalCell() const;
    void drawControlPanel();

    // Mouse handling
    bool handleMouseClick(int x, int y, XPLMMouseStatus mouseStatus);
    bool handleMouseWheel(int x, int y, int wheel, int clicks);
    bool isMouseNearIcon(int x, int y);
    bool isMouseOverKnob(int knobIndex, int x, int y);

    // Volume management
    float getVolume(int knobIndex);
    void setVolume(int knobIndex, float value);
    void testVolumeDecks();
    void updateVolumesForViewChange();
    void resolveAddonDataRefs();      // deferred XPLMFindDataRef, from the flight loop
    void serviceKnobProbes();         // per-knob probe for late-discovered add-ons
    // Writes past the "do we control this channel" guard in setVolume(). ONLY for
    // putting back a value the probe itself wrote -- leaving the 0.03125 test value
    // in somebody else's dataref because the user switched the channel off mid-probe
    // would be worse than the write we were trying to avoid.
    void writeVolumeRaw(int knobIndex, float value);

    // Configuration
    void loadConfig();
    std::string getConfigPath();
    std::string getLegacyConfigPath();
    std::string getAircraftFileName();

    // UI state
    float mainX, mainY;
    float snapMainX, snapMainY;
    // Everything is in BOXELS (XPLMGetScreenBoundsGlobal / XPLMGetMouseLocationGlobal),
    // not pixels. Modern windows deliver mouse coords in boxels, so mixing the two
    // makes hit tests unreachable on any display with UI scaling != 1.0.
    PanelLayout layout;
    XPLMFontHandle font;
    int fontAttempts;
    uint32_t currentColor;
    float currentLineWidth;
    int screenLeft, screenBottom, screenRight, screenTop;
    int screenWidth, screenHeight;   // derived: right-left, top-bottom
    bool drawControlBox;
    // Group membership only changes when a channel is switched on or off, an add-on
    // turns up, or the font finally loads and label widths become measurable. The
    // draw callback runs at frame rate, so it does not rebuild the list every frame.
    bool groupsDirty;
    bool screenSizeChanged;
    bool saveRequired;
    bool autoPosition;
    bool dragging;
    bool firstDraw;
    int prevView;
    int initialTestStage;

    // Constants
    static const float KNOB_RADIUS;
    static const float GAP_FIVE;
    static const float FIXED_TEXT_SPACE;
    static const float LABEL_FONT_SIZE;
    static const float CAPTION_FONT_SIZE;
    static const float GROUP_GAP;
    // Header icon strip, as offsets from mainX (negative = left of the anchor).
    static const float ICON_SOUND_W;
    static const float ICON_SAVE_CX;
    static const float ICON_LAYOUT_CX;
    static const float ICON_DRAG_CX;
    static const float ICON_HALF;
    static const float DRAG_RADIUS;
    static const float H_GAP;
    static const float DRAG_HALF;

    // Knobs data. One entry per Channels::DEFS entry, always, in that order.
    std::vector<VolumeKnob> knobs;
    // Rebuilt by updateKnobPositions(); the sim group first, then one per add-on owner.
    std::vector<PanelGroup> groups;

    // DataRefs
    XPLMDataRef viewExternalDataRef;

    // Helper functions
    void updateScreenSize();
    void readScreenBounds();
    void ensureFont();
    void buildGroups();
    float captionHeight() const;
    float panelWidth() const;
    float panelHeight() const;
    void updateKnobPositions();
    float clamp(float value, float min, float max);
    void drawRectangle(float x1, float y1, float x2, float y2);
    void drawFilledRectangle(float x1, float y1, float x2, float y2);
    void drawLine(float x1, float y1, float x2, float y2);
    void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3);
    void drawFilledTriangle(float x1, float y1, float x2, float y2, float x3, float y3);
    void drawCircle(float x, float y, float radius);
    void drawFilledCircle(float x, float y, float radius);
    void drawArc(float x, float y, float startAngle, float endAngle, float radius);
    void drawFilledArc(float x, float y, float startAngle, float endAngle, float radius);
    void drawAngleArrow(float x, float y, float angle, float outerRadius, float innerRadius, float width);
    void drawTickMark(float x, float y, float angle, float outerRadius, float innerRadius, float width);
    void drawString(float x, float y, const char* text);
    void setColor(float r, float g, float b, float a);
    void setColor(const float rgba[4]);
    void setLineWidth(float w);
};

#endif // VOLUME_DECK_H
