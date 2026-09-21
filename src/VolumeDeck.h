#ifndef VOLUME_DECK_H
#define VOLUME_DECK_H

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include <string>
#include <vector>

// Drawing goes through XPLMPanelGraphics (SDK 4.4.0 / X-Plane 12.4.4+) rather than
// raw OpenGL: it gives real line widths, a proper font API and correct compositing,
// and it is the path Laminar is keeping as the graphics backends move off OpenGL.
#include "XPLMPanelGraphics.h"
#include <stdint.h>

// The one place the plugin version lives. main.cpp logs it, and
// create-package.sh / docker-build-all.sh scrape this line to name the zip.
#define SOFTWARE_VERSION "1.0.1"
#define FILE_FORMAT_VERSION 2

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
    float textX;
    float textY;
    float interiorVolume;
    float exteriorVolume;
    float preMuteVolume;   // level stashed by mute_toggle; -1 when not muted
    XPLMDataRef dataRef;
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
    bool screenSizeChanged;
    bool saveRequired;
    bool autoPosition;
    bool dragging;
    bool firstDraw;
    int prevView;
    int initialTestStage;
    
    // Constants
    static const int NUM_KNOBS = 8;
    static const float KNOB_RADIUS;
    static const float GAP_FIVE;
    static const float FIXED_TEXT_SPACE;
    static const float LABEL_FONT_SIZE;
    // Header icon strip, as offsets from mainX (negative = left of the anchor).
    static const float ICON_SOUND_W;
    static const float ICON_SAVE_CX;
    static const float ICON_LAYOUT_CX;
    static const float ICON_DRAG_CX;
    static const float ICON_HALF;
    static const float DRAG_RADIUS;
    static const float H_GAP;
    static const float DRAG_HALF;
    
    // Knobs data
    std::vector<VolumeKnob> knobs;
    
    // DataRefs
    XPLMDataRef viewExternalDataRef;
    
    // Helper functions
    void updateScreenSize();
    void readScreenBounds();
    void ensureFont();
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

