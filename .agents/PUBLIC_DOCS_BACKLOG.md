# Public Docs Backlog

Pending updates to the public documentation website (`docs.seinarts.gg`, served from
`Docs/`). Per Workflow Policy §4.2, every commit with `public` documentation impact adds an
entry here; RJ clears entries when the website is updated during a website task. Agents never
edit `Docs/` directly.

Entry format: date, commit(s), what changed designer-facing, which docs pages are affected.

## Pending

### 2026-09-12 — General targeter preview API and optional visual components

Uncommitted: Targeter Preview is a Blueprintable general base with a read-only Context and
Preview Initialized, Validity Changed, Preview Updated, Point Captured, and Preview Ended hooks.
Context exposes source entity/ability, phase, explicit anchor presence, resolved capture pose,
captured points, validity/reason, and dimensions. Submitted means command handoff, not gameplay
success. Optional Targeter Mesh and Targeter Decal components own visual sources, sizing, and
Valid/Warning/Blocked material choices, with no parameter-name requirement. Missing warning or
blocked material falls back to Valid. Legacy point/facing classes retain named components,
Blueprint parents and advanced compatibility fields; old TintColor behavior is a fallback only.
Preview actor, context, mesh and decal properties share one SeinARTS category, including the
inline Valid/Warning/Blocked material fields. Legacy preview fields remain advanced.
The line preset now respects an anchor at world origin. Blueprint controls custom visuals and
can disable automatic pose following. Full contract: FRAMEWORK_MAP.md targeter presentation.

The ability's immutable Placement / Footprint Actor Class defines occupancy independently of
capture gesture and visual mesh overrides. Empty definitions retain the legacy Building Class
fallback; the old spec actor/mesh fields now appear under advanced Compatibility. Core content
and command behavior revisions are 8. Snapshot byte format is unchanged.

STP_BuildingHologram and STP_SmokeTargeter retain their parents and use explicit component
materials. Building uses the existing valid/invalid hologram instances; warning falls back to
valid, leaving the construction Placed material separate. Three smoke material instances use
the existing ring material's actual TargetColour parameter as authored asset values; runtime
preview code never writes it. Backups: Saved/PreviewRefactor/AssetsBefore. Migration:
`-run=SeinARTSEditor.SeinPreviewMigrationCommandlet -Apply`; fresh-process read-only verification:
same command with `-Verify`. Both exited 0 and logged PREVIEW_MIGRATION_VERIFIED in
`Saved/PreviewRefactor/MigrationApplyModule.log` and `Saved/PreviewRefactor/MigrationVerify.log`.

Documentation impact: public + private-agent; affected targeter, placement and smoke/decal guides.
Implemented solo at RJ's request. Actual Details-panel ergonomics and PIE visual acceptance
remain human gates.

Builds passed, including the ordinary editor restoration. Latest receipts:
`Saved/Build/a26b4eb907664c88b6a2826ae08f2f80/build-result.json` and
`Saved/Build/d365210c01cd401e9314d7921c57b036/build-result.json`. Five existing construction/widget
CPP first-include ordering violations were corrected without behavior changes to permit builds.
Framework Sim passed 127/127, Integration passed 23/23 with rendering enabled, and Determinism
passed 49/49. Six preview contract tests cover lifecycle, explicit origin anchoring, pose,
material slots, radius and custom transforms; integration loads the saved demo Blueprint classes
and checks the spawned meshes/decal switching to the configured materials and back. The restore
test uses the new immutable footprint definition with a plain point gesture and compares continued
canonical roots. Receipts: `Saved/Automation/PreviewRefactor-sim-final-result.json`,
`Saved/Automation/PreviewRefactor-integration-result.json`, and
`Saved/Automation/PreviewRefactor-determinism-result.json`.

Simulation preset stopped at the same four existing navigation terrain failures (539/543 Unit
tests passed); remaining suites were run individually. Receipt:
`Saved/Validation/26455cb935714b5192c84f3bb01c7c43/validation-result.json`. Failed tests:
AgentProjectionMatchesTerrainWallPadding, AgentTerrainParticipatesInFootprintClearance,
AgentTerrainPolicyOwnsPathAndReachabilityTopology, AuthoritativeDestinationCannotOverrideAgentHazards.
Fresh-process collision A/B matched all 120 canonical-root/raw-pose frames:
`Saved/Automation/PreviewRefactor-DeterminismAB/ab-result.json`. Global whitespace check passed.
Manifest refresh was attempted after migration. It remains blocked by the missing/unsaved
`/SeinARTSFramework/Demo/Blueprints/UI/WBP_UnitBanner` dependency of `SU_InfantrySquad`:
`Saved/PreviewRefactor/Manifest.log:2006`. The editor exited normally, but that is not a successful
manifest result. New helper configuration and material switching are automated evidence; actual
Details/PIE acceptance is still open. Compatibility presets retain HologramMesh/RingDecal names,
but now attach them to the general PreviewRoot; external graphs that assume the actor root is a
mesh/decal should use the named component instead. The two migrated data-only demos passed saved
asset and spawned-instance verification.

