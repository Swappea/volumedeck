#include "SettingsWindow.h"
#include "VolumeDeck.h"
#include "Channels.h"
#include "Palette.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPanelGraphics.h"
#include "XPLMUtilities.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Metrics. All boxels -- see the coordinate note in VolumeDeck.h.
const float PAD         = 16.0f;
const float ROW_H       = 24.0f;
const float ADDON_ROW_H = 36.0f;   // two lines: the channel, then its dataref
const float BOX         = 14.0f;   // checkbox side
const float TITLE_SIZE  = 16.0f;
const float ROW_SIZE    = 14.0f;
const float SMALL_SIZE  = 11.0f;
const float BTN_W       = 96.0f;
const float BTN_H       = 26.0f;
const float WIN_W       = 680.0f;

// Channels are laid out in a grid rather than one long column: the list grows
// sideways as add-ons are added instead of pushing the window off the screen.
// Add-on cells hold more (owner, status, dataref) so they get half the width.
const int SIM_COLS   = 4;
const int ADDON_COLS = 2;

XPLMWindowID g_window = nullptr;

struct Row {
    int   channel;
    float left, right, top, bottom;   // the cell, which is also the click target
    bool  addon;
};

struct Layout {
    float titleY;
    float subtitleY;
    float simHeaderY;
    float addonHeaderY;
    float addonEmptyY;      // where "none detected" goes when there are no add-on rows
    bool  addonEmpty;
    float noteY;
    float statusY;
    float saveL, saveT, saveR, saveB;
    float contentBottom;    // where the content runs out, for sizing the window
    std::vector<Row> rows;
};

// The single source of geometry for this window: the draw callback and the click
// handler both go through it, so a row's art and its hit target are the same rect.
void computeLayout(float left, float top, float right, Layout& out) {
    VolumeDeck* vd = VolumeDeck::getInstance();
    out.rows.clear();

    const float contentL = left + PAD;
    const float contentR = right - PAD;

    float y = top - PAD;

    out.titleY = y - TITLE_SIZE;
    y -= TITLE_SIZE + 6.0f;

    out.subtitleY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 14.0f;

    out.simHeaderY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 8.0f;

    float colW = (contentR - contentL) / (float)SIM_COLS;
    int placed = 0;
    for (int i = 0; i < vd->channelCount(); i++) {
        if (vd->channelDef(i).kind != CH_SIM) continue;

        int c = placed % SIM_COLS;
        int r = placed / SIM_COLS;

        Row row;
        row.channel = i;
        row.left    = contentL + c * colW;
        row.right   = row.left + colW;
        row.top     = y - r * ROW_H;
        row.bottom  = row.top - ROW_H;
        row.addon   = false;
        out.rows.push_back(row);
        placed++;
    }
    if (placed > 0) y -= ((placed + SIM_COLS - 1) / SIM_COLS) * ROW_H;

    y -= 12.0f;
    out.addonHeaderY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 8.0f;

    // Only add-ons that are actually present get a row. An add-on that was never
    // installed, or whose plugin has been switched off in Plugin Admin, is not a
    // setting the user can meaningfully hold an opinion about -- listing it as an
    // unchecked box would just look like something is broken.
    colW = (contentR - contentL) / (float)ADDON_COLS;
    placed = 0;
    for (int i = 0; i < vd->channelCount(); i++) {
        if (vd->channelDef(i).kind != CH_ADDON) continue;
        if (!vd->isChannelAvailable(i)) continue;

        int c = placed % ADDON_COLS;
        int r = placed / ADDON_COLS;

        Row row;
        row.channel = i;
        row.left    = contentL + c * colW;
        row.right   = row.left + colW;
        row.top     = y - r * ADDON_ROW_H;
        row.bottom  = row.top - ADDON_ROW_H;
        row.addon   = true;
        out.rows.push_back(row);
        placed++;
    }

    out.addonEmpty = (placed == 0);
    if (out.addonEmpty) {
        out.addonEmptyY = y - SMALL_SIZE;
        y -= SMALL_SIZE + 6.0f;
    } else {
        out.addonEmptyY = 0.0f;
        y -= ((placed + ADDON_COLS - 1) / ADDON_COLS) * ADDON_ROW_H;
    }

    y -= 8.0f;
    out.noteY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 12.0f;

    out.saveL = contentL;
    out.saveT = y;
    out.saveR = contentL + BTN_W;
    out.saveB = y - BTN_H;
    out.statusY = y - BTN_H * 0.5f - SMALL_SIZE * 0.35f;
    y -= BTN_H + PAD;

    out.contentBottom = y;
}

