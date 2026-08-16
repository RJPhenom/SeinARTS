# Your First Skirmish

This walkthrough takes an empty project with the SeinARTS Framework installed to a playable
two-player skirmish. Each step names the exact contract it satisfies; skipping a step produces a
loud, specific failure rather than a broken match — the framework fails closed by design.

## 1. Project settings

Open **Project Settings → Game → SeinARTS**. The defaults are playable; the settings you are most
likely to touch first:

- **Simulation Tick Rate** (default 30 Hz) and **Max Ticks Per Frame** — the fixed-timestep core.
- The system pickers (Navigation, Fog of War, Collision Resolver, Avoidance, Level Data, Broker
  Resolver). Every picker follows one convention: **None means deliberately OFF** (with an
  on-screen notice), an invalid class is an error that falls back to the shipped default, and a
  valid class replaces the default. Leave the defaults for your first match.
- **Terrain Types** — optional; each entry binds a physical material or terrain volume tag to
  three independent dials: routing cost, movement speed, and vision range.

Sim-affecting settings freeze when a match starts and participate in the lockstep compatibility
fingerprint: two peers with different values refuse to join each other instead of desyncing.

## 2. A unit to command

1. Content Browser → right-click → **SeinARTS → Unit**. This creates a Blueprint subclass of
   `SeinActor` — the Blueprint IS the unit.
2. Select the actor's **Entity Bridge** component and add entries to **Component Data**. This
   array of simulation components defines everything the unit is. Minimum useful set:
   - **Identity Component** — display name, icon, owner-facing identity tag.
   - **Movement Component** — top speed, turn rate, and (optionally) a Movement Class from the
     Movement+ extension.
   - **Navigation Component** — footprint radius, arrival acceptance.
   - **Extents Component** — the collision body.
   - **Ability Component** — grant at least the shipped Move ability so right-click orders work.
3. Add a visual mesh as usual. The simulation never reads it; presentation is yours.

The Component Data picker only offers deterministic simulation structs; that filter is part of
the lockstep safety system, not an inconvenience.

## 3. A level the simulation can read

1. Place a **Sein Level Volume** covering your playable area. Its brush defines the baked grid
   bounds; its details host the per-level navigation and fog configuration.
2. Optionally place **Sein Terrain Volumes** (or rely on physical materials) to classify ground.
3. Click **Bake Level Data** on the level volume. One click traces the shared grid and writes the
   navigation and fog layers into a regenerable asset. Re-bake after any static level change —
   baked data is deliberately not tracked in source control, so also re-bake after a fresh clone.
4. Save the level. Placed SeinARTS actors bake fixed-point transforms into the level on save;
   an unsaved legacy placement fails the match rather than desync across CPU architectures.

## 4. Player starts

Place one **Sein Player Start** per player slot:

- **Player Slot** — 1-based slot index (0 leaves the start out of the match manifest).
- **Faction / Team** — optional; factionless projects are fully supported.
- **Spawn Entity** — the unit (typically an HQ) spawned for that slot at match start.

The level's world settings (`Sein World Settings`) control whether a standalone map auto-starts
its simulation (`bAutoStartSim`) — leave it on for a skirmish map, off for menu levels.

## 5. Simulation content manifest

Run the editor console command:

```
Sein.SimulationContent.GenerateManifest
```

This records a digest of your simulation-affecting content. Matches verify it at start so peers
with mismatched content fail compatibility instead of desyncing mid-game.

## 6. Play

- **Single player:** press Play. The map bootstraps deterministically and your slot-1 units obey
  right-click orders: click to move, drag to lay a formation along the drag line (the drag line
  is the formation's front).
- **Two players:** set the PIE net mode to listen server with two players (or run two processes).
  The lobby flow assigns slots; in PIE the direct manifest synthesized from your player starts is
  used. Both clients run the full simulation and continuously exchange 128-bit state roots — if
  they ever disagree, both screens say so immediately.

## Where to go next

- [Authoring Units](../Guides/Units.md) for abilities, movement classes, and squads.
- [Determinism Rules](../Reference/Determinism.md) before you write gameplay logic.
