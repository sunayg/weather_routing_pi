# Mark rules (keep-out radius, leave to port or starboard)

Status: experimental. The router checks the rules. The easy way to set them up
is **Plan Passage** (below). Rules can also be written by hand into the saved
Weather Routing XML.

## Plan Passage (dialog)

Right-click an OpenCPN route (start, via marks, finish) and choose **Plan
Passage...**. One compact dialog replaces one configuration per leg:

- **Departure**: date and time (local). The first leg starts then; every later
  leg starts when the previous one arrives.
- **Marks, in order**: for each intermediate mark choose *Pass through*, *Leave
  to port* or *Leave to starboard*, and an optional clearance in nautical miles.
- **Keep clear of**: any OpenCPN waypoint, with a minimum distance. It applies
  to every leg.
- **Create legs** only creates the legs. **Create & compute** also routes them
  one after the other.

Boat, wind, current and the other settings come from the route currently
selected in Weather Routing (or the defaults). Set them there first.

How it works: for a mark with a side, the leg before it ends and the leg after
it starts at a *passing point* beside the mark, halfway round the turn on the
side you asked for (beside the mark on a straight pass, just beyond it at a
windward mark). A leg that ended exactly on the mark would break its own rule.
Each of the two legs checks the side by its own direction: the leg in by its
incoming course, the leg out by its outgoing course. Together they force the
rounding the right way round, including out-and-back courses and leeward marks
a little either side of the start. Barriers are twice the longer of the two
legs, at most 20 nm. "Pass through" sails to the mark itself and has no
clearance. Each leg carries only the rules for the marks at its own two ends
plus the keep-clear marks; a keep-clear mark sitting on a leg's start or
finish is dropped for that leg.

A rule is attached to a route configuration. Any routing step that breaks a
rule is rejected, in every engine that uses the shared constraint checks
(`ConstraintChecker`). A rejected step shows up as a boundary rejection in the
diagnostics.

## Writing a rule

Add one `MarkRule` child per mark inside the `Configuration` element:

```xml
<Configuration ... >
  <MarkRule Lat="18.88" Lon="72.85" ClearanceNm="0.3"
            Side="LeaveToPort" TravelBearing="155" BarrierLengthNm="20" />
</Configuration>
```

| Attribute | Meaning |
| --- | --- |
| `Lat`, `Lon` | The mark, in decimal degrees. Required. |
| `ClearanceNm` | Keep-out radius in nautical miles. `0` (default) means none. |
| `Side` | `Any` (default), `LeaveToPort` or `LeaveToStarboard`. The mark is on that side of the boat as it passes. |
| `TravelBearing` | True bearing the boat is travelling as it passes the mark, for example the bearing from the previous waypoint to the next one. Used to put the barrier on the correct side. |
| `BarrierBearing` | Alternative to `TravelBearing`: the true bearing of the barrier from the mark. Wins if both are given. The plugin writes this when it saves. |
| `BarrierLengthNm` | How far the barrier runs from the mark. Default 20. |

Edit the file with OpenCPN closed. The plugin rewrites it when it saves and
keeps the rules.

## How it works

- Keep-out: a step is rejected if it comes closer to the mark than
  `ClearanceNm`. A step that starts inside the radius and only moves away is
  allowed, so a boat that begins inside the radius can get out.
- Side: the router cannot track which side of a mark a path went round, so a
  barrier stands in for it. The barrier is a straight line from the mark, on
  the side the boat must not pass. A step that crosses or touches it is
  rejected, so a route can only get past the mark on the wrong side by going
  round the barrier's far end. Make `BarrierLengthNm` long enough to reach land
  or water the boat will not use.
- Steps are tested in a flat plane around the mark. That is accurate to well
  under a metre at race-course scale, not for legs of hundreds of miles.
- Like the boundary test, the part of a step beyond the destination is ignored.

## Limits

- Rules made by the passage dialog are per leg. Editing shared settings of a
  generated group keeps each leg's rules (they are not copied between legs).
  Rules written by hand into a multi-leg group's XML are per leg too.
- The side rule needs a start/finish route of at least two marks; the dialog
  uses the bearing from the previous mark to the next one as the direction of
  travel at each via mark.
- Passing points accumulate in the Positions list; delete old ones there.
- Geometry and leg planning are unit-tested (`test/MarkRules_tests.cpp`).
  A single mark with each rule was checked in OpenCPN. The dialog and
  multi-mark passages have been built but not yet run in OpenCPN.
