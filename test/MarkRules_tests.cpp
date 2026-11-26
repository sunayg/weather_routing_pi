// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include "MarkRules.h"

namespace wr = weather_routing;
namespace {
// A mark off Mumbai harbour; helpers turn "nm east / north of the mark" into
// latitude and longitude so the cases read as plain geometry.
constexpr double kMarkLat = 18.88, kMarkLon = 72.85;
double Lat(double north_nm) { return kMarkLat + north_nm / 60.0; }
double Lon(double east_nm) {
  return kMarkLon +
         east_nm / (60.0 * std::cos(kMarkLat * 0.017453292519943295));
}
bool Hits(const wr::MarkRule& r, double e0, double n0, double e1, double n1) {
  return wr::SegmentViolatesMarkRule(r, Lat(n0), Lon(e0), Lat(n1), Lon(e1));
}
wr::MarkRule KeepOut(double radius_nm) {
  return wr::MakeMarkRule(kMarkLat, kMarkLon, radius_nm, wr::MarkSide::Any, 0,
                          0);
}
wr::MarkRule Side(wr::MarkSide side, double travel_deg, double clearance = 0,
                  double wall_nm = 10) {
  return wr::MakeMarkRule(kMarkLat, kMarkLon, clearance, side, travel_deg,
                          wall_nm);
}
}  // namespace

TEST(MarkRules, KeepOutRejectsAStepPassingInsideTheRadius) {
  EXPECT_TRUE(Hits(KeepOut(0.3), 0.1, -1, 0.1, 1));
}
TEST(MarkRules, KeepOutAllowsAStepPassingOutsideTheRadius) {
  EXPECT_FALSE(Hits(KeepOut(0.3), 0.5, -1, 0.5, 1));
}
TEST(MarkRules, KeepOutRejectsAStepThatEndsInsideTheRadius) {
  EXPECT_TRUE(Hits(KeepOut(0.3), 0.5, -1, 0.1, 0));
}
TEST(MarkRules, KeepOutAllowsLeavingWhenTheStepStartsInside) {
  EXPECT_FALSE(Hits(KeepOut(0.3), 0.1, 0, 1.0, 0));
  EXPECT_FALSE(Hits(KeepOut(0.3), 0.1, 0, 0.5, 0.5));
}
TEST(MarkRules, KeepOutRejectsMovingDeeperWhenTheStepStartsInside) {
  EXPECT_TRUE(Hits(KeepOut(0.3), 0.2, 0, -1.0, 0));
  EXPECT_TRUE(Hits(KeepOut(0.3), 0.2, 0, 0.1, 0));
}
TEST(MarkRules, ZeroClearanceAndNoSideNeverRejects) {
  EXPECT_FALSE(Hits(KeepOut(0), 0, -1, 0, 1));
}
TEST(MarkRules, ZeroLengthStepDoesNotCrashAndNeverGetsCloser) {
  // A step that goes nowhere cannot enter the radius, even if it sits inside.
  EXPECT_FALSE(Hits(KeepOut(0.3), 0.1, 0.1, 0.1, 0.1));
  EXPECT_FALSE(Hits(KeepOut(0.3), 1, 1, 1, 1));
}

TEST(MarkRules, BarrierBearingIsOnTheForbiddenSide) {
  // Heading south, mark to port => boat passes west of it => barrier goes east.
  EXPECT_NEAR(Side(wr::MarkSide::LeaveToPort, 180).barrier_bearing_deg, 90, 1e-9);
  EXPECT_NEAR(Side(wr::MarkSide::LeaveToStarboard, 180).barrier_bearing_deg, 270,
              1e-9);
  EXPECT_NEAR(Side(wr::MarkSide::LeaveToPort, 170).barrier_bearing_deg, 80, 1e-9);
  EXPECT_NEAR(Side(wr::MarkSide::LeaveToStarboard, 350).barrier_bearing_deg, 80,
              1e-9);
  EXPECT_NEAR(Side(wr::MarkSide::LeaveToPort, 10).barrier_bearing_deg, 280, 1e-9);
}

