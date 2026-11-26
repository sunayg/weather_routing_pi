// SPDX-License-Identifier: GPL-3.0-or-later
//
// A tidal (or any) current read straight from a GRIB1 file, so a current
// forecast can be used next to the wind GRIB loaded in the GRIB plugin
// without merging the two files by hand. Reads U and V current components
// (parameters 49 UOGRD and 50 VOGRD, m/s) on regular lat/lon grids, with
// simple packing, any bits per value, decimal scale, any scan direction and
// an optional bitmap. Sampling is bilinear in space and linear in time and
// never extrapolates: outside the grid or the file's time span there is no
// value, so other current sources apply there.
#pragma once

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace weather_routing {

class CurrentGribFile {
 public:
  struct Grid {
    std::time_t valid = 0;  // UTC
    int ni = 0, nj = 0;
    double lat_first = 0, lon_first = 0;  // first point in storage order
    double dlat = 0, dlon = 0;            // signed steps in storage order
    std::vector<float> u, v;              // m/s, NAN where the bitmap is 0
  };

  // Parses every U/V message pair in a GRIB1 byte buffer.
  bool Parse(const std::vector<std::uint8_t>& bytes, std::string* error);
  bool Load(const std::string& path, std::string* error);

  // Current toward (east, north) in m/s at a position and UTC time. False
  // outside the grid or the file's time span.
  bool Sample(double lat, double lon, std::time_t time, double* u,
              double* v) const;

  bool Empty() const { return grids_.empty(); }
  std::time_t FirstTime() const;
  std::time_t LastTime() const;
  const std::vector<Grid>& Grids() const { return grids_; }

  // Shared, reloaded when the file changes on disk (by modification time).
  static std::shared_ptr<const CurrentGribFile> Cached(const std::string& path,
                                                       std::string* error);

 private:
  bool SampleGrid(const Grid& g, double lat, double lon, double* u,
                  double* v) const;
  std::vector<Grid> grids_;  // sorted by valid time
};

}  // namespace weather_routing
