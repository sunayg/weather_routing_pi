// SPDX-License-Identifier: GPL-3.0-or-later
#include <wx/wx.h>

#include <gtest/gtest.h>

#include <set>

#include "tinyxml.h"

#include "ConfigurationXml.h"
#include "RouteMap.h"
#include "RouteSettingsModel.h"

namespace wr = weather_routing;

namespace {
// A configuration with the plugin's load defaults (reading an empty element).
RouteMapConfiguration Defaults() {
  TiXmlElement empty("Configuration");
  RouteMapConfiguration c;
  wr::ConfigurationXmlReadOptions o;
  o.resolve_boat = [](const wxString& n) { return n; };
  wr::ReadConfigurationXml(&empty, c, o);
  return c;
}
const wr::SettingsField& Field(const std::string& id) {
  const wr::SettingsField* f = wr::FindSettingsField(id);
  EXPECT_NE(f, nullptr) << id;
  return *f;
}
}  // namespace

TEST(RouteSettingsModel, FieldIdsAreUniqueAndComplete) {
  std::set<std::string> ids;
  for (const auto& f : wr::RouteSettingsFields()) {
    EXPECT_TRUE(ids.insert(f.id).second) << "duplicate " << f.id;
    EXPECT_FALSE(f.label.IsEmpty()) << f.id;
    EXPECT_FALSE(f.tooltip.IsEmpty()) << f.id;
    EXPECT_TRUE(f.get && f.set) << f.id;
    if (f.kind == wr::FieldKind::Number) EXPECT_LT(f.min, f.max) << f.id;
    if (f.kind == wr::FieldKind::Choice) EXPECT_GE(f.choices.size(), 2u) << f.id;
  }
  // Every setting from the old two tabs that the design keeps per route.
  for (const char* id :
       {"time_mode", "use_current_time", "use_grib", "currents", "climatology",
        "last_valid_data", "max_true_wind", "max_waves", "avoid_land",
        "land_margin", "use_chart_depths", "min_depth", "use_motor",
        "motor_below", "motor_speed", "optimise_departure", "departure_range",
        "departure_step", "departure_display", "arrival_margin",
        "upwind_efficiency", "downwind_efficiency", "night_efficiency",
        "wind_strength", "tack_time", "gybe_time", "sail_change_time",
        "max_apparent_wind", "optimise_tacking", "comfort_alternatives",
        "avoid_boundaries", "inverted_regions", "avoid_cyclones",
        "cyclone_months", "cyclone_days", "max_latitude", "require_waves",
        "anchoring", "shoreline_detail", "engine", "adaptive", "time_step",
        "heading_step", "effort", "search_angle", "course_from", "course_to",
        "optimal_angles", "max_off_course", "max_course_angle", "integrator",
        "wind_vs_current", "recover_approach"})
    EXPECT_NE(wr::FindSettingsField(id), nullptr) << id;
}

TEST(RouteSettingsModel, EveryFieldReadsBackWhatItWrites) {
  for (const auto& f : wr::RouteSettingsFields()) {
    if (f.id == "use_chart_depths") continue;  // derived, tested below
    RouteMapConfiguration c = Defaults();
    double v = 1;
    if (f.kind == wr::FieldKind::Number) v = f.min + f.step;
    if (f.kind == wr::FieldKind::Choice) v = f.choices.size() - 1;
    if (f.id == "engine") v = 0;  // Auto
    std::vector<RouteMapConfiguration> one{c};
    wr::ApplyField(f, one, v);
    EXPECT_NEAR(f.get(one[0]), v, 1e-6) << f.id;
  }
}

TEST(RouteSettingsModel, DisplayUnitsAreConverted) {
  RouteMapConfiguration c = Defaults();
  c.DeltaTime = 300;  // seconds
  c.UpwindEfficiency = 0.9;
  c.DepartureTimeOptimizationRangeMinutes = 360;
  EXPECT_DOUBLE_EQ(Field("time_step").get(c), 5);           // minutes
  EXPECT_DOUBLE_EQ(Field("upwind_efficiency").get(c), 90);  // percent
  EXPECT_DOUBLE_EQ(Field("departure_range").get(c), 6);     // hours
  std::vector<RouteMapConfiguration> one{c};
  wr::ApplyField(Field("time_step"), one, 15);
  EXPECT_DOUBLE_EQ(one[0].DeltaTime, 900);
}

TEST(RouteSettingsModel, SummaryIsMixedOnlyWhenRoutesDisagree) {
  std::vector<RouteMapConfiguration> routes{Defaults(), Defaults()};
  const auto& wind = Field("max_true_wind");
  EXPECT_FALSE(wr::Summarize(wind, routes).mixed);
  EXPECT_DOUBLE_EQ(wr::Summarize(wind, routes).value, 50);
  routes[1].MaxTrueWindKnots = 30;
  EXPECT_TRUE(wr::Summarize(wind, routes).mixed);
  EXPECT_DOUBLE_EQ(wr::Summarize(wind, routes).value, 50);
  EXPECT_FALSE(wr::Summarize(wind, {}).mixed);
}

TEST(RouteSettingsModel, EditingAMixedFieldSetsEveryRoute) {
  std::vector<RouteMapConfiguration> routes{Defaults(), Defaults()};
  routes[1].MaxTrueWindKnots = 30;
  wr::ApplyField(Field("max_true_wind"), routes, 35.5);
  EXPECT_DOUBLE_EQ(routes[0].MaxTrueWindKnots, 35.5);
  EXPECT_DOUBLE_EQ(routes[1].MaxTrueWindKnots, 35.5);
  EXPECT_FALSE(wr::Summarize(Field("max_true_wind"), routes).mixed);
}

