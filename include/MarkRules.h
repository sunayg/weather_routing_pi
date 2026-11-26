// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <utility>
#include <vector>

// Geometry for per-mark routing rules: a keep-out radius around a mark and an
// optional "leave the mark to port / starboard" requirement. Pure functions on
// latitude/longitude in degrees and distances in nautical miles, with no
// wxWidgets or OpenCPN dependency, so they can be unit-tested on their own.
//
// All tests run in a flat plane centred on the mark (x east, y north, in
// nautical miles). That is accurate to well under a metre at the scale of a
// race course; it is not meant for legs of hundreds of miles.
namespace weather_routing {

enum class MarkSide {
  Any,               // no side requirement
  LeaveToPort,       // the mark must be on the boat's port side when passed
  LeaveToStarboard,  // the mark must be on the boat's starboard side
};

struct MarkRule {
  double lat = 0, lon = 0;  // the mark
  double clearance_nm = 0;  // keep-out radius; 0 disables it
  MarkSide side = MarkSide::Any;
  // A routing step may not cross the barrier: a straight line from the mark,
  // barrier_length_nm long, towards barrier_bearing_deg (true). It lies on the
  // side the boat must NOT pass, so a route can only get past the mark on the
  // wrong side by going round the barrier's far end. Ignored for MarkSide::Any.
  double barrier_bearing_deg = 0;
  double barrier_length_nm = 0;
};

// Initial true bearing, degrees [0, 360), of the straight line from point 0 to
// point 1 (flat-plane approximation; see above).
double TravelBearingDeg(double lat0, double lon0, double lat1, double lon1);

// Build a rule. travel_bearing_deg is the direction the boat is travelling past
// the mark (for example the bearing from the previous waypoint to the next
// one); it decides which side the barrier goes on. Choose barrier_length_nm
// long enough to reach land or clear water on the forbidden side, because a
// short barrier can be sailed round.
MarkRule MakeMarkRule(double lat, double lon, double clearance_nm,
                      MarkSide side, double travel_bearing_deg,
                      double barrier_length_nm);

// True when the straight step (lat0,lon0) -> (lat1,lon1) breaks the rule:
//  - it comes closer to the mark than clearance_nm, except that a step that
//    starts inside the radius and only moves away from the mark is allowed, so
//    a boat that begins inside the radius can leave it; or
//  - it crosses or touches the barrier (side != Any only).
bool SegmentViolatesMarkRule(const MarkRule& rule, double lat0, double lon0,
                             double lat1, double lon1);

// True when the step breaks any rule in the list.
bool SegmentViolatesAnyMarkRule(const std::vector<MarkRule>& rules,
                                double lat0, double lon0, double lat1,
                                double lon1);

// A point `nm` nautical miles from (lat, lon) along true bearing `bearing_deg`
// (flat-plane approximation, as above).
void OffsetPoint(double lat, double lon, double bearing_deg, double nm,
                 double* out_lat, double* out_lon);

// ---- Passage planning --------------------------------------------------
// A passage is an ordered list of marks: start, via marks, finish. Each via
// mark can be passed on a chosen side with a minimum clearance. Extra "avoid"
// marks only keep the route a minimum distance away.
struct PassageMark {
  double lat = 0, lon = 0;
};
struct ViaSpec {
  MarkSide side = MarkSide::Any;
  double clearance_nm = 0;
};
struct AvoidMark {
  double lat = 0, lon = 0;
  double clearance_nm = 0;
};

// One routing leg. When `end_is_mark` / `start_is_mark` is false the leg
// starts or ends at a passing point beside the mark (on the side the rule
// requires) instead of on the mark itself, because a leg that ended exactly on
// the mark would break its own rule. `rules` holds only the rules that apply to
// this leg: the via marks at its two ends and every avoid mark. A barrier that
// belongs to a mark on another leg never blocks this one.
struct PassageLeg {
  double start_lat = 0, start_lon = 0, end_lat = 0, end_lon = 0;
  bool start_is_mark = true, end_is_mark = true;
  std::vector<MarkRule> rules;
};

// marks.size() >= 2; via.size() must equal marks.size() - 2 (one per
// intermediate mark). Returns marks.size() - 1 legs, or an empty vector when
// the input is inconsistent.
std::vector<PassageLeg> PlanPassageLegs(const std::vector<PassageMark>& marks,
                                  const std::vector<ViaSpec>& via,
                                  const std::vector<AvoidMark>& avoid,
                                  double barrier_length_nm = 20.0,
                                  double pass_offset_nm = 0.15);

// ---- Checking a computed route -------------------------------------------
// How a computed path (route points, in order) passed one mark.
struct PassReport {
  bool valid = false;       // the path has at least one segment
  double closest_nm = 0;    // closest approach to the mark
  MarkSide side = MarkSide::Any;  // LeaveToPort: the mark was on the boat's
                                  // port side at the closest approach
  bool ok = false;          // clearance and side both met
};
PassReport CheckPass(const MarkRule& rule,
                     const std::vector<std::pair<double, double>>& path);

}  // namespace weather_routing