### 2026-09-12 — Ability lifecycle render notifications

Uncommitted: successful ability activation and natural/cancelled completion now enqueue the
existing On Ability Activated and On Ability Ended actor notifications. Events retain lifecycle
order when an ability immediately ends or cleanup starts a replacement. Actor Blueprints can use
these events to start/stop presentation, including montages. They represent the whole ability
lifetime; work pauses and per-shot timing require their own work state or presentation cue.
Affected pages: ability authoring and demo combat/mineral/construction animation instructions.
Documentation impact: public + private-agent. Actual demo montage playback remains a PIE gate.

### 2026-09-12 — Placement preview and live footprint admission

Uncommitted: Requires Free Footprint now uses one shared check for preview validity,
confirm/release, broker admission, and final activation after approach movement. It combines
baked navigation with live simulation blockers, including queued construction and buildings
spawned earlier in the same tick. All authored shapes, offsets, and captured yaw participate.
Missing required class/footprint/capture data rejects placement. Preview rotation resets after
a rejected drag, and an anchor at world origin remains anchored. Existing custom preview
Blueprints should use the supplied validity to select their blocked material/color.
Documentation impact: public + private-agent. Affected pages: placement/targeter and construction
guides. Full contract: FRAMEWORK_MAP.md placement section. Implemented and self-reviewed solo
at RJ's request; no independent review claimed. Validation receipts and remaining gates below.

Editor/test-enabled builds passed (207 compile/link actions in
`Saved/Build/229b5ac488a947248521e1f580ed2e0d/build-result.json`; ordinary editor receipt restored
by `Saved/Build/8a63b99752da49b9916637ef4cfa936f/build-result.json`). Full Framework Sim passed
120/120, Integration passed 22/22 with rendering enabled, and Determinism passed 49/49,
including five placement behavior/preview tests and two placement canonical/restore tests.
Receipts: `Saved/Automation/PlacementGate-all-sim-result.json`,
`Saved/Automation/PlacementGate-integration-resumed-result.json`, and
`Saved/Automation/PlacementGate-all-determinism-final-result.json`.
Simulation preset reached Unit and stopped on four existing navigation terrain failures
(534/538 passed): AgentProjectionMatchesTerrainWallPadding,
AgentTerrainParticipatesInFootprintClearance, AgentTerrainPolicyOwnsPathAndReachabilityTopology,
and AuthoritativeDestinationCannotOverrideAgentHazards. Receipt:
`Saved/Validation/8a7fe8f023d746e5a57440df060fabe5/validation-result.json`.
Remaining suites were therefore run individually. The first Integration attempt was interrupted
before launching its editor process; the resumed receipt above is the completed evidence.
Fresh-process collision A/B also passed all 120 canonical-root/raw-pose frames:
`Saved/Automation/PlacementGate-DeterminismAB/ab-result.json`. Whitespace check passed.
Configured manifest generation was attempted in a fresh hidden editor and failed because
`SU_InfantrySquad` references missing/unsaved
`/SeinARTSFramework/Demo/Blueprints/UI/WBP_UnitBanner` (log:
`Saved/Automation/PlacementGate-Manifest.log`, line 2006). The editor exited cleanly.
Manifest refresh and actual demo Blueprint blocked-color/placement PIE acceptance remain open;
automated native preview validity and transform tests are not a claim of that visual acceptance.

### 2026-09-12 — HUD click consumption

Uncommitted: SeinUserWidget defaults Consume Clicks to true. It consumes unhandled left/right
mouse down, up and double-click events after existing Blueprint handlers, preserving handled
replies and their capture/focus requests. Child controls retain normal input; minimap Blueprint
camera/order handlers need no opt-out or rewiring. Disabling consumption lets an unhandled event
continue to parents, which can still consume it. Full-screen widgets and layout canvases should
remain Not Hit-Testable (Self Only), with interactive backgrounds Visible. Wheel, mouse motion,
keyboard and preview events are unchanged. Affected page: building-the-hud; minimap input setup.
Documentation impact: public + private-agent. PIE click/drag acceptance remains required.

### 2026-09-12 — Unfinished construction does not reveal fog