// --- panel-graphics helpers, local to this window ---

void fillRect(const float c[4], float x1, float y1, float x2, float y2) {
    XPLMVertex_t v[4] = { {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} };
    XPLMPolygon(XPLMMakeColor(c[0], c[1], c[2], c[3]), v, 4);
}

void strokeRect(const float c[4], float w, float x1, float y1, float x2, float y2) {
    XPLMVertex_t v[4] = { {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2} };
    XPLMLineLoopWithWidth(XPLMMakeColor(c[0], c[1], c[2], c[3]), w, v, 4);
}

void drawLine(const float c[4], float w, float x1, float y1, float x2, float y2) {
    XPLMVertex_t v[2] = { {x1, y1}, {x2, y2} };
    XPLMLinesWithWidth(XPLMMakeColor(c[0], c[1], c[2], c[3]), w, v, 2);
}

void drawCheckbox(float cx, float cy, bool checked) {
    float h = BOX * 0.5f;
    fillRect(Palette::PANEL_BG, cx - h, cy - h, cx + h, cy + h);
    strokeRect(Palette::MUTED, 1.0f, cx - h, cy - h, cx + h, cy + h);

    if (!checked) return;

    drawLine(Palette::ICON_ACTIVE, 2.0f, cx - h + 3.0f, cy, cx - 1.0f, cy - h + 3.5f);
    drawLine(Palette::ICON_ACTIVE, 2.0f, cx - 1.0f, cy - h + 3.5f, cx + h - 2.5f, cy + h - 3.5f);
}

void sectionHeader(VolumeDeck* vd, float left, float right, float baselineY, const char* text) {
    vd->drawText(left + PAD, baselineY, text, SMALL_SIZE, Palette::MUTED, xplm_JustLeft);
    float rule = baselineY + SMALL_SIZE * 0.35f;
    float from = left + PAD + vd->measureText(text, SMALL_SIZE) + 8.0f;
    drawLine(Palette::DIVIDER, 1.0f, from, rule, right - PAD, rule);
}

void syncHeight(XPLMWindowID id);   // defined below, next to requiredHeight()

