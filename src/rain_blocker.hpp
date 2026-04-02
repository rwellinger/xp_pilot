#pragma once

// rain_blocker — suppress X-Plane's 3D rain particle effect above 120 kts.
// Port of no_starwars_rain.lua.
// Hysteresis: rain off >120 kts GS, rain back on <80 kts GS.

namespace RainBlocker {
    void init();
    void stop();
    void toggle();
    bool enabled();
}
