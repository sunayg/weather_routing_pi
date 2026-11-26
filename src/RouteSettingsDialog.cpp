// SPDX-License-Identifier: GPL-3.0-or-later
#include "WeatherRoutingMessageDialog.h"
#include "WeatherRoutingWxCompat.h"
#include <wx/wx.h>
#include <wx/datectrl.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/spinctrl.h>

#include <algorithm>
#include <list>
#include <cmath>

#include "RouteMapOverlay.h"
#include "RouteSettingsDialog.h"
#include "ConfigurationDialog.h"
#include "BoatDialog.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"

// FromDIP is missing from the older wxWidgets of the Android build.
#define WR_DIP(x) WR_FromDIP(this, (x))

namespace wr = weather_routing;
using S = wr::SettingsSection;

namespace {
// A field that only makes sense while another toggle is on.
const std::map<std::string, std::string>& Dependencies() {
  static const std::map<std::string, std::string> d = {
      {"land_margin", "avoid_land"},
      {"min_depth", "use_chart_depths"},
      {"motor_below", "use_motor"},
      {"motor_speed", "use_motor"},
      {"departure_range", "optimise_departure"},
      {"departure_step", "optimise_departure"},
      {"departure_display", "optimise_departure"},
      {"cyclone_months", "avoid_cyclones"},
      {"cyclone_days", "avoid_cyclones"},
  };
  return d;
}

wxString SectionTitle(S s) {
  switch (s) {
    case S::DepartureOptimisation: return _("Departure optimisation");
    case S::SailingPerformance: return _("Sailing performance");
    case S::Hazards: return _("Hazards and areas");
    case S::CustomAccuracy: return _("Custom accuracy");
    default: return wxEmptyString;
  }
}

const wxString kClosed = wxString::FromUTF8("▸ ");
const wxString kOpen = wxString::FromUTF8("▾ ");
}  // namespace

