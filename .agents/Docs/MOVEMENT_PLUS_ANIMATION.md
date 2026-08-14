# Animating Movement+ Vehicles

**Document version:** 0.1

Movement+ exposes typed, presentation-only telemetry for Wheeled and Tracked movement. The values
describe the latest settled vehicle motion and are safe to use in Animation Blueprints, Control
Rig, material animation, and visual or audio effects. They are transient render data, not replicated
authority or simulation input.

## 1. Read the typed state

1. Assign the vehicle's Animation Blueprint to its skeletal mesh.
2. In **Event Blueprint Update Animation**, use **Get Owning Actor**, cast to **Sein Actor**, and
   call **Get Entity Handle**.
3. Call **Get Movement+ Presentation State** once and cache the fields needed by the animation
   graph. Use **Get Movement+ Telemetry Value** only when a graph needs one channel.
4. Set **Wheel Radius Cm** to the rendered wheel radius. Set **Track Half Width Cm** to the distance
   from the vehicle centerline to either track. These measurements affect visuals only.

The world context is supplied automatically by the Animation Blueprint. The getter returns zeros
when the world, entity, or movement component is unavailable. Motion-derived fields remain zero
until settled sample history exists; Wheeled steering may already be available.

## 2. Interpret each channel

| Channel | Unit and range | Contract |
|---|---|---|
| Steering Angle | radians | Wheeled steering angle. Positive turns local +X toward +Y. Tracked movement reports zero. |
| Yaw Rate | radians/second | Settled chassis yaw. Positive rotates local +X toward +Y. |
| Normalized Throttle | `0..1` | Positive movement-driver speed change divided by the authored acceleration limit. Collision correction does not create throttle. |
| Normalized Brake | `0..1` | Negative movement-driver speed change divided by the authored deceleration limit. Collision correction does not create braking. |
| Wheel Rotation | radians in `[0, 2*pi)` | Wrapped phase derived from settled accumulated signed travel divided by Wheel Radius Cm. Forward travel advances phase; reverse travel reduces it through the wrap. |
| Left Track Velocity | cm/second | Settled signed forward speed plus yaw rate times Track Half Width Cm. Positive is forward. |
| Right Track Velocity | cm/second | Settled signed forward speed minus yaw rate times Track Half Width Cm. Positive is forward. |

For a positive yaw rate, the left-track value is greater than the right-track value. During a
stationary pivot, the two values can have opposite signs. Convert radians to degrees only where a
bone or control expects degrees; keep the cached values in their documented units.

## 3. Drive presentation

- Apply **Steering Angle** to the front steering bones or steering linkage.
- Apply **Wheel Rotation** as a wrapped phase. Use wrap-aware interpolation rather than blending
  raw values across the `2*pi` to zero boundary.
- Use signed **Left Track Velocity** and **Right Track Velocity** to set track direction and
  playback speed independently.
- Use **Normalized Throttle** and **Normalized Brake** for engine load, suspension response,
  exhaust, or material effects. They describe driver output, not displacement caused by collision
  resolution or containment.

Do not feed these values into movement, abilities, command admission, or any other deterministic
graph. The editor rejects the presentation getters in deterministic Movement and Ability
Blueprints.

## 4. Handle lifecycle boundaries

Motion-derived values need two settled samples. On the first sample, they are zero; Wheeled may
already report its current steering angle. Spawn, snapshot restore, movement-class replacement,
and movement-instance loss clear stale telemetry. Treat zero as the neutral animation state across
those transitions.

A non-positive, non-finite, or effectively zero **Wheel Radius Cm** produces zero wheel rotation.
A non-positive, non-finite, or effectively zero **Track Half Width Cm** produces zero left/right
track velocity. Correct the visual dimensions rather than compensating elsewhere in the graph.

## 5. Qualify the animation in PIE

1. Accelerate forward from rest. Throttle should rise within `0..1`, brake should remain zero, and
   wheel phase should advance in the mesh's intended direction.
2. Brake to a stop, then reverse. Brake should respond during deceleration; signed wheel/track
   motion should reverse without a discontinuity beyond the expected phase wrap.
3. Turn in both directions. Steering and yaw signs should follow the vehicle's local +X/+Y axes.
4. Pivot a Tracked vehicle. The two track velocities should separate and may take opposite signs;
   positive yaw must make the left value greater than the right.
5. Push the vehicle against an obstacle or observe a collision correction. Correction displacement
   must not appear as throttle or brake input.
6. Exercise spawn, restore/resync, and movement-class replacement. No stale wheel, track, throttle,
   or brake value should survive the transition.
7. Repeat in two-player PIE and confirm the animation remains readable under the project's normal
   interpolation and network conditions. PIE remains the final animation-mapping oracle.
