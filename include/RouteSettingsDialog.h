// SPDX-License-Identifier: GPL-3.0-or-later
//
// The redesigned, single-page route settings dialog (docs/ui-redesign.md,
// step 3). Desktop only; Android keeps ConfigurationDialog. Built from
// RouteSettingsModel and applying every change live to the selected routes,
// the same way ConfigurationDialog does.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <wx/dialog.h>

#include "RouteSettingsModel.h"

class RouteMapConfiguration;
class WeatherRouting;
class wxBoxSizer;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxDatePickerCtrl;
class wxPanel;
class wxScrolledWindow;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;
class wxWindow;

class RouteSettingsDialog : public wxDialog {
 public:
  RouteSettingsDialog(wxWindow* parent, WeatherRouting& weatherrouting);

  // Re-reads the selected routes into the controls. Safe to call often.
  void Load();

 private:
  struct FieldControls {
    const weather_routing::SettingsField* field = nullptr;
    wxCheckBox* toggle = nullptr;
    wxSpinCtrlDouble* number = nullptr;
    wxChoice* choice = nullptr;
    wxStaticText* label = nullptr;
    wxStaticText* unit = nullptr;
    wxStaticText* mixed = nullptr;
  };
  struct Section {
    wxButton* header = nullptr;
    wxPanel* body = nullptr;
    wxString title;
  };
  struct EndpointItem {
    int type = 0;  // 0 position, 1 waypoint, 2 boat
    wxString name, guid;
  };

  // The routes being edited: the selection, or every leg of a passage when
  // the selection is one or more legs of the same passage.
  std::vector<class RouteMapOverlay*> TargetRoutes() const;
  std::vector<RouteMapConfiguration> SelectedConfigurations() const;
  void Commit(const std::function<void(RouteMapConfiguration&)>& edit);

  wxPanel* BuildFieldRows(wxWindow* parent,
                          weather_routing::SettingsSection section);
  wxSizer* FieldRow(wxWindow* parent, const std::string& id);
  void AddSection(wxWindow* parent, wxBoxSizer* sizer, const wxString& title,
                  weather_routing::SettingsSection section);
  void ToggleSection(Section& s, bool open);
  void UpdateEnabledState();
  void FillEndpoints();
  void FillBoats();
  wxString ResolveBoatPath(int selection) const;

  void OnField(const std::string& id);
  void OnAccuracy();
  void OnStartEnd(bool start);
  void OnBoat();
  void OnTime();
  void OnResetDefaults();

  WeatherRouting& m_WeatherRouting;
  bool m_loading = false;

  wxScrolledWindow* m_scroll = nullptr;
  wxStaticText* m_title = nullptr;
  wxStaticText* m_selection = nullptr;
  wxStaticText* m_notice = nullptr;

  wxWindow* m_routeRow = nullptr;
  wxChoice* m_start = nullptr;
  wxChoice* m_end = nullptr;
  std::vector<EndpointItem> m_startItems, m_endItems;
  wxWindow* m_legRow = nullptr;
  wxStaticText* m_legs = nullptr;

  wxChoice* m_boat = nullptr;
  std::vector<wxString> m_boatPaths;

  wxDatePickerCtrl* m_date = nullptr;
  wxSpinCtrl* m_hour = nullptr;
  wxSpinCtrl* m_minute = nullptr;
  wxStaticText* m_timeNote = nullptr;

  wxChoice* m_accuracy = nullptr;
  wxButton* m_customButton = nullptr;

  std::map<std::string, FieldControls> m_fields;
  std::map<weather_routing::SettingsSection, Section> m_sections;
};