TEST(RouteSettingsModel, NumbersAndChoicesAreClamped) {
  std::vector<RouteMapConfiguration> routes{Defaults()};
  wr::ApplyField(Field("max_waves"), routes, 999);
  EXPECT_DOUBLE_EQ(routes[0].MaxSwellMeters, Field("max_waves").max);
  wr::ApplyField(Field("integrator"), routes, 7);
  EXPECT_EQ(routes[0].Integrator, RouteMapConfiguration::RUNGE_KUTTA);
  wr::ApplyField(Field("land_margin"), routes, -3);
  EXPECT_DOUBLE_EQ(routes[0].SafetyMarginLand, 0);
}

TEST(RouteSettingsModel, ChartDepthTickBoxFollowsTheDepth) {
  std::vector<RouteMapConfiguration> routes{Defaults()};
  const auto& tick = Field("use_chart_depths");
  EXPECT_DOUBLE_EQ(tick.get(routes[0]), 0);  // default depth 0 = off
  wr::ApplyField(tick, routes, 1);
  EXPECT_DOUBLE_EQ(routes[0].MinimumDepthMeters, 2.0);
  wr::ApplyField(Field("min_depth"), routes, 3.5);
  wr::ApplyField(tick, routes, 1);  // already on: keeps the user's depth
  EXPECT_DOUBLE_EQ(routes[0].MinimumDepthMeters, 3.5);
  wr::ApplyField(tick, routes, 0);
  EXPECT_DOUBLE_EQ(routes[0].MinimumDepthMeters, 0);
}

TEST(RouteSettingsModel, EffortChoiceMapsToPercent) {
  std::vector<RouteMapConfiguration> routes{Defaults()};
  wr::ApplyField(Field("effort"), routes, 2);
  EXPECT_EQ(routes[0].RoutingEffortPercent, 200);
  routes[0].RoutingEffortPercent = 175;  // odd saved value shows as 150%
  EXPECT_DOUBLE_EQ(Field("effort").get(routes[0]), 1);
}

TEST(RouteSettingsModel, AccuracyPresetsRoundTrip) {
  for (auto a : {wr::Accuracy::Fast, wr::Accuracy::Balanced,
                 wr::Accuracy::Thorough}) {
    RouteMapConfiguration c = Defaults();
    wr::ApplyAccuracy(c, a);
    EXPECT_TRUE(c.AutomaticAdaptiveSettings);
    EXPECT_EQ(wr::DetectAccuracy(c), a) << wr::AccuracyLabel(a);
  }
}

TEST(RouteSettingsModel, HandTunedSettingsAreCustomAndUntouched) {
  // The user's race setup: manual 5 minute step, 5 degree headings.
  RouteMapConfiguration c = Defaults();
  c.AutomaticAdaptiveSettings = false;
  c.DeltaTime = 300;
  c.ByDegrees = 5;
  EXPECT_EQ(wr::DetectAccuracy(c), wr::Accuracy::Custom);
  wr::ApplyAccuracy(c, wr::Accuracy::Custom);
  EXPECT_FALSE(c.AutomaticAdaptiveSettings);
  EXPECT_DOUBLE_EQ(c.DeltaTime, 300);
  // Choosing a preset keeps the manual numbers for when Custom is chosen again.
  wr::ApplyAccuracy(c, wr::Accuracy::Fast);
  EXPECT_DOUBLE_EQ(c.DeltaTime, 300);
  EXPECT_DOUBLE_EQ(c.ByDegrees, 5);
}

TEST(RouteSettingsModel, AccuracySummaryIsMixedWhenRoutesDiffer) {
  std::vector<RouteMapConfiguration> routes{Defaults(), Defaults()};
  wr::ApplyAccuracy(routes[0], wr::Accuracy::Balanced);
  wr::ApplyAccuracy(routes[1], wr::Accuracy::Balanced);
  ASSERT_TRUE(wr::SummarizeAccuracy(routes).has_value());
  EXPECT_EQ(*wr::SummarizeAccuracy(routes), wr::Accuracy::Balanced);
  wr::ApplyAccuracy(routes[1], wr::Accuracy::Thorough);
  EXPECT_FALSE(wr::SummarizeAccuracy(routes).has_value());
}

TEST(RouteSettingsModel, LegDeparturesChainFromArrivals) {
  const wxDateTime start(28, wxDateTime::Nov, 2026, 15, 0, 0);
  auto d = wr::ChainLegDepartures(
      start, {wxTimeSpan(0, 55, 38), wxTimeSpan(1, 15, 31), std::nullopt,
              wxTimeSpan(1, 0, 0)});
  ASSERT_EQ(d.size(), 4u);
  EXPECT_EQ(d[0], start);
  EXPECT_EQ(d[1], wxDateTime(28, wxDateTime::Nov, 2026, 15, 55, 38));
  EXPECT_EQ(d[2], wxDateTime(28, wxDateTime::Nov, 2026, 17, 11, 9));
  EXPECT_TRUE(d[3].IsValid() == false);  // leg 3 not computed yet
  EXPECT_TRUE(wr::ChainLegDepartures(start, {}).empty());
}
