// SPDX-License-Identifier: GPL-3.0-or-later
//
// The model behind the redesigned route settings dialog (docs/ui-redesign.md),
// with no GUI dependency. It describes every setting the dialog shows (its
// section, label, unit, range and how it maps onto RouteMapConfiguration),
// summarises several selected routes ("mixed" when they differ), applies an
// edit to all of them, maps the Accuracy choice onto engine settings, and
// chains passage leg departure times.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

class RouteMapConfiguration;

namespace weather_routing {

enum class SettingsSection {
  When,
  Weather,
  Limits,
  Safety,
  Motoring,
  DepartureOptimisation,
  SailingPerformance,
  Hazards,
  CustomAccuracy,
};

enum class FieldKind { Toggle, Number, Choice };

// One setting as the dialog shows it. Values cross the model as doubles in
// display units: a toggle is 0 or 1, a choice is the option index, a number is
// the stored value multiplied by `scale` (for example the time step is stored
// in seconds and shown in minutes, so scale is 1/60).
struct SettingsField {
  std::string id;  // stable key, also used in tests
  SettingsSection section;
  FieldKind kind;
  wxString label;
  wxString unit;
  wxString tooltip;
  double scale = 1.0;
  double min = 0, max = 0, step = 1;  // in display units (numbers only)
  int digits = 0;                     // decimals shown (numbers only)
  std::vector<wxString> choices;      // choices only
  std::function<double(const RouteMapConfiguration&)> get;  // display units
  std::function<void(RouteMapConfiguration&, double)> set;  // display units
};

// Every setting of the redesigned dialog, in display order. Route (start, end),
// boat and the date and time are edited with their own controls and are not
// listed here.
const std::vector<SettingsField>& RouteSettingsFields();
const SettingsField* FindSettingsField(const std::string& id);

// What the dialog shows for one field across the selected routes.
struct FieldSummary {
  bool mixed = false;  // the routes disagree; show "mixed" until edited
  double value = 0;    // display units; the first route's value when mixed
};
FieldSummary Summarize(const SettingsField& field,
                       const std::vector<RouteMapConfiguration>& routes);

// Sets the field on every route, clamping numbers to the field's range.
void ApplyField(const SettingsField& field,
                std::vector<RouteMapConfiguration>& routes, double value);

// ---- Accuracy -----------------------------------------------------------
// Fast, Balanced and Thorough let the engines choose their own numerical
// settings (the existing "automatic adaptive settings") and differ in engine
// and effort. Custom is anything else, including every hand-tuned setup; the
// numerical values stay as they are and are edited under Custom.
enum class Accuracy { Fast, Balanced, Thorough, Custom };

Accuracy DetectAccuracy(const RouteMapConfiguration& route);
// Mixed when the routes disagree.
std::optional<Accuracy> SummarizeAccuracy(
    const std::vector<RouteMapConfiguration>& routes);
// Applying Custom changes nothing (it only opens the custom settings).
void ApplyAccuracy(RouteMapConfiguration& route, Accuracy accuracy);
wxString AccuracyLabel(Accuracy accuracy);

// ---- Passage legs -------------------------------------------------------
// Departure time of each leg: the first leg leaves at `start`, every later
// leg when the previous one arrives. A leg whose duration is not known yet
// (not computed) leaves every later departure unknown (invalid wxDateTime).
std::vector<wxDateTime> ChainLegDepartures(
    const wxDateTime& start,
    const std::vector<std::optional<wxTimeSpan>>& leg_durations);

}  // namespace weather_routing