TEST(MarkRules, LeaveToPortAcceptsThePassWithTheMarkOnPortAndRejectsTheOther) {
  const auto r = Side(wr::MarkSide::LeaveToPort, 180);
  // Heading south: the boat west of the mark has it on its port (east) side.
  EXPECT_FALSE(Hits(r, -0.2, 1, -0.2, -1));
  // Passing east of the mark puts it to starboard: must cross the barrier.
  EXPECT_TRUE(Hits(r, 0.2, 1, 0.2, -1));
}
TEST(MarkRules, LeaveToStarboardIsTheMirrorImage) {
  const auto r = Side(wr::MarkSide::LeaveToStarboard, 180);
  EXPECT_FALSE(Hits(r, 0.2, 1, 0.2, -1));
  EXPECT_TRUE(Hits(r, -0.2, 1, -0.2, -1));
}
TEST(MarkRules, BarrierCanBeSailedRoundOnlyBeyondItsEnd) {
  const auto r = Side(wr::MarkSide::LeaveToPort, 180, 0, 10);
  EXPECT_TRUE(Hits(r, 9.0, 1, 9.0, -1));    // still crosses the wall
  EXPECT_FALSE(Hits(r, 11.0, 1, 11.0, -1)); // clear of the far end
}
TEST(MarkRules, BarrierTouchingTheMarkItselfIsRejected) {
  const auto r = Side(wr::MarkSide::LeaveToPort, 180);
  EXPECT_TRUE(Hits(r, 0, 1, 0, -1));
}
TEST(MarkRules, SideAnyIgnoresTheBarrier) {
  auto r = Side(wr::MarkSide::Any, 180);
  r.barrier_length_nm = 10;
  EXPECT_FALSE(Hits(r, 0.2, 1, 0.2, -1));
}
TEST(MarkRules, ClearanceAndSideApplyTogether) {
  const auto r = Side(wr::MarkSide::LeaveToPort, 180, 0.3);
  EXPECT_FALSE(Hits(r, -0.5, 1, -0.5, -1));  // right side, outside radius
  EXPECT_TRUE(Hits(r, -0.1, 1, -0.1, -1));   // right side, inside radius
  EXPECT_TRUE(Hits(r, 0.5, 1, 0.5, -1));     // wrong side
}

TEST(MarkRules, TravelBearingMatchesTheCompass) {
  EXPECT_NEAR(wr::TravelBearingDeg(0, 0, 1, 0), 0, 1e-9);
  EXPECT_NEAR(wr::TravelBearingDeg(0, 0, 0, 1), 90, 1e-9);
  EXPECT_NEAR(wr::TravelBearingDeg(0, 0, -1, 0), 180, 1e-9);
  EXPECT_NEAR(wr::TravelBearingDeg(0, 0, 0, -1), 270, 1e-9);
  // Mumbai harbour: Dolphin to Red Buoy, 154.94 degrees computed separately.
  EXPECT_NEAR(wr::TravelBearingDeg(18.9145, 72.8359, 18.8176, 72.8838), 154.94, 0.05);
}

TEST(MarkRules, WorksAcrossTheAntimeridian) {
  const auto r = wr::MakeMarkRule(0, 179.9995, 0.3, wr::MarkSide::Any, 0, 0);
  EXPECT_TRUE(wr::SegmentViolatesMarkRule(r, -0.01, 179.9995, 0.01, -179.9995));
  EXPECT_FALSE(wr::SegmentViolatesMarkRule(r, 0.5, 179.9, 0.5, -179.9));
}

TEST(MarkRules, AnyRuleInTheListCounts) {
  std::vector<wr::MarkRule> rules = {KeepOut(0.3), Side(wr::MarkSide::LeaveToPort, 180)};
  EXPECT_FALSE(wr::SegmentViolatesAnyMarkRule({}, Lat(1), Lon(0), Lat(-1), Lon(0)));
  EXPECT_TRUE(wr::SegmentViolatesAnyMarkRule(rules, Lat(1), Lon(0.5), Lat(-1), Lon(0.5)));
  EXPECT_FALSE(wr::SegmentViolatesAnyMarkRule(rules, Lat(1), Lon(-0.5), Lat(-1), Lon(-0.5)));
}