RouteSettingsDialog::RouteSettingsDialog(wxWindow* parent,
                                         WeatherRouting& weatherrouting)
    : wxDialog(parent, wxID_ANY, _("Route settings"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_WeatherRouting(weatherrouting) {
  const int pad = WR_DIP(5);
  wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

  m_title = new wxStaticText(this, wxID_ANY, _("Route settings"));
  wxFont bold = m_title->GetFont();
  bold.SetWeight(wxFONTWEIGHT_BOLD);
  m_title->SetFont(bold);
  m_selection = new wxStaticText(this, wxID_ANY, wxEmptyString);
  wxBoxSizer* head = new wxBoxSizer(wxHORIZONTAL);
  head->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
  head->Add(m_selection, 0, wxALIGN_CENTER_VERTICAL);
  top->Add(head, 0, wxEXPAND | wxALL, 2 * pad);

  m_notice = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_notice, 0, wxLEFT | wxRIGHT | wxBOTTOM, 2 * pad);

  m_scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL);
  m_scroll->SetScrollRate(0, WR_DIP(12));
  wxBoxSizer* body = new wxBoxSizer(wxVERTICAL);
  wxFlexGridSizer* grid = new wxFlexGridSizer(2, WR_DIP(6), WR_DIP(12));
  grid->AddGrowableCol(1);
  auto rowLabel = [&](const wxString& text) {
    wxStaticText* t = new wxStaticText(m_scroll, wxID_ANY, text);
    t->SetFont(bold);
    grid->Add(t, 0, wxALIGN_TOP | wxTOP, WR_DIP(4));
  };

  // ---- Route / legs ---------------------------------------------------------
  {
    rowLabel(_("Route"));
    wxPanel* p = new wxPanel(m_scroll);
    wxBoxSizer* s = new wxBoxSizer(wxHORIZONTAL);
    m_start = new wxChoice(p, wxID_ANY);
    m_end = new wxChoice(p, wxID_ANY);
    m_start->SetToolTip(_("Where the route starts: a position, an OpenCPN "
                          "waypoint, or the boat."));
    m_end->SetToolTip(_("Where the route ends."));
    s->Add(new wxStaticText(p, wxID_ANY, _("From")), 0,
           wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
    s->Add(m_start, 1, wxRIGHT, 2 * pad);
    s->Add(new wxStaticText(p, wxID_ANY, _("to")), 0,
           wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
    s->Add(m_end, 1);
    p->SetSizer(s);
    m_routeRow = p;
    wxPanel* lp = new wxPanel(m_scroll);
    wxBoxSizer* ls = new wxBoxSizer(wxVERTICAL);
    m_legs = new wxStaticText(lp, wxID_ANY, wxEmptyString);
    wxButton* marks = new wxButton(lp, wxID_ANY, _("Edit marks in Plan passage..."));
    marks->SetToolTip(_("Change the marks, their sides and clearances. This "
                        "creates the passage again."));
    ls->Add(m_legs, 0, wxBOTTOM, pad);
    ls->Add(marks, 0);
    lp->SetSizer(ls);
    m_legRow = lp;
    wxBoxSizer* both = new wxBoxSizer(wxVERTICAL);
    both->Add(p, 0, wxEXPAND);
    both->Add(lp, 0, wxEXPAND);
    grid->Add(both, 1, wxEXPAND);
    m_start->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnStartEnd(true); });
    m_end->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnStartEnd(false); });
    marks->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      auto routes = SelectedConfigurations();
      if (!routes.empty() && !routes.front().MultiLegParentRouteGUID.IsEmpty())
        m_WeatherRouting.ShowPassageDialog(routes.front().MultiLegParentRouteGUID);
    });
  }

  // ---- Boat -----------------------------------------------------------------
  {
    rowLabel(_("Boat"));
    wxBoxSizer* s = new wxBoxSizer(wxHORIZONTAL);
    m_boat = new wxChoice(m_scroll, wxID_ANY);
    m_boat->SetToolTip(_("Boat file with its polars, from the boats folder."));
    wxButton* edit = new wxButton(m_scroll, wxID_ANY, _("Edit boat"));
    s->Add(m_boat, 1, wxRIGHT, pad);
    s->Add(edit, 0);
    grid->Add(s, 1, wxEXPAND);
    m_boat->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnBoat(); });
    edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      const wxString path = ResolveBoatPath(m_boat->GetSelection());
      if (path.IsEmpty()) return;
      m_WeatherRouting.m_BoatDialog.LoadPolar(path);
      m_WeatherRouting.m_BoatDialog.Show();
      m_WeatherRouting.m_BoatDialog.Raise();
    });
  }

  // ---- When -----------------------------------------------------------------
  {
    rowLabel(_("When"));
    wxBoxSizer* v = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* s = new wxBoxSizer(wxHORIZONTAL);
    s->Add(FieldRow(m_scroll, "time_mode"), 0, wxALIGN_CENTER_VERTICAL);
    m_date = new wxDatePickerCtrl(m_scroll, wxID_ANY);
    m_hour = new wxSpinCtrl(m_scroll, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxSize(WR_DIP(110), -1), wxSP_ARROW_KEYS, 0, 23, 0);
    m_minute = new wxSpinCtrl(m_scroll, wxID_ANY, wxEmptyString,
                              wxDefaultPosition, wxSize(WR_DIP(110), -1),
                              wxSP_ARROW_KEYS | wxSP_WRAP, 0, 59, 0);
    m_date->SetToolTip(_("Date, in this computer's local time."));
    s->Add(m_date, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 2 * pad);
    s->Add(m_hour, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, pad);
    s->Add(new wxStaticText(m_scroll, wxID_ANY, wxT(":")), 0,
           wxALIGN_CENTER_VERTICAL);
    s->Add(m_minute, 0, wxALIGN_CENTER_VERTICAL);
    v->Add(s, 0);
    wxBoxSizer* s2 = new wxBoxSizer(wxHORIZONTAL);
    wxButton* now = new wxButton(m_scroll, wxID_ANY, _("Now"));
    wxButton* grib = new wxButton(m_scroll, wxID_ANY, _("GRIB start"));
    grib->SetToolTip(_("The time shown in the GRIB plugin's timeline."));
    s2->Add(FieldRow(m_scroll, "use_current_time"), 0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT, 2 * pad);
    s2->Add(now, 0, wxRIGHT, pad);
    s2->Add(grib, 0);
    v->Add(s2, 0, wxTOP, pad);
    m_timeNote = new wxStaticText(m_scroll, wxID_ANY,
                                  _("Later legs depart when the previous leg "
                                    "arrives."));
    v->Add(m_timeNote, 0, wxTOP, pad);
    grid->Add(v, 1, wxEXPAND);
    auto onTime = [this](wxCommandEvent&) { OnTime(); };
    m_date->Bind(wxEVT_DATE_CHANGED, [this](wxDateEvent&) { OnTime(); });
    m_hour->Bind(wxEVT_SPINCTRL, onTime);
    m_minute->Bind(wxEVT_SPINCTRL, onTime);
    now->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      const wxDateTime t = wxDateTime::Now();
      m_date->SetValue(t);
      m_hour->SetValue(t.GetHour());
      m_minute->SetValue(t.GetMinute());
      OnTime();
    });
    grib->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      const wxDateTime t = m_WeatherRouting.m_ConfigurationDialog.m_GribTimelineTime;
      if (!t.IsValid()) return;
      m_date->SetValue(t);
      m_hour->SetValue(t.GetHour());
      m_minute->SetValue(t.GetMinute());
      OnTime();
    });
  }

  auto addPlainSection = [&](const wxString& title, S section) {
    rowLabel(title);
    grid->Add(BuildFieldRows(m_scroll, section), 1, wxEXPAND);
  };
  addPlainSection(_("Weather"), S::Weather);
  addPlainSection(_("Limits"), S::Limits);
  addPlainSection(_("Safety"), S::Safety);
  addPlainSection(_("Motoring"), S::Motoring);

  // ---- Accuracy -------------------------------------------------------------
  {
    rowLabel(_("Accuracy"));
    wxBoxSizer* s = new wxBoxSizer(wxHORIZONTAL);
    m_accuracy = new wxChoice(m_scroll, wxID_ANY);
    for (auto a : {wr::Accuracy::Fast, wr::Accuracy::Balanced,
                   wr::Accuracy::Thorough, wr::Accuracy::Custom})
      m_accuracy->Append(wr::AccuracyLabel(a));
    m_accuracy->SetToolTip(
        _("Fast: a quick look. Balanced: normal use. Thorough: slower, tries "
          "every engine. Custom: set the engine and its numbers yourself."));
    m_customButton = new wxButton(m_scroll, wxID_ANY, _("Custom..."));
    s->Add(m_accuracy, 0, wxRIGHT, pad);
    s->Add(m_customButton, 0);
    grid->Add(s, 1, wxEXPAND);
    m_accuracy->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnAccuracy(); });
    m_customButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      Section& c = m_sections[S::CustomAccuracy];
      ToggleSection(c, !c.body->IsShown());
    });
  }
  body->Add(grid, 0, wxEXPAND | wxALL, 2 * pad);

  AddSection(m_scroll, body, SectionTitle(S::DepartureOptimisation),
             S::DepartureOptimisation);
  AddSection(m_scroll, body, SectionTitle(S::SailingPerformance),
             S::SailingPerformance);
  AddSection(m_scroll, body, SectionTitle(S::Hazards), S::Hazards);
  AddSection(m_scroll, body, SectionTitle(S::CustomAccuracy),
             S::CustomAccuracy);
  m_scroll->SetSizer(body);
  top->Add(m_scroll, 1, wxEXPAND);

  // ---- Buttons --------------------------------------------------------------
  wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
  wxButton* reset = new wxButton(this, wxID_ANY, _("Reset to defaults"));
  wxButton* classic = new wxButton(this, wxID_ANY, _("Classic settings..."));
  classic->SetToolTip(_("Open the previous settings dialog."));
  wxButton* compute = new wxButton(this, wxID_ANY, _("Compute"));
  wxButton* close = new wxButton(this, wxID_CLOSE, _("Close"));
  buttons->Add(reset, 0, wxRIGHT, pad);
  buttons->Add(classic, 0);
  buttons->AddStretchSpacer();
  buttons->Add(compute, 0, wxRIGHT, pad);
  buttons->Add(close, 0);
  top->Add(buttons, 0, wxEXPAND | wxALL, 2 * pad);
  reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnResetDefaults(); });
  classic->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    m_WeatherRouting.m_ConfigurationDialog.Show();
    m_WeatherRouting.m_ConfigurationDialog.Raise();
  });
  compute->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxCommandEvent event;
    m_WeatherRouting.OnCompute(event);
  });
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Hide(); });

  SetSizer(top);
  // Show the whole main page without scrolling when the screen allows it,
  // never taller than 85% of the screen (1366 x 768 target).
  const wxSize screen = wxGetDisplaySize();
  const wxSize content = body->GetMinSize();
  m_scroll->SetMinSize(
      wxSize(content.x + WR_DIP(24), std::min(content.y, screen.y * 85 / 100 -
                                                              WR_DIP(140))));
  m_scroll->FitInside();
  Fit();
  SetSize(std::min(std::max(GetSize().x, WR_DIP(600)), screen.x * 9 / 10),
          std::min(GetSize().y, screen.y * 9 / 10));
  SetMinSize(wxSize(WR_DIP(480), WR_DIP(360)));
  CentreOnParent();
}

