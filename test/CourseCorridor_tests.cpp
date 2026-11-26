// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "CourseCorridor.h"

namespace co = weather_routing::corridor;

namespace {
// Initial great-circle bearing, close enough for the checks below.
double GcBearing(double lat0, double lon0, double lat1, double lon1) {
  const double r = 3.14159265358979323846 / 180;
  const double y = std::sin((lon1 - lon0) * r) * std::cos(lat1 * r);
  const double x = std::cos(lat0 * r) * std::sin(lat1 * r) -
                   std::sin(lat0 * r) * std::cos(lat1 * r) *
                       std::cos((lon1 - lon0) * r);
  return std::atan2(y, x) / r;
}
}  // namespace

TEST(CourseCorridor, PointOnTheDirectLineHasNoDeviation) {
  // Review I4: 50N 0E to 55N 8E used to show ~29 degrees on the line itself.
  const double sLat = 50, sLon = 0, eLat = 55, eLon = 8;
  const double midLat = 52.5, midLon = 4;
  EXPECT_TRUE(co::WithinDivertedCourse(sLat, sLon, eLat, eLon, 5, midLat, midLon));
  EXPECT_TRUE(co::WithinCourseAngle(sLat, sLon, GcBearing(sLat, sLon, eLat, eLon),
                                    5, midLat, midLon));
}

TEST(CourseCorridor, CorridorIsTheSameWidthOnBothSides) {
  // Points 0.5 degree of latitude either side of the course midpoint.
  const double sLat = 40, sLon = 0, eLat = 50, eLon = 10;
  const double mLat = 45, mLon = 5;
  for (double limit : {10.0, 20.0, 40.0}) {
    for (double off : {0.3, 0.8, 1.5}) {
      const bool left = co::WithinDivertedCourse(sLat, sLon, eLat, eLon, limit,
                                                 mLat + off, mLon - off);
      const bool right = co::WithinDivertedCourse(sLat, sLon, eLat, eLon, limit,
                                                  mLat - off, mLon + off);
      EXPECT_EQ(left, right) << "limit " << limit << " offset " << off;
    }
  }
}

TEST(CourseCorridor, FarOffCourseIsRejectedAndOffMeansOff) {
  EXPECT_FALSE(co::WithinDivertedCourse(40, 0, 50, 10, 30, 45, -5));
  EXPECT_TRUE(co::WithinDivertedCourse(40, 0, 50, 10, 180, 45, -5));
  EXPECT_FALSE(co::WithinCourseAngle(40, 0, 45, 30, 35, 5));  // behind start
  EXPECT_TRUE(co::WithinCourseAngle(40, 0, 45, 180, 35, 5));
}
