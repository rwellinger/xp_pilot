#include "runway_data.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
// apt.dat row codes we care about. 101 (water) carries no centerline.
constexpr const char *ROW_AIRPORT       = "1";
constexpr const char *ROW_SEAPLANE_BASE = "16";
constexpr const char *ROW_HELIPORT      = "17";
constexpr const char *ROW_LAND_RUNWAY   = "100";
constexpr const char *ROW_HELIPAD       = "102";

// "102 <designator> <lat> <lon> <heading> <length> <width> ..."
constexpr size_t HELIPAD_MIN_FIELDS = 4;

// "100 <width> <surface> <shoulder> <smoothness> <cl_lights> <edge_lights> <signs>"
// followed by two ends of nine fields each.
constexpr size_t RUNWAY_HEADER_FIELDS = 8;
constexpr size_t RUNWAY_END_FIELDS    = 9;
constexpr size_t RUNWAY_TOTAL_FIELDS  = RUNWAY_HEADER_FIELDS + 2 * RUNWAY_END_FIELDS;

bool starts_with_row_code(const std::string &line, const char *code)
{
    const size_t len = std::strlen(code);
    return line.size() > len && line.compare(0, len, code) == 0 && std::isspace(static_cast<unsigned char>(line[len]));
}

std::vector<std::string> split_whitespace(const std::string &line)
{
    std::vector<std::string> fields;
    std::istringstream       in(line);
    std::string              token;
    while (in >> token)
        fields.push_back(token);
    return fields;
}

double to_double(const std::string &s)
{
    return std::strtod(s.c_str(), nullptr);
}

RunwayEnd parse_runway_end(const std::vector<std::string> &fields, size_t first)
{
    RunwayEnd end;
    end.designator            = fields[first];
    end.lat                   = to_double(fields[first + 1]);
    end.lon                   = to_double(fields[first + 2]);
    end.displaced_threshold_m = static_cast<float>(to_double(fields[first + 3]));
    return end;
}

bool parse_land_runway(const std::vector<std::string> &fields, Runway &out)
{
    if (fields.size() < RUNWAY_TOTAL_FIELDS)
        return false;

    out.width_m = static_cast<float>(to_double(fields[1]));
    out.ends[0] = parse_runway_end(fields, RUNWAY_HEADER_FIELDS);
    out.ends[1] = parse_runway_end(fields, RUNWAY_HEADER_FIELDS + RUNWAY_END_FIELDS);
    return true;
}

bool parse_helipad(const std::vector<std::string> &fields, Helipad &out)
{
    if (fields.size() < HELIPAD_MIN_FIELDS)
        return false;

    out.lat = to_double(fields[2]);
    out.lon = to_double(fields[3]);
    return true;
}

// Seaplane bases and heliports open a new airport block just like land airports do;
// missing them would hand their helipads to whatever airport preceded them.
bool is_airport_row(const std::string &code)
{
    return code == ROW_AIRPORT || code == ROW_SEAPLANE_BASE || code == ROW_HELIPORT;
}

bool is_wanted_row(const std::string &line)
{
    return starts_with_row_code(line, ROW_AIRPORT) || starts_with_row_code(line, ROW_SEAPLANE_BASE) ||
           starts_with_row_code(line, ROW_HELIPORT) || starts_with_row_code(line, ROW_LAND_RUNWAY) ||
           starts_with_row_code(line, ROW_HELIPAD);
}

// The airport row is "1 <elevation> <tower> <deprecated> <ICAO> <name...>". Field 4 is
// the identifier XPLMGetNavAidInfo also reports, so it matches what the logger recorded.
std::string airport_id_of(const std::vector<std::string> &fields)
{
    return fields.size() > 4 ? fields[4] : std::string();
}
} // namespace

std::map<std::string, AirportSurfaces> RunwayData::load_surfaces(const std::string           &apt_dat_path,
                                                                 const std::set<std::string> &icaos)
{
    std::map<std::string, AirportSurfaces> found;
    if (icaos.empty())
        return found;

    std::ifstream file(apt_dat_path);
    if (!file.is_open())
        return found;

    std::string current_icao;
    std::string line;
    while (std::getline(file, line))
    {
        // Cheap prefilter — apt.dat has a dozen other row codes starting with '1'
        // (110 pavement, 1302 metadata, ...) and tokenizing them all is the bulk of
        // the cost on a 12-million-line file.
        if (!is_wanted_row(line))
            continue;

        std::vector<std::string> fields = split_whitespace(line);
        if (fields.empty())
            continue;

        if (is_airport_row(fields[0]))
        {
            const std::string id = airport_id_of(fields);
            current_icao         = icaos.count(id) ? id : std::string();
            if (!current_icao.empty())
                found.emplace(current_icao, AirportSurfaces());
            else if (found.size() == icaos.size())
                break; // every requested airport is complete
            continue;
        }

        if (current_icao.empty())
            continue;

        if (fields[0] == ROW_LAND_RUNWAY)
        {
            Runway runway;
            if (parse_land_runway(fields, runway))
                found[current_icao].runways.push_back(runway);
        }
        else if (fields[0] == ROW_HELIPAD)
        {
            Helipad helipad;
            if (parse_helipad(fields, helipad))
                found[current_icao].helipads.push_back(helipad);
        }
    }

    return found;
}

std::map<std::string, std::vector<Runway>> RunwayData::load_runways(const std::string           &apt_dat_path,
                                                                    const std::set<std::string> &icaos)
{
    std::map<std::string, std::vector<Runway>> runways;
    for (auto &[icao, surfaces] : load_surfaces(apt_dat_path, icaos))
        runways.emplace(icao, std::move(surfaces.runways));
    return runways;
}