// ---- Building ---------------------------------------------------------------

wxSizer* RouteSettingsDialog::FieldRow(wxWindow* parent, const std::string& id) {
  const wr::SettingsField* f = wr::FindSettingsField(id);
  wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
  if (!f) return row;
  FieldControls fc;
  fc.field = f;
  const int pad = WR_DIP(5);
  switch (f->kind) {
    case wr::FieldKind::Toggle:
      fc.toggle = new wxCheckBox(parent, wxID_ANY, f->label, wxDefaultPosition,
                                 wxDefaultSize, wxCHK_3STATE);
      fc.toggle->SetToolTip(f->tooltip);
      row->Add(fc.toggle, 0, wxALIGN_CENTER_VERTICAL);
      fc.toggle->Bind(wxEVT_CHECKBOX, [this, id](wxCommandEvent&) { OnField(id); });
      break;
    case wr::FieldKind::Number:
      fc.label = new wxStaticText(parent, wxID_ANY, f->label);
      fc.number = new wxSpinCtrlDouble(parent, wxID_ANY, wxEmptyString,
                                       wxDefaultPosition,
                                       wxSize(WR_DIP(130), -1), wxSP_ARROW_KEYS,
                                       f->min, f->max, f->min, f->step);
      fc.number->SetDigits(f->digits);
      fc.number->SetToolTip(f->tooltip);
      fc.label->SetToolTip(f->tooltip);
      fc.unit = new wxStaticText(parent, wxID_ANY, f->unit);
      row->Add(fc.label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
      row->Add(fc.number, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
      row->Add(fc.unit, 0, wxALIGN_CENTER_VERTICAL);
      fc.number->Bind(wxEVT_SPINCTRLDOUBLE,
                      [this, id](wxSpinDoubleEvent&) { OnField(id); });
      break;
    case wr::FieldKind::Choice: {
      wxArrayString items;
      for (const wxString& c : f->choices) items.Add(c);
      if (f->id != "time_mode") {
        fc.label = new wxStaticText(parent, wxID_ANY, f->label);
        fc.label->SetToolTip(f->tooltip);
        row->Add(fc.label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
      }
      fc.choice = new wxChoice(parent, wxID_ANY, wxDefaultPosition,
                               wxDefaultSize, items);
      fc.choice->SetToolTip(f->tooltip);
      row->Add(fc.choice, 0, wxALIGN_CENTER_VERTICAL);
      fc.choice->Bind(wxEVT_CHOICE, [this, id](wxCommandEvent&) { OnField(id); });
      break;
    }
  }
  fc.mixed = new wxStaticText(parent, wxID_ANY, _("(mixed)"));
  fc.mixed->SetToolTip(_("The selected routes have different values. Change "
                         "it to set all of them."));
  fc.mixed->Hide();
  row->Add(fc.mixed, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, pad);
  m_fields[id] = fc;
  return row;
}

wxPanel* RouteSettingsDialog::BuildFieldRows(wxWindow* parent, S section) {
  wxPanel* p = new wxPanel(parent);
  wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
  // Fields that share a line in the design.
  static const std::map<std::string, std::string> kSameLine = {
      {"currents", "use_grib"},          {"max_waves", "max_true_wind"},
      {"land_margin", "avoid_land"},     {"min_depth", "use_chart_depths"},
      {"motor_below", "use_motor"},      {"motor_speed", "use_motor"},
      {"departure_step", "departure_range"},
      {"cyclone_months", "avoid_cyclones"}, {"cyclone_days", "avoid_cyclones"},
      {"gybe_time", "tack_time"},        {"course_to", "course_from"},
      {"heading_step", "time_step"},     {"max_course_angle", "max_off_course"},
  };
  wxBoxSizer* line = nullptr;
  for (const wr::SettingsField& f : wr::RouteSettingsFields()) {
    if (f.section != section) continue;
    if (f.id == "time_mode" || f.id == "use_current_time") continue;
    const bool join = kSameLine.count(f.id) && line;
    if (!join) {
      line = new wxBoxSizer(wxHORIZONTAL);
      s->Add(line, 0, wxBOTTOM, WR_DIP(4));
    } else {
      line->AddSpacer(WR_DIP(14));
    }
    line->Add(FieldRow(p, f.id), 0, wxALIGN_CENTER_VERTICAL);
  }
  p->SetSizer(s);
  return p;
}

void RouteSettingsDialog::AddSection(wxWindow* parent, wxBoxSizer* sizer,
                                     const wxString& title, S section) {
  Section sec;
  sec.title = title;
  sec.header = new wxButton(parent, wxID_ANY, kClosed + title,
                            wxDefaultPosition, wxDefaultSize,
                            wxBU_LEFT | wxBORDER_NONE);
  sec.body = BuildFieldRows(parent, section);
  sizer->Add(sec.header, 0, wxEXPAND | wxLEFT | wxRIGHT, WR_DIP(10));
  sizer->Add(sec.body, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, WR_DIP(24));
  sec.body->Hide();
  m_sections[section] = sec;
  sec.header->Bind(wxEVT_BUTTON, [this, section](wxCommandEvent&) {
    Section& s = m_sections[section];
    ToggleSection(s, !s.body->IsShown());
  });
}

void RouteSettingsDialog::ToggleSection(Section& s, bool open) {
  s.body->Show(open);
  s.header->SetLabel((open ? kOpen : kClosed) + s.title);
  m_scroll->FitInside();
  m_scroll->Layout();
  Layout();
}

// ---- Loading ----------------------------------------------------------------

std::vector<RouteMapOverlay*> RouteSettingsDialog::TargetRoutes() const {
  return m_WeatherRouting.SelectedRoutesOrPassage();
}

std::vector<RouteMapConfiguration> RouteSettingsDialog::SelectedConfigurations()
    const {
  std::vector<RouteMapConfiguration> out;
  for (RouteMapOverlay* r : TargetRoutes()) out.push_back(r->GetConfiguration());
  return out;
}

namespace {
// "Race test: Dolphin - Green Mark: pass Green Mark (stbd) 114452" is the
// unique position name of a passing point; show "pass Green Mark (stbd)".
wxString ShortPointName(const wxString& name, const wxString& passage) {
  wxString s = name;
  if (!passage.IsEmpty() && s.StartsWith(passage + _T(": ")))
    s = s.Mid(passage.length() + 2);
  const int space = s.Find(' ', true);
  if (space != wxNOT_FOUND) {
    const wxString tail = s.Mid(space + 1);
    if (tail.length() == 6 && tail.IsNumber()) s = s.Left(space);
  }
  return s;
}
}  // namespace

void RouteSettingsDialog::FillEndpoints() {
  auto fill = [](wxChoice* c, std::vector<EndpointItem>& items, bool start) {
    items.clear();
    c->Clear();
    if (start) {
      items.push_back({2, wxEmptyString, wxEmptyString});
      c->Append(_("Boat position"));
    }
    for (const auto& p : RouteMap::Positions) {
      items.push_back({0, p.Name, wxEmptyString});
      c->Append(p.Name);
    }
    wxArrayString guids = GetWaypointGUIDArray();
    for (size_t i = 0; i < guids.GetCount(); ++i) {
      PlugIn_Waypoint w;
      if (!GetSingleWaypoint(guids[i], &w)) continue;
      items.push_back({1, w.m_MarkName, guids[i]});
      c->Append(wxString::Format(_("%s (waypoint)"), w.m_MarkName));
    }
  };
  fill(m_start, m_startItems, true);
  fill(m_end, m_endItems, false);
}

void RouteSettingsDialog::FillBoats() {
  m_boat->Clear();
  m_boatPaths.clear();
  wxArrayString files;
  const wxString dir = weather_routing_pi::StandardPath() + _T("boats");
  if (wxDir::Exists(dir)) wxDir::GetAllFiles(dir, &files, _T("*.xml"), wxDIR_FILES);
  files.Sort();
  for (const wxString& f : files) {
    m_boatPaths.push_back(f);
    m_boat->Append(wxFileName(f).GetName());
  }
}

wxString RouteSettingsDialog::ResolveBoatPath(int selection) const {
  if (selection < 0 || selection >= static_cast<int>(m_boatPaths.size()))
    return wxEmptyString;
  return m_boatPaths[selection];
}

void RouteSettingsDialog::Load() {
  m_loading = true;
  const auto routes = SelectedConfigurations();
  const bool any = !routes.empty();
  m_scroll->Enable(any);

  bool candidate = false;
  for (const auto& r : routes) candidate |= r.DepartureTimeOptimizationCandidate;

  // Passage: every selected route is a leg of one generated group.
  bool passage = any && routes.front().IsMultiLegGenerated &&
                 !routes.front().MultiLegGroupId.IsEmpty();
  for (const auto& r : routes)
    passage &= r.IsMultiLegGenerated &&
               r.MultiLegGroupId == routes.front().MultiLegGroupId;

  if (!any) {
    m_title->SetLabel(_("Route settings"));
    m_selection->SetLabel(_("No route selected"));
  } else if (passage) {
    m_title->SetLabel(wxString::Format(_("Passage settings: %s"),
                                       routes.front().MultiLegParentRouteName));
    m_selection->SetLabel(
        wxString::Format(_("applies to all %zu legs"), routes.size()));
  } else {
    m_title->SetLabel(wxString::Format(_("Route settings: %s to %s"),
                                       routes.front().Start, routes.front().End));
    m_selection->SetLabel(routes.size() == 1
                              ? wxString(_("1 route selected"))
                              : wxString::Format(_("%zu routes selected"),
                                                 routes.size()));
  }
  m_notice->SetLabel(candidate
                         ? _("A tried departure time is selected; use Classic "
                             "settings... to change it.")
                         : wxString());
  m_notice->Show(candidate);
  m_scroll->Enable(any && !candidate);

  // Route row or leg list.
  m_routeRow->Show(!passage);
  m_legRow->Show(passage);
  m_timeNote->Show(passage);
  if (passage) {
    wxString legs;
    for (const auto& r : routes)
      legs += wxString::Format(
          _("%d  %s to %s\n"), r.MultiLegLegIndex,
          ShortPointName(r.Start, r.MultiLegParentRouteName),
          ShortPointName(r.End, r.MultiLegParentRouteName));
    m_legs->SetLabel(legs.Trim());
  }

  FillEndpoints();
  FillBoats();
  if (any) {
    const RouteMapConfiguration& c = routes.front();
    bool sameStart = true, sameEnd = true, sameBoat = true;
    for (const auto& r : routes) {
      sameStart &= r.StartType == c.StartType && r.Start == c.Start;
      sameEnd &= r.End == c.End;
      sameBoat &= r.boatFileName == c.boatFileName;
    }
    auto select = [](wxChoice* ch, const std::vector<EndpointItem>& items,
                     int type, const wxString& name, const wxString& guid) {
      for (size_t i = 0; i < items.size(); ++i) {
        const EndpointItem& it = items[i];
        if (it.type != type) continue;
        if ((type == 1 && it.guid == guid) || (type != 1 && it.name == name) ||
            type == 2) {
          ch->SetSelection(static_cast<int>(i));
          return;
        }
      }
      ch->SetSelection(wxNOT_FOUND);
    };
    const int startType =
        c.StartType == RouteMapConfiguration::START_FROM_BOAT       ? 2
        : c.StartType == RouteMapConfiguration::START_FROM_WAYPOINT ? 1
                                                                    : 0;
    const int endType = c.EndType == RouteMapConfiguration::END_AT_WAYPOINT ? 1 : 0;
    if (sameStart) select(m_start, m_startItems, startType, c.Start, c.StartGUID);
    else m_start->SetSelection(wxNOT_FOUND);
    if (sameEnd) select(m_end, m_endItems, endType, c.End, c.EndGUID);
    else m_end->SetSelection(wxNOT_FOUND);

    m_boat->SetSelection(wxNOT_FOUND);
    if (sameBoat)
      for (size_t i = 0; i < m_boatPaths.size(); ++i)
        if (wxFileName(m_boatPaths[i]).SameAs(wxFileName(c.boatFileName)))
          m_boat->SetSelection(static_cast<int>(i));

    const bool arrive = c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME;
    const wxDateTime t = arrive ? c.PlannedArrivalTime : c.StartTime;
    if (t.IsValid()) {
      m_date->SetValue(t);
      m_hour->SetValue(t.GetHour());
      m_minute->SetValue(t.GetMinute());
    }
  }

  for (auto& [id, fc] : m_fields) {
    const wr::FieldSummary s = wr::Summarize(*fc.field, routes);
    fc.mixed->Show(any && s.mixed);
    if (fc.toggle)
      fc.toggle->Set3StateValue(s.mixed ? wxCHK_UNDETERMINED
                                : s.value != 0 ? wxCHK_CHECKED
                                               : wxCHK_UNCHECKED);
    if (fc.number) fc.number->SetValue(s.value);
    if (fc.choice)
      fc.choice->SetSelection(s.mixed ? wxNOT_FOUND
                                      : static_cast<int>(std::lround(s.value)));
  }

  const auto accuracy = wr::SummarizeAccuracy(routes);
  m_accuracy->SetSelection(accuracy ? static_cast<int>(*accuracy) : wxNOT_FOUND);
  if (accuracy && *accuracy == wr::Accuracy::Custom &&
      !m_sections[S::CustomAccuracy].body->IsShown())
    ToggleSection(m_sections[S::CustomAccuracy], true);

  UpdateEnabledState();
  m_scroll->FitInside();
  Layout();
  m_loading = false;
}

void RouteSettingsDialog::UpdateEnabledState() {
  for (const auto& [child, parent] : Dependencies()) {
    auto c = m_fields.find(child);
    auto p = m_fields.find(parent);
    if (c == m_fields.end() || p == m_fields.end() || !p->second.toggle) continue;
    const bool on = p->second.toggle->Get3StateValue() != wxCHK_UNCHECKED;
    for (wxWindow* w : std::initializer_list<wxWindow*>{
             c->second.toggle, c->second.number, c->second.choice,
             c->second.label, c->second.unit})
      if (w) w->Enable(on);
  }
  auto adaptive = m_fields.find("adaptive");
  if (adaptive != m_fields.end() && adaptive->second.toggle) {
    const bool manual =
        adaptive->second.toggle->Get3StateValue() != wxCHK_CHECKED;
    for (const char* id : {"time_step", "heading_step"}) {
      auto it = m_fields.find(id);
      if (it == m_fields.end()) continue;
      for (wxWindow* w : std::initializer_list<wxWindow*>{
               it->second.number, it->second.label, it->second.unit})
        if (w) w->Enable(manual);
    }
  }
  auto leaveNow = m_fields.find("use_current_time");
  const bool timed = leaveNow == m_fields.end() || !leaveNow->second.toggle ||
                     leaveNow->second.toggle->Get3StateValue() != wxCHK_CHECKED;
  m_date->Enable(timed);
  m_hour->Enable(timed);
  m_minute->Enable(timed);
}

// ---- Applying ---------------------------------------------------------------

void RouteSettingsDialog::Commit(
    const std::function<void(RouteMapConfiguration&)>& edit) {
  if (m_loading) return;
  bool refresh = false;
  for (RouteMapOverlay* route : TargetRoutes()) {
    RouteMapConfiguration configuration = route->GetConfiguration();
    if (configuration.DepartureTimeOptimizationCandidate) continue;
    edit(configuration);
    m_WeatherRouting.PreserveMultiLegLegFieldsForDialog(route, configuration);
    route->SetConfiguration(configuration);
    m_WeatherRouting.SaveLastUsedConfigurationDefaults(configuration);
    // Same rule as the classic dialog: a moved start resets the route.
    RouteMapConfiguration applied = route->GetConfiguration();
    if (applied.StartLat != configuration.StartLat ||
        applied.StartLon != configuration.StartLon) {
      route->Reset();
      refresh = true;
    } else if (applied.EndLat != configuration.EndLat ||
               applied.EndLon != configuration.EndLon) {
      refresh = true;
    }
  }
  m_WeatherRouting.UpdateCurrentConfigurations();
  if (refresh && m_WeatherRouting.GetParent())
    m_WeatherRouting.GetParent()->Refresh();
  m_WeatherRouting.ScheduleAutoSave();
  Load();
}

void RouteSettingsDialog::OnField(const std::string& id) {
  if (m_loading) return;
  auto it = m_fields.find(id);
  if (it == m_fields.end()) return;
  const FieldControls& fc = it->second;
  double value = 0;
  if (fc.toggle) {
    if (fc.toggle->Get3StateValue() == wxCHK_UNDETERMINED) return;
    value = fc.toggle->IsChecked() ? 1 : 0;
  } else if (fc.number) {
    value = fc.number->GetValue();
  } else if (fc.choice) {
    if (fc.choice->GetSelection() == wxNOT_FOUND) return;
    value = fc.choice->GetSelection();
  }
  const wr::SettingsField* field = fc.field;
  Commit([field, value](RouteMapConfiguration& c) {
    std::vector<RouteMapConfiguration> one{c};
    wr::ApplyField(*field, one, value);
    c = one.front();
  });
}

void RouteSettingsDialog::OnAccuracy() {
  if (m_loading) return;
  const int sel = m_accuracy->GetSelection();
  if (sel == wxNOT_FOUND) return;
  const auto accuracy = static_cast<wr::Accuracy>(sel);
  if (accuracy == wr::Accuracy::Custom) {
    // Custom keeps every number; turn off automatic choice so they are used.
    Commit([](RouteMapConfiguration& c) { c.AutomaticAdaptiveSettings = false; });
    ToggleSection(m_sections[S::CustomAccuracy], true);
    return;
  }
  Commit([accuracy](RouteMapConfiguration& c) { wr::ApplyAccuracy(c, accuracy); });
  ToggleSection(m_sections[S::CustomAccuracy], false);
}

void RouteSettingsDialog::OnStartEnd(bool start) {
  if (m_loading) return;
  wxChoice* ch = start ? m_start : m_end;
  const auto& items = start ? m_startItems : m_endItems;
  const int sel = ch->GetSelection();
  if (sel < 0 || sel >= static_cast<int>(items.size())) return;
  const EndpointItem item = items[sel];
  Commit([start, item](RouteMapConfiguration& c) {
    if (start) {
      c.StartType = item.type == 2   ? RouteMapConfiguration::START_FROM_BOAT
                    : item.type == 1 ? RouteMapConfiguration::START_FROM_WAYPOINT
                                     : RouteMapConfiguration::START_FROM_POSITION;
      if (item.type != 2) c.Start = item.name;
      c.StartGUID = item.type == 1 ? item.guid : wxString();
    } else {
      c.EndType = item.type == 1 ? RouteMapConfiguration::END_AT_WAYPOINT
                                 : RouteMapConfiguration::END_AT_POSITION;
      c.End = item.name;
      c.EndGUID = item.type == 1 ? item.guid : wxString();
    }
  });
}

void RouteSettingsDialog::OnBoat() {
  if (m_loading) return;
  const wxString path = ResolveBoatPath(m_boat->GetSelection());
  if (path.IsEmpty()) return;
  Commit([path](RouteMapConfiguration& c) { c.boatFileName = path; });
  // Load the new boat into each route, as opening a file does.
  for (RouteMapOverlay* r : TargetRoutes()) r->LoadBoat();
}

void RouteSettingsDialog::OnTime() {
  if (m_loading) return;
  wxDateTime t = m_date->GetValue();
  t.SetHour(m_hour->GetValue());
  t.SetMinute(m_minute->GetValue());
  t.SetSecond(0);
  t.SetMillisecond(0);
  Commit([t](RouteMapConfiguration& c) {
    if (c.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME)
      c.PlannedArrivalTime = t;
    else
      c.StartTime = t;
  });
}

void RouteSettingsDialog::OnResetDefaults() {
  WR_MessageDialog ask(this,
                       _("Reset every setting on this page to its default for "
                         "the selected routes? Route, boat and time are kept."),
                       _("Weather Routing"), wxYES_NO | wxICON_QUESTION);
  if (ask.ShowModal() != wxID_YES) return;
  const RouteMapConfiguration defaults =
      m_WeatherRouting.DefaultConfiguration(false);
  Commit([&defaults](RouteMapConfiguration& c) {
    for (const wr::SettingsField& f : wr::RouteSettingsFields()) {
      if (f.section == S::When) continue;
      f.set(c, f.get(defaults));
    }
  });
}
