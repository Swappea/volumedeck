#ifndef VOLUME_COMMANDS_H
#define VOLUME_COMMANDS_H

// Custom X-Plane commands for the volume knobs, so the panel can be driven from
// a keyboard/joystick binding or over the local web API (Stream Deck and friends)
// instead of only by the mouse.
//
// Commands outlive the plugin that created them (see XPLMUtilities.h), so they
// are created once at XPluginStart and only the handlers come and go.
namespace VolumeCommands {
    void create();              // XPluginStart
    void registerHandlers();    // XPluginEnable
    void unregisterHandlers();  // XPluginDisable
}

#endif // VOLUME_COMMANDS_H
