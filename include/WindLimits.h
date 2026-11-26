// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wind limits applied to sailing (not motoring): a separate, lower maximum
// when sailing upwind (true wind angle under 90 degrees), and a minimum wind
// below which the boat does not sail. 0 turns a limit off. The overall
// maximum true wind (MaxTrueWindKnots) is applied elsewhere, at every angle.
#pragma once

#include <cmath>

namespace weather_routing {

inline bool SailingAllowedByWindLimits(double tws_knots, double twa_degrees,
                                       double max_upwind_knots,
                                       double min_knots) {
  if (!std::isfinite(tws_knots)) return true;  // data checks live elsewhere
  if (min_knots > 0 && tws_knots < min_knots) return false;
  const double twa = std::fabs(twa_degrees);
  if (max_upwind_knots > 0 && twa < 90.0 && tws_knots > max_upwind_knots)
    return false;
  return true;
}

}  // namespace weather_routing
