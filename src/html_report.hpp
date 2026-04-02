#pragma once
#include <ctime>
#include <string>
#include <vector>

// ── Data structures shared between flight_logger and logbook_ui ───────────────

struct TrackPoint
{
    time_t t   = 0;
    double lat = 0, lon = 0;
    int    alt_ft = 0, spd_kts = 0, vs_fpm = 0;
};

struct LandingData
{
    float       fpm            = 0;
    float       g_force        = 0;
    float       pitch_deg      = 0;
    float       pitch_rate     = 0;
    float       agl_ft         = 0;
    float       float_time     = 0;
    time_t      time           = 0;
    int         wind_speed_kts = 0;
    int         wind_dir_mag   = 0;
    int         headwind_kts   = 0;
    int         crosswind_kts  = 0;
    std::string flare;
    std::string rating;
    std::string wind_status;
    std::string crosswind_side;
};

struct FlightData
{
    std::string              filename;  // basename, e.g. "2026-04-01_LSZB_LSGG_DA42.json"
    std::string              date;      // "YYYY-MM-DD"
    std::string              start_utc; // "HH:MM"
    std::string              end_utc;   // "HH:MM"
    std::string              departure_icao;
    std::string              arrival_icao;
    std::string              aircraft_icao;
    std::string              aircraft_tail;
    time_t                   start_time      = 0;
    time_t                   end_time        = 0;
    int                      block_time_min  = 0;
    int                      max_altitude_ft = 0;
    int                      max_speed_kts   = 0;
    std::vector<TrackPoint>  track;
    std::vector<LandingData> landings;
};

// ── HTML report / index generation ───────────────────────────────────────────

namespace HtmlReport
{
// Render one HTML flight report. Returns report filename (basename) or "".
std::string generate(const FlightData &fd, const std::string &data_dir, const std::string &json_filename,
                     const std::string &profile_name, const std::array<int, 4> &profile_thresholds);

// Regenerate index.html from all JSON files in data_dir/flights/.
void generate_index(const std::string &data_dir);
} // namespace HtmlReport

// ── JSON parsing (for logbook and report regeneration) ─────────────────────

FlightData parse_flight_json(const std::string &json, const std::string &filename);
