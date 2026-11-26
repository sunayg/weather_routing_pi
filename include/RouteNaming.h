// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plain-language names for the routes list (docs/ui-redesign.md, main
// window): "Dolphin to Red Buoy, Green Mark to port" instead of separate
// start and end columns, and passage legs grouped under their passage.
#pragma once

#include <vector>

#include <wx/string.h>

class RouteMapConfiguration;

namespace weather_routing {

struct NamedPoint {
  wxString name;
  double lat = 0, lon = 0;
};

// "<passage>: pass Green Mark (stbd) 114452" (the unique position name of an
// older passing point) becomes "pass Green Mark (stbd)". Other names are
// returned unchanged.
wxString ShortPointName(const wxString& name, const wxString& passage);

// A unique, short position name for a passing point beside `mark`, such as
// "pass Green Mark (port)", adding " 2", " 3" ... when `taken` already has it.
wxString PassingPointName(const wxString& mark, bool port,
                          const std::vector<wxString>& taken);

// The name shown in the routes list. `points` (positions and waypoints) is
// used to name the marks of mark rules; a rule on an unnamed spot shows its
// coordinates.
wxString DescribeRoute(const RouteMapConfiguration& route,
                       const std::vector<NamedPoint>& points);

// Name of the position or waypoint within about 90 m of (lat, lon), or the
// coordinates when there is none.
wxString NameForPoint(double lat, double lon,
                      const std::vector<NamedPoint>& points);

// True when every word of `filter` appears (case-insensitively) in `text`.
bool MatchesFilter(const wxString& text, const wxString& filter);

}  // namespace weather_routing
