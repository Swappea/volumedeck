#include "Channels.h"
#include <cstring>

namespace Channels {

const ChannelDef DEFS[] = {
    // X-Plane's own channels. These exist from the moment the sim is up.
    { "master",   "Master",   nullptr, nullptr, "sim/operation/sound/master_volume_ratio",   CH_SIM, 0.0f, 1.0f },
    { "exterior", "Exterior", nullptr, nullptr, "sim/operation/sound/exterior_volume_ratio", CH_SIM, 0.0f, 1.0f },
    { "interior", "Interior", nullptr, nullptr, "sim/operation/sound/interior_volume_ratio", CH_SIM, 0.0f, 1.0f },
    { "pilot",    "Pilot",    nullptr, nullptr, "sim/operation/sound/pilot_volume_ratio",    CH_SIM, 0.0f, 1.0f },
    { "copilot",  "Copilot",  nullptr, nullptr, "sim/operation/sound/copilot_volume_ratio",  CH_SIM, 0.0f, 1.0f },
    { "radio",    "Radio",    nullptr, nullptr, "sim/operation/sound/radio_volume_ratio",    CH_SIM, 0.0f, 1.0f },
    { "enviro",   "Enviro",   nullptr, nullptr, "sim/operation/sound/enviro_volume_ratio",   CH_SIM, 0.0f, 1.0f },
    { "ui",       "UI",       nullptr, nullptr, "sim/operation/sound/ui_volume_ratio",       CH_SIM, 0.0f, 1.0f },

    // Third-party channels. Their datarefs belong to another plugin, so they may not
    // exist at all (add-on not installed) or may appear after we enable (plugin load
    // order is not guaranteed) -- VolumeDeck re-looks them up from the flight loop.
    //
    // X-ATC-Chatter by SRS. Verified against X-Plane 12.4.4 with the add-on running:
    // the dataref is float, is_writable, sits in 0..1, accepts a written value and is
    // not re-asserted on the next frame, so the standard writability probe passes.
    // Signature read from the shipped binary; Plugin Admin shows it as
    // "X-ATC-Chatter Player".
    { "xatc_chatter", "Chatter", "X-ATC-Chatter", "SRS.X-ATC-Chatter",
      "SRS/X-ATC-Chatter/chatter_volume", CH_ADDON, 0.0f, 1.0f },
};

static_assert(sizeof(DEFS) / sizeof(DEFS[0]) == COUNT,
              "Channels::COUNT must match the DEFS table -- VolumeCommands sizes its "
              "static binding table from COUNT.");

int findBySlug(const char* slug) {
    if (slug == nullptr) return -1;
    for (int i = 0; i < COUNT; i++) {
        if (strcmp(DEFS[i].slug, slug) == 0) return i;
    }
    return -1;
}

}   // namespace Channels
