// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cmath>

#include "CurrentGribFile.h"

namespace wr = weather_routing;

namespace {
const std::string kFile = std::string(WR_TEST_CURRENT_DIR) +
                          "/mumbai_current_2026-11-28.grb";
std::time_t Utc(int h, int m) {
  std::tm t{};
  t.tm_year = 2026 - 1900;
  t.tm_mon = 10;  // November
  t.tm_mday = 28;
  t.tm_hour = h;
  t.tm_min = m;
  return timegm(&t);
}
}  // namespace

TEST(CurrentGribFile, ReadsTheTideModelOutput) {
  wr::CurrentGribFile f;
  std::string error;
  ASSERT_TRUE(f.Load(kFile, &error)) << error;
  ASSERT_EQ(f.Grids().size(), 9u);  // hourly U/V pairs
  EXPECT_EQ(f.FirstTime(), Utc(8, 30));
  EXPECT_EQ(f.LastTime(), Utc(16, 30));
  EXPECT_EQ(f.Grids()[0].ni, 18);
  EXPECT_EQ(f.Grids()[0].nj, 26);
}

TEST(CurrentGribFile, MatchesThePythonDecoderAtAGridPoint) {
  // Row 10, column 10 at 10:30 UTC: 18.90 N, 72.88 E, U -0.56204, V -0.6766
  // (decoded independently from the same file).
  wr::CurrentGribFile f;
  ASSERT_TRUE(f.Load(kFile, nullptr));
  double u = 0, v = 0;
  ASSERT_TRUE(f.Sample(18.90, 72.88, Utc(10, 30), &u, &v));
  EXPECT_NEAR(u, -0.56204, 2e-4);
  EXPECT_NEAR(v, -0.6766, 2e-4);
}

TEST(CurrentGribFile, InterpolatesInTimeBetweenHours) {
  wr::CurrentGribFile f;
  ASSERT_TRUE(f.Load(kFile, nullptr));
  double u0, v0, u1, v1, um, vm;
  ASSERT_TRUE(f.Sample(18.90, 72.88, Utc(10, 30), &u0, &v0));
  ASSERT_TRUE(f.Sample(18.90, 72.88, Utc(11, 30), &u1, &v1));
  ASSERT_TRUE(f.Sample(18.90, 72.88, Utc(11, 0), &um, &vm));
  EXPECT_NEAR(um, (u0 + u1) / 2, 1e-9);
  EXPECT_NEAR(vm, (v0 + v1) / 2, 1e-9);
}

TEST(CurrentGribFile, NothingOutsideTheGridOrTimeSpan) {
  wr::CurrentGribFile f;
  ASSERT_TRUE(f.Load(kFile, nullptr));
  double u, v;
  EXPECT_FALSE(f.Sample(18.90, 72.88, Utc(7, 0), &u, &v));   // before
  EXPECT_FALSE(f.Sample(18.90, 72.88, Utc(17, 0), &u, &v));  // after
  EXPECT_FALSE(f.Sample(19.20, 72.88, Utc(10, 30), &u, &v)); // north of grid
  EXPECT_FALSE(f.Sample(18.90, 73.20, Utc(10, 30), &u, &v)); // east of grid
  EXPECT_TRUE(f.Sample(18.75, 72.95, Utc(16, 30), &u, &v));  // corner, end
}

TEST(CurrentGribFile, RejectsFilesWithoutCurrent) {
  wr::CurrentGribFile f;
  std::string error;
  EXPECT_FALSE(f.Parse({'n', 'o', 't', ' ', 'g', 'r', 'i', 'b'}, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(f.Load("/nonexistent.grb", &error));
}

TEST(CurrentGribFile, CacheReturnsTheSameFile) {
  auto a = wr::CurrentGribFile::Cached(kFile, nullptr);
  auto b = wr::CurrentGribFile::Cached(kFile, nullptr);
  ASSERT_TRUE(a);
  EXPECT_EQ(a.get(), b.get());
}