// ---- Passage planning ---------------------------------------------------
namespace {
// Start north, finish south, one via mark in the middle (all on one meridian).
std::vector<wr::PassageMark> Course() {
  return {{kMarkLat + 3.0 / 60, kMarkLon}, {kMarkLat, kMarkLon},
          {kMarkLat - 3.0 / 60, kMarkLon}};
}
}  // namespace

TEST(PassagePlan, OffsetPointMovesTheRequestedDistanceAlongTheBearing) {
  double lat, lon;
  wr::OffsetPoint(kMarkLat, kMarkLon, 90, 1.0, &lat, &lon);
  EXPECT_NEAR(lat, kMarkLat, 1e-9);
  EXPECT_NEAR((lon - kMarkLon) * 60 * std::cos(kMarkLat * 0.017453292519943295),
              1.0, 1e-9);
  wr::OffsetPoint(kMarkLat, kMarkLon, 0, 2.0, &lat, &lon);
  EXPECT_NEAR(lat, kMarkLat + 2.0 / 60, 1e-9);
}

TEST(PassagePlan, InconsistentInputGivesNoLegs) {
  EXPECT_TRUE(wr::PlanPassageLegs({}, {}, {}).empty());
  EXPECT_TRUE(wr::PlanPassageLegs(Course(), {}, {}).empty());  // needs 1 via spec
}

TEST(PassagePlan, TwoMarksMakeOneLegWithOnlyAvoidRules) {
  auto legs = wr::PlanPassageLegs({Course()[0], Course()[2]}, {},
                               {{kMarkLat, kMarkLon + 0.01, 0.5}});
  ASSERT_EQ(legs.size(), 1u);
  EXPECT_TRUE(legs[0].start_is_mark);
  EXPECT_TRUE(legs[0].end_is_mark);
  ASSERT_EQ(legs[0].rules.size(), 1u);
  EXPECT_DOUBLE_EQ(legs[0].rules[0].clearance_nm, 0.5);
}

TEST(PassagePlan, ViaMarkWithNoRuleKeepsTheRealMark) {
  auto legs = wr::PlanPassageLegs(Course(), {{wr::MarkSide::Any, 0}}, {});
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_TRUE(legs[0].end_is_mark);
  EXPECT_TRUE(legs[1].start_is_mark);
  EXPECT_NEAR(legs[0].end_lat, kMarkLat, 1e-12);
  EXPECT_TRUE(legs[0].rules.empty());
}

TEST(PassagePlan, LeaveToPortPutsThePassingPointOnTheStarboardSideOfTheMark) {
  // Heading south, mark to port => boat passes west of it? No: mark on the
  // boat's port (east) side means the boat is WEST of the mark.
  auto legs = wr::PlanPassageLegs(Course(), {{wr::MarkSide::LeaveToPort, 0}}, {});
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_FALSE(legs[0].end_is_mark);
  EXPECT_FALSE(legs[1].start_is_mark);
  EXPECT_LT(legs[0].end_lon, kMarkLon);
  EXPECT_NEAR(legs[0].end_lon, legs[1].start_lon, 1e-12);
  EXPECT_NEAR(legs[0].end_lat, legs[1].start_lat, 1e-12);
}

TEST(PassagePlan, LeaveToStarboardPutsThePassingPointEastWhenHeadingSouth) {
  auto legs =
      wr::PlanPassageLegs(Course(), {{wr::MarkSide::LeaveToStarboard, 0}}, {});
  EXPECT_GT(legs[0].end_lon, kMarkLon);
}

