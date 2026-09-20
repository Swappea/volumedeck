#include "VolumeCommands.h"
#include "VolumeDeck.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include <cstdio>

namespace {

enum CmdKind {
    CMD_ADJUST,
    CMD_MUTE,
    CMD_PANEL_TOGGLE,
    CMD_PANEL_LAYOUT,
    CMD_PANEL_SAVE
};

struct CmdBinding {
    const char*    name;
    const char*    description;
    CmdKind        kind;
    int            knob;            // knob index; -1 for the panel commands
    int            clicks;          // step direction for CMD_ADJUST
    XPLMCommandRef ref;
    float          nextRepeatTime;  // XPLMGetElapsedTime() deadline for the next repeat
};

// Must match knobNames[] in VolumeDeck.cpp -- these become the command names.
const char* const CHANNELS[] = {
    "master", "exterior", "interior", "pilot", "copilot", "radio", "enviro", "ui"
};
const int NUM_CHANNELS = 8;

// up + down + mute_toggle per channel, plus panel/toggle and panel/save.
const int NUM_COMMANDS = NUM_CHANNELS * 3 + 3;

// xplm_CommandContinue fires every frame; at 60fps an unthrottled 0.02 step would
// sweep the full range in well under a second.
const float REPEAT_INTERVAL = 0.1f;   // ~10 steps/sec while held

CmdBinding g_commands[NUM_COMMANDS];
char       g_names[NUM_COMMANDS][64];
char       g_descs[NUM_COMMANDS][96];
bool       g_created = false;
bool       g_registered = false;

void addCommand(int& n, const char* name, const char* desc, CmdKind kind, int knob, int clicks) {
    snprintf(g_names[n], sizeof(g_names[n]), "%s", name);
    snprintf(g_descs[n], sizeof(g_descs[n]), "%s", desc);
    g_commands[n].name           = g_names[n];
    g_commands[n].description    = g_descs[n];
    g_commands[n].kind           = kind;
    g_commands[n].knob           = knob;
    g_commands[n].clicks         = clicks;
    g_commands[n].ref            = nullptr;
    g_commands[n].nextRepeatTime = 0.0f;
    n++;
}

void buildTable() {
    int n = 0;
    char name[64];
    char desc[96];

    for (int c = 0; c < NUM_CHANNELS; c++) {
        snprintf(name, sizeof(name), "volumedeck/%s/up", CHANNELS[c]);
        snprintf(desc, sizeof(desc), "VolumeDeck: %s volume up", CHANNELS[c]);
        addCommand(n, name, desc, CMD_ADJUST, c, 1);

        snprintf(name, sizeof(name), "volumedeck/%s/down", CHANNELS[c]);
        snprintf(desc, sizeof(desc), "VolumeDeck: %s volume down", CHANNELS[c]);
        addCommand(n, name, desc, CMD_ADJUST, c, -1);

        snprintf(name, sizeof(name), "volumedeck/%s/mute_toggle", CHANNELS[c]);
        snprintf(desc, sizeof(desc), "VolumeDeck: mute/unmute %s", CHANNELS[c]);
        addCommand(n, name, desc, CMD_MUTE, c, 0);
    }

    addCommand(n, "volumedeck/panel/toggle",
               "VolumeDeck: show/hide the volume panel", CMD_PANEL_TOGGLE, -1, 0);
    addCommand(n, "volumedeck/panel/save",
               "VolumeDeck: save volumes for this aircraft", CMD_PANEL_SAVE, -1, 0);
    addCommand(n, "volumedeck/panel/layout_toggle",
               "VolumeDeck: switch between column and row layout", CMD_PANEL_LAYOUT, -1, 0);
}

int commandHandler(XPLMCommandRef /*inCommand*/, XPLMCommandPhase inPhase, void* inRefcon) {
    CmdBinding* b = static_cast<CmdBinding*>(inRefcon);
    if (b == nullptr) return 1;

    try {
        VolumeDeck* vc = VolumeDeck::getInstance();

        // The startup probe spends its first few seconds writing and restoring every
        // channel; anything we did here would be overwritten by it.
        if (!vc->isReady()) return 1;

        switch (b->kind) {
            case CMD_PANEL_TOGGLE:
                if (inPhase == xplm_CommandBegin) vc->toggleControlBox();
                break;

            case CMD_PANEL_SAVE:
                if (inPhase == xplm_CommandBegin) vc->saveConfig();
                break;

            case CMD_PANEL_LAYOUT:
                if (inPhase == xplm_CommandBegin) vc->toggleLayout();
                break;

            case CMD_MUTE:
                if (inPhase == xplm_CommandBegin) vc->muteToggle(b->knob);
                break;

            case CMD_ADJUST:
                if (inPhase == xplm_CommandBegin) {
                    vc->adjustKnobVolume(b->knob, b->clicks);
                    b->nextRepeatTime = XPLMGetElapsedTime() + REPEAT_INTERVAL;
                } else if (inPhase == xplm_CommandContinue) {
                    float now = XPLMGetElapsedTime();
                    if (now >= b->nextRepeatTime) {
                        vc->adjustKnobVolume(b->knob, b->clicks);
                        b->nextRepeatTime = now + REPEAT_INTERVAL;
                    }
                }
                break;
        }
    } catch (...) {
        XPLMDebugString("VolumeDeck: [ERROR] Exception in commandHandler!\n");
    }

    return 1;   // let other plugins and X-Plane see the command too
}

} // namespace

void VolumeCommands::create() {
    if (g_created) return;

    XPLMDebugString("VolumeDeck: [CMD] Creating commands...\n");
    buildTable();

    int failed = 0;
    for (int i = 0; i < NUM_COMMANDS; i++) {
        g_commands[i].ref = XPLMCreateCommand(g_commands[i].name, g_commands[i].description);
        if (g_commands[i].ref == nullptr) {
            char msg[128];
            snprintf(msg, sizeof(msg), "VolumeDeck: [ERROR] Failed to create command %s\n",
                     g_commands[i].name);
            XPLMDebugString(msg);
            failed++;
        }
    }

    g_created = true;

    char msg[128];
    snprintf(msg, sizeof(msg), "VolumeDeck: [CMD] Created %d commands (%d failed)\n",
             NUM_COMMANDS - failed, failed);
    XPLMDebugString(msg);
}

void VolumeCommands::registerHandlers() {
    if (!g_created || g_registered) return;

    for (int i = 0; i < NUM_COMMANDS; i++) {
        if (g_commands[i].ref != nullptr) {
            XPLMRegisterCommandHandler(g_commands[i].ref, commandHandler, 1, &g_commands[i]);
        }
    }

    g_registered = true;
    XPLMDebugString("VolumeDeck: [CMD] Command handlers registered\n");
}

void VolumeCommands::unregisterHandlers() {
    if (!g_registered) return;

    for (int i = 0; i < NUM_COMMANDS; i++) {
        if (g_commands[i].ref != nullptr) {
            XPLMUnregisterCommandHandler(g_commands[i].ref, commandHandler, 1, &g_commands[i]);
        }
    }

    g_registered = false;
    XPLMDebugString("VolumeDeck: [CMD] Command handlers unregistered\n");
}
