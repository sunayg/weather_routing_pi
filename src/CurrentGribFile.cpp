// SPDX-License-Identifier: GPL-3.0-or-later
#include "CurrentGribFile.h"

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>

namespace weather_routing {
namespace {

std::uint32_t U(const std::uint8_t* p, int n) {
  std::uint32_t v = 0;
  for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
  return v;
}
// GRIB1 sign-magnitude integers.
long S(const std::uint8_t* p, int n) {
  const std::uint32_t v = U(p, n);
  const std::uint32_t top = 1u << (8 * n - 1);
  return (v & top) ? -static_cast<long>(v & (top - 1)) : static_cast<long>(v);
}
double Ibm(const std::uint8_t* p) {
  const double sign = (p[0] & 0x80) ? -1.0 : 1.0;
  const int e = (p[0] & 0x7F) - 64;
  const double m = U(p + 1, 3) / 16777216.0;
  return sign * m * std::pow(16.0, e);
}
std::time_t TimeGm(int year, int month, int day, int hour, int minute) {
  std::tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
#ifdef _WIN32
  return _mkgmtime(&t);
#else
  return timegm(&t);
#endif
}
// Seconds for a GRIB1 time unit code (table 4).
long UnitSeconds(int unit) {
  switch (unit) {
    case 0: return 60;
    case 1: return 3600;
    case 2: return 86400;
    case 10: return 3 * 3600;
    case 11: return 6 * 3600;
    case 12: return 12 * 3600;
    case 13: return 15 * 60;
    case 14: return 30 * 60;
    case 254: return 1;
    default: return -1;
  }
}
struct Field {
  int param = 0;
  std::time_t valid = 0;
  CurrentGribFile::Grid grid;  // values in grid.u
};
}  // namespace

bool CurrentGribFile::Parse(const std::vector<std::uint8_t>& b,
                            std::string* error) {
  auto fail = [error](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  std::vector<Field> fields;
  std::size_t i = 0;
  while (i + 8 <= b.size()) {
    if (b[i] != 'G' || b[i + 1] != 'R' || b[i + 2] != 'I' || b[i + 3] != 'B') {
      ++i;  // tolerate padding between messages
      continue;
    }
    const std::size_t total = U(&b[i + 4], 3);
    const int edition = b[i + 7];
    if (edition != 1) return fail("only GRIB edition 1 current files are supported");
    if (total < 12 || i + total > b.size()) return fail("truncated GRIB message");
    const std::uint8_t* pds = &b[i + 8];
    const std::size_t pl = U(pds, 3);
    if (pl < 28) return fail("short product definition section");
    const bool hasGds = pds[7] & 0x80, hasBms = pds[7] & 0x40;
    const int param = pds[8];
    const std::uint8_t* p = pds + pl;
    if (param != 49 && param != 50) {  // not a current component: skip
      i += total;
      continue;
    }
    if (!hasGds) return fail("current message without a grid section");
    const std::uint8_t* gds = p;
    const std::size_t gl = U(gds, 3);
    if (gds[5] != 0) return fail("current grid is not a regular lat/lon grid");
    p += gl;
    const std::uint8_t* bms = nullptr;
    if (hasBms) {
      bms = p;
      p += U(bms, 3);
    }
    const std::uint8_t* bds = p;
    if (bds[3] & 0xF0 & 0x80) return fail("spherical harmonic data not supported");

    Field f;
    f.param = param;
    const int century = pds[24], yy = pds[12];
    const int year = (century - 1) * 100 + yy;
    const long unit = UnitSeconds(pds[17]);
    if (unit < 0) return fail("unsupported GRIB time unit");
    long offset = 0;
    switch (pds[20]) {  // time range indicator
      case 0: case 1: offset = pds[18]; break;
      case 10: offset = static_cast<long>(U(&pds[18], 2)); break;
      default: offset = pds[19]; break;  // averages etc.: end of period
    }
    f.valid = TimeGm(year, pds[13], pds[14], pds[15], pds[16]) + offset * unit;

    Grid& g = f.grid;
    g.ni = static_cast<int>(U(&gds[6], 2));
    g.nj = static_cast<int>(U(&gds[8], 2));
    const double la1 = S(&gds[10], 3) / 1000.0, lo1 = S(&gds[13], 3) / 1000.0;
    const double la2 = S(&gds[17], 3) / 1000.0, lo2 = S(&gds[20], 3) / 1000.0;
    const int scan = gds[27];
    const bool iNeg = scan & 0x80, jPos = scan & 0x40, jFast = scan & 0x20;
    if (jFast) return fail("column-major GRIB scanning not supported");
    if (g.ni < 2 || g.nj < 2) return fail("current grid too small");
    double dlon = std::fabs(lo2 - lo1) / (g.ni - 1);
    if (lo2 < lo1 && !iNeg) dlon = (lo2 + 360.0 - lo1) / (g.ni - 1);
    g.lat_first = la1;
    g.lon_first = lo1;
    g.dlat = (jPos ? 1 : -1) * std::fabs(la2 - la1) / (g.nj - 1);
    g.dlon = (iNeg ? -1 : 1) * dlon;

    const int D = static_cast<int>(S(&pds[26], 2));
    const int E = static_cast<int>(S(&bds[4], 2));
    const double R = Ibm(&bds[6]);
    const int nb = bds[10];
    const double scaleE = std::pow(2.0, E), scaleD = std::pow(10.0, -D);
    const std::size_t n = static_cast<std::size_t>(g.ni) * g.nj;
    g.u.assign(n, NAN);
    const std::uint8_t* data = bds + 11;
    const std::size_t dataBytes = U(bds, 3) - 11;
    std::size_t bit = 0, packed = 0;
    for (std::size_t k = 0; k < n; ++k) {
      if (bms && !(bms[6 + k / 8] & (0x80 >> (k % 8)))) continue;
      std::uint32_t x = 0;
      for (int q = 0; q < nb; ++q, ++bit) {
        const std::size_t byte = bit / 8;
        if (byte >= dataBytes) return fail("packed data shorter than the grid");
        x = (x << 1) | ((data[byte] >> (7 - bit % 8)) & 1);
      }
      g.u[k] = static_cast<float>((R + x * scaleE) * scaleD);
      ++packed;
    }
    fields.push_back(std::move(f));
    i += total;
  }

  // Pair U and V with the same valid time and grid.
  grids_.clear();
  for (const Field& fu : fields) {
    if (fu.param != 49) continue;
    for (const Field& fv : fields) {
      if (fv.param != 50 || fv.valid != fu.valid ||
          fv.grid.ni != fu.grid.ni || fv.grid.nj != fu.grid.nj)
        continue;
      Grid g = fu.grid;
      g.valid = fu.valid;
      g.v = fv.grid.u;
      grids_.push_back(std::move(g));
      break;
    }
  }
  std::sort(grids_.begin(), grids_.end(),
            [](const Grid& a, const Grid& c) { return a.valid < c.valid; });
  if (grids_.empty()) return fail("no current (UOGRD/VOGRD) fields found");
  return true;
}

bool CurrentGribFile::Load(const std::string& path, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + path;
    return false;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  return Parse(bytes, error);
}

std::time_t CurrentGribFile::FirstTime() const {
  return grids_.empty() ? 0 : grids_.front().valid;
}
std::time_t CurrentGribFile::LastTime() const {
  return grids_.empty() ? 0 : grids_.back().valid;
}

bool CurrentGribFile::SampleGrid(const Grid& g, double lat, double lon,
                                 double* u, double* v) const {
  double dl = lon - g.lon_first;
  while (dl > 180) dl -= 360;
  while (dl < -180) dl += 360;
  const double fi = dl / g.dlon, fj = (lat - g.lat_first) / g.dlat;
  if (fi < -1e-9 || fj < -1e-9 || fi > g.ni - 1 + 1e-9 || fj > g.nj - 1 + 1e-9)
    return false;
  const int i0 = std::min(static_cast<int>(std::floor(fi)), g.ni - 2);
  const int j0 = std::min(static_cast<int>(std::floor(fj)), g.nj - 2);
  const double ti = std::clamp(fi - i0, 0.0, 1.0), tj = std::clamp(fj - j0, 0.0, 1.0);
  auto at = [&g](const std::vector<float>& a, int i, int j) {
    return static_cast<double>(a[static_cast<std::size_t>(j) * g.ni + i]);
  };
  double su = 0, sv = 0, w = 0;
  const double wts[4] = {(1 - ti) * (1 - tj), ti * (1 - tj), (1 - ti) * tj,
                         ti * tj};
  const int ii[4] = {i0, i0 + 1, i0, i0 + 1}, jj[4] = {j0, j0, j0 + 1, j0 + 1};
  for (int k = 0; k < 4; ++k) {
    const double a = at(g.u, ii[k], jj[k]), b = at(g.v, ii[k], jj[k]);
    if (!std::isfinite(a) || !std::isfinite(b) || wts[k] <= 0) continue;
    su += wts[k] * a;
    sv += wts[k] * b;
    w += wts[k];
  }
  if (w < 0.5) return false;  // mostly masked out
  *u = su / w;
  *v = sv / w;
  return true;
}

bool CurrentGribFile::Sample(double lat, double lon, std::time_t time,
                             double* u, double* v) const {
  if (grids_.empty() || time < grids_.front().valid ||
      time > grids_.back().valid)
    return false;
  std::size_t k = 0;
  while (k + 1 < grids_.size() && grids_[k + 1].valid <= time) ++k;
  double u0, v0;
  if (!SampleGrid(grids_[k], lat, lon, &u0, &v0)) return false;
  if (grids_[k].valid == time || k + 1 >= grids_.size()) {
    *u = u0;
    *v = v0;
    return true;
  }
  double u1, v1;
  if (!SampleGrid(grids_[k + 1], lat, lon, &u1, &v1)) return false;
  const double t = static_cast<double>(time - grids_[k].valid) /
                   static_cast<double>(grids_[k + 1].valid - grids_[k].valid);
  *u = u0 + (u1 - u0) * t;
  *v = v0 + (v1 - v0) * t;
  return true;
}

std::shared_ptr<const CurrentGribFile> CurrentGribFile::Cached(
    const std::string& path, std::string* error) {
  static std::mutex mutex;
  static std::map<std::string, std::pair<std::time_t,
                                         std::shared_ptr<const CurrentGribFile>>>
      cache;
  struct stat st {};
  if (path.empty() || stat(path.c_str(), &st) != 0) {
    if (error) *error = "current file not found: " + path;
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(path);
  if (it != cache.end() && it->second.first == st.st_mtime)
    return it->second.second;
  auto file = std::make_shared<CurrentGribFile>();
  if (!file->Load(path, error)) {
    cache.erase(path);
    return nullptr;
  }
  cache[path] = {st.st_mtime, file};
  return file;
}

}  // namespace weather_routing
