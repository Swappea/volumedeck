#ifndef VOLUME_SETTINGS_WINDOW_H
#define VOLUME_SETTINGS_WINDOW_H

// The Settings window, opened from Plugins > VolumeDeck > Settings.
//
// It is a second XPLMCreateWindowEx window rather than an XPWidgets dialog: widgets
// are legacy, draw through the OpenGL bridge, and would not match the panel. This one
// draws with XPLMPanelGraphics through VolumeDeck's own font and palette, in the same
// boxel coordinate space as everything else.
//
// Like the panel, every piece of geometry comes from one function (computeLayout), so
// the row you see and the row your click lands on cannot drift apart.
namespace SettingsWindow {
    void create();    // XPluginEnable, after VolumeDeck::initialize()
    void destroy();   // XPluginDisable
    void toggle();
    bool isVisible();
}

#endif // VOLUME_SETTINGS_WINDOW_H
