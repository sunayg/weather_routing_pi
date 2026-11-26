// SPDX-License-Identifier: GPL-3.0-or-later
#include "RouteNaming.h"

#include <cmath>

#include <wx/wx.h>
#include <wx/tokenzr.h>

#include "MarkRules.h"
#include "RouteMap.h"

namespace weather_routing {
namespace {

// Distance in nautical miles between two nearby points (flat plane).
double NearNm(double lat0, double lon0, double lat1, double lon1) {
  const double kx = std::cos(lat0 * M_PI / 180.0) * 60.0;
  return std::hypot((lon1 - lon0) * kx, (lat1 - lat0) * 60.0);
}

wxString MarkName(const MarkRule& rule, const std::vector<NamedPoint>& points) {
  const NamedPoint* best = nullptr;
  double bestNm = 0.05;  // about 90 m
  for (const NamedPoint& p : points) {
    const double d = NearNm(rule.lat, rule.lon, p.lat, p.lon);
    if (d <= bestNm && !p.name.IsEmpty()) {
      best = &p;
      bestNm = d;
    }
  }
  if (best) return best->name;
  return wxString::Format(_T("%.4f, %.4f"), rule.lat, rule.lon);
}

wxString FormatNm(double nm) {
  wxString s = wxString::Format(_T("%.2f"), nm);
  while (s.EndsWith(_T("0"))) s.RemoveLast();
  if (s.EndsWith(_T("."))) s.RemoveLast();
  return s;
}

}  // namespace

wxString ShortPointName(const wxString& name, const wxString& passage) {
  wxString s = name;
  if (!passage.IsEmpty() && s.StartsWith(passage + _T(": ")))
    s = s.Mid(passage.length() + 2);
  const int space = s.Find(' ', true);
  if (space != wxNOT_FOUND && s.StartsWith(_T("pass "))) {
    const wxString tail = s.Mid(space + 1);
    if (tail.length() == 6 && tail.IsNumber()) s = s.Left(space);
  }
  return s;
}

wxString PassingPointName(const wxString& mark, bool port,
                          const std::vector<wxString>& taken) {
  const wxString base = wxString::Format(_T("pass %s (%s)"), mark,
                                         port ? _T("port") : _T("stbd"));
  auto used = [&](const wxString& n) {
    for (const wxString& t : taken)
      if (t == n) return true;
    return false;
  };
  if (!used(base)) return base;
  for (int i = 2;; ++i) {
    const wxString n = wxString::Format(_T("%s %d"), base, i);
    if (!used(n)) return n;
  }
}

wxString DescribeRoute(const RouteMapConfiguration& route,
                       const std::vector<NamedPoint>& points) {
  const bool fromBoat =
      route.StartType == RouteMapConfiguration::START_FROM_BOAT;
  const wxString passage = route.MultiLegParentRouteName;
  const wxString start =
      fromBoat ? wxString(_("Boat")) : ShortPointName(route.Start, passage);
  const wxString end = ShortPointName(route.End, passage);

  if (route.IsMultiLegGenerated && route.MultiLegLegIndex > 0) {
    return wxString::Format(_("%s, leg %d of %d: %s to %s"),
                            passage.IsEmpty() ? wxString(_("Passage")) : passage,
                            route.MultiLegLegIndex, route.MultiLegLegCount,
                            start, end);
  }

  wxString name = wxString::Format(_("%s to %s"), start, end);
  for (const MarkRule& rule : route.MarkRules) {
    const wxString mark = MarkName(rule, points);
    if (rule.side == MarkSide::LeaveToPort)
      name += wxString::Format(_(", %s to port"), mark);
    else if (rule.side == MarkSide::LeaveToStarboard)
      name += wxString::Format(_(", %s to starboard"), mark);
    if (rule.clearance_nm > 0)
      name += wxString::Format(_(", keep %s nm off %s"),
                               FormatNm(rule.clearance_nm), mark);
  }
  if (route.DepartureTimeOptimizationCandidate)
    name += wxString::Format(_(", departure %+d min"),
                             route.DepartureTimeOptimizationOffsetMinutes);
  return name;
}

bool MatchesFilter(const wxString& text, const wxString& filter) {
  const wxString hay = text.Lower();
  wxStringTokenizer words(filter.Lower(), _T(" \t"), wxTOKEN_STRTOK);
  while (words.HasMoreTokens())
    if (!hay.Contains(words.GetNextToken())) return false;
  return true;
}

}  // namespace weather_routing