TEST(PassagePlan, PassingPointRespectsTheRuleItBelongsTo) {
  for (auto side : {wr::MarkSide::LeaveToPort, wr::MarkSide::LeaveToStarboard}) {
    auto legs = wr::PlanPassageLegs(Course(), {{side, 0.3}}, {});
    ASSERT_EQ(legs.size(), 2u);
    // Each leg approaches / leaves the mark without breaking its own rule.
    for (const auto& leg : legs)
      for (const auto& r : leg.rules)
        EXPECT_FALSE(wr::SegmentViolatesMarkRule(
            r, leg.start_lat, leg.start_lon, leg.end_lat, leg.end_lon))
            << "side " << static_cast<int>(side);
    // The passing point is outside the keep-out radius.
    const double d =
        (legs[0].end_lon - kMarkLon) * 60 * std::cos(kMarkLat * 0.017453292519943295);
    EXPECT_GT(std::fabs(d), 0.3);
  }
}

TEST(PassagePlan, EachLegGetsOnlyItsOwnMarksRules) {
  std::vector<wr::PassageMark> m = {{kMarkLat + 6.0 / 60, kMarkLon},
                                 {kMarkLat + 3.0 / 60, kMarkLon},
                                 {kMarkLat, kMarkLon},
                                 {kMarkLat - 3.0 / 60, kMarkLon}};
  auto legs = wr::PlanPassageLegs(
      m, {{wr::MarkSide::LeaveToPort, 0}, {wr::MarkSide::LeaveToStarboard, 0}},
      {});
  ASSERT_EQ(legs.size(), 3u);
  EXPECT_EQ(legs[0].rules.size(), 1u);  // mark 1 only
  EXPECT_EQ(legs[1].rules.size(), 2u);  // marks 1 and 2
  EXPECT_EQ(legs[2].rules.size(), 1u);  // mark 2 only
  EXPECT_NEAR(legs[0].rules[0].lat, m[1].lat, 1e-12);
  EXPECT_NEAR(legs[2].rules[0].lat, m[2].lat, 1e-12);
}

TEST(PassagePlan, AvoidMarksApplyToEveryLeg) {
  auto legs = wr::PlanPassageLegs(Course(), {{wr::MarkSide::Any, 0}},
                               {{kMarkLat, kMarkLon + 0.02, 0.4},
                                {kMarkLat, kMarkLon - 0.02, 0.2}});
  for (const auto& leg : legs) EXPECT_EQ(leg.rules.size(), 2u);
}

TEST(PassagePlan, AvoidMarkWithZeroClearanceIsDropped) {
  auto legs = wr::PlanPassageLegs(Course(), {{wr::MarkSide::Any, 0}},
                               {{kMarkLat, kMarkLon + 0.02, 0}});
  for (const auto& leg : legs) EXPECT_TRUE(leg.rules.empty());
}

TEST(PassagePlan, StartAndFinishAreNeverMoved) {
  auto legs = wr::PlanPassageLegs(Course(), {{wr::MarkSide::LeaveToPort, 0.2}}, {});
  EXPECT_NEAR(legs[0].start_lat, Course()[0].lat, 1e-12);
  EXPECT_NEAR(legs[1].end_lat, Course()[2].lat, 1e-12);
  EXPECT_TRUE(legs[0].start_is_mark);
  EXPECT_TRUE(legs[1].end_is_mark);
}

