# Authoring Units

A unit type is a Blueprint subclass of `SeinActor`. Its Entity Bridge component carries the
Component Data array that defines the unit's simulation identity; everything else on the actor is
presentation.

## The authoring loop

1. **Create** — Content Browser → right-click → SeinARTS → Unit.
2. **Define** — add simulation components to the bridge's Component Data:
   - `Identity Component` — name, icon, portrait, identity tag (auto-generated from the asset
     name; rename-safe).
   - `Movement Component` — top speed, turn rate, reverse authoring, avoidance weight, and the
     **Movement Class** picker. The framework ships Basic and Basic Unit; the Movement+
     extension adds Infantry, Wheeled, Tracked, Hover, and Flight, each with a per-class tuning
     struct that appears automatically when you pick the class.
   - `Navigation Component` — footprint fallback radius, arrival acceptance, repath policy,
     nav-layer mask, forbidden terrain tags.
   - `Extents Component` — collision shapes (also drive nav baking, fog blocking, and marquee
     selection silhouettes via authored flags).
   - `Ability Component` — granted abilities and the default-command table that maps right-click
     contexts (ground, enemy, friendly) to abilities per unit.
   - `Vision Component` — fog-of-war stamps (radial, rectangular, or conical, per vision layer).
3. **Present** — meshes, animation, and effects as ordinary Unreal work. Subscribe to the
   bridge's visual events (spawn, death, damage, squad changes) from your render components.
   Movement+ publishes typed render-only telemetry (steer angle, throttle/brake, wheel phase,
   track velocities) for animation Blueprints.

## Abilities

Everything a unit does is an ability: Move, attack, produce, garrison, reinforce, set rally.
Create one with right-click → SeinARTS → Ability.

- Abilities are Blueprints with latent execution (the async Move To node drives movement and
  survives save/restore and reconnection).
- Activation gates — cooldown, tag requirements, cost — are declared on the ability and enforced
  identically by the simulation and the UI (buttons grey out for the same reason activation
  would fail).
- Production is an ability that enqueues a producible; the producible's class defaults carry
  build time and refund policy.
- Targeted abilities declare a targeter spec (point, or point-with-facing for building
  placement); the player controller runs the targeting UI and submits the captured points with
  the command.

## Custom simulation data

Right-click → SeinARTS → Component creates a designer struct the Component Data picker accepts.
The struct editor enforces determinism: non-deterministic field types are removed on save with a
notification. Read and write your struct from ability graphs with the typed Get/Set Component
nodes.

## Squads (extension)

Add a `Squad Component` to a unit Blueprint to make it a squad container: authored slots each
carry a member class, formation offset, and reinforcement cost/build time. The squad actor
tracks its members' centroid (attach banners to it), dispatches orders through slot-aware
formations, and supports paid, cancellable reinforcement out of the box. Members are selected as
their squad; the squad is one element in multi-squad formation gestures.

## Cover (extension)

Give a Blueprint a `Cover Component` to make it a cover provider: a protection area, a quality
tag (Heavy/Light/Negative or your own), directionality, and authored slots (with an editor
generate-and-scatter tool). Units opt into cover-seeking with the `SeinARTS.Cover.UsesCover`
tag. Cover destinations are exact: the slot shown in the order preview is the slot the unit is
delivered to, reservations prevent double-claims, and terrain tags can grant area cover (for
example, roads as negative cover) with no placed providers.
