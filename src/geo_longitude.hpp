#pragma once

// Longitude wraps at ±180°, which breaks anything that treats it as a plain number.
// A Pacific crossing steps from 179.99 to -179.99 and a naive bounding box then spans
// the whole planet: the flight from Auckland to Los Angeles rendered as a world map
// with the track jumping from one edge to the other.
//
// The fix is to stop wrapping: express every longitude relative to a reference, so a
// track heading east past the date line continues at 181, 182, ... rather than
// restarting at -179. Mercator's x axis is linear in longitude, so values beyond ±180
// project correctly without any further special casing.

namespace GeoLongitude
{
// `longitude` expressed near `reference`, within ±180° of it.
inline double unwrapped_near(double longitude, double reference)
{
    while (longitude - reference > 180.0)
        longitude -= 360.0;
    while (reference - longitude > 180.0)
        longitude += 360.0;
    return longitude;
}
} // namespace GeoLongitude
