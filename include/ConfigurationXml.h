// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading and writing one route configuration (<Configuration> element of
// WeatherRoutingConfiguration.xml) without any GUI dependency, so it can be
// unit-tested and shared by every dialog that edits a configuration.
#pragma once

#include <functional>

#include <wx/datetime.h>
#include <wx/string.h>

class TiXmlElement;
class RouteMapConfiguration;

namespace weather_routing {

// Things the reader needs from the running plugin. The defaults suit tests.
struct ConfigurationXmlReadOptions {
  // Turns the saved boat file name into a usable path, or "" when no such
  // boat exists. Default: keep the name as saved.
  std::function<wxString(const wxString&)> resolve_boat;
  // Default for ExploreComfortAlternatives when the file does not say and the
  // engine is "All".
  bool comfort_exploration_default = true;
  // Passage shoreline resolution when the file does not say.
  int default_shoreline_resolution = 2;
  // "Now", used for UseCurrentTime and missing start times.
  wxDateTime now = wxDateTime::Now();
};

// Fills `configuration` from a <Configuration> element, applying the same
// defaults and clamping the plugin has always used.
void ReadConfigurationXml(TiXmlElement* e, RouteMapConfiguration& configuration,
                          const ConfigurationXmlReadOptions& options = {});

// Writes every saved attribute and <MarkRule> child of `configuration` to `e`.
void WriteConfigurationXml(const RouteMapConfiguration& configuration,
                           TiXmlElement* e);

}  // namespace weather_routing