Uncommitted: the default fog implementation suppresses all vision stamps from unfinished
construction sites, including queued and paused sites. Completion restores normal authored
vision on the next scheduled fog update. Starting construction removes existing live sight;
other sources and already explored terrain remain. Applies to both level-placed and spawned
entities using the construction lifecycle, without extra Blueprint wiring or settings.
Documentation impact: public + private-agent. Affected pages: construction and fog/minimap guides.
Full contract: FRAMEWORK_MAP.md fog section.

Independent read-only source review found no production defects. Four regression tests cover
lifecycle/first-reveal behavior, overlap and stationary restamping, serial/parallel canonical
roots, and fresh-world restore with a pending cached-footprint removal. These now compile and
pass within the Sim 120/120 and Determinism 49/49 runs recorded in the placement entry above.
Earlier Live Coding/header build blockers are resolved. Scoped whitespace check passed.
Fog behavior revision is 3, stamp-system revision is 2, and content-contributor revision is 2;
schema/codec bytes remain unchanged. Broader validation still has the four navigation failures
recorded above; refreshed content manifest and PIE acceptance remain pending.

### 2026-09-12 — Production queue policies and runtime overrides

Uncommitted: the Producible component exposes the five approved Queue Policy choices and
conditionally shows Queue Amount for fixed policies. These are lifetime purchase allowances:
queued plus successfully completed entries count; cancellation frees pending allowance and
unit death does not replenish completed allowance. Exact producible class is the item key.
Runtime producer/player Set/Clear Queue Policy nodes override defaults without changing class
assets or discarding queue/history. Producer overrides take precedence. Can Enqueue Production
supports availability checks; wire it into Can Activate for preflight/disabled-button feedback.
Backend enqueue always enforces the limit. Ownership transfer cancels pending entries with
normal refunds, as explicitly requested. Full contract: FRAMEWORK_MAP.md production section.

Documentation impact: public + private-agent. Affected pages: production/ability API, production
and research tutorial, queue UI, capture/ownership, and runtime upgrades. Snapshot/envelope v19
and contributor revision 6 require a regenerated simulation-content manifest; old snapshots
are rejected. Existing producible assets default to Multi-Queueable. Editor/PIE acceptance
still needs policy/count visibility and the research ability's actual availability wiring.

Verification: independent read-only review completed; Development editor and test builds
passed. Production policy tests passed 11/11 (including fresh snapshot continuation,
serial/parallel roots, competing commands, completion callbacks, and capture refunds).
Full Sim passed 113/113, Blueprint ability determinism 17/17, snapshot envelope 5/5,
Determinism 45/45, and Integration 22/22 with rendering enabled. Fresh-process collision
A/B matched all 120 canonical-root and raw-pose frames. Receipts are under
`Saved/Automation/ProductionPolicy-*-result.json` and
`Saved/Automation/ProductionPolicy-DeterminismAB/ab-result.json`.

The Simulation validation preset stopped in Unit: the new snapshot golden was corrected
and its five tests rerun successfully; four untouched navigation terrain assertions remain
unresolved (AgentProjectionMatchesTerrainWallPadding, AgentTerrainParticipatesInFootprintClearance,
AgentTerrainPolicyOwnsPathAndReachabilityTopology, AuthoritativeDestinationCannotOverrideAgentHazards).
No baseline attribution is claimed. Receipt:
`Saved/Validation/dffab23878aa467da70cf1589f448445/validation-result.json`.
The initial NullRHI integration run hit a canvas RenderTarget assertion; the subsequent
rendered run passed all 22 tests. Global diff whitespace check passed.

Configured manifest regeneration was attempted but failed: the existing
`/Game/SeinARTSExamples/Blueprints/Entities/Units/SU_InfantrySquad` asset references missing
`/SeinARTSFramework/Demo/Blueprints/UI/WBP_UnitBanner`. Existing asset WIP was preserved.
Repair that reference and regenerate the manifest before PIE acceptance; the policy change
does not have a refreshed manifest yet. Evidence: `Saved/Logs/ProductionPolicy-manifest.log`.

### 2026-09-11 — Queue identity, queue owner, and effect presentation

Uncommitted: **Get Production Queue** reads Display Name, Icon, and Identity Tag from
the Identity payload of the class passed to **Enqueue Production**, for both units and
research. Missing identity remains empty; research effects no longer substitute their
tag/name. Each queue item now exposes **Queue Owner**, the producer's full entity handle,
alongside Queue Index. These are a snapshot; widgets must refresh after queue changes.

Effects expose optional **Display Name**, **Description**, and **Icon** under General for
effect presentation. Effect Tag remains the gameplay identity. Queue presentation still
comes from the queued class. The new presentation fields are excluded from canonical
reflected state; this change does not alter production completion or duplicate eligibility.
Affected pages: production queue UI, effect authoring/API, and
`guides/adding-production-and-research`.

