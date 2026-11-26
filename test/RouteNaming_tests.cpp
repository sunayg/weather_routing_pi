// SPDX-License-Identifier: GPL-3.0-or-later
#include <wx/wx.h>

#include <gtest/gtest.h>

#include "MarkRules.h"
#include "RouteMap.h"
#include "RouteNaming.h"

namespace wr = weather_routing;

namespace {
RouteMapConfiguration Route(const wxString& start, const wxString& end) {
  RouteMapConfiguration c;
  c.Start = start;
  c.End = end;
  c.IsMultiLegGenerated = false;
  c.DepartureTimeOptimizationCandidate = false;
  return c;
}
const std::vector<wr::NamedPoint> kPoints = {
    {"Green Mark", 18.884232, 72.864540}, {"Red Buoy", 18.817607, 72.883774}};
}  // namespace

TEST(RouteNaming, PlainRouteIsStartToEnd) {
  EXPECT_EQ(wr::DescribeRoute(Route("Dolphin", "Red Buoy"), kPoints),
            "Dolphin to Red Buoy");
}

TEST(RouteNaming, StartFromBoat) {
  auto c = Route("ignored", "Red Buoy");
  c.StartType = RouteMapConfiguration::START_FROM_BOAT;
  EXPECT_EQ(wr::DescribeRoute(c, kPoints), "Boat to Red Buoy");
}

TEST(RouteNaming, MarkRulesNameTheNearestMark) {
  auto c = Route("Dolphin", "Red Buoy");
  // Saved positions are rounded; 18.8842 is still Green Mark.
  c.MarkRules.push_back(wr::MakeMarkRule(18.8842, 72.8645, 0,
                                         wr::MarkSide::LeaveToPort, 155, 20));
  EXPECT_EQ(wr::DescribeRoute(c, kPoints),
            "Dolphin to Red Buoy, Green Mark to port");
  c.MarkRules = {wr::MakeMarkRule(18.884232, 72.864540, 1.0,
                                  wr::MarkSide::Any, 0, 0)};
  EXPECT_EQ(wr::DescribeRoute(c, kPoints),
            "Dolphin to Red Buoy, keep 1 nm off Green Mark");
  c.MarkRules = {wr::MakeMarkRule(18.9, 72.9, 0.25,
                                  wr::MarkSide::LeaveToStarboard, 155, 20)};
  EXPECT_EQ(wr::DescribeRoute(c, kPoints),
            "Dolphin to Red Buoy, 18.9000, 72.9000 to starboard, keep 0.25 nm "
            "off 18.9000, 72.9000");
}

TEST(RouteNaming, LegsNameTheirPassageAndShortenPassingPoints) {
  auto c = Route("Dolphin",
                 "Race test: Dolphin - Green Mark - Red Buoy: pass Green Mark "
                 "(stbd) 114452");
  c.IsMultiLegGenerated = true;
  c.MultiLegParentRouteName = "Race test: Dolphin - Green Mark - Red Buoy";
  c.MultiLegLegIndex = 1;
  c.MultiLegLegCount = 2;
  EXPECT_EQ(wr::DescribeRoute(c, kPoints),
            "Race test: Dolphin - Green Mark - Red Buoy, leg 1 of 2: Dolphin to "
            "pass Green Mark (stbd)");
}

TEST(RouteNaming, TriedDepartureShowsItsOffset) {
  auto c = Route("Dolphin", "Red Buoy");
  c.DepartureTimeOptimizationCandidate = true;
  c.DepartureTimeOptimizationOffsetMinutes = -30;
  EXPECT_EQ(wr::DescribeRoute(c, kPoints), "Dolphin to Red Buoy, departure -30 min");
}

TEST(RouteNaming, ShortPointNameLeavesOrdinaryNamesAlone) {
  EXPECT_EQ(wr::ShortPointName("Red Buoy", "Race"), "Red Buoy");
  EXPECT_EQ(wr::ShortPointName("pass Green Mark (port)", ""), "pass Green Mark (port)");
  EXPECT_EQ(wr::ShortPointName("Buoy 123456", ""), "Buoy 123456");
}

TEST(RouteNaming, PassingPointNamesAreShortAndUnique) {
  EXPECT_EQ(wr::PassingPointName("Green Mark", true, {}), "pass Green Mark (port)");
  EXPECT_EQ(wr::PassingPointName("Green Mark", false, {"pass Green Mark (port)"}),
            "pass Green Mark (stbd)");
  EXPECT_EQ(wr::PassingPointName("Green Mark", true,
                                 {"pass Green Mark (port)", "pass Green Mark (port) 2"}),
            "pass Green Mark (port) 3");
}

TEST(RouteNaming, FilterMatchesEveryWordAnywhere) {
  const wxString text = "Dolphin to Red Buoy, Green Mark to port Seabird";
  EXPECT_TRUE(wr::MatchesFilter(text, ""));
  EXPECT_TRUE(wr::MatchesFilter(text, "green PORT"));
  EXPECT_FALSE(wr::MatchesFilter(text, "green starboard"));
}
