# Authoring a First Skirmish

**Document version:** 0.1

This is the minimum studio-owned content path for a movable two-player prototype. It establishes
framework wiring, not game balance, combat design, or a production map.

## 1. Create project-owned content

- Keep maps, factions, units, abilities, settings, the simulation-content manifest, and baked level
  data in the consuming project.
- Treat this monorepo's `/Game/SeinARTSExamples` assets as reference material; they are not part of
  release plugin ZIPs and must never become downstream dependencies.
- Framework plugin assets under `/SeinARTSFramework` can seed the input/UI shell, but production
  projects should own their game-specific subclasses and presentation.

## 2. Configure the framework

In **Project Settings > SeinARTS**:

1. Select the shipped or project-specific Level Data, Navigation, Fog of War, command-authority,
   and faction-service classes. Do not leave a required policy at `None`; documented `None` values
   fail closed or disable that service.
2. Define terrain, resources, factions, simulation cadence, lockstep settings, and any additional
   deterministic command schemas used by the game.
3. Assign a project-owned `Simulation Content Manifest` under `/Game/Generated`.
4. Add the skirmish map to `Available Maps`, set its exact player-slot count, and assign the default
   gameplay/menu maps used by the lobby flow.

Every sim-affecting setting is a multiplayer compatibility input. Change it deliberately and
requalify the complete cohort.

## 3. Author one unit

Create a Blueprint subclass of `ASeinActor`. Its `SeinARTS Entity Bridge` is automatic; author the
unit template in the bridge's `Component Data` array. A movable selectable prototype commonly uses:

- `FSeinIdentityComponent` for its stable identity tag and UI metadata;
- `FSeinExtentsComponent` for authoritative body geometry and nav blocking;
- `FSeinNavigationComponent` for clearance, arrival, terrain, layer, and repath policy;
- `FSeinMovementComponent` for speed, turn rate, movement class, and class-specific tuning; and
- `FSeinAbilityComponent` containing the project's movement/command abilities.

Add only the components required by the unit's gameplay. `Component Data` is a spawn template, not
a live mirror of runtime sim state. Use fixed-point fields and command/ability APIs for simulation.
After spawn, the deterministic transform is authoritative; do not drive movement by moving the
render Actor. Animation, materials, and visual effects consume sim output only.

## 4. Author the map

1. Use a project-owned GameMode derived from `ASeinGameMode`, PlayerController derived from
   `ASeinPlayerController`, HUD derived from `ASeinHUD`, and camera/input assets appropriate to the
   project.
2. Place one `ASeinPlayerStart` per participant. Give every playable start a unique `Player Slot`
   greater than zero and assign its faction/team bootstrap values. The lobby slot count must match.
3. Place the initial project-owned unit actors and assign their `Player Slot` ownership, or spawn
   them through the project's deterministic scenario/bootstrap path.
4. Place one `ASeinLevelVolume` over the playable area, configure its navigation/FoW layers, and use
   **Bake Level Data**. Baked output is regenerable and must be rebuilt after relevant map changes.

`TeamID` seeds initial defaults only. Ordered player-pair capability grants are authoritative after
bootstrap; do not encode future diplomacy or shared vision by repeatedly comparing teams.

## 5. Seal and verify

1. Run `Sein.SimulationContent.GenerateManifest` and save the project-owned manifest.
2. Run `Test-SeinARTSInstallation.ps1` and resolve every error.
3. Build Development Editor and Shipping targets.
4. In one-player PIE, verify selection, command admission, path preview, movement, arrival, fog, and
   UI ownership.
5. In two-player PIE, verify unique slot/faction assignment, command ownership, equal lockstep roots,
   reconnect behavior, and expected input-delay cadence.
6. Compile representative Blueprints and repeat the project's automated and packaged consumer gates
   before treating the setup as a reusable project template.

The next production layer is game-specific: combat abilities/effects, economy/production, squads,
cover policy, shared vision, targeting, UI, animation, and balance should be added incrementally with
their deterministic state lifecycle and qualification evidence.
