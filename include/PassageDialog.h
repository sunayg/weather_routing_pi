// SPDX-License-Identifier: GPL-3.0-or-later
//
// A compact dialog for planning a passage (cruise or race) through an OpenCPN route: pick a
// start time, say how each intermediate mark is to be passed, and list marks
// to keep clear of. It replaces setting up one Weather Routing configuration
// per leg by hand.
#pragma once

#include <wx/datetime.h>
#include <wx/dialog.h>
#include <wx/string.h>

#include <vector>

#include "MarkRules.h"
#include "RouteWaypointExtractor.h"

class wxButton;
class wxChoice;
class wxDatePickerCtrl;
class wxListBox;
class wxSpinCtrl;
class wxSpinCtrlDouble;

// What the user asked for.
struct PassageRequest {
  wxDateTime start;  // local time of departure from the first mark
  // One entry per intermediate mark of the route, in route order.
  std::vector<weather_routing::ViaSpec> via;
  std::vector<weather_routing::AvoidMark> avoid;
  bool compute_now = false;  // compute all legs straight away
};

// A mark the user can pick to keep clear of (any OpenCPN waypoint).
struct PassageMarkChoice {
  wxString name;
  double lat = 0, lon = 0;
};

class PassageDialog : public wxDialog {
 public:
  PassageDialog(wxWindow* parent, const wxString& route_name,
                  const std::vector<RouteWaypointInfo>& waypoints,
                  const std::vector<PassageMarkChoice>& avoid_choices,
                  const wxDateTime& default_start);

  // Valid after ShowModal() returned wxID_OK.
  PassageRequest GetRequest() const;

 private:
  void OnAddAvoid(wxCommandEvent&);
  void OnRemoveAvoid(wxCommandEvent&);
  void OnCreate(wxCommandEvent&);
  void OnCreateAndCompute(wxCommandEvent&);

  std::vector<PassageMarkChoice> m_avoid_choices;
  std::vector<wxChoice*> m_side;
  std::vector<wxSpinCtrlDouble*> m_clearance;
  wxDatePickerCtrl* m_date = nullptr;
  wxSpinCtrl* m_hour = nullptr;
  wxSpinCtrl* m_minute = nullptr;
  wxChoice* m_avoid_pick = nullptr;
  wxSpinCtrlDouble* m_avoid_clearance = nullptr;
  wxListBox* m_avoid_list = nullptr;
  std::vector<weather_routing::AvoidMark> m_avoid;
  bool m_compute_now = false;
};
