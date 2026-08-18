#include "airspace_cache.hpp"

#include <cmath>
#include <mutex>
#include <thread>

namespace
{

std::string s_airspace_path; // set once by init(), read by the worker

std::mutex            s_mutex;
AirspaceBounds        s_cached_bounds{};  // guarded by s_mutex
std::vector<Airspace> s_cached;           // guarded by s_mutex
bool                  s_have_cache = false; // guarded by s_mutex

std::thread    s_loader;
bool           s_loading = false;       // UI thread only
AirspaceBounds s_loading_bounds{};      // written before the thread starts, read by it

bool same_bounds(const AirspaceBounds &a, const AirspaceBounds &b)
{
    constexpr double epsilon = 1e-9;
    return std::fabs(a.lat_min - b.lat_min) < epsilon && std::fabs(a.lat_max - b.lat_max) < epsilon &&
           std::fabs(a.lon_min - b.lon_min) < epsilon && std::fabs(a.lon_max - b.lon_max) < epsilon;
}

} // namespace

void AirspaceCache::init(const std::string &airspace_txt_path) { s_airspace_path = airspace_txt_path; }

void AirspaceCache::stop()
{
    if (s_loader.joinable())
        s_loader.join();
    s_loading = false;

    std::lock_guard<std::mutex> lock(s_mutex);
    s_cached.clear();
    s_have_cache = false;
}

std::vector<Airspace> AirspaceCache::for_bounds(const AirspaceBounds &bounds)
{
    if (s_airspace_path.empty())
        return {};

    // Reap a finished load before deciding whether another one is needed.
    if (s_loading)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_have_cache && same_bounds(s_cached_bounds, s_loading_bounds))
        {
            if (s_loader.joinable())
                s_loader.join();
            s_loading = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_have_cache && same_bounds(s_cached_bounds, bounds))
            return s_cached;
    }

    if (s_loading)
        return {}; // a load is in flight; the next view change will pick this one up

    // The worker reads s_loading_bounds and s_airspace_path rather than capturing them:
    // building captures could throw, and an exception escaping a thread entry point
    // terminates X-Plane. Both are written before the thread starts and only rewritten
    // once it has been joined.
    s_loading_bounds = bounds;
    s_loading        = true;
    s_loader         = std::thread(
        []() noexcept
        {
            try
            {
                auto found = AirspaceData::load_airspaces(s_airspace_path, s_loading_bounds);

                std::lock_guard<std::mutex> lock(s_mutex);
                s_cached_bounds = s_loading_bounds;
                s_cached        = std::move(found);
                s_have_cache    = true;
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_cached_bounds = s_loading_bounds;
                s_cached.clear();
                s_have_cache = true; // remember the attempt so it is not retried every frame
            }
        });

    return {};
}
