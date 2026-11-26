# Weather Routing UI redesign

Status: desktop steps 1-8 implemented on `feature/via-mark-rules` (see
"Implementation status" at the end). Phone and tablet (step 9) not started.
This fork (sunayg/weather_routing_pi) does not need to stay mergeable with
upstream.

## Principles

- A **passage** is the main object. Its legs sit underneath it, and their start
  times are computed from the previous leg's arrival.
- One page per dialog, with rarely used settings in collapsed sections. No
  setting appears in two places.
- Plain labels in sentence case, units always shown, a tooltip on every control.
- Layout uses hand-written wxWidgets sizers. wxFormBuilder and
  `WeatherRouting.fbp` are retired: the generated code has been hand-edited for
  years and the `.fbp` no longer matches it.
- Every dialog must fit 1366 x 768.
- Saved files (`WeatherRoutingConfiguration.xml`, boats, polars) keep their
  format, so existing setups keep working.

## Main window

- Menus: File, Route, View, Help.
- Toolbar: Plan passage (primary), New route, Compute (with a dropdown holding
  Compute all), Stop, Results, Settings.
- List: a tree of passages and their legs, plus single routes. Default columns
  are show-on-chart tick box, name, departs, arrives, distance, state, and a row
  menu. The other columns can be chosen in Settings.
- Route names describe the route (start, finish, start time, rules) and can be
  renamed.
- A filter box above the list replaces the Filter dialog.
- Positions stay in a collapsed section, because passing points use them.
- A status bar shows the boat, the GRIB coverage, whether current is on, and
  what is computing.
- Row menu items: Edit settings, Edit marks in Plan passage, Compute, Show
  results, Go to on chart, Save as route, Save as track, Simplify route, Export
  GPX, Duplicate, Try other departure times, Clear result, Delete.
- Changing a setting marks that route's result as out of date.

## Route settings (single page)

| Section | Contents |
| --- | --- |
| Route | From, To (position, waypoint or boat). For a passage, the leg list and "Edit marks in Plan passage". |
| Boat | Boat picker, Edit boat. |
| When | Depart at or arrive by; date and time in local time; Now; GRIB start. |
| Weather | Wind from GRIB; tidal current; what to use if the GRIB runs out (climatology type, last valid). |
| Limits | Max true wind, max wave height. |
| Safety | Avoid land (GSHHG) and margin; "Use chart depths" tick box and minimum depth. |
| Motoring | Motor when slower than X, at speed Y. |
| Accuracy | Fast / Balanced / Thorough, plus Custom... |

Collapsed sections:

- **Departure optimisation**: range, step, show on chart, arrival safety margin.
- **Sailing performance**: polar efficiency (upwind, downwind, night), wind
  strength percentage, tack, gybe and sail-change time, max apparent wind,
  optimise tacking, comfort alternatives.
- **Hazards and areas**: OCPN Draw boundaries, inverted regions, cyclone tracks,
  max latitude, require wave data, anchoring, shoreline detail and data.
- **Custom accuracy**: engine and engine masks, time step, heading step, effort,
  search angle, course range and polar optimal angles, max off direct course,
  max course angle, integrator, wind vs current, recover final approach.

Moved to global Settings, as one "Auto" choice: GRIB timeline caches, search
memory budgets, departure-time route workers, chart-safety RAM cache. The
per-route XML attributes are still written so older builds can read the file.

Behaviour:

- Changes apply live, as now, with Reset to defaults and Compute buttons.
- When several routes are selected, fields that differ show "mixed" until
  changed.
- For a passage, settings apply to every leg. Marks are edited only in Plan
  passage.

## Results (one window, separate from the chart)

- Headline: departs, arrives, elapsed, distance, average speed.
- Tabs:
  - **Summary**: per-leg table with a rule check showing whether each mark rule
    was met and the actual passing distance; wind, current, upwind share, tacks
    and gybes, sails, comfort; data gaps; rule cost compared with no rules.
  - **Table**: every computed step by default (a selector offers coarser rows),
    14 default columns out of 40, Columns...
  - **Plot**: two variables against time, mark changes drawn on it, tap to show
    that moment on the chart.
  - **Compare**: every route with the same start and finish, ranked by arrival,
    including fastest and comfort alternatives.
  - **Details**: the settings used, the computation statistics (from the old
    Statistics window), and warnings.
- Export CSV and Save as route sit in the title bar.
- Replaces: Details and report, Statistics, Plot, the Weather Routing table and
  Route comparison.

