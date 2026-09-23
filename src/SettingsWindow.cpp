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
const float WIN_W       = 480.0f;

XPLMWindowID g_window = nullptr;

struct Row {
    int   channel;
    float top;
    float bottom;
    bool  addon;
};

struct Layout {
    float titleY;
    float subtitleY;
    float simHeaderY;
    float addonHeaderY;
    float noteY;
    float statusY;
    float saveL, saveT, saveR, saveB;
    float contentBottom;          // where the content runs out, for sizing the window
    std::vector<Row> rows;
};

// The single source of geometry for this window: the draw callback and the click
// handler both go through it, so a row's art and its hit target are the same rect.
void computeLayout(float left, float top, float right, Layout& out) {
    VolumeDeck* vd = VolumeDeck::getInstance();
    out.rows.clear();

    float y = top - PAD;

    out.titleY = y - TITLE_SIZE;
    y -= TITLE_SIZE + 6.0f;

    out.subtitleY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 14.0f;

    out.simHeaderY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 8.0f;

    for (int i = 0; i < vd->channelCount(); i++) {
        if (vd->channelDef(i).kind != CH_SIM) continue;
        Row r;
        r.channel = i;
        r.top     = y;
        r.bottom  = y - ROW_H;
        r.addon   = false;
        out.rows.push_back(r);
        y -= ROW_H;
    }

    y -= 12.0f;
    out.addonHeaderY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 8.0f;

    for (int i = 0; i < vd->channelCount(); i++) {
        if (vd->channelDef(i).kind != CH_ADDON) continue;
        Row r;
        r.channel = i;
        r.top     = y;
        r.bottom  = y - ADDON_ROW_H;
        r.addon   = true;
        out.rows.push_back(r);
        y -= ADDON_ROW_H;
    }

    y -= 8.0f;
    out.noteY = y - SMALL_SIZE;
    y -= SMALL_SIZE + 12.0f;

    out.saveL = left + PAD;
    out.saveT = y;
    out.saveR = left + PAD + BTN_W;
    out.saveB = y - BTN_H;
    out.statusY = y - BTN_H * 0.5f - SMALL_SIZE * 0.35f;
    y -= BTN_H + PAD;

    out.contentBottom = y;
    (void)right;
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

void drawCheckbox(float cx, float cy, bool checked, bool dimmed) {
    float h = BOX * 0.5f;
    fillRect(Palette::PANEL_BG, cx - h, cy - h, cx + h, cy + h);
    strokeRect(dimmed ? Palette::DISABLED : Palette::MUTED, 1.0f,
               cx - h, cy - h, cx + h, cy + h);

    if (!checked) return;

    const float* tick = dimmed ? Palette::DISABLED : Palette::ICON_ACTIVE;
    drawLine(tick, 2.0f, cx - h + 3.0f, cy, cx - 1.0f, cy - h + 3.5f);
    drawLine(tick, 2.0f, cx - 1.0f, cy - h + 3.5f, cx + h - 2.5f, cy + h - 3.5f);
}

void sectionHeader(VolumeDeck* vd, float left, float right, float baselineY, const char* text) {
    vd->drawText(left + PAD, baselineY, text, SMALL_SIZE, Palette::MUTED, xplm_JustLeft);
    float rule = baselineY + SMALL_SIZE * 0.35f;
    float from = left + PAD + vd->measureText(text, SMALL_SIZE) + 8.0f;
    drawLine(Palette::DIVIDER, 1.0f, from, rule, right - PAD, rule);
}

void drawWindow(XPLMWindowID id, void* /*refcon*/) {
    try {
        VolumeDeck* vd = VolumeDeck::getInstance();

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

        for (size_t i = 0; i < lay.rows.size(); i++) {
            const Row& row = lay.rows[i];
            int ch = row.channel;
            const ChannelDef& def = vd->channelDef(ch);

            bool on        = vd->isChannelEnabled(ch);
            bool available = vd->isChannelAvailable(ch);
            // Anchored to the row TOP, not its centre: an add-on row is taller
            // because its dataref goes on a second line underneath.
            float textY    = row.top - ROW_H * 0.5f - ROW_SIZE * 0.35f;

            drawCheckbox(left + PAD + BOX * 0.5f, textY + ROW_SIZE * 0.35f, on, !available);

            const float* nameColor = available ? Palette::LABEL : Palette::DISABLED;
            vd->drawText(left + PAD + BOX + 10.0f, textY, def.display, ROW_SIZE,
                         nameColor, xplm_JustLeft);

            if (!row.addon) {
                // Right-hand column: what this channel is currently set to. Shown even
                // when the knob is hidden, because the channel is still live.
                char pct[16];
                snprintf(pct, sizeof(pct), "%d%%", (int)(vd->getChannelVolume(ch) * 100.0f + 0.5f));
                vd->drawText(right - PAD, textY, pct, ROW_SIZE,
                             on ? Palette::MUTED : Palette::DISABLED, xplm_JustRight);
                continue;
            }

            // Add-on row: whose channel it is, whether we found it, and the dataref
            // itself so a support question can be answered from a screenshot.
            float ownerX = left + PAD + BOX + 10.0f + vd->measureText(def.display, ROW_SIZE) + 8.0f;
            if (def.owner != nullptr) {
                vd->drawText(ownerX, textY, def.owner, SMALL_SIZE, Palette::MUTED, xplm_JustLeft);
            }

            const char* status;
            const float* statusColor;
            if (!available) {
                status = "not detected";
                statusColor = Palette::DISABLED;
            } else if (vd->isChannelEnabled(ch) && vd->isKnobLocked(ch)) {
                status = "read-only";
                statusColor = Palette::SAVE_DIRTY;
            } else {
                status = "detected";
                statusColor = Palette::SAVE_CLEAN;
            }
            vd->drawText(right - PAD, textY, status, SMALL_SIZE, statusColor, xplm_JustRight);

            vd->drawText(left + PAD + BOX + 10.0f, row.bottom + 8.0f, def.dataref,
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
            vd->saveConfig();
            return 1;
        }

        for (size_t i = 0; i < lay.rows.size(); i++) {
            const Row& row = lay.rows[i];
            if (y > row.top || y < row.bottom) continue;
            if (x < l + PAD || x > r - PAD) continue;

            // The whole row is the target, not just the box -- and an undetected
            // add-on can still be switched on, so the preference is already in place
            // when the add-on does turn up.
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

// Height the content actually needs, so the window is never cut off when the
// registry grows another add-on.
float requiredHeight() {
    Layout lay;
    computeLayout(0.0f, 0.0f, WIN_W, lay);
    return -lay.contentBottom;
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
    // Fixed height: the content is a list, not something that reflows.
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
    XPLMSetWindowIsVisible(g_window, show ? 1 : 0);
    if (show) {
        // The panel's mouse sink covers the whole screen in this same layer, so make
        // sure we are the one that sees these clicks.
        XPLMBringWindowToFront(g_window);
    }
}
