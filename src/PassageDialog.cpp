// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassageDialog.h"

#include <wx/wx.h>
#include <wx/datectrl.h>
#include <wx/spinctrl.h>

namespace {
enum { ID_ADD_AVOID = wxID_HIGHEST + 4200, ID_REMOVE_AVOID, ID_CREATE,
       ID_CREATE_COMPUTE };

const double kMaxNm = 20.0;
}  // namespace

PassageDialog::PassageDialog(
    wxWindow* parent, const wxString& route_name,
    const std::vector<RouteWaypointInfo>& waypoints,
    const std::vector<PassageMarkChoice>& avoid_choices,
    const wxDateTime& default_start,
    std::function<wxString()> settings_summary,
    std::function<void()> change_settings)
    : wxDialog(parent, wxID_ANY,
               wxString::Format(_("Plan passage: %s"), route_name),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_avoid_choices(avoid_choices),
      m_settings_summary(std::move(settings_summary)) {
  wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

  top->AddSpacer(5);

  // --- Departure -----------------------------------------------------------
  wxStaticBoxSizer* dep =
      new wxStaticBoxSizer(wxHORIZONTAL, this, _("Departure (local time)"));
  wxDateTime start = default_start.IsValid() ? default_start : wxDateTime::Now();
  m_date = new wxDatePickerCtrl(this, wxID_ANY, start);
  m_hour = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                          wxSize(60, -1), wxSP_ARROW_KEYS, 0, 23,
                          start.GetHour());
  m_minute = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxSize(60, -1), wxSP_ARROW_KEYS, 0, 59,
                            start.GetMinute());
  dep->Add(m_date, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
  dep->Add(m_hour, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
  dep->Add(new wxStaticText(this, wxID_ANY, wxT(":")), 0,
           wxALIGN_CENTER_VERTICAL);
  dep->Add(m_minute, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
  top->Add(dep, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

  // --- Marks of the route ----------------------------------------------------
  wxStaticBoxSizer* marks =
      new wxStaticBoxSizer(wxVERTICAL, this, _("Marks, in order"));
  wxFlexGridSizer* grid = new wxFlexGridSizer(3, 6, 10);
  grid->AddGrowableCol(0);
  const size_t n = waypoints.size();
  wxArrayString sides;
  sides.Add(_("Pass through"));
  sides.Add(_("Leave to port"));
  sides.Add(_("Leave to starboard"));
  for (size_t i = 0; i < n; ++i) {
    wxString label;
    if (i == 0)
      label = wxString::Format(_("Start: %s"), waypoints[i].name);
    else if (i + 1 == n)
      label = wxString::Format(_("Finish: %s"), waypoints[i].name);
    else
      label = wxString::Format(_("%zu. %s"), i, waypoints[i].name);
    grid->Add(new wxStaticText(this, wxID_ANY, label), 0,
              wxALIGN_CENTER_VERTICAL);
    if (i == 0 || i + 1 == n) {
      grid->AddSpacer(0);
      grid->AddSpacer(0);
      continue;
    }
    wxChoice* side = new wxChoice(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, sides);
    side->SetSelection(0);
    wxSpinCtrlDouble* clr = new wxSpinCtrlDouble(
        this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1),
        wxSP_ARROW_KEYS, 0.0, kMaxNm, 0.0, 0.05);
    clr->SetDigits(2);
    grid->Add(side, 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* c = new wxBoxSizer(wxHORIZONTAL);
    c->Add(clr, 0, wxALIGN_CENTER_VERTICAL);
    c->Add(new wxStaticText(this, wxID_ANY, _(" nm clear")), 0,
           wxALIGN_CENTER_VERTICAL);
    grid->Add(c);
    m_side.push_back(side);
    m_clearance.push_back(clr);
    // A clearance only makes sense with a side: passing through a mark
    // means sailing to it.
    clr->Enable(false);
    side->Bind(wxEVT_CHOICE, [side, clr](wxCommandEvent& e) {
      clr->Enable(side->GetSelection() > 0);
      e.Skip();
    });
  }
  if (n < 3)
    marks->Add(new wxStaticText(
                   this, wxID_ANY,
                   _("This route has no intermediate marks, only start and "
                     "finish.")),
               0, wxALL, 5);
  marks->Add(grid, 0, wxEXPAND | wxALL, 5);
  top->Add(marks, 0, wxEXPAND | wxALL, 10);

  // --- Keep clear of -----------------------------------------------------------
  wxStaticBoxSizer* avoid =
      new wxStaticBoxSizer(wxVERTICAL, this, _("Keep clear of"));
  wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
  wxArrayString names;
  for (const auto& c : m_avoid_choices) names.Add(c.name);
  m_avoid_pick = new wxChoice(this, wxID_ANY, wxDefaultPosition,
                              wxSize(200, -1), names);
  if (!names.IsEmpty()) m_avoid_pick->SetSelection(0);
  m_avoid_clearance = new wxSpinCtrlDouble(
      this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1),
      wxSP_ARROW_KEYS, 0.05, kMaxNm, 0.5, 0.05);
  m_avoid_clearance->SetDigits(2);
  wxButton* add = new wxButton(this, ID_ADD_AVOID, _("Add"));
  row->Add(m_avoid_pick, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  row->Add(m_avoid_clearance, 0, wxALIGN_CENTER_VERTICAL);
  row->Add(new wxStaticText(this, wxID_ANY, _(" nm  ")), 0,
           wxALIGN_CENTER_VERTICAL);
  row->Add(add, 0, wxALIGN_CENTER_VERTICAL);
  avoid->Add(row, 0, wxEXPAND | wxALL, 5);
  m_avoid_list = new wxListBox(this, wxID_ANY, wxDefaultPosition,
                               wxSize(-1, 70));
  avoid->Add(m_avoid_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);
  avoid->Add(new wxButton(this, ID_REMOVE_AVOID, _("Remove selected")), 0,
             wxALL, 5);
  top->Add(avoid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

  // --- Settings the legs will use --------------------------------------------
  {
    wxStaticBoxSizer* box =
        new wxStaticBoxSizer(wxHORIZONTAL, this, _("Settings for every leg"));
    m_settings = new wxStaticText(
        this, wxID_ANY,
        m_settings_summary ? m_settings_summary()
                           : wxString(_("From the route selected in Weather "
                                        "Routing.")));
    box->Add(m_settings, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    if (change_settings) {
      wxButton* change = new wxButton(this, wxID_ANY, _("Change..."));
      change->SetToolTip(_("Open the settings of the route selected in "
                           "Weather Routing; the legs copy them."));
      change->Bind(wxEVT_BUTTON,
                   [change_settings](wxCommandEvent&) { change_settings(); });
      box->Add(change, 0, wxALL, 5);
    }
    top->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    // Pick up changes made in the settings window.
    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& e) {
      if (e.GetActive() && m_settings_summary)
        m_settings->SetLabel(m_settings_summary());
      e.Skip();
    });
  }

  // --- Buttons ------------------------------------------------------------------
  wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
  m_only_create = new wxCheckBox(this, wxID_ANY, _("Only create the legs"));
  m_only_create->SetToolTip(_("Create the legs without computing them, for "
                              "example to change a leg first."));
  buttons->Add(m_only_create, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
  buttons->AddStretchSpacer();
  buttons->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0, wxALL, 5);
  wxButton* go = new wxButton(this, ID_CREATE_COMPUTE, _("Compute passage"));
  go->SetDefault();
  buttons->Add(go, 0, wxALL, 5);
  top->Add(buttons, 0, wxEXPAND | wxALL, 5);

  SetSizerAndFit(top);
  CentreOnParent();

  Bind(wxEVT_BUTTON, &PassageDialog::OnAddAvoid, this, ID_ADD_AVOID);
  Bind(wxEVT_BUTTON, &PassageDialog::OnRemoveAvoid, this, ID_REMOVE_AVOID);
  Bind(wxEVT_BUTTON, &PassageDialog::OnCreate, this, ID_CREATE);
  Bind(wxEVT_BUTTON, &PassageDialog::OnCreateAndCompute, this,
       ID_CREATE_COMPUTE);
}

void PassageDialog::OnAddAvoid(wxCommandEvent&) {
  const int sel = m_avoid_pick->GetSelection();
  if (sel == wxNOT_FOUND || sel >= static_cast<int>(m_avoid_choices.size()))
    return;
  const PassageMarkChoice& c = m_avoid_choices[sel];
  weather_routing::AvoidMark m;
  m.lat = c.lat;
  m.lon = c.lon;
  m.clearance_nm = m_avoid_clearance->GetValue();
  m_avoid.push_back(m);
  m_avoid_list->Append(
      wxString::Format(_("%s  (%.2f nm)"), c.name, m.clearance_nm));
}

void PassageDialog::OnRemoveAvoid(wxCommandEvent&) {
  const int sel = m_avoid_list->GetSelection();
  if (sel == wxNOT_FOUND) return;
  m_avoid.erase(m_avoid.begin() + sel);
  m_avoid_list->Delete(sel);
}

void PassageDialog::OnCreate(wxCommandEvent&) {
  m_compute_now = false;
  EndModal(wxID_OK);
}

void PassageDialog::OnCreateAndCompute(wxCommandEvent&) {
  m_compute_now = !(m_only_create && m_only_create->IsChecked());
  EndModal(wxID_OK);
}

PassageRequest PassageDialog::GetRequest() const {
  PassageRequest r;
  wxDateTime d = m_date->GetValue();
  d.SetHour(m_hour->GetValue());
  d.SetMinute(m_minute->GetValue());
  d.SetSecond(0);
  d.SetMillisecond(0);
  r.start = d;
  for (size_t i = 0; i < m_side.size(); ++i) {
    weather_routing::ViaSpec v;
    switch (m_side[i]->GetSelection()) {
      case 1: v.side = weather_routing::MarkSide::LeaveToPort; break;
      case 2: v.side = weather_routing::MarkSide::LeaveToStarboard; break;
      default: v.side = weather_routing::MarkSide::Any; break;
    }
    v.clearance_nm = v.side == weather_routing::MarkSide::Any
                         ? 0.0
                         : m_clearance[i]->GetValue();
    r.via.push_back(v);
  }
  r.avoid = m_avoid;
  r.compute_now = m_compute_now;
  return r;
}
