// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moved from WeatherRouting::OpenXML / SaveXML. Changes: what came from the
// running plugin now comes from ConfigurationXmlReadOptions, and decimal
// settings and mark positions are saved at full precision (they used to be
// cut to whole numbers and to 6 significant digits).
#include "ConfigurationXml.h"

#include <algorithm>
#include <cmath>

#include <wx/wx.h>

#include "tinyxml.h"

#include "DepartureScheduler.h"
#include "GribTimelineCachePolicy.h"
#include "MarkRules.h"
#include "RouteMap.h"
#include "RoutingEngineSettingsPersistence.h"
#include "RoutingResourcePolicy.h"
#include "RoutingTimePersistence.h"
#include "ShorelineSettings.h"
#include "Utilities.h"

namespace weather_routing {

void ReadConfigurationXml(TiXmlElement* e, RouteMapConfiguration& configuration,
                          const ConfigurationXmlReadOptions& options) {
  configuration.RouteGUID = wxString::FromUTF8(e->Attribute("GUID"));
  configuration.StartType =
      (RouteMapConfiguration::StartDataType)AttributeInt(
          e, "StartType", RouteMapConfiguration::START_FROM_POSITION);
  configuration.Start = wxString::FromUTF8(e->Attribute("Start"));
  configuration.EndType =
      (RouteMapConfiguration::EndDataType)AttributeInt(
          e, "EndType", RouteMapConfiguration::END_AT_POSITION);
  const char* startGuid = e->Attribute("StartGUID");
  if (startGuid) configuration.StartGUID = wxString::FromUTF8(startGuid);
  const char* endGuid = e->Attribute("EndGUID");
  if (endGuid) configuration.EndGUID = wxString::FromUTF8(endGuid);
  if (configuration.StartType ==
          RouteMapConfiguration::START_FROM_POSITION &&
      !configuration.StartGUID.IsEmpty())
    configuration.StartType = RouteMapConfiguration::START_FROM_WAYPOINT;
  if (configuration.EndType == RouteMapConfiguration::END_AT_POSITION &&
      !configuration.EndGUID.IsEmpty())
    configuration.EndType = RouteMapConfiguration::END_AT_WAYPOINT;
  configuration.UseCurrentTime =
      AttributeBool(e, "UseCurrentTime", false);
  const int routingTimeMode =
      AttributeInt(e, "RoutingTimeMode",
                   RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME);
  configuration.TimeMode =
      routingTimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
          ? RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
          : RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
  configuration.ArrivalSearchHorizonMinutes =
      std::max(60, AttributeInt(e, "ArrivalSearchHorizonMinutes",
                                30 * 24 * 60));
  configuration.ArrivalSafetyMarginMinutes =
      std::max(0,
               AttributeInt(e, "ArrivalSafetyMarginMinutes", 30));
  configuration.DepartureTimeOptimizationEnabled =
      AttributeBool(e, "DepartureTimeOptimizationEnabled", false);
  configuration.DepartureTimeOptimizationChartDisplay = std::clamp(
      AttributeInt(e, "DepartureTimeOptimizationChartDisplay", 0), 0, 2);
  configuration.DepartureTimeOptimizationRangeMinutes =
      AttributeInt(e, "DepartureTimeOptimizationRangeMinutes", 360);
  configuration.DepartureTimeOptimizationStepMinutes =
      AttributeInt(e, "DepartureTimeOptimizationStepMinutes", 60);
  configuration.DepartureTimeOptimizationConcurrentRoutes = std::max(
      0, std::min(
             kMaximumParallelDepartureCandidates,
             AttributeInt(
                 e, "DepartureTimeOptimizationConcurrentRoutes", 0)));
  configuration.EngineSettings = ReadRoutingEngineSettings(*e);
  configuration.AutomaticAdaptiveSettings = AttributeBool(e, "AutomaticAdaptiveSettings", false);
  configuration.ExploreComfortAlternatives = AttributeBool(
      e, "ExploreComfortAlternatives",
      configuration.EngineSettings.engine == RoutingEngine::All &&
          options.comfort_exploration_default);
  configuration.ComfortAdditionalPercent =
      std::clamp(AttributeInt(e, "ComfortAdditionalPercent", 200), 0, 400);
  configuration.ComfortMaximumSeconds =
      std::clamp(AttributeInt(e, "ComfortMaximumSeconds", 20), 0, 3600);
  configuration.ShorelineResolution = ReadPassageShorelineResolution(*e, options.default_shoreline_resolution);
  configuration.QuickShorelineResolution = ReadShorelineResolution(*e, kDefaultQuickShorelineResolution, "QuickShorelineResolution");
  configuration.ChartShorelineResolution = ReadShorelineResolution(*e, 0, "ChartShorelineResolution");
  configuration.MainGribTimelineCacheMiB =
      NormalizeGribTimelineCacheMiB(
          AttributeInt(e, "MainGribTimelineCacheMiB",
                       kMainGribTimelineCacheDefaultMiB),
          false);
  configuration.QuickGribTimelineCacheMiB =
      NormalizeGribTimelineCacheMiB(
          AttributeInt(e, "QuickGribTimelineCacheMiB",
                       kQuickGribTimelineCacheDefaultMiB),
          true);
  configuration.RoutingEffortPercent =
      NormalizeRoutingEffortPercent(
          AttributeInt(e, "RoutingEffortPercent",
                       kDefaultRoutingEffortPercent));
  configuration.IsMultiLegGenerated =
      AttributeBool(e, "IsMultiLegGenerated", false);
  const char* multiLegGroupId = e->Attribute("MultiLegGroupId");
  if (multiLegGroupId)
    configuration.MultiLegGroupId = wxString::FromUTF8(multiLegGroupId);
  const char* multiLegParentRouteGUID =
      e->Attribute("MultiLegParentRouteGUID");
  if (multiLegParentRouteGUID)
    configuration.MultiLegParentRouteGUID =
        wxString::FromUTF8(multiLegParentRouteGUID);
  const char* multiLegParentRouteName =
      e->Attribute("MultiLegParentRouteName");
  if (multiLegParentRouteName)
    configuration.MultiLegParentRouteName =
        wxString::FromUTF8(multiLegParentRouteName);
  configuration.MultiLegLegIndex = AttributeInt(e, "MultiLegLegIndex", 0);
  configuration.MultiLegLegCount = AttributeInt(e, "MultiLegLegCount", 0);
  if (configuration.UseCurrentTime) {
    // The current time will be overridden when the route is computed.
    configuration.StartTime = options.now;
  } else {
    configuration.StartTime = ReadRoutingTime(*e,
        "StartDate", "StartTime", "StartTimeUnixSeconds", options.now);
  }
  configuration.PlannedArrivalTime = ReadRoutingTime(*e,
      "PlannedArrivalDate", "PlannedArrivalTime", "PlannedArrivalUnixSeconds",
      configuration.StartTime + wxTimeSpan::Hours(24));

  configuration.End = wxString::FromUTF8(e->Attribute("End"));
  configuration.DeltaTime = AttributeDouble(e, "dt", 0);

  if (const char* current = e->Attribute("CurrentFile"))
    configuration.CurrentFile = wxString::FromUTF8(current);
  configuration.boatFileName = wxString::FromUTF8(e->Attribute("Boat"));
  if (options.resolve_boat)
    configuration.boatFileName = options.resolve_boat(configuration.boatFileName);

  configuration.Integrator =
      (RouteMapConfiguration::IntegratorType)AttributeInt(e, "Integrator",
                                                          0);

  configuration.MaxDivertedCourse =
      AttributeDouble(e, "MaxDivertedCourse", 90);
  configuration.MaxCourseAngle =
      AttributeDouble(e, "MaxCourseAngle", 180);
  configuration.MaxSearchAngle =
      AttributeDouble(e, "MaxSearchAngle", 120);
  configuration.MaxTrueWindKnots =
      AttributeDouble(e, "MaxTrueWindKnots", 50);
  configuration.MaxApparentWindKnots =
      AttributeDouble(e, "MaxApparentWindKnots", 50);
  configuration.MaxUpwindTrueWindKnots =
      std::max(0.0, AttributeDouble(e, "MaxUpwindTrueWindKnots", 0));
  configuration.MinTrueWindKnots =
      std::max(0.0, AttributeDouble(e, "MinTrueWindKnots", 0));

  configuration.RequireWaveCoverage = AttributeBool(e, "RequireWaveCoverage", false);
  configuration.MaxSwellMeters =
      AttributeDouble(e, "MaxSwellMeters", 20.);
  configuration.MaxLatitude = AttributeDouble(e, "MaxLatitude", 90);
  configuration.TackingTime = AttributeDouble(e, "TackingTime", 0);
  configuration.JibingTime = AttributeDouble(e, "JibingTime", 0);
  configuration.SailPlanChangeTime =
      AttributeDouble(e, "SailPlanChangeTime", 0);
  configuration.WindVSCurrent = AttributeDouble(e, "WindVSCurrent", 0);

  configuration.AvoidCycloneTracks =
      AttributeBool(e, "AvoidCycloneTracks", false);
  configuration.CycloneMonths = AttributeInt(e, "CycloneMonths", 2);
  configuration.CycloneDays = AttributeInt(e, "CycloneDays", 0);

  configuration.UseGrib = AttributeBool(e, "UseGrib", true);
  configuration.ClimatologyType =
      (RouteMapConfiguration::ClimatologyDataType)AttributeInt(
          e, "ClimatologyType", RouteMapConfiguration::CUMULATIVE_MAP);
  configuration.AllowDataDeficient =
      AttributeBool(e, "AllowDataDeficient", false);
  configuration.WindStrength = AttributeDouble(e, "WindStrength", 1);

  configuration.UpwindEfficiency =
      AttributeDouble(e, "UpwindEfficiency", 1.);
  configuration.DownwindEfficiency =
      AttributeDouble(e, "DownwindEfficiency", 1.);
  configuration.NightCumulativeEfficiency =
      AttributeDouble(e, "NightCumulativeEfficiency", 1.);

  configuration.DetectLand = AttributeBool(e, "DetectLand", true);
  configuration.SafetyMarginLand =
      AttributeDouble(e, "SafetyMarginLand", 0.);
  configuration.MinimumDepthMeters =
      wxMax(0.0, AttributeDouble(e, "MinimumDepthMeters", 0.));
  configuration.DetectBoundary =
      AttributeBool(e, "DetectBoundary", false);
  configuration.MarkRules.clear();
  for (TiXmlElement* m = e->FirstChildElement("MarkRule"); m;
       m = m->NextSiblingElement("MarkRule")) {
    MarkRule rule;
    rule.lat = AttributeDouble(m, "Lat", NAN);
    rule.lon = AttributeDouble(m, "Lon", NAN);
    if (!std::isfinite(rule.lat) || !std::isfinite(rule.lon)) continue;
    rule.clearance_nm =
        wxMax(0.0, AttributeDouble(m, "ClearanceNm", 0.));
    const char* side = m->Attribute("Side");
    const wxString sideName = side ? wxString::FromUTF8(side) : wxString();
    if (sideName == "LeaveToPort")
      rule.side = MarkSide::LeaveToPort;
    else if (sideName == "LeaveToStarboard")
      rule.side = MarkSide::LeaveToStarboard;
    else
      rule.side = MarkSide::Any;
    rule.barrier_length_nm =
        wxMax(0.0, AttributeDouble(m, "BarrierLengthNm", 20.));
    if (m->Attribute("BarrierBearing")) {
      rule.barrier_bearing_deg = AttributeDouble(m, "BarrierBearing", 0.);
    } else {
      /* Hand-written files may give the direction of travel past the
         mark instead; the barrier side follows from it. */
      rule = MakeMarkRule(
          rule.lat, rule.lon, rule.clearance_nm, rule.side,
          AttributeDouble(m, "TravelBearing", 0.),
          rule.barrier_length_nm);
    }
    configuration.MarkRules.push_back(rule);
  }
  configuration.Currents = AttributeBool(e, "Currents", true);
  configuration.OptimizeTacking =
      AttributeBool(e, "OptimizeTacking", false);

  configuration.InvertedRegions =
      AttributeBool(e, "InvertedRegions", false);
  configuration.UseReverseReachabilityRecovery =
      AttributeBool(e, "UseReverseReachabilityRecovery", false);
  configuration.Anchoring = AttributeBool(e, "Anchoring", false);

  configuration.FromDegree = AttributeDouble(e, "FromDegree", 0);
  configuration.ToDegree = AttributeDouble(e, "ToDegree", 180);
  configuration.UseOptimalAngles =
      AttributeBool(e, "UseOptimalAngles", false);
  configuration.ByDegrees = AttributeDouble(e, "ByDegrees", 5.);
  configuration.UseMotor = AttributeBool(e, "UseMotor", false);
  configuration.MotorSpeedThreshold =
      AttributeDouble(e, "MotorSpeedThreshold", 2.0);
  configuration.MotorSpeed = AttributeDouble(e, "MotorSpeed", 5.0);
}

void WriteConfigurationXml(const RouteMapConfiguration& configuration,
                           TiXmlElement* c) {

  if (!configuration.RouteGUID.IsEmpty())
    c->SetAttribute("GUID", configuration.RouteGUID.mb_str());

  c->SetAttribute("StartType", configuration.StartType);
  c->SetAttribute("Start", configuration.Start.mb_str());
  if (!configuration.StartGUID.IsEmpty())
    c->SetAttribute("StartGUID", configuration.StartGUID.mb_str());
  c->SetAttribute("EndType", configuration.EndType);
  c->SetAttribute("UseCurrentTime", configuration.UseCurrentTime);
  c->SetAttribute("RoutingTimeMode",
                  static_cast<int>(configuration.TimeMode));
  c->SetAttribute("ArrivalSearchHorizonMinutes",
                  configuration.ArrivalSearchHorizonMinutes);
  c->SetAttribute("ArrivalSafetyMarginMinutes",
                  configuration.ArrivalSafetyMarginMinutes);
  c->SetAttribute("DepartureTimeOptimizationEnabled",
                  configuration.DepartureTimeOptimizationEnabled);
  c->SetAttribute("DepartureTimeOptimizationChartDisplay",
                  configuration.DepartureTimeOptimizationChartDisplay);
  c->SetAttribute("DepartureTimeOptimizationRangeMinutes",
                  configuration.DepartureTimeOptimizationRangeMinutes);
  c->SetAttribute("DepartureTimeOptimizationStepMinutes",
                  configuration.DepartureTimeOptimizationStepMinutes);
  c->SetAttribute("DepartureTimeOptimizationConcurrentRoutes",
                  configuration.DepartureTimeOptimizationConcurrentRoutes);
  WriteRoutingEngineSettings(configuration.EngineSettings, *c);
  c->SetAttribute("AutomaticAdaptiveSettings", configuration.AutomaticAdaptiveSettings);
  c->SetAttribute("ExploreComfortAlternatives", configuration.ExploreComfortAlternatives);
  c->SetAttribute("ComfortAdditionalPercent", configuration.ComfortAdditionalPercent);
  c->SetAttribute("ComfortMaximumSeconds", configuration.ComfortMaximumSeconds);
  WritePassageShorelineResolution(*c, configuration.ShorelineResolution);
  c->SetAttribute("ChartShorelineResolution", configuration.ChartShorelineResolution);
  c->SetAttribute("MainGribTimelineCacheMiB",
                  configuration.MainGribTimelineCacheMiB);
  c->SetAttribute("QuickGribTimelineCacheMiB",
                  configuration.QuickGribTimelineCacheMiB);
  c->SetAttribute(
      "RoutingEffortPercent",
      NormalizeRoutingEffortPercent(
          configuration.RoutingEffortPercent));
  c->SetAttribute("IsMultiLegGenerated", configuration.IsMultiLegGenerated);
  if (!configuration.MultiLegGroupId.IsEmpty())
    c->SetAttribute("MultiLegGroupId",
                    configuration.MultiLegGroupId.mb_str());
  if (!configuration.MultiLegParentRouteGUID.IsEmpty())
    c->SetAttribute("MultiLegParentRouteGUID",
                    configuration.MultiLegParentRouteGUID.mb_str());
  if (!configuration.MultiLegParentRouteName.IsEmpty())
    c->SetAttribute("MultiLegParentRouteName",
                    configuration.MultiLegParentRouteName.mb_str());
  if (configuration.MultiLegLegIndex)
    c->SetAttribute("MultiLegLegIndex", configuration.MultiLegLegIndex);
  if (configuration.MultiLegLegCount)
    c->SetAttribute("MultiLegLegCount", configuration.MultiLegLegCount);
  if (!configuration.UseCurrentTime)
    WriteRoutingTime(*c, configuration.StartTime,
        "StartDate", "StartTime", "StartTimeUnixSeconds");
  WriteRoutingTime(*c, configuration.PlannedArrivalTime,
      "PlannedArrivalDate", "PlannedArrivalTime", "PlannedArrivalUnixSeconds");
  c->SetAttribute("End", configuration.End.mb_str());
  if (!configuration.EndGUID.IsEmpty())
    c->SetAttribute("EndGUID", configuration.EndGUID.mb_str());
  c->SetDoubleAttribute("dt", configuration.DeltaTime);

  c->SetAttribute("Boat", configuration.boatFileName.ToUTF8());
  if (!configuration.CurrentFile.IsEmpty())
    c->SetAttribute("CurrentFile", configuration.CurrentFile.ToUTF8());

  c->SetAttribute("Integrator", configuration.Integrator);

  c->SetDoubleAttribute("MaxDivertedCourse", configuration.MaxDivertedCourse);
  c->SetDoubleAttribute("MaxCourseAngle", configuration.MaxCourseAngle);
  c->SetDoubleAttribute("MaxSearchAngle", configuration.MaxSearchAngle);
  c->SetDoubleAttribute("MaxTrueWindKnots", configuration.MaxTrueWindKnots);
  c->SetDoubleAttribute("MaxApparentWindKnots", configuration.MaxApparentWindKnots);
  // Written only when set, so files from earlier versions stay unchanged.
  if (configuration.MaxUpwindTrueWindKnots > 0)
    c->SetDoubleAttribute("MaxUpwindTrueWindKnots",
                          configuration.MaxUpwindTrueWindKnots);
  if (configuration.MinTrueWindKnots > 0)
    c->SetDoubleAttribute("MinTrueWindKnots", configuration.MinTrueWindKnots);

  c->SetDoubleAttribute("MaxSwellMeters", configuration.MaxSwellMeters);
  c->SetAttribute("RequireWaveCoverage", configuration.RequireWaveCoverage);
  c->SetDoubleAttribute("MaxLatitude", configuration.MaxLatitude);
  c->SetDoubleAttribute("TackingTime", configuration.TackingTime);
  c->SetDoubleAttribute("JibingTime", configuration.JibingTime);
  c->SetDoubleAttribute("SailPlanChangeTime", configuration.SailPlanChangeTime);
  c->SetDoubleAttribute("WindVSCurrent", configuration.WindVSCurrent);

  c->SetAttribute("AvoidCycloneTracks", configuration.AvoidCycloneTracks);
  c->SetAttribute("CycloneMonths", configuration.CycloneMonths);
  c->SetAttribute("CycloneDays", configuration.CycloneDays);

  c->SetAttribute("UseGrib", configuration.UseGrib);
  c->SetAttribute("ClimatologyType", configuration.ClimatologyType);
  c->SetAttribute("AllowDataDeficient", configuration.AllowDataDeficient);
  c->SetDoubleAttribute("WindStrength", configuration.WindStrength);

  c->SetDoubleAttribute("UpwindEfficiency", configuration.UpwindEfficiency);
  c->SetDoubleAttribute("DownwindEfficiency",
                        configuration.DownwindEfficiency);
  c->SetDoubleAttribute("NightCumulativeEfficiency",
                        configuration.NightCumulativeEfficiency);

  c->SetAttribute("DetectLand", configuration.DetectLand);
  c->SetDoubleAttribute("SafetyMarginLand", configuration.SafetyMarginLand);
  c->SetDoubleAttribute("MinimumDepthMeters",
                        configuration.MinimumDepthMeters);
  c->SetAttribute("DetectBoundary", configuration.DetectBoundary);
  c->SetAttribute("Currents", configuration.Currents);
  c->SetAttribute("OptimizeTacking", configuration.OptimizeTacking);

  c->SetAttribute("InvertedRegions", configuration.InvertedRegions);
  c->SetAttribute("UseReverseReachabilityRecovery",
                  configuration.UseReverseReachabilityRecovery);
  c->SetAttribute("Anchoring", configuration.Anchoring);

  c->SetDoubleAttribute("FromDegree", configuration.FromDegree);
  c->SetDoubleAttribute("ToDegree", configuration.ToDegree);
  c->SetAttribute("UseOptimalAngles", configuration.UseOptimalAngles);
  c->SetDoubleAttribute("ByDegrees", configuration.ByDegrees);
  c->SetAttribute("UseMotor", configuration.UseMotor);
  c->SetDoubleAttribute("MotorSpeedThreshold",
                        configuration.MotorSpeedThreshold);
  c->SetDoubleAttribute("MotorSpeed", configuration.MotorSpeed);

  for (const MarkRule& rule : configuration.MarkRules) {
    TiXmlElement* m = new TiXmlElement("MarkRule");
    m->SetAttribute("Lat", wxString::Format("%.6f", rule.lat).mb_str());
    m->SetAttribute("Lon", wxString::Format("%.6f", rule.lon).mb_str());
    m->SetDoubleAttribute("ClearanceNm", rule.clearance_nm);
    m->SetAttribute(
        "Side",
        rule.side == MarkSide::LeaveToPort
            ? "LeaveToPort"
            : rule.side == MarkSide::LeaveToStarboard
                  ? "LeaveToStarboard"
                  : "Any");
    m->SetDoubleAttribute("BarrierBearing", rule.barrier_bearing_deg);
    m->SetDoubleAttribute("BarrierLengthNm", rule.barrier_length_nm);
    c->LinkEndChild(m);
  }
}

}  // namespace weather_routing