## Settings

- **On the chart**: wind barbs, current arrows, comfort colours, isochrons,
  route colour and line width.
- **Routes list**: column chooser.
- **Computer**: workers, plus memory and caches (Auto).
- **Diagnostics**, collapsed: the address-space monitor and logging.

## Boat

- The sail list comes first, each polar with a note on what it is for, followed
  by Add, Edit and Remove.
- The polar plot has a wind speed slider. A view picker offers polar or
  cross-over, and true or apparent.
- Save and Save as. Opening a boat moves to the boat picker in route settings.

## Smaller dialogs

- **Plan passage**: departs, marks (side and clearance), keep clear (add or
  remove), a settings summary with Change..., and Compute passage. "Create legs
  only" becomes an option under Change...
- **Batch**, renamed **Compare variations**: tick-box variations (departure
  every N minutes for H hours, other boats, wind 80 to 120 percent, nearby
  starts and finishes) with a live count. Results go to the Compare tab.
- **Filter**: replaced by the filter box in the main window.
- **Edit polar**: the grid comes first. Scale by %, Learn from NMEA log and
  Estimate from boat size each open their own small step. Fix the label typos
  and drop the "Submarine" hull type.
- **Route check**: land clearance and the same depth tick box as route
  settings; findings grouped by leg in plain words, each with a Show button.
- **About**: version, fork and build date; Manual, Report a problem, Copy
  version info.

## Implementation order

1. Extract XML load and save from the GUI class, and add a round-trip test on a
   real configuration file (all attributes).
2. Add a form model with no GUI dependency, covering the "mixed" state, the
   accuracy presets and leg time chaining, with unit tests.
3. Route settings dialog (desktop). Keep the existing member names so
   `ConfigurationDialog.cpp`, the HostContract self-tests and
   `weather_routing_pi.cpp` keep compiling.
4. Main window tree, toolbar and menus.
5. Results window.
6. Settings, then Boat and Edit polar.
7. Compare variations, Plan passage polish, Route check, About.
8. Delete the `.fbp` and `src/WeatherRoutingUI-old.cpp`.
9. Phone and tablet pages, built from the same section builders.

Until step 9, the Android build keeps its current pages. The GitHub Actions
Android build must stay green at every step.

## Risks

- Android currently reparents desktop sizers by name
  (`WeatherRoutingUI.cpp`, the `__OCPN__ANDROID__` block after the Advanced
  page). Keep the old builder under `#ifdef __OCPN__ANDROID__` until step 9.
- Some CMake contract tests grep the source for function names. Run the full
  test suite after every step.
- `wxCollapsiblePane` behaviour differs between platforms. The fallback is a
  "Show more" toggle that shows and hides sizers.
- Translations: changed labels need `po/` updates.

## Implementation status (desktop)

| Step | State | Notes |
| --- | --- | --- |
| 1 XML round trip | Done | `src/ConfigurationXml.cpp`, tests on two real files. Fixed decimal settings and mark positions losing precision. |
| 2 Settings model | Done | `src/RouteSettingsModel.cpp`, tested. Accuracy presets: Fast = Auto 100%, Balanced = Auto 150%, Thorough = All 200% (initial values). |
| 3 Route settings | Done | `src/RouteSettingsDialog.cpp`. Classic settings stays reachable from it. |
| 4 Main window | Done, partly | Toolbar, filter, Name column (`src/RouteNaming.cpp`), Route menu, status line. Legs are named rows, not folded under a passage row: 167 code paths assume one route per list row. |
| 5 Results | Done | `src/ResultsDialog.cpp`: headline, Summary with mark-rule check of the computed path, Table (leg chooser), Compare, Details. Plot, full report and engine statistics still open as their own windows. |
| 6 Settings, Boat, Edit polar | Done, partly | Existing controls moved into compact layouts. Not on Windows (memory monitor layout) or Android. Edit polar keeps tabs with clearer names. Memory and caches are not yet one global "Auto" choice. |
| 7 Smaller dialogs | Done, partly | Plan passage: settings summary with Change..., one Compute passage button. Batch renamed Compare variations (same pages). Route check: plainer labels only. About: version, fork, build; Manual, Report a problem, Copy version info. |
| 8 Remove stale files | Done | `WeatherRouting.fbp`, `WeatherRoutingUI-old.cpp`, `WeatherRoutingUI-win-mem-mon.*` removed (unreferenced). |
| 9 Phone and tablet | Not started | Android keeps its existing pages; builds only when requested. |
