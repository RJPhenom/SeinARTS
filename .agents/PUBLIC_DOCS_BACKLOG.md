# Public Docs Backlog

Pending updates to the public documentation website (`docs.seinarts.gg`, served from
`Docs/`). Per Workflow Policy §4.2, every commit with `public` documentation impact adds an
entry here; RJ clears entries when the website is updated during a website task. Agents never
edit `Docs/` directly.

Entry format: date, commit(s), what changed designer-facing, which docs pages are affected.

## Pending

### 2026-09-05 — Neutral ability cooldown sharing

Uncommitted implementation: ability Cooldown Scope now offers Owner Only / Shared Group.
Sharing requires an explicitly opted-in command broker; Squad owns that opt-in, and ordinary
temporary selections do not share. Existing enum values and inherited shared defaults migrate
without asset resaves. New generic assets created by the Ability factory explicitly use Owner Only;
children inherit their parent. Native default / Reset to Default remains Shared Group for compatibility.
Cancellation refunds captured recipients only while the cooldown still belongs to the cancelled
activation. Newer cooldowns survive; older overwritten cooldowns are not reconstructed.

Affected pages: ability authoring, cooldown/refund reference, Squad integration, migration.
Core and Squad content revisions plus ability pool schema/behavior revisions changed; regenerate
the simulation-content manifest and do not mix old/new simulation snapshots or peers.

### 2026-09-02 — Formation preview opt-in moved render-side

Commits: `f10107c` (merged `f20a9e8`), headers `403b086`.

Designer-facing changes any preview/setup docs must reflect:

- Preview opt-in is now the **Navigation Renderer** component added to a unit Blueprint
  (`USeinNavigationRendererComponent`, SeinARTSFramework). No component = no markers for that
  unit. On a squad's actor Blueprint it opts in every member with one renderer.
- The component's **Preview Actor Class** picks the renderer per unit; None falls back to the
  project default, then the framework mesh-quad renderer.
- **Removed settings**: `Enable Formation Preview` (master switch) is gone. **Formation
  Preview Actor Class** remains but is now only the project-default renderer, not an
  enable/disable.
- **Removed fields**: `Show Navigation Preview` (navigation component) and
  `Show Formation Preview` (squad component) no longer exist. Migration: add the Navigation
  Renderer to units/squads that should show markers.
- New dev console toggle: `Sein.Preview.Disable 1` suppresses marker drawing (render-only).
- Compatibility: component schema change; old replays/snapshots rejected via epoch
  `SeinARTS.Replay.8`.

Likely affected pages: formation preview / destination preview setup, plugin settings
reference, squad authoring, navigation component reference, any migration/changelog page.

### 2026-09-05 — Cursor picking uses Sein Extents

Entity click selection, hover, ping targets, and contextual command targets now intersect
live Sein Extents at the displayed actor pose. Boxes, capsules, local offsets, and compound
shapes participate; mesh collision profiles, physics assets, actor scale, and sim collision
response flags do not. Nearest visible live extents win, with full entity handles breaking
equal-depth ties. Missing/empty extents do not fall back to mesh picking. World geometry
does not occlude entity picking. Selection Trace Channel remains serialized for compatibility
and controls only world-geometry fallback, which ignores all Sein actors.

Documentation impact: public + private-agent. Update selection/unit setup and controller
settings documentation when website scope opens. PIE before/during/after an actual group
move order remains a runtime gate; automated actor-transform tests are narrower evidence.

### 2026-09-05 — Selection policy on Sein Extents

Author selection behavior on the existing **Sein Extents** component under **Selection**:

- **Selection Policy**: Unrestricted (default), Like Units Only, Single Only, Disabled.
- **Include in Drag Selection**: true by default; false excludes new marquee acquisitions,
  while clicks, select-all/type actions and control-group recall remain eligible.
- **Selection Group**: optional exact gameplay tag. When either unit requires Like Units,
  both must share the same explicit tag; if neither has a tag, exact actor class is the key.
  Parent/child tags do not match. Unrestricted does not override another unit's restriction.
- **Selection Priority**: higher wins when replacing a mixed selection; equal priorities use
  full entity handles. Additive operations preserve current compatible selection first.

Single Only limits the entire selection to one entity; it does not independently disable drag.
For production buildings that should never enter a drag selection, set Single Only and turn
Include in Drag Selection off. A plain click replaces the selection; an incompatible Shift/Ctrl
addition leaves it intact. No unit/building classification is hardcoded.

All acquisitions pass through the controller's shared resolver after member-to-squad resolution.
The effective actor must be live, registered, owned, visible, runtime-selectable and have nonempty
Extents. Squads therefore need their own Extents and policy; no mesh-bounds or member-policy
fallback is provided. Runtime eligibility changes revalidate the selection and preserve focus by
actor identity. Hover, ability targets, and contextual command targets do not use these policies.

The fields bake through the existing Extents authoring wrapper and participate in reflected
canonical state and snapshot serialization. CoreEntity simulation-content contributor revision 4
invalidates older content digests; regenerate both manifest profiles. Existing authored Extents
retain permissive defaults. The existing Is Selectable BP query still reports the runtime flag,
not the full local controller policy (ownership/group/visibility are contextual).

Normal and placed-actor spawns now initialize the runtime selectable flag to true before
component injection and spawn callbacks. Previously the pool cleared that flag and neither
spawn path restored it; enforcing the flag consistently required fixing this initialization.
Abstract internal entities remain opt-in. Snapshot restoration retains the serialized flag.

Documentation impact: public + private-agent. Update component, selection, control-group and
migration reference pages when website scope opens. Interactive Blueprint Details and actual
PIE click/drag/group-move checks remain separate from native automation evidence.

Validation: Development and Shipping passed. Selection automation passed 15/15 in both
All (`Saved/Automation/SeinARTS.Unit.Selection-20260905-200347-e98ab372/index.json`) and
Framework (`Saved/Automation/SeinARTS.Unit.Selection-20260905-200220-41ef76eb/index.json`).
The five roots from the separate serial process
(`Saved/Automation/SeinARTS.Unit.Selection.Policy.Process.SerialSelectionTrace-20260905-200313-128f1a81/Automation.log`)
match the final All run's parallel and serial traces. Snapshot continuation agrees for three
ticks, disabled flags survive restore, and a recycled slot starts a new rendered entity selectable.
Independent adversarial review found no blocking defects. Collision-off fixture warnings are
expected: selection geometry deliberately works without simulation collision enabled.