// ---- Checking a computed route ----------------------------------------------
TEST(PassCheck, ReportsClosestApproachAndSide) {
  // Heading south 0.2 nm west of the mark: mark on the port (east) side.
  std::vector<std::pair<double, double>> west = {{Lat(1), Lon(-0.2)},
                                                 {Lat(-1), Lon(-0.2)}};
  auto r = wr::CheckPass(Side(wr::MarkSide::LeaveToPort, 180), west);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.closest_nm, 0.2, 1e-6);
  EXPECT_EQ(r.side, wr::MarkSide::LeaveToPort);
  EXPECT_TRUE(r.ok);
  std::vector<std::pair<double, double>> east = {{Lat(1), Lon(0.2)},
                                                 {Lat(-1), Lon(0.2)}};
  r = wr::CheckPass(Side(wr::MarkSide::LeaveToPort, 180), east);
  EXPECT_EQ(r.side, wr::MarkSide::LeaveToStarboard);
  EXPECT_FALSE(r.ok);
}
TEST(PassCheck, ClearanceMustBeKept) {
  std::vector<std::pair<double, double>> p = {{Lat(1), Lon(0.2)},
                                              {Lat(-1), Lon(0.2)}};
  EXPECT_FALSE(wr::CheckPass(KeepOut(0.3), p).ok);
  EXPECT_TRUE(wr::CheckPass(KeepOut(0.1), p).ok);
}
TEST(PassCheck, UsesTheNearestOfSeveralSegments) {
  std::vector<std::pair<double, double>> p = {
      {Lat(2), Lon(-1)}, {Lat(0.5), Lon(-0.5)}, {Lat(-0.5), Lon(-0.5)},
      {Lat(-2), Lon(1)}};
  auto r = wr::CheckPass(KeepOut(0), p);
  EXPECT_NEAR(r.closest_nm, 0.5, 1e-6);
  EXPECT_EQ(r.side, wr::MarkSide::LeaveToPort);
}
TEST(PassCheck, TooShortPathIsInvalid) {
  EXPECT_FALSE(wr::CheckPass(KeepOut(0.3), {{Lat(0), Lon(1)}}).valid);
}

// ---- Regression tests from the routing engine review (C1-C3) -----------------
namespace {
using Path = std::vector<std::pair<double, double>>;  // (east nm, north nm)
// True when some segment of the path breaks one of the leg's rules.
bool Breaks(const wr::PassageLeg& leg, const Path& p) {
  for (size_t i = 0; i + 1 < p.size(); ++i)
    for (const auto& r : leg.rules)
      if (wr::SegmentViolatesMarkRule(r, Lat(p[i].second), Lon(p[i].first),
                                      Lat(p[i + 1].second), Lon(p[i + 1].first)))
        return true;
  return false;
}
std::pair<double, double> EastNorth(double lat, double lon) {
  return {(lon - kMarkLon) * 60 * std::cos(kMarkLat * 0.017453292519943295),
          (lat - kMarkLat) * 60};
}
// Start at the origin, windward mark 2 nm north, leeward/finish near start.
std::vector<wr::PassageLeg> WindwardLeeward(double finishEastNm,
                                            wr::MarkSide side) {
  return wr::PlanPassageLegs({{Lat(0), Lon(0)}, {Lat(2), Lon(0)},
                              {Lat(0), Lon(finishEastNm)}},
                             {{side, 0}}, {});
}
}  // namespace

TEST(PassageReview, PortRoundingOfAWindwardMarkIsEnforced) {
  // C1: whatever side of the start the leeward mark sits, the correct
  // anticlockwise rounding passes and the clockwise one does not.
  for (double finish : {0.0, 0.12, -0.12}) {
    auto legs = WindwardLeeward(finish, wr::MarkSide::LeaveToPort);
    ASSERT_EQ(legs.size(), 2u);
    const auto pass = EastNorth(legs[0].end_lat, legs[0].end_lon);
    EXPECT_GT(pass.second, 2.0) << "passing point beyond the mark, finish "
                                << finish;
    // Up the east side, over the top, down the west side.
    EXPECT_FALSE(Breaks(legs[0], {{0, 0}, {0.3, 1.5}, {0.3, 2.1}, pass}))
        << finish;
    EXPECT_FALSE(Breaks(legs[1], {pass, {-0.3, 2.1}, {-0.3, 1.5}, {finish, 0}}))
        << finish;
    // The other way round breaks both legs.
    EXPECT_TRUE(Breaks(legs[0], {{0, 0}, {-0.3, 1.5}, {-0.3, 2.1}, pass}))
        << finish;
    EXPECT_TRUE(Breaks(legs[1], {pass, {0.3, 2.1}, {0.3, 1.5}, {finish, 0}}))
        << finish;
  }
}

