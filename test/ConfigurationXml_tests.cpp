// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-trip tests for one route configuration in
// WeatherRoutingConfiguration.xml. They protect saved settings while the
// dialogs that edit them are redesigned: reading a real file and writing it
// back must keep every attribute and every <MarkRule>.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <wx/wx.h>

#include "tinyxml.h"

#include "ConfigurationXml.h"
#include "MarkRules.h"
#include "RouteMap.h"

namespace wr = weather_routing;

namespace {

const std::string kDir = WR_TEST_CONFIGURATION_DIR;

wr::ConfigurationXmlReadOptions TestOptions() {
  wr::ConfigurationXmlReadOptions o;
  o.resolve_boat = [](const wxString& name) { return name; };
  o.now = wxDateTime(1, wxDateTime::Jan, 2026, 12, 0, 0);
  return o;
}

std::map<std::string, std::string> Attributes(const TiXmlElement* e) {
  std::map<std::string, std::string> out;
  for (const TiXmlAttribute* a = e->FirstAttribute(); a; a = a->Next())
    out[a->Name()] = a->Value();
  return out;
}

// Equal as numbers when both parse as numbers, else as text.
bool SameValue(const std::string& a, const std::string& b) {
  char* ea = nullptr;
  char* eb = nullptr;
  const double da = std::strtod(a.c_str(), &ea);
  const double db = std::strtod(b.c_str(), &eb);
  const bool na = !a.empty() && ea && *ea == '\0';
  const bool nb = !b.empty() && eb && *eb == '\0';
  if (na && nb)
    return std::fabs(da - db) <= 1e-9 * std::max(1.0, std::fabs(da));
  return a == b;
}

// Every attribute of `expected` is in `actual` with the same value, and
// `actual` adds nothing. Reports every difference, not just the first.
void ExpectSameAttributes(const TiXmlElement* expected,
                          const TiXmlElement* actual,
                          const std::string& where) {
  const auto want = Attributes(expected);
  const auto got = Attributes(actual);
  for (const auto& [name, value] : want) {
    auto it = got.find(name);
    if (it == got.end()) {
      ADD_FAILURE() << where << ": attribute " << name << " was dropped";
      continue;
    }
    EXPECT_TRUE(SameValue(value, it->second))
        << where << ": " << name << " was \"" << value << "\", now \""
        << it->second << "\"";
  }
  for (const auto& [name, value] : got)
    if (!want.count(name))
      ADD_FAILURE() << where << ": attribute " << name << "=\"" << value
                    << "\" was added";
}

void ExpectSameMarkRules(const TiXmlElement* expected,
                         const TiXmlElement* actual, const std::string& where) {
  std::vector<const TiXmlElement*> a, b;
  for (auto* m = expected->FirstChildElement("MarkRule"); m;
       m = m->NextSiblingElement("MarkRule"))
    a.push_back(m);
  for (auto* m = actual->FirstChildElement("MarkRule"); m;
       m = m->NextSiblingElement("MarkRule"))
    b.push_back(m);
  ASSERT_EQ(a.size(), b.size()) << where << ": mark rule count";
  for (size_t i = 0; i < a.size(); ++i)
    ExpectSameAttributes(a[i], b[i],
                         where + " MarkRule " + std::to_string(i + 1));
}

TiXmlElement RoundTrip(TiXmlElement* in) {
  RouteMapConfiguration c;
  wr::ReadConfigurationXml(in, c, TestOptions());
  TiXmlElement out("Configuration");
  wr::WriteConfigurationXml(c, &out);
  return out;
}

void CheckFile(const std::string& file) {
  TiXmlDocument doc;
  ASSERT_TRUE(doc.LoadFile((kDir + "/" + file).c_str())) << file;
  int n = 0;
  for (TiXmlElement* e = doc.RootElement()->FirstChildElement("Configuration");
       e; e = e->NextSiblingElement("Configuration")) {
    const std::string where = file + " configuration " + std::to_string(++n);
    TiXmlElement out = RoundTrip(e);
    ExpectSameAttributes(e, &out, where);
    ExpectSameMarkRules(e, &out, where);
  }
  EXPECT_GT(n, 0) << file << " has no configurations";
}

}  // namespace

TEST(ConfigurationXml, RealRoutesFileSurvivesReadAndWrite) {
  CheckFile("WeatherRoutingConfiguration.routes.xml");
}

TEST(ConfigurationXml, RealPassageFileSurvivesReadAndWrite) {
  CheckFile("WeatherRoutingConfiguration.passage.xml");
}

TEST(ConfigurationXml, WritingTwiceGivesTheSameResult) {
  TiXmlDocument doc;
  ASSERT_TRUE(doc.LoadFile(
      (kDir + "/WeatherRoutingConfiguration.routes.xml").c_str()));
  TiXmlElement* e = doc.RootElement()->FirstChildElement("Configuration");
  ASSERT_NE(e, nullptr);
  TiXmlElement first = RoundTrip(e);
  TiXmlElement second = RoundTrip(&first);
  EXPECT_EQ(Attributes(&first), Attributes(&second));
}

