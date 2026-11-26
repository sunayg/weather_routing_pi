// SPDX-License-Identifier: GPL-3.0-or-later
//
// One results window for the selected route or passage (docs/ui-redesign.md,
// step 5): headline times, a per-leg summary with mark-rule checks, the step
// table, a comparison of routes with the same start and finish, and the
// settings used. Plot, the full report and engine statistics stay in their
// own windows for now and open from the Details tab.
#pragma once

#include <vector>

#include <wx/dialog.h>
#include <wx/timer.h>

class RouteMapOverlay;
class RoutingTablePanel;
class WeatherRoute;
class WeatherRouting;
class wxListCtrl;
class wxListEvent;
class wxNotebook;
class wxStaticText;
class wxTextCtrl;

class ResultsDialog : public wxDialog {
 public:
  ResultsDialog(wxWindow* parent, WeatherRouting& weatherrouting);

  // Re-reads the selected route or passage. Safe to call often.
  void Load();

 private:
  struct CompareItem {
    wxString name;
    std::vector<RouteMapOverlay*> routes;  // one, or every leg of a passage
    wxDateTime arrives;
    double distance = 0;
    wxString comfort, state;
  };

  WeatherRoute* RowFor(RouteMapOverlay* route) const;
  void LoadSummary(const std::vector<RouteMapOverlay*>& routes);
  void LoadCompare(const std::vector<RouteMapOverlay*>& routes);
  void LoadDetails(const std::vector<RouteMapOverlay*>& routes);
  void OnCompareActivated(wxListEvent& event);

  WeatherRouting& m_WeatherRouting;
  wxTimer m_refresh;

  wxStaticText* m_title = nullptr;
  wxStaticText* m_departs = nullptr;
  wxStaticText* m_arrives = nullptr;
  wxStaticText* m_elapsed = nullptr;
  wxStaticText* m_distance = nullptr;
  wxStaticText* m_speed = nullptr;
  wxNotebook* m_tabs = nullptr;
  wxListCtrl* m_legs = nullptr;
  wxTextCtrl* m_notes = nullptr;
  RoutingTablePanel* m_table = nullptr;
  wxListCtrl* m_compare = nullptr;
  wxStaticText* m_compareNote = nullptr;
  wxTextCtrl* m_details = nullptr;
  std::vector<CompareItem> m_compareItems;
};