TEST(PassageReview, StarboardRoundingIsTheMirrorImage) {
  auto legs = WindwardLeeward(0.0, wr::MarkSide::LeaveToStarboard);
  const auto pass = EastNorth(legs[0].end_lat, legs[0].end_lon);
  EXPECT_FALSE(Breaks(legs[0], {{0, 0}, {-0.3, 1.5}, {-0.3, 2.1}, pass}));
  EXPECT_FALSE(Breaks(legs[1], {pass, {0.3, 2.1}, {0.3, 1.5}, {0, 0}}));
  EXPECT_TRUE(Breaks(legs[0], {{0, 0}, {0.3, 1.5}, {0.3, 2.1}, pass}));
}

TEST(PassageReview, OutAndBackCourseCanLeaveAndReturn) {
  // C2: S -> W -> S -> W -> S, W 2 nm east, all port. No barrier may touch
  // a leg's own start or finish, so a leg can leave and arrive.
  std::vector<wr::PassageMark> m = {{Lat(0), Lon(0)}, {Lat(0), Lon(2)},
                                    {Lat(0), Lon(0)}, {Lat(0), Lon(2)},
                                    {Lat(0), Lon(0)}};
  auto legs = wr::PlanPassageLegs(
      m, {{wr::MarkSide::LeaveToPort, 0}, {wr::MarkSide::LeaveToPort, 0},
          {wr::MarkSide::LeaveToPort, 0}},
      {});
  ASSERT_EQ(legs.size(), 4u);
  for (size_t i = 0; i < legs.size(); ++i) {
    const auto s = EastNorth(legs[i].start_lat, legs[i].start_lon);
    const auto e = EastNorth(legs[i].end_lat, legs[i].end_lon);
    // A short first step from the start in a few directions is allowed.
    int free = 0;
    for (double deg = 0; deg < 360; deg += 30) {
      const double r = deg * 0.017453292519943295;
      free += !Breaks(legs[i], {s, {s.first + 0.05 * std::sin(r),
                                    s.second + 0.05 * std::cos(r)}});
    }
    EXPECT_GE(free, 6) << "leg " << i + 1 << " can hardly leave its start";
    // And it can arrive from somewhere nearby.
    int arrive = 0;
    for (double deg = 0; deg < 360; deg += 30) {
      const double r = deg * 0.017453292519943295;
      arrive += !Breaks(legs[i], {{e.first + 0.05 * std::sin(r),
                                   e.second + 0.05 * std::cos(r)},
                                  e});
    }
    EXPECT_GE(arrive, 6) << "leg " << i + 1 << " can hardly arrive";
  }
}

TEST(PassageReview, PassThroughIgnoresClearanceSoTheLegCanFinish) {
  // C3: "pass through" with a clearance used to make a leg end inside its
  // own keep-out circle.
  auto legs = wr::PlanPassageLegs(Course(), {{wr::MarkSide::Any, 0.1}}, {});
  ASSERT_EQ(legs.size(), 2u);
  EXPECT_TRUE(legs[0].end_is_mark);
  EXPECT_TRUE(legs[0].rules.empty());
  EXPECT_TRUE(legs[1].rules.empty());
}

TEST(PassageReview, KeepClearMarkOnALegEndIsDroppedForThatLeg) {
  // An avoid mark on the finish would make the last leg impossible.
  auto legs = wr::PlanPassageLegs({Course()[0], Course()[2]}, {},
                                  {{Course()[2].lat, Course()[2].lon, 0.3}});
  ASSERT_EQ(legs.size(), 1u);
  EXPECT_TRUE(legs[0].rules.empty());
}

TEST(PassageReview, BarrierIsSizedToTheCourse) {
  auto legs = WindwardLeeward(0.0, wr::MarkSide::LeaveToPort);
  ASSERT_FALSE(legs[0].rules.empty());
  EXPECT_NEAR(legs[0].rules[0].barrier_length_nm, 4.0, 1e-6);  // 2 x 2 nm
}
