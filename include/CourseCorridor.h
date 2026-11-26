// SPDX-License-Identifier: GPL-3.0-or-later
//
// The course-corridor limits (MaxCourseAngle, MaxDivertedCourse) as pure
// functions. Longitude differences are scaled by cos(latitude) the same way
// in every bearing that is compared. The previous code scaled one bearing by
// cos(lat)/2 and the other not at all, so a point on the direct line already
// showed a deviation (18-29 degrees on diagonal courses) and the corridor was
// lopsided (routing engine review I4).
#pragma once

#include <cmath>

namespace weather_routing {
namespace corridor {

inline double Wrap180(double d) {
  d = std::fmod(d + 180.0, 360.0);
  if (d < 0) d += 360.0;
  return d - 180.0;
}

// Bearing in degrees from point 0 to point 1 in a plane where longitude is
// scaled by cos(scale_lat).
inline double PlaneBearing(double lat0, double lon0, double lat1, double lon1,
                           double scale_lat) {
  const double dn = lat1 - lat0;
  const double de =
      Wrap180(lon1 - lon0) * std::cos(scale_lat * 3.14159265358979323846 / 180);
  return std::atan2(de, dn) * 180.0 / 3.14159265358979323846;
}
inline double PlaneDistance(double lat0, double lon0, double lat1, double lon1,
                            double scale_lat) {
  const double dn = lat1 - lat0;
  const double de =
      Wrap180(lon1 - lon0) * std::cos(scale_lat * 3.14159265358979323846 / 180);
  return std::hypot(de, dn);
}

// The point (lat, lon) is within max_angle of the start-to-end bearing, seen
// from the start.
inline bool WithinCourseAngle(double start_lat, double start_lon,
                              double start_end_bearing, double max_angle,
                              double lat, double lon) {
  if (max_angle >= 180) return true;
  const double b = PlaneBearing(start_lat, start_lon, lat, lon, lat);
  return std::fabs(Wrap180(start_end_bearing - b)) <= max_angle;
}

// The point (lat, lon) does not divert more than max_diverted from the line
// between start and end: the bearing back to the start and the bearing from
// the end agree within the limit, which widens near the end of the route.
inline bool WithinDivertedCourse(double start_lat, double start_lon,
                                 double end_lat, double end_lon,
                                 double max_diverted, double lat, double lon) {
  if (max_diverted >= 180) return true;
  const double from_end = PlaneBearing(end_lat, end_lon, lat, lon, lat);
  const double dist = PlaneDistance(end_lat, end_lon, lat, lon, lat);
  const double to_start = PlaneBearing(lat, lon, start_lat, start_lon, lat);
  const double dist1 = PlaneDistance(lat, lon, start_lat, start_lon, lat);
  if (dist <= 0) return true;
  double term = (dist1 + dist) / dist;
  term = std::pow(term / 16, 4) + 1;  // 1 until close to the end, then large
  return std::fabs(Wrap180(to_start - from_end)) <= max_diverted * term;
}

}  // namespace corridor
}  // namespace weather_routing
