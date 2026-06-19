# Drag-Order & Formation Modularity — Working Plan

> **Scratch doc for Claude's own use** (compaction insurance), per RJ 2026-06-17. NOT a handoff doc —
> all work happens in this one session. Update the Progress Log as steps land.

## Goal (RJ's mental model)

Make drag-order handling **and** formation-authoring modular/Blueprint-pluggable, so any team/designer
can author "what a right-click-drag does" and "how units arrange" and just plug it in. Remove the
`bFormationSpreadEnabled` bool from plugin settings (it doesn't belong there). Keep the genre-neutral,
designer-first ethos. Framework is near feature-complete → this is an extensibility/ergonomics pass.

Three target behaviors:
1. Default move = blob (units share a destination) — status quo, becomes the **Blob** formation.
2. Right-click-**drag** = a formation (RoN / Total War style) — via gesture → guide → formation.
3. Squads behave ~as today — but rebuilt on the new formation model (their current per-slot vector
   offsets are brittle).

## Architecture — three decoupled, Blueprint-authorable seams

```
drag captured (PC) → [1] USeinOrderGesture → FSeinOrderTarget → command
                                                                   │ (lockstep)
   sim: resolver assembles OrderTarget + broker centroid/facing → [2] USeinFormation → positions+facing
                                                                   │
   render: [3] presentation plug draws gesture + resolved positions (preview === commit)
```

- **[1] `USeinOrderGesture`** — RENDER-side (`SeinARTSFramework`). Interprets the raw drag (point list)
  into a deterministic `FSeinOrderTarget` (guide points + nominated formation tag), baked into the
  command. Only the issuing client runs it; output is fixed-point and rides the lockstep command, so
  determinism is preserved. Default impl: click→point, drag→line (nominates Line formation).
- **[2] `USeinFormation`** — SIM-side (`SeinARTSCoreEntity`). `BuildFormation(World, Members,
  OrderTarget) → FSeinFormationLayout {Positions, Facing}`. **Stateless / pure-compute, invoked on the
  CDO** (formations hold only config UPROPERTYs; no instancing/pooling). Deterministic (fixed-point
  only). Stock: **Blob** (=old spread-OFF), **Grid** (=old spread-ON), **Line**, later **SlotFormation**
  (squads), and designer subclasses (spline/path march, wedge, …).
- **[3] Presentation** — RENDER-side. Construction-preview-style plug: draws the in-progress gesture +
  resolved formation positions (spline mesh, holograms, RoN dots). Reads OrderTarget + preview positions.

### Key types
- `FSeinOrderTarget` (SeinBrokerTypes.h, SeinDeterministic): Anchor, GuidePoints (ordered list —
  empty/1=click, 2=line, N=path), TargetEntity, FormationTag, CurrentCentroid, CurrentFacing.
  Serialized subset (GuidePoints + FormationTag) rides the order; centroid/facing filled by resolver.
- `FSeinFormationLayout` (exists): Positions[] + Facing. Returned by BuildFormation; shared by
  preview + commit (the sacred preview-parity invariant #6).

### Integration with the existing resolver (NON-breaking, squad-last)
Existing `USeinCommandBrokerResolver` owns BOTH dispatch (which ability/member — KEEP) and formation
geometry (`ResolvePositions`/`ResolveFormationLayout`/`ReassignSlots`/`PostProcessPositions`). We
extract geometry into `USeinFormation` and **delegate**:
- `USeinDefaultCommandBrokerResolver` gets `DefaultFormationClass` (default Blob) + `FormationsByTag`
  + `ResolveFormation(tag)`.
- `ResolveFormationLayout`: if a formation resolves → `Layout = Formation->BuildFormation(...)`; ELSE
  fall back to the OLD inline path (`ComputeFormationFacing` + `ResolvePositions`). Then `ReassignSlots`
  + `PostProcessPositions` as today.
- The squad resolver overrides `ResolvePositions` (authored slots). It configures NO formation →
  takes the fallback path → **unchanged** until the final squad step (then it gets a SlotFormation and
  drops the override). No mid-sequence breakage.
- `ComputeFormationFacing` + `YawFacingFromXY` move onto `USeinFormation` as shared statics (resolver
  fallback + squad call the new location). `ReassignSlots` + the `PostProcessPositions` cover hook STAY
  on the resolver. Cover extension (overrides `PostProcessPositions`) is unaffected.

### Determinism notes
- Gesture runs render-side on the issuer ONLY; OrderTarget (fixed-point) goes into the command and
  replicates identically → all clients replay the same path. Quantize captured float points to
  fixed-point when building the payload.
- Formations are sim, fixed-point only, called identically by preview (render, pure dry-run) and
  commit (sim) → preview === commit.
- Drag sampling = by cursor distance travelled (even spacing, capped count), NOT a wall-clock timer.

### Selection / granularity (Q2 decision — RJ approved 2026-06-17)
Project-wide default class (formation default on the resolver CDO; gesture default on the PC), with the
API shaped so per-unit/per-faction override can layer on later. NOT wiring per-faction now. Total-War-
style runtime formation switching (hotkeys) is a separate UI layer, out of scope.

## Sequence (tasks #1–#9)
1. `USeinFormation` base + `FSeinOrderTarget`; move ComputeFormationFacing onto it.   ← in progress
2. Blob + Grid formations (InterUnitSpacing moves to Grid).
3. Resolver delegates geometry to formation (Blob default; ResolvePositions fallback kept).
4. Remove `bFormationSpreadEnabled` (settings + DefaultGame.ini + resolver include).
5. OrderTarget guide-path plumbing (command→order→input; project points).
6. PC drag-path capture + `USeinOrderGesture` seam (default point/line).
7. Preview (SeinComputeFormationPreview takes OrderTarget) + presentation seam.
8. Line reference formation + default gesture nominates it on drag.
9. Squad refactor onto SlotFormation (LAST).
Build incrementally (Build.ps1, background; read the log not just exit code; close editor for relink).
PIE-verify at: blob unchanged (after #3-4), drag→line (after #8), squads (after #9).

## Key file coordinates (verified 2026-06-17)
- Resolver base: `Plugins/SeinARTSFramework/Source/SeinARTSCoreEntity/Public/Brokers/SeinCommandBrokerResolver.h`
- Default resolver: `.../Private/Brokers/SeinDefaultCommandBrokerResolver.cpp` (+ `.../Public/Brokers/...h`)
  - bool gate: `ResolvePositions_Implementation` ~`:441`; grid ~`:448-499`; layout chain ~`:239-266`.
  - ComputeFormationFacing ~`:184`; YawFacingFromXY (anon ns) ~`:40`; ReassignSlots ~`:202`.
- Broker types: `.../Public/Brokers/SeinBrokerTypes.h` (FSeinBrokerOrderPayload/QueuedOrder/OrderInput,
  FSeinFormationLayout, FormationEnd fields; FSeinOrderTarget added here).
- Settings: `.../Public/Settings/PluginSettings.h` — `bFormationSpreadEnabled` ~`:518`;
  `DefaultBrokerResolverClass` ~`:157`. Config: `Config/DefaultGame.ini` ~`:34`.
- Preview BPFL: `.../Private/Brokers/SeinCommandBrokerBPFL.cpp` `SeinComputeFormationPreview` ~`:263`.
- PC: `Plugins/SeinARTSFramework/Source/SeinARTSFramework/Private/Player/SeinPlayerController.cpp` —
  drag detect ~`:1294`; release branch ~`:459`; IssueSmartCommandEx ~`:1091`. Same IA_Command for
  click+drag, split by CommandDragThreshold; today keeps only start + live-current (no path history).
- Squad resolver: `Plugins/SeinARTSSquadExtension/Source/SeinARTSSquad/Private/SeinSquadDispatchResolver.cpp`
  (overrides ResolvePositions ~`:198`; zero-offset fallback to Super ~`:332`). Slot offset:
  `FSeinSquadSlot::OffsetTransform` in `.../SeinARTSCoreEntity/Public/Components/SeinSquadComponent.h:90`.
- Cover resolvers override ONLY PostProcessPositions (keep working): Cover ext
  `.../SeinARTSCover/.../SeinCoverAwareDefaultBrokerResolver.cpp:155`.

## Stale comments to fix when touched
- "unauthored squads spread sensibly" in SeinSquadDispatchResolver.cpp — false (blobs when bool off; bool being removed anyway).
- Squad CLAUDE.md says squad resolver overrides ResolveFormationLayout — it does NOT (only ResolvePositions/ResolveDispatch).
- FSeinFormationLayout docstring cites `USeinWorldSubsystem::ComputeFormationPreview` — actually `USeinCommandBrokerBPFL::SeinComputeFormationPreview`.
- Various `DESIGN §5` dangling refs in broker headers.

## Build gotcha (movement module — NOT a formation bug)
`SeinMoverHandle.{h,cpp}` (Plugins/SeinARTSFramework/Source/SeinARTSMovement/.../Movement/) are RJ's
**untracked** (`??`) movement-rebuild WIP. The `.cpp` fully implements all 22 `USeinMoverHandle`
methods. UE's adaptive-unity *incremental* build (driven by `git status`) mishandles brand-new
untracked `.cpp` after a UHT-invalidating header change: it drops the file from the unity blob
without compiling it standalone → `LNK2019 unresolved USeinMoverHandle::*` when relinking
`UnrealEditor-SeinARTSMovement.dll`. Every module the formation work touches still compiles+links.
**Fix:** a clean rebuild (`Build.ps1 -ExtraArgs '-Clean'`) recompiles all `.cpp` regardless of git
state. Recurs on header-changing incremental builds until those two files are `git add`-tracked
(RJ's call — git is user-controlled). If an incremental build fails ONLY on `USeinMoverHandle`, it's
this, not the new code.

## Task 7 seams (mapped 2026-06-17)
- **Preview lives in the COVER extension, not base.** `USeinFormationPreviewSubsystem`
  (SeinARTSCoverSquad, ULocalPlayerSubsystem+Tickable; binds PC OnCursorUpdated/OnSelectionChanged →
  RefreshPreview each tick) → `ASeinFormationPreviewActor` (SeinARTSCover, pooled decals, Blueprintable,
  class via `USeinARTSCoverSettings::FormationPreviewActorClass`). Base has only
  `ASeinHUD::DrawCommandDragLine` (SeinHUD.cpp:421 — a 2-pt canvas line from PC CommandDragStart/Current).
- **7a parity gap (resolver already ready):** `USeinCommandBrokerBPFL::SeinComputeFormationPreview`
  (CoreEntity, SeinCommandBrokerBPFL.cpp:263) builds FSeinOrderTargets but leaves GuidePoints empty +
  FormationTag invalid — squad path :360-363, loose path :410-413 → always blob. Fix: extend its
  signature to take guide+tag, populate both. Then `USeinFormationPreviewSubsystem::RefreshPreview`
  (SeinFormationPreviewSubsystem.cpp:160, call :218) must read PC drag state (bIsCommandDragging,
  CommandDragStart, CommandDragPath) + run the SAME gesture and pass guide/tag in. PC
  `ResolveOrderGesture()` is protected (SeinPlayerController.h:326) → add a public "order from current
  drag" accessor so preview + commit share gesture logic. Anchor convention: commit uses
  CommandDragStart for a drag.
- **7b presentation pattern (targeter ghost, IN BASE):** `USeinTargeterSpec::PreviewClass`
  (FSoftClassPath — cross-module-safe, SeinTargeterSpec.h:95) → `ASeinTargeterPreview` (Blueprintable,
  Abstract; BlueprintNativeEvent `OnPreviewUpdated`, `UpdatePreview(...)`) → `USeinTargeterSubsystem`
  spawns SpawnActorDeferred+InitializePreview+FinishSpawning, pumps UpdateCursor each tick, despawns on
  cancel. Closest sibling = ASeinFormationPreviewActor (decals).
- **OPEN:** base-vs-Cover placement of preview/presentation; how much presentation to build now.
- Stale: FSeinFormationLayout docstring (SeinBrokerTypes.h:271-273) cites USeinWorldSubsystem::
  ComputeFormationPreview — actually USeinCommandBrokerBPFL::SeinComputeFormationPreview.

## Task 7 PORT plan (proposed 2026-06-17 — awaiting RJ go)
Cover entanglement is isolated: only the per-cell cover-quality query+tint (SeinFormationPreviewSubsystem.cpp:244-288) + settings reads are Cover-specific; the rest uses base types only.
- **Base (SeinARTSFramework) gains:** `ASeinFormationPreviewActor` (ported from Cover — decal pool,
  SetPositions(positions, qualities), generic `QualityTints` tag→color neutral default,
  BlueprintNativeEvent styling hook = the "drop a preview blueprint" surface);
  `USeinFormationPreviewSubsystem` (ported generic 90%, made DRAG/GESTURE-aware for parity);
  `FormationPreviewActorClass` + `bEnableFormationPreview` on USeinARTSCoreSettings; a registered
  delegate `FSeinPreviewQualityProvider(positions, observer)->tags` (idiomatic cross-module hook like
  NavProjectResolver; unbound = neutral).
- **Cover becomes consumer:** binds the quality delegate (its FoW-gated QueryBestCoverQualityAt,
  throttled); configures base actor QualityTints for cover tags; DELETE CoverSquad's
  USeinFormationPreviewSubsystem; MOVE Cover's ASeinFormationPreviewActor to base.
- **Parity:** thread order-target (guide+tag) into SeinComputeFormationPreview + PC public
  "order-from-current-drag" accessor.
- Dependencies still point up (base owns preview; Cover registers the delegate). Squad works
  unchanged (expansion is base data).
- Alt considered: Cover actor-SUBCLASS instead of the delegate. Chose delegate (one actor class, base
  tints generically, matches framework idiom).

## Task 7 PORT — execution log
- **Stage A (relocation, behavior-preserving) DONE (building):**
  - NEW base files: `SeinARTSFramework/.../Preview/SeinFormationPreviewActor.{h,cpp}` (ported; cover-tag
    seeding removed → empty QualityTints default; SEINARTSFRAMEWORK_API) and
    `.../Preview/SeinFormationPreviewSubsystem.{h,cpp}` (ported generic; cover-query → the new delegate).
  - `USeinWorldSubsystem` (CoreEntity): added `FSeinPreviewQualityProvider` delegate type +
    `PreviewQualityProvider` member (next to NavProjectResolver/AuthoritativeDestinationResolver).
  - `USeinARTSCoreSettings` (PluginSettings.h): added `FormationPreviewActorClass` (MetaClass →
    /Script/SeinARTSFramework.SeinFormationPreviewActor) + `bEnableFormationPreview` ("Formation Preview").
  - Cover: `USeinCoverSubsystem::HookSimWorldEvents` now also binds `PreviewQualityProvider` (cover
    quality per cell, FoW-observer-gated) right after AuthoritativeDestinationResolver; added FoW include.
  - Cover settings: removed FormationPreviewActorClass + bEnableFormationPreview (note left pointing to base).
  - DELETED old files: CoverSquad `SeinFormationPreviewSubsystem.{h,cpp}` + Cover `Preview/SeinFormationPreviewActor.{h,cpp}`.
  - CoreRedirect (DefaultEngine.ini): /Script/SeinARTSCover.SeinFormationPreviewActor → /Script/SeinARTSFramework.SeinFormationPreviewActor (SFP_FormationPreview BP reparents; property names kept so its tints survive).
  - DefaultGame.ini: moved FormationPreviewActorClass + bEnableFormationPreview from the Cover section to [/Script/SeinARTSCoreEntity.SeinARTSCoreSettings].
  - Behaviour preserved: hover preview still shows (blob for loose units, cover-tinted via the delegate).
- **Stage B (drag-aware parity) — NEXT:** thread the order target (guide + tag) into
  `USeinCommandBrokerBPFL::SeinComputeFormationPreview` (populate GuidePoints/FormationTag on the
  FSeinOrderTargets it builds — squad path ~:360, loose ~:410); add a PC public "order from current
  drag state" accessor; the base subsystem feeds the live drag's gesture result into the preview so a
  right-click-drag previews the LINE (restores invariant #6).

## Task 7 DONE (2026-06-17)
- Stage A (relocation) verified green. Stage B (drag-aware parity) code-complete + MODULE-validated:
  `SeinComputeFormationPreview` now takes (GuidePoints, FormationTag) and applies them to the LOOSE
  group only (squads stay slot-driven, matching the dispatch guard); it nav-projects the guide like the
  commit. PC gained `BuildPreviewOrder(cursor → anchor/guide/tag)` mirroring OnCommandReleased; the base
  preview subsystem feeds that into the BPFL → a right-click-drag previews the LINE, a click previews the
  blob (preview === commit, invariant #6). All touched modules compiled + linked.
- **Editor-target build BLOCKED (external):** parallel movement agent's UNTRACKED
  `SeinARTSEditor/.../Util/SeinMovementTuningExport.cpp` fails — uses `FStructVariableDescription`
  through a forward decl (needs its full include, e.g. Kismet2/StructureEditorUtils.h or wherever UE
  fully defines it). 2nd parallel-agent cross-contamination (1st was SeinMovement.h). Per RJ's earlier
  "you'll fix movement" call, that side is RJ/agent's. Task 7 code is validated regardless; full-editor
  green + PIE pending the movement file compiling.

## Task 9 DONE (code-complete 2026-06-17)
- New `SeinARTSSquad/.../SeinSlotFormation.{h,cpp}` (USeinFormation subclass) — ports the squad's
  authored per-slot OffsetTransform layout (SlotIndex→tag fallback, rotate by facing, nav-project;
  unauthored/unresolved → blob at anchor).
- `USeinSquadDispatchResolver`: added a constructor setting `DefaultFormationClass = USeinSlotFormation`;
  REMOVED the `ResolvePositions` override (decl + def, the def via sed since em-dash comments wouldn't
  string-match). Squads now go through the formation pipeline like loose units; the ResolveDispatch
  guard (drops gesture guide/tag) keeps them slot-driven. Docstrings updated (.h class doc, .cpp brief,
  squad CLAUDE.md dispatch section).
- Build-VERIFIED GREEN (2026-06-17): SeinSlotFormation.cpp + SeinSquadDispatchResolver.cpp compiled,
  SeinARTSSquad.dll + SeinARTSCoverSquad.dll linked, editor target Succeeded (movement agent fixed
  their editor file, so the full target builds).

## ALL 9 TASKS DONE + BUILD-GREEN. Remaining = PIE behavioural verification (RJ; interactive).
PIE checklist: (1) click-move loose units → blob (status quo); (2) right-click-DRAG loose units →
LINE formation + a matching destination preview along the line; (3) squads move → authored slots
(unchanged), ignore drag; (4) cover decals still tinted (delegate); (5) SFP_FormationPreview BP
reparented OK via the CoreRedirect. Note: a project wanting grid-by-default for loose units sets the
default resolver's DefaultFormationClass = USeinGridFormation.

## Backlog / next sessions (teed up 2026-06-17)
Recommended next session — **"Drawn-path orders + their look"** (realizes RJ's "march along a drawn
path" idea; builds directly on the existing guide point-list + preview-actor seams):
- `USeinPathFormation` (USeinFormation) — distribute N members along the FULL guide polyline by
  arc-length (single-file or ranked-along-path). Line is the 2-point special case; this is the N-point
  generalization. Use USeinLineFormation as the template.
- Gesture: a full-path mode (forward the whole `CommandDragPath`) — either `USeinOrderGesture::bForwardFullPath`
  on a Path gesture subclass, or a modifier key selecting path-vs-line. Nominate a `SeinARTS.Formation.Path` tag.
- Presentation: an `ASeinFormationPreviewActor` subclass (or BP) drawing a spline mesh along the path.

Larger follow-on — **Formation-authoring EDITOR tool**: visual slot editor + preset generation +
dynamic procedural formations (1–1000, Total-War style). Backed by the now-procedural USeinFormation
interface (it already takes N + an order target). Editor-module / Slate effort; its own multi-session arc.

Quick wins (anytime): more stock formations — Column, Wedge, Box, Circle/Ring — each a small
USeinFormation subclass.

Smaller / optional:
- Richer base presentation: facing arrows; replace the HUD 2-point drag line (ASeinHUD::DrawCommandDragLine)
  with a formation-aware visual.
- Squad↔gesture interaction: let a squad drag honor a formation (remove the ResolveDispatch tag-drop
  guard in USeinSquadDispatchResolver) + decide the behaviour.
- Per-faction / per-unit formation selection (deferred from the project-global Q2 decision).
- Runtime formation switching (Total-War-style hotkeys; a UI layer feeding FormationTag per order).

## Progress Log
- 2026-06-17: Plan finalized; 9 tasks created.
- 2026-06-17: Tasks #1–#4 done + incremental-build-verified (formations exist; resolver delegates;
  bool removed; zero behaviour change by design — DefaultFormationClass null → blob fallback).
- 2026-06-17: Task #5 edits done (GuidePoints+FormationTag on payload/queued/input structs;
  ResolveFormationLayout now takes FSeinOrderTarget; call sites updated in default/squad resolvers +
  preview BPFL; ProcessCommands projects guide points). Incremental build linked all touched modules;
  movement link failure was the untracked-WIP gotcha above → running a clean rebuild to confirm.
- 2026-06-17: Clean rebuild — ALL formation-touched modules (CoreEntity, Framework, Squad, Cover)
  compiled **and LINKED** clean → tasks #1–#5 validated at module level. The clean build also exposed
  a PRE-EXISTING compile error in RJ's movement-rebuild WIP: `SeinMovement.h` ~140 — MSVC parses the
  (byte-valid, balanced) `/** */` block as code (C2059 / C3873 '0x2014' / C4138 '*/ outside comment'),
  breaking the `Tick` decl and cascading to `CachedHandle` / `BP_Tick` across `SeinMovement.cpp`, all
  movement subclasses, and `SeinMoveToAction.cpp:811`. NOT formation-related. `od -c` / `cat -A` of
  lines 100–145 show clean UTF-8 with balanced comments → suspect stale Intermediate/PCH or an editor
  artifact, not the visible source. **RJ chose to fix movement themselves (2026-06-17).**
  **>>> PAUSED** formation tasks #6–#9 until movement clean-builds; then resume #6 and run a verifying
  build. Editor target cannot fully build / PIE until movement compiles. Error log: build_full.log.
- 2026-06-17: Parallel agent fixed movement; baseline clean-build green. Resumed.
- 2026-06-17: Tasks #6 + #8 done + build-verified (compile + link green incl. movement). **Drag→line
  wired end-to-end:** PC distance-samples the drag into CommandDragPath → USeinOrderGesture (render-
  side, Blueprintable; default click→blob, drag→line nominating SeinARTS.Formation.Line) → command
  payload (GuidePoints + FormationTag) → resolver assembles FSeinOrderTarget → ResolveFormation(tag)
  → USeinLineFormation. Default resolver ctor seeds FormationsByTag[Formation.Line]=Line for OOB
  behaviour. New: USeinOrderGesture(+structs), USeinLineFormation, USeinFormation::FacingFromDirection,
  Formation/Formation.Line tags, PC OrderGestureClass/CommandDragPath/CommandDragSampleDistance,
  IssueSmartCommandEx now takes (GuidePoints, FormationTag) not FormationEnd. **Squads guarded** — the
  squad resolver drops the gesture guide/tag from its Target → authored-slot ResolvePositions runs
  (acts like today). **Did #8 before #7** so drag→line is verifiable. PIE behavioural check pending
  (interactive drag — RJ to eyeball).
- NEXT: #7 — (a) preview parity: thread the OrderTarget into USeinCommandBrokerBPFL::
  SeinComputeFormationPreview so the hover/drag preview reflects the line (TODAY it would show a blob
  while commit makes a line → violates invariant #6). (b) presentation seam: render-side, construction-
  preview-style plug for "how the drag looks". Then #9 squad refactor (last).
- 2026-06-18: Tasks #7 + #9 done, then a long PIE-driven iteration pass (RJ eyeballing, many rounds).
  ALL drag/formation behaviour now **PIE-CONFIRMED** (loose units + 1/2/3-squad drags). Changes:
  * Preview ported to BASE (ASeinFormationPreviewActor / USeinFormationPreviewSubsystem out of Cover;
    Cover tints via USeinWorldSubsystem::PreviewQualityProvider). Squads refactored onto USeinSlotFormation.
  * Drag DEFAULT changed Line -> **Box** (USeinBoxFormation, Total-War rank box; gesture
    DragFormationTag = Formation.Box). bFormationSpreadEnabled removed.
  * Facing reworked to pure drag-perpendicular HANDEDNESS (USeinFormation::DragFacingDir = (-Dir.Y,Dir.X)),
    NO centroid. RJ: "the drag perpendicular is the overriding authority on facing." (A selection-centroid
    facing attempt was BUILT then fully REVERTED at RJ's request -- it reacted to positions, which he rejected.)
  * Depth flipped so formations fill BEHIND the line: box BackDir = FaceDir; multi-squad DepthDir = FaceDir.
  * Multi-squad -> box-of-squads in ComputeMultiBrokerAnchors (the SHARED commit+preview helper): front
    rank of squad anchors along the drag, depth ranks behind. Wide drag = row; narrow = depth column.
  * SQUAD slot facing = NEGATED perpendicular FacingFromDirection(-DragFace) -- opposite the box sign on
    purpose, so the authored body extends BEHIND the line (raw perpendicular made it face INTO its own
    body, rear rank landing in front). This was the holdout bug, pinned via RJ's MS-Paint reference.
  * Single squad now CENTERS on the drag midpoint (ComputeMultiBrokerAnchors N==1 -> guide midpoint),
    not the drag start (ClickTarget = CommandDragStart for a drag).
  * Debug: a temp [SlotFmt] UE_LOG (GuidePts + DragFaceZero) confirmed the guide reaches squad
    SlotFormation mid-drag (GuidePts=2); the real bugs were facing SIGN + single-squad anchor, NOT
    guide-routing or a stale DLL (a wrong "stale DLL = fixed" claim was corrected by RJ). Log stripped.
  FEATURE PASS COMPLETE. Backlog above (drawn-path orders recommended next) untouched.
