# Current file (tidal streams without merging GRIBs)

A route can read its current from its own GRIB1 file, for example the tidal
stream forecast written by `mumbaitide` (`python -m mumbaitide.gribcurrent`),
while the wind comes from the GRIB loaded in the GRIB plugin as usual. There is
no need to merge the two files.

## Use

1. Load the wind GRIB in the GRIB plugin as usual.
2. In Weather Routing, open the route's settings (double-click it).
3. Under **Weather**, tick **Tidal current**, then choose the **Current file**.
   The line under it shows the times and area the file covers, or why it
   cannot be read.
4. Compute.

For a passage, set it on any leg: passage settings apply to every leg. Plan
Passage copies it from the selected route, and new routes remember the last
one used.

## What it does

- Where the file covers the position and time, its current is used, ahead of
  any current in the GRIB plugin's file and of climatology.
- Outside the file's area or time span, current comes from the GRIB plugin or
  climatology as before. Make the file cover the whole race window.
- Values are interpolated bilinearly in space and linearly in time; nothing is
  extrapolated.
- It applies in every engine (they share one current source), and Results >
  Details names the file used.

## File format

GRIB edition 1, regular lat/lon grid, parameters 49 (UOGRD) and 50 (VOGRD) in
m/s, one U and V pair per time. Simple packing with any bits per value, decimal
scale, scan direction and an optional bitmap are supported. GRIB2 current files
are not read yet; load those in the GRIB plugin instead.

## Limits

- The GRIB plugin does not show this file's current on the chart unless you
  also open the file there; the routing uses it either way.
- Tested on the `mumbaitide` output (`testdata/currents/`); other producers'
  GRIB1 current files should work but have not been tried.
