#include "map_overlay_cache.hpp"

#include <cmath>
#include <mutex>
#include <thread>

namespace
{

struct Bounds
{
    double lat_min = 0, lat_max = 0, lon_min = 0, lon_max = 0;
};

std::string s_airspace_path;  // set once by init(), read by the worker
std::string s_coastline_path; // ditto

std::mutex                     s_mutex;
Bounds                         s_cached_bounds{};    // guarded by s_mutex
MapOverlayCache::Overlay       s_cached;             // guarded by s_mutex
bool                           s_have_cache = false; // guarded by s_mutex

std::thread s_loader;
bool        s_loading = false;  // UI thread only
Bounds      s_loading_bounds{}; // written before the thread starts, read by it

bool same(const Bounds &a, const Bounds &b)
{
    constexpr double epsilon = 1e-9;
    return std::fabs(a.lat_min - b.lat_min) < epsilon && std::fabs(a.lat_max - b.lat_max) < epsilon &&
           std::fabs(a.lon_min - b.lon_min) < epsilon && std::fabs(a.lon_max - b.lon_max) < epsilon;
}

} // namespace

void MapOverlayCache::init(const std::string &airspace_txt_path, const std::string &coastlines_dat_path)
{
    s_airspace_path  = airspace_txt_path;
    s_coastline_path = coastlines_dat_path;
}

void MapOverlayCache::stop()
{
    if (s_loader.joinable())
        s_loader.join();
    s_loading = false;

    std::lock_guard<std::mutex> lock(s_mutex);
    s_cached     = Overlay();
    s_have_cache = false;
}

MapOverlayCache::Overlay MapOverlayCache::for_bounds(double lat_min, double lat_max, double lon_min, double lon_max)
{
    if (s_airspace_path.empty() && s_coastline_path.empty())
        return {};

    const Bounds wanted{lat_min, lat_max, lon_min, lon_max};

    // Reap a finished load before deciding whether another one is needed.
    if (s_loading)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_have_cache && same(s_cached_bounds, s_loading_bounds))
        {
            if (s_loader.joinable())
                s_loader.join();
            s_loading = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_have_cache && same(s_cached_bounds, wanted))
            return s_cached;
    }

    if (s_loading)
        return {}; // a load is in flight; the next view change picks this one up

    // The worker reads the globals rather than capturing them: building captures could
    // throw, and an exception escaping a thread entry point terminates X-Plane. They are
    // written before the thread starts and only rewritten once it has been joined.
    s_loading_bounds = wanted;
    s_loading        = true;
    s_loader         = std::thread(
        []() noexcept
        {
            Overlay loaded;
            try
            {
                const Bounds &b = s_loading_bounds;
                if (!s_airspace_path.empty())
                    loaded.airspaces =
                        AirspaceData::load_airspaces(s_airspace_path, {b.lat_min, b.lat_max, b.lon_min, b.lon_max});
                if (!s_coastline_path.empty())
                    loaded.outlines =
                        CoastlineData::load_outlines(s_coastline_path, {b.lat_min, b.lat_max, b.lon_min, b.lon_max});
            }
            catch (...) // NOLINT(bugprone-empty-catch) — a failed load just means no overlay
            {
                loaded = Overlay();
            }

            std::lock_guard<std::mutex> lock(s_mutex);
            s_cached_bounds = s_loading_bounds;
            s_cached        = std::move(loaded);
            s_have_cache    = true; // remember the attempt so it is not retried every frame
        });

    return {};
}
