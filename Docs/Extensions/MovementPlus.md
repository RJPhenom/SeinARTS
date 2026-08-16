# Movement+ (SeinARTS Movement+ Extension)

Movement+ is an opt-in runtime plugin that adds five concrete movement modes — Infantry, Wheeled
Vehicle, Tracked Vehicle, Hover (Helicopter), and Flight (Fixed-Wing) — plus the per-class tuning
data each needs. The base framework already ships two working defaults (Basic and Basic Unit), so
nothing breaks without this extension; you just lose the specialized modes, vehicle steering,
hover/flight altitude handling, and animation telemetry below.

## Assigning a movement mode

1. Open your unit Blueprint's entity bridge and add/select the **Movement Component** entry in
   **Component Data**.
2. Set **Movement Class** to one of the five Movement+ modes: `Infantry`, `Wheeled Vehicle`,
   `Tracked Vehicle`, `Hover (Helicopter)`, or `Flight (Fixed-Wing)`.
3. The details panel auto-swaps in a matching **Movement Class Data** sub-struct the moment you
   pick a class — you never author this by hand.
4. Top Speed, Turn Rate, and reverse authoring (**Can Reverse**, **Reverse Top Speed**, the two
   engage-threshold fields) stay on the base Movement Component regardless of class.

An invalid or missing Movement Class (e.g. Movement+ not installed on a peer) falls back to the
framework's Basic movement rather than erroring — see the lockstep note at the end.

### Subclassing for custom feel

Every mode class is a normal `UCLASS` you can subclass in Blueprint; the shared harness calls an
explicit Blueprint-overridable **BP Tick** entry each simulation step, so you can layer per-unit
logic on top of a mode's C++ policy without forking the class. The Movement Class picker filters on
the framework's base movement class, so your subclass stays discoverable automatically.

## Mode by mode

**Infantry** — the lightweight, momentum-aware baseline: foot units ramp speed up/down smoothly
(rather than snapping) and turn to face their travel direction. Tuning: **Acceleration** /
**Deceleration** (world units/sec²) — set both very high for a near-instant, non-snappy response.

**Wheeled Vehicle** — bicycle-kinematics steering (jeeps, armored cars, half-tracks). **Wheelbase**
and **Max Steer Angle** set the honest minimum turn radius; **Steer Response** controls how
snappily the wheel angle chases its target. **Low Speed Turn Rate** / **Turn Assist Fade Speed**
give a near-stationary vehicle extra turn rate above the honest bicycle model (fading out by cruise
speed) so it doesn't crawl through wide from-rest turns; set Low Speed Turn Rate to 0 for a strict
bicycle model. **Turn Speed Floor** and **Sharp Turn Brake Angle/Strength** slow the vehicle into
sharp corners. Gotcha: the unit-level **Turn Rate** on the base Movement Component is a *third*
turning governor — at low values every planned turn collapses to minimum radius and the
open-ground "wide swoop vs. tight braked corner" feel never shows up. Author vehicle Turn Rate as
roughly `TopSpeed / desired cruise arc radius`.

**Tracked Vehicle** — a two-mode controller split by speed. Above **Pivot Speed** it drives arcs
like a wheeled vehicle; at or below it, if misaligned past **Pivot Align Dot** it pivots in place
at zero throttle until aligned, then drives forward. **Turn Acceleration** adds optional
rotational inertia to the hull's yaw rate (0 = instant, the pre-inertia default). **Min Turn
Radius** stays 0 for a chassis that always pivots at sharp corners; setting it non-zero declares
the chassis non-pivoting, and the maneuver planner switches to the full wheeled-style word ladder
at that radius instead. Known exception: idle settle-facing still rotates a parked chassis in
place regardless (a base ground-mode behavior — turn off **Settle To Formation Facing** under
Project Settings → SeinARTS if unwanted).

**Hover (Helicopter)** — "flying tank" semantics: can stop and hover, pivots to face its target
before driving, uses full Turn Rate at any speed, and bypasses ground pathfinding (flies straight
through static obstacles). **Cruise Altitude** / **Altitude Clearance Threshold** set the target
and floor height above the cell surface (whatever occupies it — roof, wall, hill); **Altitude
Change Rate** is climb/descent speed; **Altitude** is the live runtime value that lerps toward
cruise and persists across orders.

