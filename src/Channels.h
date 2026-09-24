#ifndef VOLUME_CHANNELS_H
#define VOLUME_CHANNELS_H

// The single source of truth for every channel the panel can drive.
//
// This used to be two hand-maintained lists of the same eight strings -- knobNames[]
// in VolumeDeck.cpp and CHANNELS[] in VolumeCommands.cpp -- which could silently
// drift apart and rename a command out from under someone's keybinding. Both now
// read this table instead.
//
// ORDER IS PART OF THE FILE FORMAT AND THE COMMAND NAMES. A channel's index is what
// VolumeCommands binds to and what the per-aircraft config line positions by, and a
// channel stays in the table even when its dataref is missing or the user has turned
// it off. Append new channels at the end; never reorder or delete.

enum ChannelKind {
    CH_SIM   = 0,   // X-Plane's own sim/operation/sound/* channels
    CH_ADDON = 1    // a channel owned by a third-party plugin
};

struct ChannelDef {
    const char* slug;       // command slug and config key ("master", "xatc_chatter")
    const char* display;    // knob label ("Master", "Chatter")
    const char* owner;      // owning plugin, shown as the group caption
    // XPLM plugin signature of the owner, or nullptr for X-Plane's own channels.
    // Checked alongside the dataref: a plugin switched off in Plugin Admin may leave
    // its datarefs registered, so the dataref alone cannot tell us the add-on is
    // actually running. XPLMIsPluginEnabled() can.
    const char* pluginSignature;
    const char* dataref;
    ChannelKind kind;
    // The dataref units that correspond to a knob at 0.0 and at 1.0. Everything above
    // the getVolume/setVolume pair works in normalised 0..1, so an add-on that stores
    // 0..100 or decibels only needs its numbers here.
    float       minValue;
    float       maxValue;
};

namespace Channels {

// Compile-time so VolumeCommands can still size its static binding table.
// static_assert in Channels.cpp keeps it honest against DEFS[].
const int COUNT = 9;

// Deliberately declared WITHOUT a bound. Given the bound here, the static_assert in
// Channels.cpp would be checking sizeof against the very number it was told, and a
// COUNT bumped without adding the row would compile into a zero-filled entry -- whose
// null slug then reaches strcmp() and std::string at startup. Unsized, the definition
// completes the type from its initialisers and the assert can actually fail.
extern const ChannelDef DEFS[];

inline const ChannelDef& get(int index) { return DEFS[index]; }

int findBySlug(const char* slug);   // -1 when unknown

}   // namespace Channels

#endif // VOLUME_CHANNELS_H