Validation: scoped diff whitespace and independent source review passed. The initial
production modules compiled/linked; the new queue/digest regression test's strict float
assertions were corrected to CQTest IsNear. Final build/test execution is blocked by
concurrent construction API edits: UHT rejects the exposed ESeinConstructionInitialState
pointer/default nullptr on SpawnEntity. Receipt: `Saved/Validation/QueueIdentityFocusedFinal.json`.
The focused wrapper also encounters existing gameplay-tag INI EOF whitespace
(`Saved/Validation/03f22105cf404ecbaddfc0d8a90ca252/validation-result.json`). Once construction
compiles, rerun `SeinARTS.Unit.UI.ProductionQueue` with the Framework runner without SkipBuild;
check effect defaults, the Queue Owner split pin, and research queue imagery in the editor/PIE.

### 2026-09-11 — Ability production cancellation node

Uncommitted: **Cancel Production** is available beside **Enqueue Production** on ability
graphs. Queue Index is zero-based and defaults to 0 (front); the bool result reports removal.
It uses the existing command cancellation/refund path, refunding the entry's original payer
under its captured policy. Waiting-item cancellation preserves front progress; cancelling
the front resets progress and completion stall state. Calls require simulation authorization.
Affected pages: production/ability API reference and `guides/adding-production-and-research`.

Validation: scoped diff whitespace and independent source review passed. With Unreal closed,
test-enabled and ordinary Development editor builds passed (incremental/up-to-date receipts:
`Saved/Build/1bd964e2db3d409cab299f8b3e32df95/build-result.json` and
`Saved/Build/4ea5c8baf7484a2cbb8436f449382f08/build-result.json`). All 14 production-cost
tests and 16 ability-determinism editor tests passed, including the new cancellation and node
validation cases (`Saved/Automation/CancelProduction-editor-closed-result.json` and
`Saved/Automation/CancelProduction-blueprint-result.json`). The focused wrapper previously
stopped on unrelated gameplay-tag INI EOF whitespace; direct focused runners supplied the
passing evidence. Interactive node discovery and front/waiting cancellation in PIE remain.

### 2026-09-11 — Automatic generated identity migration and cleanup

User-directed policy change: Content Browser asset-name changes migrate generated identity tags
and their complete descendant hierarchy automatically, remove old picker definitions, repair loaded
references, and retain hidden compatibility redirects. Folder moves and manually owned tags retain
identity. Initialize Tag also reconciles a remembered generated identity after reset. Document
rename-back, conflict reporting, and the audit/cleanup retention rules. Saved-content prerequisites
and Undo-history confirmation apply to explicit migration/cleanup, not automatic asset renames.
Public pages affected: gameplay tags, entity/ability/effect creation, project settings, asset
rename/duplication, migration/troubleshooting. Editor build, 19 focused tests, and fresh-process verification of the repaired Cancel identity pass.
Normal Save All persists dependent Blueprint recompilation; hidden redirects preserve earlier saved
references. See FRAMEWORK_MAP.md for current evidence. Docs/ was not edited.

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

- 2026-09-11 construction lifecycle: update building production guide for default-off Start Queued for Construction, Spawn Construction Site and job-handle Queue/Start/Complete/Pause APIs; work fields on the existing Sein Construction component and game-authored HP/material completion; explicit Finished/Construction component groups; generic Entity Binding and Entity Widget context; separate stage/state transition delegates and silent snapshot refresh. Remove mandatory preview actor, automatic mesh hiding and completed-percent assumptions. See CONSTRUCTION_LIFECYCLE.md. Public pages were not edited in this task.

- 2026-09-12 ability activation inputs: document Expose on Activate, typed Activate Ability /
  Issue Ability / Make Ability Inputs nodes, local-controller versus simulation authority,
  native constructor exposure and typed input building, per-activation defaults and validation,
  input limits and protocol incompatibility. Update the production queue cancellation example
  to submit through its explicit Queue Owner. Queue indices still have positional semantics.
  Public Docs were not edited; see FRAMEWORK_MAP.md, Ability activation inputs.

- Construction compatibility: framework behavior epoch is now `SeinARTS.Replay.10`; old compiled construction behavior is rejected, including in editor-session admission. See CONSTRUCTION_LIFECYCLE.md.

- 2026-09-12 ability node naming: the class-selected player-input node is now Activate Ability
  (previously Submit Ability); the explicit-packet function is Activate Ability With Inputs.
  Reflected function and node identities are unchanged. The old Activate Ability (Direct)
  remains an immediate simulation-only bypass pending caller migration and retirement.

- Work consolidation: remove all setup instructions for a separate Sein Construction Work component. Required Work is authored on Sein Construction; HP-based games ignore the work fields. Existing API node names remain unchanged.