**Flight (Fixed-Wing)** — can't stop or stall; **Min Speed Ratio** floors forward speed as a
fraction of Top Speed (default 0.6), and the unit drifts past its destination rather than stopping
on arrival — loiter/idle behavior is your AI controller's job, out of scope here. **Wheelbase** /
**Max Steer Angle** loosely model turning circle the same way Wheeled's bicycle model does; **Steer
Response** controls bank-angle interpolation speed. Altitude fields work exactly like Hover's.
Also bypasses ground pathfinding.

## Vehicle maneuvers: bounded start maneuvers, not a route solver

Wheeled and Tracked post-process only the *start* of a route with a small, curated, deterministic
candidate set — a departure arc, straight reverse, K-turn/multi-point turn, or reverse-out of a
tight corridor — driven as typed Arc/Straight path segments. It engages only when the chassis is
badly misaligned with the route ahead; after that head, the vehicle follows the rest of the
ordinary A*-produced straight polyline with runtime steering (curvature feed-forward on arcs,
pure-pursuit carrot on straights).

This is **not** a general Reeds-Shepp/Dubins route solver and does not re-curve the whole path —
it exists solely to make a vehicle's initial reorientation look plausible. Don't expect curved
routes deep into a long path.

- **Maneuver Planning** (per mode) toggles this off entirely, falling back to plain pursuit
  steering.
- **Forward Path Bias** prefers a forward-only plan; a reversing plan only wins when the forward
  route is more than this factor longer (default 1.35).
- **Reverse Plan Max Distance** caps how far the planner backs up hunting for turning room before
  giving up and falling back to pivot-assisted pursuit.

### Reverse is opt-in

Reverse capability is authored on the **base** Movement Component (**Can Reverse**, **Reverse Top
Speed**, two engage-threshold fields) and defaults **off**. Wheeled and Tracked each carry their
own **Can Reverse** flag in mode sub-data, defaulting **on**, OR-combined with the base flag — that
is why wheeled/tracked units reverse out of the box even with the base flag off. Untick both to
forbid reverse on a specific vehicle. Reverse speed always comes from the base component's Reverse
Top Speed.

## Animation and telemetry

Wheeled and Tracked publish typed, render-only telemetry through the **SeinARTS Movement+
Library** function library for your AnimBP: **Get Movement+ Presentation State** returns steering
angle, yaw rate, normalized throttle/brake, wrapped wheel rotation, and left/right track velocity
for an entity (given the mesh's wheel radius and track half-width); **Get Movement+ Telemetry
Value** reads a single channel. Telemetry samples *settled*, post-collision transforms and the
driver's real velocity output, so a collision correction can't masquerade as input. It lives in a
Transient render-state slot, resets on spawn/restore/class change, and is explicitly blocked from
Movement/Ability Blueprint graphs — presentation-only by construction, never a feedback path into
simulation.

## Limitations

- **Flight has no full 3D avoidance/collision model.** Hover and Flight bypass ground pathfinding
  (straight through static obstacles) and there is no shipped air-to-air avoidance; bank
  application and loiter are still open areas. Treat flight as "gets from A to B, avoids the
  ground only," not an air-combat maneuvering system.
- **Collision narrowphase is 2D, yaw-only, by design** — an intentional scope boundary, not a gap.
- **The maneuver planner is a bounded start-maneuver stage, not a route solver** (see above); the
  coarse tail is always straight-segment A* with runtime steering.
- Hover strafe and flight idle/loiter semantics are still active tuning areas.

## Settings and lockstep determinism

Movement+ has no separate project-settings page — every tunable lives on the per-unit Movement
Component and its mode sub-data, ordinary `SeinDeterministic` authored data that already
participates in canonical simulation state. There is nothing extra to register with the project's
lockstep config-fingerprint system for Movement+ tuning itself.

One thing to be deliberate about: the Movement Class picker resolves a soft class path at runtime.
If Movement+ is installed on one machine in a match but not another, the peer missing it silently
falls back to Basic movement for every unit that should use a Movement+ mode — a safety net, not a
hard failure, but it means simulations will diverge unless every player has the same extensions
enabled. Treat installed extensions as part of your build/version parity checklist.