void drawWindow(XPLMWindowID id, void* /*refcon*/) {
    try {
        VolumeDeck* vd = VolumeDeck::getInstance();

        // Before reading the geometry, not after: the add-on section grows the moment
        // a plugin is detected, and this window can be open when that happens. Without
        // this the note line and the Save button fall below the bottom edge and become
        // invisible AND unclickable -- clicks outside the window never reach us -- until
        // it is closed and reopened.
        syncHeight(id);

        int l, t, r, b;
        XPLMGetWindowGeometry(id, &l, &t, &r, &b);
        float left = (float)l, top = (float)t, right = (float)r, bottom = (float)b;

        Layout lay;
        computeLayout(left, top, right, lay);

        fillRect(Palette::WINDOW_BG, left, top, right, bottom);

        vd->drawText(left + PAD, lay.titleY, "VolumeDeck", TITLE_SIZE,
                     Palette::LABEL, xplm_JustLeft);
        vd->drawText(right - PAD, lay.titleY, "v" SOFTWARE_VERSION, SMALL_SIZE,
                     Palette::MUTED, xplm_JustRight);
        vd->drawText(left + PAD, lay.subtitleY,
                     "Choose which channels the panel shows and controls.",
                     SMALL_SIZE, Palette::MUTED, xplm_JustLeft);

        sectionHeader(vd, left, right, lay.simHeaderY, "X-PLANE CHANNELS");
        sectionHeader(vd, left, right, lay.addonHeaderY, "THIRD-PARTY ADD-ONS");

        if (lay.addonEmpty) {
            vd->drawText(left + PAD, lay.addonEmptyY,
                         "No supported add-ons are running.",
                         SMALL_SIZE, Palette::DISABLED, xplm_JustLeft);
        }

        for (size_t i = 0; i < lay.rows.size(); i++) {
            const Row& row = lay.rows[i];
            int ch = row.channel;
            const ChannelDef& def = vd->channelDef(ch);

            bool on = vd->isChannelEnabled(ch);
            // Anchored to the cell TOP, not its centre: an add-on cell is taller
            // because its dataref goes on a second line underneath.
            float textY = row.top - ROW_H * 0.5f - ROW_SIZE * 0.35f;
            float textL = row.left + BOX + 10.0f;
            float textR = row.right - 12.0f;

            drawCheckbox(row.left + BOX * 0.5f, textY + ROW_SIZE * 0.35f, on);
            vd->drawText(textL, textY, def.display, ROW_SIZE, Palette::LABEL, xplm_JustLeft);

            if (!row.addon) {
                // What this channel is currently set to. Shown even when the knob is
                // hidden, because the channel is still live.
                char pct[16];
                snprintf(pct, sizeof(pct), "%d%%",
                         (int)(vd->getChannelVolume(ch) * 100.0f + 0.5f));
                vd->drawText(textR, textY, pct, ROW_SIZE,
                             on ? Palette::MUTED : Palette::DISABLED, xplm_JustRight);
                continue;
            }

            // Add-on cell: whose channel it is, whether we can write it, and the
            // dataref itself so a support question can be answered from a screenshot.
            if (def.owner != nullptr) {
                float ownerX = textL + vd->measureText(def.display, ROW_SIZE) + 8.0f;
                vd->drawText(ownerX, textY, def.owner, SMALL_SIZE, Palette::MUTED, xplm_JustLeft);
            }

            bool readOnly = on && vd->isKnobLocked(ch);
            vd->drawText(textR, textY, readOnly ? "read-only" : "detected", SMALL_SIZE,
                         readOnly ? Palette::SAVE_DIRTY : Palette::SAVE_CLEAN, xplm_JustRight);

            vd->drawText(textL, row.bottom + 8.0f, def.dataref,
                         SMALL_SIZE, Palette::DISABLED, xplm_JustLeft);
        }

        vd->drawText(left + PAD, lay.noteY,
                     "Add-on levels are saved once, globally. X-Plane channels stay per aircraft.",
                     SMALL_SIZE, Palette::MUTED, xplm_JustLeft);

        // Save button. Same red/green language as the panel's floppy icon.
        bool dirty = vd->isSaveRequired();
        fillRect(dirty ? Palette::SAVE_DIRTY : Palette::SAVE_CLEAN,
                 lay.saveL, lay.saveT, lay.saveR, lay.saveB);
        vd->drawText((lay.saveL + lay.saveR) * 0.5f,
                     lay.saveB + (BTN_H - ROW_SIZE) * 0.5f + 2.0f,
                     "Save now", ROW_SIZE, Palette::PANEL_BG, xplm_JustCenter);

        vd->drawText(lay.saveR + 12.0f, lay.statusY,
                     dirty ? "Unsaved changes" : "All settings saved",
                     SMALL_SIZE, dirty ? Palette::LABEL : Palette::MUTED, xplm_JustLeft);
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in settings drawWindow!\n");
    }
}

int handleClick(XPLMWindowID id, int x, int y, int mouse, void* /*refcon*/) {
    try {
        if ((XPLMMouseStatus)mouse != xplm_MouseDown) return 1;

        VolumeDeck* vd = VolumeDeck::getInstance();

        int l, t, r, b;
        XPLMGetWindowGeometry(id, &l, &t, &r, &b);

        Layout lay;
        computeLayout((float)l, (float)t, (float)r, lay);

        if (x >= lay.saveL && x <= lay.saveR && y <= lay.saveT && y >= lay.saveB) {
            // Same guard the command handlers and the menu apply: during the startup
            // probe the knobs hold probe scratch rather than the user's levels.
            if (vd->isReady()) vd->saveConfig();
            return 1;
        }

        for (size_t i = 0; i < lay.rows.size(); i++) {
            const Row& row = lay.rows[i];
            if (y > row.top || y < row.bottom) continue;
            if (x < row.left || x > row.right) continue;

            // The whole cell is the target, not just the box.
            vd->setChannelEnabled(row.channel, !vd->isChannelEnabled(row.channel));
            return 1;
        }
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in settings handleClick!\n");
    }

    // Always consumed: this window is opaque, and a click falling through to the
    // sim behind it would be a surprise.
    return 1;
}