TEST(ConfigurationXml, EmptyElementGetsDefaultsThatAreStable) {
  TiXmlElement empty("Configuration");
  TiXmlElement first = RoundTrip(&empty);
  TiXmlElement second = RoundTrip(&first);
  EXPECT_EQ(Attributes(&first), Attributes(&second));
  // A few defaults sailors rely on.
  EXPECT_EQ(Attributes(&first)["MaxTrueWindKnots"], "50");
  EXPECT_EQ(Attributes(&first)["DetectLand"], "1");
  EXPECT_EQ(Attributes(&first)["Currents"], "1");
  EXPECT_EQ(Attributes(&first)["UseGrib"], "1");
}

TEST(ConfigurationXml, DecimalSettingsKeepTheirFraction) {
  // These used to be saved as whole numbers (35.5 kn came back as 35).
  RouteMapConfiguration c;
  {
    TiXmlElement empty("Configuration");
    wr::ReadConfigurationXml(&empty, c, TestOptions());
  }
  c.DeltaTime = 150.5;
  c.MaxDivertedCourse = 95.5;
  c.MaxCourseAngle = 170.5;
  c.MaxSearchAngle = 110.5;
  c.MaxTrueWindKnots = 35.5;
  c.MaxApparentWindKnots = 40.25;
  c.MaxLatitude = 60.75;
  c.TackingTime = 12.5;
  c.JibingTime = 22.5;
  c.SailPlanChangeTime = 7.5;
  c.WindVSCurrent = 0.5;
  TiXmlElement out("Configuration");
  wr::WriteConfigurationXml(c, &out);
  RouteMapConfiguration back;
  wr::ReadConfigurationXml(&out, back, TestOptions());
  EXPECT_DOUBLE_EQ(back.DeltaTime, 150.5);
  EXPECT_DOUBLE_EQ(back.MaxDivertedCourse, 95.5);
  EXPECT_DOUBLE_EQ(back.MaxCourseAngle, 170.5);
  EXPECT_DOUBLE_EQ(back.MaxSearchAngle, 110.5);
  EXPECT_DOUBLE_EQ(back.MaxTrueWindKnots, 35.5);
  EXPECT_DOUBLE_EQ(back.MaxApparentWindKnots, 40.25);
  EXPECT_DOUBLE_EQ(back.MaxLatitude, 60.75);
  EXPECT_DOUBLE_EQ(back.TackingTime, 12.5);
  EXPECT_DOUBLE_EQ(back.JibingTime, 22.5);
  EXPECT_DOUBLE_EQ(back.SailPlanChangeTime, 7.5);
  EXPECT_DOUBLE_EQ(back.WindVSCurrent, 0.5);
}

TEST(ConfigurationXml, MarkPositionsKeepSixDecimals) {
  // Mark positions used to be cut to 6 significant digits (about 10 m).
  RouteMapConfiguration c;
  {
    TiXmlElement empty("Configuration");
    wr::ReadConfigurationXml(&empty, c, TestOptions());
  }
  c.MarkRules.push_back(wr::MakeMarkRule(18.884232, 72.864540, 0.3,
                                         wr::MarkSide::LeaveToPort, 155, 20));
  TiXmlElement out("Configuration");
  wr::WriteConfigurationXml(c, &out);
  RouteMapConfiguration back;
  wr::ReadConfigurationXml(&out, back, TestOptions());
  ASSERT_EQ(back.MarkRules.size(), 1u);
  EXPECT_NEAR(back.MarkRules[0].lat, 18.884232, 1e-7);
  EXPECT_NEAR(back.MarkRules[0].lon, 72.864540, 1e-7);
  EXPECT_EQ(back.MarkRules[0].side, wr::MarkSide::LeaveToPort);
  EXPECT_NEAR(back.MarkRules[0].barrier_bearing_deg, 65, 1e-9);
  EXPECT_DOUBLE_EQ(back.MarkRules[0].clearance_nm, 0.3);
}

TEST(ConfigurationXml, HandWrittenTravelBearingBecomesABarrier) {
  TiXmlElement e("Configuration");
  TiXmlElement rule("MarkRule");
  rule.SetAttribute("Lat", "18.884232");
  rule.SetAttribute("Lon", "72.864540");
  rule.SetAttribute("Side", "LeaveToStarboard");
  rule.SetAttribute("TravelBearing", "155");
  e.InsertEndChild(rule);
  RouteMapConfiguration c;
  wr::ReadConfigurationXml(&e, c, TestOptions());
  ASSERT_EQ(c.MarkRules.size(), 1u);
  EXPECT_NEAR(c.MarkRules[0].barrier_bearing_deg, 245, 1e-9);
  EXPECT_DOUBLE_EQ(c.MarkRules[0].barrier_length_nm, 20);
}
