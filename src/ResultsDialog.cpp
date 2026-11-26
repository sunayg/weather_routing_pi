// SPDX-License-Identifier: GPL-3.0-or-later
#include "WeatherRoutingMessageDialog.h"
#include "WeatherRoutingWxCompat.h"
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <map>

#include "MarkRules.h"
#include "ResultsDialog.h"
#include "RouteMapOverlay.h"
#include "RouteNaming.h"
#include "RouteSettingsModel.h"
#include "RoutingTablePanel.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"

namespace wr = weather_routing;

namespace {
// "6.73 NMi" -> 6.73; anything unparsable -> NAN.
double LeadingNumber(const wxString& s) {
  double v = NAN;
  wxString first = s.BeforeFirst(' ');
  first.Replace(_T(","), _T("."));
  if (!first.ToCDouble(&v)) return NAN;
  return v;
}

wxString FormatSpan(const wxTimeSpan& span) {
  const long minutes = static_cast<long>(span.GetMinutes());
  if (minutes < 0) return _("N/A");
  return wxString::Format(_("%ldh %02ldm"), minutes / 60, minutes % 60);
}

std::vector<wr::NamedPoint> KnownPoints() {
  std::vector<wr::NamedPoint> points;
  for (const auto& p : RouteMap::Positions)
    points.push_back({p.Name, p.lat, p.lon});
  return points;
}

wxStaticText* Metric(wxWindow* parent, wxSizer* row, const wxString& label) {
  wxBoxSizer* box = new wxBoxSizer(wxVERTICAL);
  box->Add(new wxStaticText(parent, wxID_ANY, label), 0);
  wxStaticText* value = new wxStaticText(parent, wxID_ANY, _("N/A"));
  wxFont f = value->GetFont();
  f.SetPointSize(f.GetPointSize() + 3);
  f.SetWeight(wxFONTWEIGHT_BOLD);
  value->SetFont(f);
  box->Add(value, 0);
  row->Add(box, 1, wxALL, WR_FromDIP(parent, 6));
  return value;
}
}  // namespace

