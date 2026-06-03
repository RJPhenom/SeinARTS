# Steering and Path-Planning Sources

References used during the wheeled vehicle movement redesign. Organized by topic.

## Reeds-Shepp curve fitting

The Reeds-Shepp word enumeration replaces the legacy Dubins fitter. Forward+reverse motion primitives, 48 canonical word patterns, closed-form analytical solution per word.

- **Reeds, J. A., and Shepp, L. A.** (1990). *Optimal paths for a car that goes both forwards and backwards.* Pacific Journal of Mathematics, 145(2), 367-393. The original paper. Enumerates the 48 canonical word patterns; proves shortest-path optimality among smooth curves with bounded turn radius.

- **OMPL `ReedsSheppStateSpace.cpp`** (BSD license). The canonical open-source C++ reference implementation. We ported the per-pattern formulas and the four-symmetry framework (identity, timeflip, reflect, timeflip+reflect) from this code. Math is float in OMPL; we adapted to `FFixedPoint` for sim-determinism.
  - <https://github.com/ompl/ompl/blob/main/src/ompl/base/spaces/src/ReedsSheppStateSpace.cpp>

- **Hybrid A* / Reeds-Shepp reference** (informational only — we use 2D A* + curve-fit post-process, not Hybrid A*).
  - <https://github.com/RajPShinde/Hybrid-A-Star>
  - <https://blog.habrador.com/2015/11/explaining-hybrid-star-pathfinding.html>
  - **Improved Analytic Expansions in Hybrid A-Star Path Planning for Non-Holonomic Robots** (MDPI 2022). <https://www.mdpi.com/2076-3417/12/12/5999>

## Stanley cross-track-error control

Used for Straight segment tracking in `TickSegmentAware`. Combines heading alignment + cross-track correction in one term.

- **Hoffmann, G. M., Tomlin, C. J., Montemerlo, M., and Thrun, S.** (2007). *Autonomous automobile trajectory tracking for off-road driving: Controller design, experimental validation and racing.* American Control Conference. The Stanley paper — controller used on the Stanford DARPA Grand Challenge winning entry.
  - Canonical formula: `δ = ψ + atan2(k × e, v)` where ψ is heading error to path tangent, e is cross-track error, v is speed.

## Company of Heroes pathfinding (genre reference)

CoH is the gameplay reference for vehicle movement feel. CoH uses 2D A* on a clearance-annotated grid + a "turn plan" post-process — structurally what we have now. Verified across two primary sources.

- **Chris Jurney (Relic Entertainment)** — GDC 2007. *Dealing with Destruction: AI from the Trenches of Company of Heroes.* Described CoH's pathfinding system: variant of the Brushfire algorithm for variable-sized-agent clearance, A* on the annotated grid. The clearance approach is structurally what our `WallDistance` field + `AgentFootprintRadius` cost-shaping does.
  - <http://gdc.chrisjurney.com/> (slide hosting page)
  - <http://aigamedev.com/premium/presentations/dealing-with-destruction/> (presentation summary)
  - **Jurney, C.** *Company of Heroes Squad Formations Explained.* AI Game Programming Wisdom 4, 2008. Companion article on squad-formation movement.

- **Relic Entertainment dev writeups** — CoH3 path improvements. Explicit on the architecture: A* finds cells, "turn plans" make them look like vehicles. Forward moves prefer smooth curves with small direction deviations; reverse moves prefer shortest. The dev quote driving the forward-preference bias in our Reeds-Shepp selector:
  > "Important to note here is that there is a distinction between the pathfinding algorithm and turn plans. CoH uses an A* (A-star) algorithm to find the shortest path. A* tells the game which grid squares vehicles should follow to reach their destination. Turn plans are how the vehicle 'realistically' interprets the movement to those squares, making the tanks seem more authentic."
  >
  > "When issuing forward movements, we want vehicles to follow a path with small direction deviations (smooth curve), so they do not lose speed. For reverse movements our path searching algorithm will try to stick to the shortest path when possible."

  - **Coral Viper (1.6.0) — Pathfinding Improvements** (Relic Help, requires browser; 403 to curl). <https://help.relic.com/hc/en-us/articles/39572638372755--PC-Coral-Viper-1-6-0-Pathfinding-Improvements>
  - **Year-1 Anniversary (1.5.0) Patch Notes — companyofheroes.com.** <https://www.companyofheroes.com/en/post/pc-year-1-anniversary-150-patch-notes>

