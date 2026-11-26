// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarkRules.h"

#include <algorithm>
#include <cmath>

namespace weather_routing {
namespace {
constexpr double kDegToRad = 0.017453292519943295;
constexpr double kNmPerDegLat = 60.0;
constexpr double kEps = 1e-12;

struct Vec {
  double x, y;  // nautical miles east, north of the mark
};

double WrapDeg(double d) {
  d = std::fmod(d + 180.0, 360.0);
  if (d < 0) d += 360.0;
  return d - 180.0;
}

Vec ToPlane(double mark_lat, double mark_lon, double lat, double lon) {
  const double kx = std::cos(mark_lat * kDegToRad) * kNmPerDegLat;
  return {WrapDeg(lon - mark_lon) * kx, (lat - mark_lat) * kNmPerDegLat};
}

double Cross(const Vec& a, const Vec& b) { return a.x * b.y - a.y * b.x; }
Vec Sub(const Vec& a, const Vec& b) { return {a.x - b.x, a.y - b.y}; }

// Closest approach of segment p0-p1 to the origin; sets at_start when the
// closest point is p0 itself (the step never gets nearer than where it began).
double ClosestApproach(const Vec& p0, const Vec& p1, bool* at_start) {
  const Vec d = Sub(p1, p0);
  const double len2 = d.x * d.x + d.y * d.y;
  double t = 0;
  if (len2 > kEps) t = -(p0.x * d.x + p0.y * d.y) / len2;
  t = std::clamp(t, 0.0, 1.0);
  if (at_start) *at_start = (t <= 0.0);
  return std::hypot(p0.x + t * d.x, p0.y + t * d.y);
}

bool OnSegment(const Vec& a, const Vec& b, const Vec& p) {
  return p.x >= std::min(a.x, b.x) - kEps && p.x <= std::max(a.x, b.x) + kEps &&
         p.y >= std::min(a.y, b.y) - kEps && p.y <= std::max(a.y, b.y) + kEps;
}

// Closed-segment intersection test, touching and collinear overlap included.
bool SegmentsIntersect(const Vec& a, const Vec& b, const Vec& c, const Vec& d) {
  const double d1 = Cross(Sub(b, a), Sub(c, a));
  const double d2 = Cross(Sub(b, a), Sub(d, a));
  const double d3 = Cross(Sub(d, c), Sub(a, c));
  const double d4 = Cross(Sub(d, c), Sub(b, c));
  if (((d1 > kEps && d2 < -kEps) || (d1 < -kEps && d2 > kEps)) &&
      ((d3 > kEps && d4 < -kEps) || (d3 < -kEps && d4 > kEps)))
    return true;
  if (std::fabs(d1) <= kEps && OnSegment(a, b, c)) return true;
  if (std::fabs(d2) <= kEps && OnSegment(a, b, d)) return true;
  if (std::fabs(d3) <= kEps && OnSegment(c, d, a)) return true;
  if (std::fabs(d4) <= kEps && OnSegment(c, d, b)) return true;
  return false;
}
}  // namespace

double TravelBearingDeg(double lat0, double lon0, double lat1, double lon1) {
  const Vec v = ToPlane(lat0, lon0, lat1, lon1);
  double b = std::atan2(v.x, v.y) / kDegToRad;
  if (b < 0) b += 360.0;
  if (b >= 360.0) b -= 360.0;
  return b;
}

MarkRule MakeMarkRule(double lat, double lon, double clearance_nm,
                      MarkSide side, double travel_bearing_deg,
                      double barrier_length_nm) {
  MarkRule r;
  r.lat = lat;
  r.lon = lon;
  r.clearance_nm = clearance_nm;
  r.side = side;
  r.barrier_length_nm = barrier_length_nm;
  // Heading along the travel bearing, port is 90 degrees to the left. If the
  // mark is to be left to port the boat passes on the mark's starboard side, so
  // the forbidden side (where the barrier goes) is to the mark's port.
  double b = travel_bearing_deg;
  if (side == MarkSide::LeaveToPort) b -= 90.0;
  if (side == MarkSide::LeaveToStarboard) b += 90.0;
  b = std::fmod(b, 360.0);
  if (b < 0) b += 360.0;
  r.barrier_bearing_deg = b;
  return r;
}

bool SegmentViolatesMarkRule(const MarkRule& rule, double lat0, double lon0,
                             double lat1, double lon1) {
  const Vec p0 = ToPlane(rule.lat, rule.lon, lat0, lon0);
  const Vec p1 = ToPlane(rule.lat, rule.lon, lat1, lon1);

  if (rule.clearance_nm > 0) {
    bool at_start = false;
    const double closest = ClosestApproach(p0, p1, &at_start);
    const bool starts_inside = std::hypot(p0.x, p0.y) < rule.clearance_nm;
    if (closest < rule.clearance_nm && !(starts_inside && at_start))
      return true;
  }

  if (rule.side != MarkSide::Any && rule.barrier_length_nm > 0) {
    const double b = rule.barrier_bearing_deg * kDegToRad;
    const Vec wall_end = {rule.barrier_length_nm * std::sin(b),
                          rule.barrier_length_nm * std::cos(b)};
    if (SegmentsIntersect(p0, p1, {0.0, 0.0}, wall_end)) return true;
  }
  return false;
}

bool SegmentViolatesAnyMarkRule(const std::vector<MarkRule>& rules,
                                double lat0, double lon0, double lat1,
                                double lon1) {
  for (const MarkRule& r : rules)
    if (SegmentViolatesMarkRule(r, lat0, lon0, lat1, lon1)) return true;
  return false;
}

void OffsetPoint(double lat, double lon, double bearing_deg, double nm,
                 double* out_lat, double* out_lon) {
  const double b = bearing_deg * kDegToRad;
  const double kx = std::cos(lat * kDegToRad) * kNmPerDegLat;
  if (out_lat) *out_lat = lat + nm * std::cos(b) / kNmPerDegLat;
  if (out_lon) *out_lon = lon + nm * std::sin(b) / kx;
}

namespace {
double Norm360(double d) {
  d = std::fmod(d, 360.0);
  return d < 0 ? d + 360.0 : d;
}
double DistanceNm(double lat0, double lon0, double lat1, double lon1) {
  const Vec v = ToPlane(lat0, lon0, lat1, lon1);
  return std::hypot(v.x, v.y);
}
}  // namespace

std::vector<PassageLeg> PlanPassageLegs(const std::vector<PassageMark>& marks,
                                        const std::vector<ViaSpec>& via,
                                        const std::vector<AvoidMark>& avoid,
                                        double barrier_length_nm,
                                        double pass_offset_nm) {
  const size_t n = marks.size();
  if (n < 2 || via.size() != n - 2) return {};

  // A side rule is judged by each leg on its own: the leg into the mark by
  // its incoming direction, the leg out by its outgoing direction. Using the
  // chord from the previous mark to the next one (as before) gave an
  // arbitrary barrier at a rounding mark, where that chord is short, and put
  // the barrier through the start of an out-and-back course.
  struct Via {
    bool sided = false;
    MarkRule in, out;  // rules for the leg ending here / starting here
    double pass_lat = 0, pass_lon = 0;
  };
  std::vector<Via> v(n);
  for (size_t k = 1; k + 1 < n; ++k) {
    const ViaSpec& s = via[k - 1];
    if (s.side == MarkSide::Any) continue;  // pass through the mark itself
    const PassageMark& m = marks[k];
    const double b_in = TravelBearingDeg(marks[k - 1].lat, marks[k - 1].lon,
                                         m.lat, m.lon);
    const double b_out = TravelBearingDeg(m.lat, m.lon, marks[k + 1].lat,
                                          marks[k + 1].lon);
    const double len_in = DistanceNm(marks[k - 1].lat, marks[k - 1].lon,
                                     m.lat, m.lon);
    const double len_out = DistanceNm(m.lat, m.lon, marks[k + 1].lat,
                                      marks[k + 1].lon);
    // Long enough that the wrong side cannot be reached by a short detour,
    // short enough not to reach unrelated water on long passages.
    const double wall = std::clamp(2.0 * std::max(len_in, len_out), 1.0,
                                   barrier_length_nm);
    v[k].sided = true;
    v[k].in = MakeMarkRule(m.lat, m.lon, s.clearance_nm, s.side, b_in, wall);
    v[k].out = MakeMarkRule(m.lat, m.lon, s.clearance_nm, s.side, b_out, wall);

    // Passing point: halfway round the turn, on the allowed side. With the
    // mark to port the boat turns left round it (to starboard: right), so a
    // turn towards the mark of up to 210 degrees is a rounding, including a
    // leeward mark a little either side of the start. Anything beyond that is
    // read as a bend away from the mark (at most 150 degrees).
    const bool port = s.side == MarkSide::LeaveToPort;
    const double toward = Norm360(port ? b_in - b_out : b_out - b_in);
    const double turn = toward <= 210.0 ? toward : toward - 360.0;
    const double allowed_in = port ? b_in + 90.0 : b_in - 90.0;
    const double away = Norm360(port ? allowed_in - turn / 2.0
                                     : allowed_in + turn / 2.0);
    const double off = std::max(pass_offset_nm, s.clearance_nm + 0.05);
    OffsetPoint(m.lat, m.lon, away, off, &v[k].pass_lat, &v[k].pass_lon);
  }

  std::vector<PassageLeg> legs(n - 1);
  for (size_t i = 0; i + 1 < n; ++i) {
    PassageLeg& leg = legs[i];
    const size_t a = i, b = i + 1;
    leg.start_is_mark = !v[a].sided;
    leg.end_is_mark = !v[b].sided;
    leg.start_lat = v[a].sided ? v[a].pass_lat : marks[a].lat;
    leg.start_lon = v[a].sided ? v[a].pass_lon : marks[a].lon;
    leg.end_lat = v[b].sided ? v[b].pass_lat : marks[b].lat;
    leg.end_lon = v[b].sided ? v[b].pass_lon : marks[b].lon;
    if (v[a].sided) leg.rules.push_back(v[a].out);
    if (v[b].sided) leg.rules.push_back(v[b].in);
    for (const AvoidMark& m : avoid) {
      if (m.clearance_nm <= 0) continue;
      // A leg cannot start or finish inside a keep-clear circle; such a mark
      // sits on the course itself and is dropped for this leg.
      if (DistanceNm(m.lat, m.lon, leg.start_lat, leg.start_lon) <
              m.clearance_nm ||
          DistanceNm(m.lat, m.lon, leg.end_lat, leg.end_lon) < m.clearance_nm)
        continue;
      leg.rules.push_back(
          MakeMarkRule(m.lat, m.lon, m.clearance_nm, MarkSide::Any, 0, 0));
    }
  }
  return legs;
}

PassReport CheckPass(const MarkRule& rule,
                     const std::vector<std::pair<double, double>>& path) {
  PassReport r;
  if (path.size() < 2) return r;
  r.valid = true;
  double best = 1e300, cross = 0;
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    const Vec a = ToPlane(rule.lat, rule.lon, path[i].first, path[i].second);
    const Vec b =
        ToPlane(rule.lat, rule.lon, path[i + 1].first, path[i + 1].second);
    const double d = ClosestApproach(a, b, nullptr);
    if (d < best) {
      best = d;
      // Mark (the origin) left of the direction of travel => on port.
      cross = Cross(Sub(b, a), Sub({0.0, 0.0}, a));
    }
  }
  r.closest_nm = best;
  r.side = cross > 0 ? MarkSide::LeaveToPort : MarkSide::LeaveToStarboard;
  const bool clear = best + 1e-9 >= rule.clearance_nm;
  const bool sideOk = rule.side == MarkSide::Any || rule.side == r.side;
  r.ok = clear && sideOk;
  return r;
}

}  // namespace weather_routing