ResultsDialog::ResultsDialog(wxWindow* parent, WeatherRouting& weatherrouting)
    : wxDialog(parent, wxID_ANY, _("Results"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_WeatherRouting(weatherrouting),
      m_refresh(this) {
  const int pad = WR_FromDIP(this, 5);
  wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

  m_title = new wxStaticText(this, wxID_ANY, _("Results"));
  wxFont bold = m_title->GetFont();
  bold.SetWeight(wxFONTWEIGHT_BOLD);
  m_title->SetFont(bold);
  top->Add(m_title, 0, wxALL, 2 * pad);

  wxBoxSizer* metrics = new wxBoxSizer(wxHORIZONTAL);
  m_departs = Metric(this, metrics, _("Departs"));
  m_arrives = Metric(this, metrics, _("Arrives"));
  m_elapsed = Metric(this, metrics, _("Elapsed"));
  m_distance = Metric(this, metrics, _("Distance"));
  m_speed = Metric(this, metrics, _("Average speed"));
  top->Add(metrics, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

  m_tabs = new wxNotebook(this, wxID_ANY);

  // ---- Summary --------------------------------------------------------------
  {
    wxPanel* p = new wxPanel(m_tabs);
    wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
    m_legs = new wxListCtrl(p, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_HRULES | wxLC_SINGLE_SEL);
    const wxString cols[] = {_("Leg"),     _("Departs"), _("Arrives"),
                             _("Distance"), _("Avg speed"), _("Wind avg / max"),
                             _("Current avg"), _("Tacks / gybes"), _("Comfort"),
                             _("Mark rules")};
    for (unsigned i = 0; i < WXSIZEOF(cols); ++i) m_legs->InsertColumn(i, cols[i]);
    s->Add(m_legs, 1, wxEXPAND | wxALL, pad);
    m_notes = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             wxSize(-1, WR_FromDIP(this, 70)),
                             wxTE_MULTILINE | wxTE_READONLY);
    s->Add(m_notes, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    p->SetSizer(s);
    m_tabs->AddPage(p, _("Summary"));
  }

  // ---- Table ----------------------------------------------------------------
  m_table = new RoutingTablePanel(m_tabs, m_WeatherRouting, nullptr);
  // Its Close button hides its parent, which here would be the tabs.
  for (wxWindow* child : m_table->GetChildren())
    if (auto* b = dynamic_cast<wxButton*>(child))
      if (b->GetLabel() == _("Close")) b->Hide();
  m_tabs->AddPage(m_table, _("Table"));

  // ---- Compare --------------------------------------------------------------
  {
    wxPanel* p = new wxPanel(m_tabs);
    wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
    m_compareNote = new wxStaticText(p, wxID_ANY, wxEmptyString);
    s->Add(m_compareNote, 0, wxALL, pad);
    m_compare = new wxListCtrl(p, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_HRULES | wxLC_SINGLE_SEL);
    const wxString cols[] = {_("Route"), _("Arrives"), _("Extra"),
                             _("Distance"), _("Comfort"), _("State")};
    for (unsigned i = 0; i < WXSIZEOF(cols); ++i)
      m_compare->InsertColumn(i, cols[i]);
    m_compare->SetToolTip(_("Double-click a route to select it in the list."));
    s->Add(m_compare, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    p->SetSizer(s);
    m_tabs->AddPage(p, _("Compare"));
    m_compare->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ResultsDialog::OnCompareActivated,
                    this);
  }

  // ---- Details --------------------------------------------------------------
  {
    wxPanel* p = new wxPanel(m_tabs);
    wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
    m_details = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    s->Add(m_details, 1, wxEXPAND | wxALL, pad);
    wxBoxSizer* b = new wxBoxSizer(wxHORIZONTAL);
    wxButton* plot = new wxButton(p, wxID_ANY, _("Plot..."));
    wxButton* report = new wxButton(p, wxID_ANY, _("Full report..."));
    wxButton* stats = new wxButton(p, wxID_ANY, _("Engine statistics..."));
    b->Add(plot, 0, wxRIGHT, pad);
    b->Add(report, 0, wxRIGHT, pad);
    b->Add(stats, 0);
    s->Add(b, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);
    p->SetSizer(s);
    m_tabs->AddPage(p, _("Details"));
    plot->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { m_WeatherRouting.OnPlot(e); });
    report->Bind(wxEVT_BUTTON,
                 [this](wxCommandEvent& e) { m_WeatherRouting.OnReport(e); });
    stats->Bind(wxEVT_BUTTON,
                [this](wxCommandEvent& e) { m_WeatherRouting.OnStatistics(e); });
  }
  top->Add(m_tabs, 1, wxEXPAND | wxALL, pad);

  wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
  buttons->AddStretchSpacer();
  wxButton* close = new wxButton(this, wxID_CLOSE, _("Close"));
  buttons->Add(close, 0);
  top->Add(buttons, 0, wxEXPAND | wxALL, 2 * pad);
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Hide(); });

  // While a selected route computes, keep the numbers current.
  Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
    if (!IsShown()) {
      m_refresh.Stop();
      return;
    }
    bool running = false;
    for (RouteMapOverlay* r : m_WeatherRouting.SelectedRoutesOrPassage())
      running |= r->Running();
    if (running) Load();
  });

  SetSizer(top);
  const wxSize screen = wxGetDisplaySize();
  SetSize(std::min(WR_FromDIP(this, 900), screen.x * 9 / 10),
          std::min(WR_FromDIP(this, 560), screen.y * 85 / 100));
  CentreOnParent();
}

WeatherRoute* ResultsDialog::RowFor(RouteMapOverlay* route) const {
  for (WeatherRoute* w : m_WeatherRouting.m_WeatherRoutes)
    if (w->routemapoverlay == route) return w;
  return nullptr;
}

