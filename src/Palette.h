#ifndef VOLUME_PALETTE_H
#define VOLUME_PALETTE_H

// Dark + amber, the classic avionics look. Shared by the panel (VolumeDeck.cpp) and
// the settings window (SettingsWindow.cpp) so the two cannot drift into different
// shades of the same idea.
//
// These are const arrays at namespace scope, so each translation unit gets its own
// internal-linkage copy -- no ODR problem from including this in several files.
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
    // Settings window / group captions: quieter than LABEL so headings and the owner
    // plugin name read as annotation rather than as another control.
    const float MUTED[4]      = {150/255.0f, 156/255.0f, 168/255.0f, 1.00f};
    const float DIVIDER[4]    = { 70/255.0f,  75/255.0f,  86/255.0f, 1.00f};
    const float WINDOW_BG[4]  = { 26/255.0f,  28/255.0f,  33/255.0f, 0.97f};
}

#endif // VOLUME_PALETTE_H
