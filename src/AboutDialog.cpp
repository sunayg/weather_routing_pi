/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */
#include <cstdint>

#include <wx/wx.h>
#include <wx/clipbrd.h>

#include <stdlib.h>
#include <math.h>

#include "AboutDialog.h"
#include "weather_routing_pi.h"
#include "WeatherRoutingWxCompat.h"
#include "AndroidDialogHeader.h"
#include "WeatherRoutingBrowser.h"

AboutDialog::AboutDialog(wxWindow* parent) : AboutDialogBase(parent) {
  //     m_stVersion->SetLabel(wxString::Format(_T("%d.%d"),PLUGIN_VERSION_MAJOR,
  //     PLUGIN_VERSION_MINOR));

  m_stVersion->SetLabel(wxString::Format(
      _T("%d.%d.%d"), PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR,
      PLUGIN_VERSION_PATCH));

#ifndef __OCPN__ANDROID__
  // Desktop (docs/ui-redesign.md, About): which build is running, and the
  // three things people come here for.
  SetTitle(_("About Weather Routing"));
  m_stVersion->SetLabel(wxString::Format(
      _("Version %d.%d.%d, sunayg fork, built %s"), PLUGIN_VERSION_MAJOR,
      PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH, wxString(__DATE__)));
  m_bAboutAuthor->Hide();
  wxSizer* buttons = m_bClose->GetContainingSizer();
  if (buttons) {
    const int at = 0;
    wxButton* manual = new wxButton(m_bClose->GetParent(), wxID_ANY, _("Manual"));
    wxButton* report =
        new wxButton(m_bClose->GetParent(), wxID_ANY, _("Report a problem"));
    wxButton* copy =
        new wxButton(m_bClose->GetParent(), wxID_ANY, _("Copy version info"));
    buttons->Insert(at, copy, 0, wxALL, 5);
    buttons->Insert(at, report, 0, wxALL, 5);
    buttons->Insert(at, manual, 0, wxALL, 5);
    manual->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
      WR_OpenBrowser(
          "https://opencpn-manuals.github.io/main/weather_routing/index.html");
    });
    report->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
      WR_OpenBrowser("https://github.com/sunayg/weather_routing_pi/issues");
    });
    copy->SetToolTip(_("Copy the version, build and system to paste into a "
                       "problem report."));
    copy->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
      const wxString info = wxString::Format(
          "Weather Routing %d.%d.%d (sunayg fork), built %s %s\nOpenCPN plugin API %d.%d\n%s\n%s",
          PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH,
          wxString(__DATE__), wxString(__TIME__), OCPN_API_VERSION_MAJOR,
          OCPN_API_VERSION_MINOR, wxGetOsDescription(), wxString(wxVERSION_STRING));
      if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(info));
        wxTheClipboard->Close();
      }
    });
  }
  Layout();
  Fit();
#endif

#ifdef __OCPN__ANDROID__
  auto* old = GetSizer();
  SetSizer(nullptr, false);
  old->Clear(false);
  delete old;
  auto* root = new wxBoxSizer(wxVERTICAL);
  auto* page = new wxScrolledWindow(this, wxID_ANY);
  page->SetScrollRate(0, 20);
  WR_EnableAndroidScrolling(page);
  auto* content = new wxBoxSizer(wxVERTICAL);
  const int width = GetCanvasByIndex(0)->GetClientSize().x - 80;
  for (auto* text : {m_staticText135, m_stVersion, m_staticText110}) {
    text->Reparent(page);
    wxFont font = text->GetFont();
    font.SetPointSize(text == m_stVersion ? 22 : 17);
    text->SetFont(font);
    WR_WrapAndroidText(text, text->GetLabel(), width);
    content->Add(text, 0, wxEXPAND | wxALL, 16);
  }
  m_bAboutAuthor->Reparent(page);
  m_bAboutAuthor->SetMinSize(wxSize(-1, 72));
  content->Add(m_bAboutAuthor, 0, wxEXPAND | wxALL, 16);
  m_bClose->Hide();
  page->SetSizer(content);
  root->Add(page, 1, wxEXPAND);
  SetSizer(root);
  WR_StyleAndroidControls(this);
  WR_AddAndroidDoneHeader(this, _("About xWeatherRouting"),
                         [this]() { EndModal(wxID_OK); });
  const wxSize sz = GetCanvasByIndex(0)->GetClientSize();
  SetSize(sz.x - 24, sz.y - 24);
  CentreOnParent();
#endif
}

void AboutDialog::OnAboutAuthor(wxCommandEvent& event) {
  WR_OpenBrowser(_T(ABOUT_AUTHOR_URL));
}