void ResultsDialog::Load() {
  if (IsShown() && !m_refresh.IsRunning()) m_refresh.Start(2000);
  const std::vector<RouteMapOverlay*> routes =
      m_WeatherRouting.SelectedRoutesOrPassage();
  if (routes.empty()) {
    m_title->SetLabel(_("No route selected"));
    for (wxStaticText* t : {m_departs, m_arrives, m_elapsed, m_distance, m_speed})
      t->SetLabel(_("N/A"));
    m_legs->DeleteAllItems();
    m_compare->DeleteAllItems();
    m_notes->Clear();
    m_details->Clear();
    m_table->SetRouteMap(nullptr);
    return;
  }

  const RouteMapConfiguration first = routes.front()->GetConfiguration();
  WeatherRoute* firstRow = RowFor(routes.front());
  WeatherRoute* lastRow = RowFor(routes.back());
  const bool passage = first.IsMultiLegGenerated && routes.size() > 1;
  m_title->SetLabel(passage ? first.MultiLegParentRouteName
                            : (firstRow ? firstRow->Name : first.Start));

  // Headline.
  m_departs->SetLabel(firstRow ? firstRow->StartTime : _("N/A"));
  m_arrives->SetLabel(lastRow ? lastRow->EndTime : _("N/A"));
  double distance = 0;
  bool distanceKnown = true;
  for (RouteMapOverlay* r : routes) {
    WeatherRoute* w = RowFor(r);
    const double d = w ? LeadingNumber(w->Distance) : NAN;
    if (std::isnan(d)) distanceKnown = false;
    else distance += d;
  }
  const wxDateTime start = first.StartTime;
  const wxDateTime end = routes.back()->EndTime();
  const bool timed = start.IsValid() && end.IsValid() && end > start &&
                     routes.back()->ReachedDestination();
  m_elapsed->SetLabel(timed ? FormatSpan(end - start) : _("N/A"));
  m_distance->SetLabel(distanceKnown
                           ? wxString::Format(_T("%.2f %s"), distance,
                                              getUsrDistanceUnit_Plugin())
                           : _("N/A"));
  const double hours = timed ? (end - start).GetSeconds().ToDouble() / 3600 : 0;
  m_speed->SetLabel(distanceKnown && hours > 0
                        ? wxString::Format(_T("%.1f %s"), distance / hours,
                                           getUsrSpeedUnit_Plugin())
                        : _("N/A"));

  LoadSummary(routes);
  m_table->SetRouteMap(routes.front());
  LoadCompare(routes);
  LoadDetails(routes);
  Layout();
}

void ResultsDialog::LoadSummary(const std::vector<RouteMapOverlay*>& routes) {
  m_legs->DeleteAllItems();
  const auto points = KnownPoints();
  wxString notes;
  for (size_t i = 0; i < routes.size(); ++i) {
    RouteMapOverlay* r = routes[i];
    WeatherRoute* w = RowFor(r);
    if (!w) continue;
    const RouteMapConfiguration c = r->GetConfiguration();
    const long row = m_legs->InsertItem(m_legs->GetItemCount(),
                                        routes.size() > 1
                                            ? wxString::Format(_T("%zu"), i + 1)
                                            : wxString(_("Route")));
    m_legs->SetItem(row, 1, w->StartTime);
    m_legs->SetItem(row, 2, w->EndTime);
    m_legs->SetItem(row, 3, w->Distance);
    m_legs->SetItem(row, 4, w->AvgSpeed);
    m_legs->SetItem(row, 5, w->AvgWind + _T(" / ") + w->MaxWind);
    m_legs->SetItem(row, 6, w->AvgCurrent);
    m_legs->SetItem(row, 7, w->Tacks + _T(" / ") + w->Jibes);
    m_legs->SetItem(row, 8, w->Comfort);

    // Mark rules: check the computed path, not just the settings.
    wxString rules;
    if (!c.MarkRules.empty()) {
      std::vector<std::pair<double, double>> path;
      if (r->ReachedDestination())
        for (const PlotData& p : r->GetPlotData(false))
          path.push_back({p.lat, p.lon});
      for (const wr::MarkRule& rule : c.MarkRules) {
        const wxString mark = wr::NameForPoint(rule.lat, rule.lon, points);
        if (!rules.IsEmpty()) rules += _T("; ");
        const wr::PassReport pass = wr::CheckPass(rule, path);
        if (!pass.valid) {
          rules += wxString::Format(_("%s: not computed"), mark);
          continue;
        }
        const wxString side = pass.side == wr::MarkSide::LeaveToPort
                                  ? _("to port")
                                  : _("to starboard");
        rules += wxString::Format(_("%s %s, %.2f nm %s"), mark, side,
                                  pass.closest_nm,
                                  pass.ok ? _("OK") : _("BROKEN"));
      }
    }
    m_legs->SetItem(row, 9, rules);
    if (!w->WeatherSourceDetail.IsEmpty())
      notes += wxString::Format(_("%s: %s\n"),
                                routes.size() > 1
                                    ? wxString::Format(_("Leg %zu"), i + 1)
                                    : wxString(_("Data")),
                                w->WeatherSourceDetail);
  }
  for (int col = 0; col < m_legs->GetColumnCount(); ++col)
    m_legs->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
  for (int col = 0; col < m_legs->GetColumnCount(); ++col) {
    const int header = m_legs->GetColumnWidth(col);
    m_legs->SetColumnWidth(col, wxLIST_AUTOSIZE);
    if (m_legs->GetColumnWidth(col) < header)
      m_legs->SetColumnWidth(col, header);
  }
  m_notes->ChangeValue(notes.Trim());
}