- **Coral Viper pathfinding terminology referenced**:
  - "Turn plan" — vehicle's realistic interpretation of cell-path; what we call curve fit
  - "Tightening" / "widening" — post-process knobs
  - "Donut" — bug where vehicles loop around their target instead of approaching
  - "Casemate" — turretless tank-destroyer vehicles (StuG, Jagdpanzer) that must face their target before firing

## Clearance-based pathfinding

The clearance grid (per-cell distance to nearest blocked cell) supports variable-sized agents. CoH uses a Brushfire variant; we use multi-source BFS at bake time.

- **Harabor, D., Botea, A.** (2008). *Hierarchical Annotated A\* Search.* AAAI / AIIDE. Academic comparison of clearance-based + hierarchical approaches; references the CoH approach explicitly.
  - <https://harabor.net/data/papers/harabor-aigamedev09.pdf>

- **Harablog — Clearance-based Pathfinding** (blog summary).
  - <https://harablog.wordpress.com/2009/01/29/clearance-based-pathfinding/>

## Bicycle kinematic model

The wheeled chassis uses the classical 2D bicycle model: `ω = (v / L) × tan(δ)` where L is wheelbase, δ is steer angle, ω is yaw rate, v is signed forward speed.

- Standard robotics / autonomous-vehicle textbook material. Min turn radius from the model: `R_min = L / tan(δ_max)`.
- For arc-railing feedforward steer: `δ_feedforward = atan(L / R)` — the steer angle that makes `ω = v/R` (correct rate for arc of radius R at speed v).

## Group / formation movement (informational)

Tangential to vehicle steering but useful context.

- **Group Pathfinding & Movement in RTS Style Games** — gamedeveloper.com article, with CoH referenced.
  - <https://www.gamedeveloper.com/programming/group-pathfinding-movement-in-rts-style-games>

- **Group Movement research** (Aitor Simona).
  - <https://aitorsimona.github.io/Research_GroupMovement/>

## Why Hybrid A* is NOT used

Hybrid A* (3D state space — x, y, θ — with motion primitives) is the rigorous robotics answer. We considered it but rejected because:
1. CoH itself does NOT use Hybrid A*. It uses 2D A* + turn plans. We're matching CoH-quality, not robotics-grade.
2. RTS-scale paths don't justify the complexity. The 2D A* + Reeds-Shepp post-process covers the same scenarios for our target game feel.
3. Computational cost is higher (3D state-space search × motion primitives × analytic expansion).

The decision is documented in the project memory `project_wheeled_redesign.md`.

## Implementation cross-references in this repo

- **Reeds-Shepp solver**: `Source/SeinARTSNavigation/Private/Default/SeinReedsShepp.h/.cpp`. 8 base patterns × 4 symmetries = 32 canonical words.
- **World-space emission**: `Source/SeinARTSNavigation/Private/SeinNavigationAStar.cpp`, `EmitReedsSheppPath` (anonymous namespace) and `BuildReedsSheppTrajectory`.
- **Clearance grid**: `Source/SeinARTSNavigation/Private/SeinNavigationAStar.cpp`, `WallDistance` field + `RebuildWallDistanceField`.
- **Stanley XTE on Straights**: `Source/SeinARTSMovement/Private/Movement/SeinWheeledVehicleMovement.cpp`, `Tick` Straight branch.
- **Stanley-on-circle for Arcs**: same file, Arc branch.
- **Bicycle kinematics**: same file, shared kinematics block after segment branches.