XPLMCursorStatus handleCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }

void handleKey(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}

int handleRightClick(XPLMWindowID, int, int, XPLMMouseStatus, void*) { return 1; }

int handleWheel(XPLMWindowID, int, int, int, int, void*) { return 1; }

// Height the content needs right now. The add-on section grows and shrinks as
// plugins come and go, so this is re-checked rather than fixed at creation.
float requiredHeight() {
    Layout lay;
    computeLayout(0.0f, 0.0f, WIN_W, lay);
    return -lay.contentBottom;
}

// Keep the window exactly as tall as its contents. Called when the window is opened
// rather than from the draw callback: the add-on section changes size when a plugin
// comes or goes, and resizing a window out from under its own draw pass is asking
// for a frame drawn against last frame's geometry.
void syncHeight(XPLMWindowID id) {
    static float lastHeight = 0.0f;

    float want = requiredHeight();
    if (want == lastHeight) return;
    lastHeight = want;

    int l, t, r, b;
    XPLMGetWindowGeometry(id, &l, &t, &r, &b);
    XPLMSetWindowResizingLimits(id, (int)WIN_W, (int)want, (int)WIN_W, (int)want);
    XPLMSetWindowGeometry(id, l, t, l + (int)WIN_W, t - (int)want);
}

}   // namespace

void SettingsWindow::create() {
    if (g_window != nullptr) return;

    int sl, st, sr, sb;
    XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);

    float h = requiredHeight();
    float cx = (float)(sl + sr) * 0.5f;
    float cy = (float)(sb + st) * 0.5f;

    // Zero-initialised: SDK 4.4.0 added fields to this struct that we do not set,
    // and they must read as 0 rather than stack garbage. Same reasoning as the
    // mouse-sink window in main.cpp.
    XPLMCreateWindow_t params = {};
    params.structSize = sizeof(params);
    params.left   = (int)(cx - WIN_W * 0.5f);
    params.right  = (int)(cx + WIN_W * 0.5f);
    params.top    = (int)(cy + h * 0.5f);
    params.bottom = (int)(cy - h * 0.5f);
    params.visible = 0;
    params.drawWindowFunc       = drawWindow;
    params.handleMouseClickFunc = handleClick;
    params.handleKeyFunc        = handleKey;
    params.handleCursorFunc     = handleCursor;
    params.handleMouseWheelFunc = handleWheel;
    params.handleRightClickFunc = handleRightClick;
    params.refcon = nullptr;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    params.layer = xplm_WindowLayerFloatingWindows;
    params.contentType = xplm_WindowContentTypePanelGraphics;

    g_window = XPLMCreateWindowEx(&params);
    if (g_window == nullptr) {
        XPLMDebugString("VolumeDeck: [ERROR] Failed to create settings window!\n");
        return;
    }

    XPLMSetWindowTitle(g_window, "VolumeDeck Settings");
    XPLMSetWindowPositioningMode(g_window, xplm_WindowPositionFree, -1);
    // Fixed size: the content is a grid, not something that reflows.
    XPLMSetWindowResizingLimits(g_window, (int)WIN_W, (int)h, (int)WIN_W, (int)h);

    XPLMDebugString("VolumeDeck: [SETTINGS] Settings window created\n");
}

void SettingsWindow::destroy() {
    if (g_window == nullptr) return;
    XPLMDestroyWindow(g_window);
    g_window = nullptr;
}

bool SettingsWindow::isVisible() {
    return g_window != nullptr && XPLMGetWindowIsVisible(g_window) != 0;
}

void SettingsWindow::toggle() {
    if (g_window == nullptr) return;

    bool show = !isVisible();
    if (show) {
        // An add-on may have come or gone since this was last open.
        syncHeight(g_window);
        XPLMSetWindowIsVisible(g_window, 1);
        // The panel's mouse sink covers the whole screen in this same layer, so make
        // sure we are the one that sees these clicks.
        XPLMBringWindowToFront(g_window);
    } else {
        XPLMSetWindowIsVisible(g_window, 0);
    }
}
