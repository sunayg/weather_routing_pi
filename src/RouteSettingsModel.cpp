// SPDX-License-Identifier: GPL-3.0-or-later
#include "RouteSettingsModel.h"

#include <algorithm>
#include <cmath>

#include <wx/wx.h>

#include "RouteMap.h"
#include "RoutingEngineSettings.h"

namespace weather_routing {
namespace {

using C = RouteMapConfiguration;

SettingsField Toggle(std::string id, SettingsSection s, wxString label,
                     wxString tip, std::function<bool(const C&)> get,
                     std::function<void(C&, bool)> set) {
  SettingsField f;
  f.id = std::move(id);
  f.section = s;
  f.kind = FieldKind::Toggle;
  f.label = std::move(label);
  f.tooltip = std::move(tip);
  f.get = [get](const C& c) { return get(c) ? 1.0 : 0.0; };
  f.set = [set](C& c, double v) { set(c, v != 0); };
  return f;
}

// A number stored in `member` and shown multiplied by `scale`.
template <typename T>
SettingsField Number(std::string id, SettingsSection s, wxString label,
                     wxString unit, wxString tip, T C::*member, double min,
                     double max, double step, int digits, double scale = 1.0) {
  SettingsField f;
  f.id = std::move(id);
  f.section = s;
  f.kind = FieldKind::Number;
  f.label = std::move(label);
  f.unit = std::move(unit);
  f.tooltip = std::move(tip);
  f.scale = scale;
  f.min = min;
  f.max = max;
  f.step = step;
  f.digits = digits;
  f.get = [member, scale](const C& c) {
    return static_cast<double>(c.*member) * scale;
  };
  f.set = [member, scale](C& c, double v) {
    const double stored = v / scale;
    if constexpr (std::is_integral_v<T>)
      c.*member = static_cast<T>(std::lround(stored));
    else
      c.*member = static_cast<T>(stored);
  };
  return f;
}

SettingsField Choice(std::string id, SettingsSection s, wxString label,
                     wxString tip, std::vector<wxString> choices,
                     std::function<int(const C&)> get,
                     std::function<void(C&, int)> set) {
  SettingsField f;
  f.id = std::move(id);
  f.section = s;
  f.kind = FieldKind::Choice;
  f.label = std::move(label);
  f.tooltip = std::move(tip);
  f.choices = std::move(choices);
  f.get = [get](const C& c) { return static_cast<double>(get(c)); };
  f.set = [set](C& c, double v) { set(c, static_cast<int>(std::lround(v))); };
  return f;
}

std::vector<SettingsField> BuildFields() {
  using S = SettingsSection;
  std::vector<SettingsField> f;

  // ---- When ----------------------------------------------------------------
  f.push_back(Choice(
      "time_mode", S::When, _("Plan by"),
      _("Depart at a set time, or arrive by a set time and let the router "
        "find the departure."),
      {_("Depart at"), _("Arrive by")},
      [](const C& c) { return c.TimeMode == C::ROUTE_BY_ARRIVAL_TIME ? 1 : 0; },
      [](C& c, int v) {
        c.TimeMode = v == 1 ? C::ROUTE_BY_ARRIVAL_TIME : C::ROUTE_BY_DEPARTURE_TIME;
      }));
  f.push_back(Toggle(
      "use_current_time", S::When, _("Leave now"),
      _("Use the time the route is computed as the departure time."),
      [](const C& c) { return c.UseCurrentTime; },
      [](C& c, bool v) { c.UseCurrentTime = v; }));

  // ---- Weather -------------------------------------------------------------
  f.push_back(Toggle("use_grib", S::Weather, _("Wind from GRIB"),
                     _("Use the GRIB files loaded in the GRIB plugin."),
                     [](const C& c) { return c.UseGrib; },
                     [](C& c, bool v) { c.UseGrib = v; }));
  f.push_back(Toggle("currents", S::Weather, _("Tidal current"),
                     _("Include current (from GRIB or climatology) in boat "
                       "speed over ground."),
                     [](const C& c) { return c.Currents; },
                     [](C& c, bool v) { c.Currents = v; }));
  f.push_back(Choice(
      "climatology", S::Weather, _("If the GRIB runs out"),
      _("What to use where or when there is no GRIB data."),
      {_("Stop routing"), _("Climatology currents only"),
       _("Climatology wind map"), _("Climatology wind map, no calms"),
       _("Most likely climatology wind"), _("Average climatology wind")},
      [](const C& c) { return static_cast<int>(c.ClimatologyType); },
      [](C& c, int v) {
        c.ClimatologyType = static_cast<C::ClimatologyDataType>(
            std::clamp(v, 0, static_cast<int>(C::AVERAGE)));
      }));
  f.push_back(Toggle(
      "last_valid_data", S::Weather, _("Use the last forecast when data runs out"),
      _("Keep using the last valid GRIB values instead of stopping."),
      [](const C& c) { return c.AllowDataDeficient; },
      [](C& c, bool v) { c.AllowDataDeficient = v; }));

  // ---- Limits --------------------------------------------------------------
  f.push_back(Number("max_true_wind", S::Limits, _("Max wind"), _("kn"),
                     _("Do not sail in more true wind than this."),
                     &C::MaxTrueWindKnots, 0, 100, 1, 0));
  f.push_back(Number("max_waves", S::Limits, _("Max waves"), _("m"),
                     _("Do not sail in higher waves than this."),
                     &C::MaxSwellMeters, 0, 20, 0.5, 1));

  // ---- Safety --------------------------------------------------------------
  f.push_back(Toggle("avoid_land", S::Safety, _("Avoid land"),
                     _("Keep the route off land, using the GSHHG shoreline."),
                     [](const C& c) { return c.DetectLand; },
                     [](C& c, bool v) { c.DetectLand = v; }));
  f.push_back(Number("land_margin", S::Safety, _("Margin"), _("nm"),
                     _("Extra distance to keep from the shoreline."),
                     &C::SafetyMarginLand, 0, 10, 0.05, 2));
  // Depth used to be "minimum depth, 0 = off", which was easy to miss. The
  // tick box is derived from the depth: ticking it turns a 0 into 2 m.
  f.push_back(Toggle(
      "use_chart_depths", S::Safety, _("Use chart depths"),
      _("Avoid water shallower than the minimum depth on the loaded charts."),
      [](const C& c) { return c.MinimumDepthMeters > 0; },
      [](C& c, bool v) {
        if (!v)
          c.MinimumDepthMeters = 0;
        else if (c.MinimumDepthMeters <= 0)
          c.MinimumDepthMeters = 2.0;
      }));
  f.push_back(Number("min_depth", S::Safety, _("Min depth"), _("m"),
                     _("Charted depth the route must stay above (at chart "
                       "datum)."),
                     &C::MinimumDepthMeters, 0, 100, 0.5, 1));

  // ---- Motoring ------------------------------------------------------------
  f.push_back(Toggle("use_motor", S::Motoring, _("Motor when slow"),
                     _("Switch to the engine when sailing is slower than the "
                       "threshold."),
                     [](const C& c) { return c.UseMotor; },
                     [](C& c, bool v) { c.UseMotor = v; }));
  f.push_back(Number("motor_below", S::Motoring, _("When slower than"), _("kn"),
                     _("Boat speed under which the engine is used."),
                     &C::MotorSpeedThreshold, 0, 20, 0.1, 1));
  f.push_back(Number("motor_speed", S::Motoring, _("Motor at"), _("kn"),
                     _("Boat speed under engine."), &C::MotorSpeed, 0, 30, 0.1,
                     1));

  // ---- Departure optimisation ---------------------------------------------
  f.push_back(Toggle(
      "optimise_departure", S::DepartureOptimisation,
      _("Try other departure times"),
      _("Compute the route for several departure times around the one set."),
      [](const C& c) { return c.DepartureTimeOptimizationEnabled; },
      [](C& c, bool v) { c.DepartureTimeOptimizationEnabled = v; }));
  f.push_back(Number("departure_range", S::DepartureOptimisation,
                     _("Earlier and later by up to"), _("h"),
                     _("How far before and after the set time to try."),
                     &C::DepartureTimeOptimizationRangeMinutes, 0.25, 72, 0.25,
                     2, 1.0 / 60));
  f.push_back(Number("departure_step", S::DepartureOptimisation, _("Every"),
                     _("min"), _("Time between the departures tried."),
                     &C::DepartureTimeOptimizationStepMinutes, 5, 720, 5, 0));
  f.push_back(Choice(
      "departure_display", S::DepartureOptimisation, _("Show on chart"),
      _("Which of the tried departures to draw."),
      {_("Selected departure"), _("All departures"), _("Chosen by hand")},
      [](const C& c) { return c.DepartureTimeOptimizationChartDisplay; },
      [](C& c, int v) {
        c.DepartureTimeOptimizationChartDisplay = std::clamp(v, 0, 2);
      }));
  f.push_back(Number("arrival_margin", S::DepartureOptimisation,
                     _("Arrive-by safety margin"), _("min"),
                     _("When planning by arrival time, arrive at least this "
                       "early."),
                     &C::ArrivalSafetyMarginMinutes, 0, 1440, 5, 0));

  // ---- Sailing performance -------------------------------------------------
  f.push_back(Number("upwind_efficiency", S::SailingPerformance,
                     _("Polar efficiency upwind"), _("%"),
                     _("Boat speed upwind as a share of the polar."),
                     &C::UpwindEfficiency, 10, 200, 1, 0, 100));
  f.push_back(Number("downwind_efficiency", S::SailingPerformance,
                     _("Polar efficiency downwind"), _("%"),
                     _("Boat speed downwind as a share of the polar."),
                     &C::DownwindEfficiency, 10, 200, 1, 0, 100));
  f.push_back(Number("night_efficiency", S::SailingPerformance,
                     _("Polar efficiency at night"), _("%"),
                     _("Boat speed at night as a share of the polar."),
                     &C::NightCumulativeEfficiency, 10, 200, 1, 0, 100));
  f.push_back(Number("wind_strength", S::SailingPerformance,
                     _("Wind is"), _("% of forecast"),
                     _("Scale the forecast wind, for example 90 if the "
                       "forecast usually runs strong."),
                     &C::WindStrength, 10, 300, 5, 0, 100));
  f.push_back(Number("tack_time", S::SailingPerformance, _("Time lost per tack"),
                     _("s"), _("Seconds lost each time the boat tacks."),
                     &C::TackingTime, 0, 600, 5, 0));
  f.push_back(Number("gybe_time", S::SailingPerformance, _("Time lost per gybe"),
                     _("s"), _("Seconds lost each time the boat gybes."),
                     &C::JibingTime, 0, 600, 5, 0));
  f.push_back(Number("sail_change_time", S::SailingPerformance,
                     _("Time lost per sail change"), _("s"),
                     _("Seconds lost each time the boat changes polar."),
                     &C::SailPlanChangeTime, 0, 1800, 10, 0));
  f.push_back(Number("max_apparent_wind", S::SailingPerformance,
                     _("Max apparent wind"), _("kn"),
                     _("Do not sail in more apparent wind than this."),
                     &C::MaxApparentWindKnots, 0, 100, 1, 0));
  f.push_back(Toggle("optimise_tacking", S::SailingPerformance,
                     _("Optimise tacking"),
                     _("Search for fewer, better-placed tacks."),
                     [](const C& c) { return c.OptimizeTacking; },
                     [](C& c, bool v) { c.OptimizeTacking = v; }));
  f.push_back(Toggle("comfort_alternatives", S::SailingPerformance,
                     _("Also find comfortable alternatives"),
                     _("Search for a gentler route next to the fastest one."),
                     [](const C& c) { return c.ExploreComfortAlternatives; },
                     [](C& c, bool v) { c.ExploreComfortAlternatives = v; }));

  // ---- Hazards and areas ---------------------------------------------------
  f.push_back(Toggle("avoid_boundaries", S::Hazards,
                     _("Avoid OCPN Draw boundaries"),
                     _("Keep out of boundaries drawn with the OCPN Draw "
                       "plugin."),
                     [](const C& c) { return c.DetectBoundary; },
                     [](C& c, bool v) { c.DetectBoundary = v; }));
  f.push_back(Toggle("inverted_regions", S::Hazards, _("Stay inside regions"),
                     _("Treat the region files as areas to stay inside "
                       "instead of areas to avoid."),
                     [](const C& c) { return c.InvertedRegions; },
                     [](C& c, bool v) { c.InvertedRegions = v; }));
  f.push_back(Toggle("avoid_cyclones", S::Hazards, _("Avoid cyclone tracks"),
                     _("Avoid historical cyclone tracks near the date "
                       "(climatology)."),
                     [](const C& c) { return c.AvoidCycloneTracks; },
                     [](C& c, bool v) { c.AvoidCycloneTracks = v; }));
  f.push_back(Number("cyclone_months", S::Hazards, _("Within"), _("months"),
                     _("How far from the date a track counts."),
                     &C::CycloneMonths, 0, 12, 1, 0));
  f.push_back(Number("cyclone_days", S::Hazards, _("and"), _("days"),
                     _("Extra days added to the months."), &C::CycloneDays, 0,
                     31, 1, 0));
  f.push_back(Number("max_latitude", S::Hazards, _("Stay below latitude"),
                     _("°"), _("Do not route further north or south than this."),
                     &C::MaxLatitude, 0, 90, 1, 0));
  f.push_back(Toggle("require_waves", S::Hazards, _("Need wave data"),
                     _("Stop where the GRIB has no wave height."),
                     [](const C& c) { return c.RequireWaveCoverage; },
                     [](C& c, bool v) { c.RequireWaveCoverage = v; }));
  f.push_back(Toggle("anchoring", S::Hazards, _("Allow anchoring"),
                     _("Let the boat anchor to wait out foul current."),
                     [](const C& c) { return c.Anchoring; },
                     [](C& c, bool v) { c.Anchoring = v; }));
  f.push_back(Choice(
      "shoreline_detail", S::Hazards, _("Shoreline detail"),
      _("Detail of the GSHHG shoreline used to avoid land."),
      {_("Crude"), _("Low"), _("Intermediate"), _("High"), _("Full")},
      [](const C& c) { return c.ShorelineResolution; },
      [](C& c, int v) { c.ShorelineResolution = std::clamp(v, 0, 4); }));

  // ---- Custom accuracy -----------------------------------------------------
  {
    std::vector<wxString> engines;
    for (RoutingEngine e : kEngineChoices)
      engines.push_back(wxString::FromUTF8(EngineTitle(e)));
    f.push_back(Choice(
        "engine", S::CustomAccuracy, _("Engine"),
        _("Which routing engine to use. Auto tries several and stops at the "
          "first good route."),
        engines,
        [](const C& c) { return EngineSelection(c.EngineSettings.engine); },
        [](C& c, int v) {
          const RoutingEngine e = EngineFromSelection(v);
          if (e != RoutingEngine::Unsupported) c.EngineSettings.engine = e;
        }));
  }
  f.push_back(Toggle("adaptive", S::CustomAccuracy,
                     _("Let the engine choose its numbers"),
                     _("Choose time step and heading step automatically. The "
                       "values below are kept for when this is off."),
                     [](const C& c) { return c.AutomaticAdaptiveSettings; },
                     [](C& c, bool v) { c.AutomaticAdaptiveSettings = v; }));
  f.push_back(Number("time_step", S::CustomAccuracy, _("Time step"), _("min"),
                     _("Time between isochrones."), &C::DeltaTime, 1, 1440, 1,
                     0, 1.0 / 60));
  f.push_back(Number("heading_step", S::CustomAccuracy, _("Heading step"),
                     _("°"), _("Angle between the headings tried."),
                     &C::ByDegrees, 0.5, 60, 0.5, 1));
  f.push_back(Choice(
      "effort", S::CustomAccuracy, _("Effort"),
      _("How hard the engine searches before giving up."),
      {_("100% standard"), _("150% extended"), _("200% thorough"),
       _("400% exhaustive")},
      [](const C& c) {
        const int p = c.RoutingEffortPercent;
        return p >= 400 ? 3 : p >= 200 ? 2 : p >= 150 ? 1 : 0;
      },
      [](C& c, int v) {
        static const int kPercent[] = {100, 150, 200, 400};
        c.RoutingEffortPercent = kPercent[std::clamp(v, 0, 3)];
      }));
  f.push_back(Number("search_angle", S::CustomAccuracy, _("Search either side"),
                     _("°"), _("How far either side of the direct course to "
                               "search."),
                     &C::MaxSearchAngle, 10, 180, 5, 0));
  f.push_back(Number("course_from", S::CustomAccuracy, _("Courses from"),
                     _("° to wind"), _("Closest angle to the wind to sail."),
                     &C::FromDegree, 0, 180, 1, 0));
  f.push_back(Number("course_to", S::CustomAccuracy, _("Courses to"),
                     _("° to wind"), _("Widest angle to the wind to sail."),
                     &C::ToDegree, 0, 180, 1, 0));
  f.push_back(Toggle("optimal_angles", S::CustomAccuracy,
                     _("Use the polar's optimal angles"),
                     _("Add the polar's best upwind and downwind angles to the "
                       "headings tried."),
                     [](const C& c) { return c.UseOptimalAngles; },
                     [](C& c, bool v) { c.UseOptimalAngles = v; }));
  f.push_back(Number("max_off_course", S::CustomAccuracy,
                     _("Max off direct course"), _("°"),
                     _("Largest angle a step may turn away from the direct "
                       "course."),
                     &C::MaxDivertedCourse, 0, 180, 5, 0));
  f.push_back(Number("max_course_angle", S::CustomAccuracy,
                     _("Max course angle"), _("°"),
                     _("Largest change of course between steps."),
                     &C::MaxCourseAngle, 0, 180, 5, 0));
  f.push_back(Choice("integrator", S::CustomAccuracy, _("Integrator"),
                     _("How each step is calculated."),
                     {_("Newton"), _("Runge-Kutta")},
                     [](const C& c) { return static_cast<int>(c.Integrator); },
                     [](C& c, int v) {
                       c.Integrator = v == 1 ? C::RUNGE_KUTTA : C::NEWTON;
                     }));
  f.push_back(Number("wind_vs_current", S::CustomAccuracy, _("Wind vs current"),
                     wxEmptyString,
                     _("Penalty for wind against current (0 = none)."),
                     &C::WindVSCurrent, 0, 10, 0.1, 1));
  f.push_back(Toggle("recover_approach", S::CustomAccuracy,
                     _("Recover the final approach"),
                     _("Search backwards from the destination when the "
                       "forward search cannot reach it."),
                     [](const C& c) { return c.UseReverseReachabilityRecovery; },
                     [](C& c, bool v) { c.UseReverseReachabilityRecovery = v; }));
  return f;
}

}  // namespace

const std::vector<SettingsField>& RouteSettingsFields() {
  static const std::vector<SettingsField> fields = BuildFields();
  return fields;
}

const SettingsField* FindSettingsField(const std::string& id) {
  for (const SettingsField& f : RouteSettingsFields())
    if (f.id == id) return &f;
  return nullptr;
}

FieldSummary Summarize(const SettingsField& field,
                       const std::vector<RouteMapConfiguration>& routes) {
  FieldSummary s;
  if (routes.empty()) return s;
  s.value = field.get(routes.front());
  for (size_t i = 1; i < routes.size(); ++i)
    if (std::fabs(field.get(routes[i]) - s.value) > 1e-9) {
      s.mixed = true;
      break;
    }
  return s;
}

void ApplyField(const SettingsField& field,
                std::vector<RouteMapConfiguration>& routes, double value) {
  if (field.kind == FieldKind::Number)
    value = std::clamp(value, field.min, field.max);
  if (field.kind == FieldKind::Choice && !field.choices.empty())
    value = std::clamp(std::round(value), 0.0,
                       static_cast<double>(field.choices.size() - 1));
  for (RouteMapConfiguration& r : routes) field.set(r, value);
}

// ---- Accuracy -------------------------------------------------------------
namespace {
struct AccuracyPreset {
  Accuracy accuracy;
  RoutingEngine engine;
  int effort;
};
// Initial values. They change only engine and effort; the engines pick their
// own time and heading steps while adaptive settings are on.
constexpr AccuracyPreset kPresets[] = {
    {Accuracy::Fast, RoutingEngine::Auto, 100},
    {Accuracy::Balanced, RoutingEngine::Auto, 150},
    {Accuracy::Thorough, RoutingEngine::All, 200},
};
}  // namespace

Accuracy DetectAccuracy(const RouteMapConfiguration& route) {
  if (!route.AutomaticAdaptiveSettings) return Accuracy::Custom;
  for (const AccuracyPreset& p : kPresets)
    if (route.EngineSettings.engine == p.engine &&
        route.RoutingEffortPercent == p.effort)
      return p.accuracy;
  return Accuracy::Custom;
}

std::optional<Accuracy> SummarizeAccuracy(
    const std::vector<RouteMapConfiguration>& routes) {
  if (routes.empty()) return std::nullopt;
  const Accuracy first = DetectAccuracy(routes.front());
  for (const RouteMapConfiguration& r : routes)
    if (DetectAccuracy(r) != first) return std::nullopt;
  return first;
}

void ApplyAccuracy(RouteMapConfiguration& route, Accuracy accuracy) {
  for (const AccuracyPreset& p : kPresets)
    if (p.accuracy == accuracy) {
      route.AutomaticAdaptiveSettings = true;
      route.EngineSettings.engine = p.engine;
      route.RoutingEffortPercent = p.effort;
      return;
    }
}

wxString AccuracyLabel(Accuracy accuracy) {
  switch (accuracy) {
    case Accuracy::Fast: return _("Fast");
    case Accuracy::Balanced: return _("Balanced");
    case Accuracy::Thorough: return _("Thorough");
    case Accuracy::Custom: return _("Custom");
  }
  return wxEmptyString;
}

// ---- Passage legs ---------------------------------------------------------
std::vector<wxDateTime> ChainLegDepartures(
    const wxDateTime& start,
    const std::vector<std::optional<wxTimeSpan>>& leg_durations) {
  std::vector<wxDateTime> out;
  wxDateTime t = start;
  for (const auto& d : leg_durations) {
    out.push_back(t);
    if (t.IsValid() && d) t = t + *d;
    else t = wxDateTime();
  }
  return out;
}

}  // namespace weather_routing