void ResultsDialog::LoadCompare(const std::vector<RouteMapOverlay*>& routes) {
  m_compare->DeleteAllItems();
  m_compareItems.clear();
  const RouteMapConfiguration first = routes.front()->GetConfiguration();
  const RouteMapConfiguration last = routes.back()->GetConfiguration();
  const wxString from = first.Start, to = last.End;

  // Each passage counts once (all its legs); every other route on its own.
  std::map<wxString, CompareItem> passages;
  for (WeatherRoute* w : m_WeatherRouting.m_WeatherRoutes) {
    RouteMapOverlay* r = w->routemapoverlay;
    const RouteMapConfiguration c = r->GetConfiguration();
    if (c.DepartureTimeOptimizationCandidate) continue;
    if (c.IsMultiLegGenerated && !c.MultiLegGroupId.IsEmpty()) {
      CompareItem& item = passages[c.MultiLegGroupId];
      item.name = c.MultiLegParentRouteName;
      item.routes.push_back(r);
      continue;
    }
    CompareItem item;
    item.name = w->Name;
    item.routes = {r};
    m_compareItems.push_back(item);
  }
  for (auto& [id, item] : passages) {
    std::sort(item.routes.begin(), item.routes.end(),
              [](RouteMapOverlay* a, RouteMapOverlay* b) {
                return a->GetConfiguration().MultiLegLegIndex <
                       b->GetConfiguration().MultiLegLegIndex;
              });
    m_compareItems.push_back(item);
  }

  // Keep only routes between the same start and finish.
  std::vector<CompareItem> same;
  for (CompareItem& item : m_compareItems) {
    if (item.routes.front()->GetConfiguration().Start != from ||
        item.routes.back()->GetConfiguration().End != to)
      continue;
    WeatherRoute* lastRow = RowFor(item.routes.back());
    item.arrives = item.routes.back()->ReachedDestination()
                       ? item.routes.back()->EndTime()
                       : wxDateTime();
    item.distance = 0;
    for (RouteMapOverlay* r : item.routes) {
      WeatherRoute* w = RowFor(r);
      const double d = w ? LeadingNumber(w->Distance) : NAN;
      if (!std::isnan(d)) item.distance += d;
    }
    item.comfort = lastRow ? lastRow->Comfort : wxString();
    item.state = lastRow ? lastRow->State : wxString();
    same.push_back(item);
  }
  std::sort(same.begin(), same.end(), [](const CompareItem& a, const CompareItem& b) {
    if (a.arrives.IsValid() != b.arrives.IsValid()) return a.arrives.IsValid();
    return a.arrives.IsValid() && a.arrives < b.arrives;
  });
  m_compareItems = same;
  m_compareNote->SetLabel(wxString::Format(
      _("Routes from %s to %s, fastest first."),
      wr::ShortPointName(from, first.MultiLegParentRouteName),
      wr::ShortPointName(to, last.MultiLegParentRouteName)));

  wxDateTime best;
  for (const CompareItem& item : m_compareItems)
    if (item.arrives.IsValid()) {
      best = item.arrives;
      break;
    }
  for (const CompareItem& item : m_compareItems) {
    const long row = m_compare->InsertItem(m_compare->GetItemCount(), item.name);
    m_compare->SetItem(row, 1, item.arrives.IsValid()
                                   ? item.arrives.Format(_T("%a %H:%M"))
                                   : wxString(_("N/A")));
    wxString extra = _("N/A");
    if (item.arrives.IsValid() && best.IsValid()) {
      const long min = static_cast<long>((item.arrives - best).GetMinutes());
      extra = min == 0 ? wxString(_("fastest"))
                       : wxString::Format(_("+%ld min"), min);
    }
    m_compare->SetItem(row, 2, extra);
    m_compare->SetItem(row, 3, wxString::Format(_T("%.2f"), item.distance));
    m_compare->SetItem(row, 4, item.comfort);
    m_compare->SetItem(row, 5, item.state);
    if (item.routes.front() == routes.front())
      m_compare->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
  }
  for (int col = 0; col < m_compare->GetColumnCount(); ++col)
    m_compare->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
  m_compare->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

void ResultsDialog::OnCompareActivated(wxListEvent& event) {
  const long row = event.GetIndex();
  if (row < 0 || row >= static_cast<long>(m_compareItems.size())) return;
  const CompareItem& item = m_compareItems[row];
  const RouteMapConfiguration c = item.routes.front()->GetConfiguration();
  if (c.IsMultiLegGenerated && !c.MultiLegGroupId.IsEmpty()) {
    m_WeatherRouting.SelectMultiLegGroup(c.MultiLegGroupId);
  } else {
    wxListCtrl* list = m_WeatherRouting.m_panel->m_lWeatherRoutes;
    for (long i = 0; i < list->GetItemCount(); ++i) {
      auto* w = reinterpret_cast<WeatherRoute*>(wxUIntToPtr(list->GetItemData(i)));
      list->SetItemState(i,
                         w && w->routemapoverlay == item.routes.front()
                             ? wxLIST_STATE_SELECTED
                             : 0,
                         wxLIST_STATE_SELECTED);
    }
  }
  Load();
}

void ResultsDialog::LoadDetails(const std::vector<RouteMapOverlay*>& routes) {
  std::vector<RouteMapConfiguration> configs;
  for (RouteMapOverlay* r : routes) configs.push_back(r->GetConfiguration());
  wxString text;
  text += _("Settings used\n");
  text += wxString::Format(_("  Boat: %s\n"),
                           wxFileName(configs.front().boatFileName).GetName());
  const auto accuracy = wr::SummarizeAccuracy(configs);
  text += wxString::Format(_("  Accuracy: %s\n"),
                           accuracy ? wr::AccuracyLabel(*accuracy)
                                    : wxString(_("mixed")));
  using S = wr::SettingsSection;
  for (const wr::SettingsField& f : wr::RouteSettingsFields()) {
    if (f.section != S::Weather && f.section != S::Limits &&
        f.section != S::Safety && f.section != S::Motoring &&
        f.section != S::CustomAccuracy)
      continue;
    const wr::FieldSummary s = wr::Summarize(f, configs);
    wxString value;
    if (s.mixed) value = _("mixed");
    else if (f.kind == wr::FieldKind::Toggle) value = s.value ? _("yes") : _("no");
    else if (f.kind == wr::FieldKind::Choice) {
      const int i = static_cast<int>(std::lround(s.value));
      value = i >= 0 && i < static_cast<int>(f.choices.size()) ? f.choices[i]
                                                                : wxString(_T("?"));
    } else {
      value = wxString::Format(_T("%.*f"), f.digits, s.value);
      if (!f.unit.IsEmpty()) value += _T(" ") + f.unit;
    }
    text += wxString::Format(_T("  %s: %s\n"), f.label, value);
  }
  text += _("\nComputation\n");
  for (size_t i = 0; i < routes.size(); ++i) {
    WeatherRoute* w = RowFor(routes[i]);
    if (!w) continue;
    text += wxString::Format(_("  %s: %s"),
                             routes.size() > 1
                                 ? wxString::Format(_("Leg %zu"), i + 1)
                                 : wxString(_("Route")),
                             w->State);
    if (!w->WeatherSource.IsEmpty())
      text += wxString::Format(_(", data %s"), w->WeatherSource);
    text += _T("\n");
  }
  m_details->ChangeValue(text);
}
