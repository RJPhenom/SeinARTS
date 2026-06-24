# SeinARTS — BP-Authorable Movement Modes (Source Plan)

**Date:** 2026-06-17 · **Status:** active · **Scope:** make movement modes fully designer-authorable
in Blueprint, in the **base framework**, with no dependency on the Movement+ extension.

## 0. How to use this document
Self-contained source of truth for the movement-mode-authoring pass. A fresh session can execute
from *this file + the code*. Line citations are **starting coordinates** — re-ground against live
code before editing. Obeys the project-root guardrails (no worktrees; git is RJ's; build via the
PowerShell tool then read the log; determinism is sacred; **destination-preview-is-sacred** untouched).

## 1. Goal
A designer can author a custom movement mode (e.g. "wheeled") **entirely in Blueprint**, without the
Movement+ extension, and plug it into a unit via the existing `FSeinMovementComponent::MovementClass`
picker. Tuning is authored as **Blueprint variables** that an automation exports into a deterministic
**UDS** which auto-fills `MovementClassData`. The mode's *logic* is authorable at two altitudes —
shallow (decision hooks) and deep (the whole tick) — and both ship now.

## 2. Decisions locked (RJ, 2026-06-17)
- **Both authoring routes ship now.** Tier 0 (pick a mode, author tuning only) + Tier 1 (override
  decision hooks) + Tier 2 (override the whole tick). The determinism lint is built up front (Tier 2
  is an unguarded desync vector without it).
- **Tuning = BP variables → generated UDS.** Author variables on the mode BP; an automation mirrors
  them into a determinism-filtered UDS that fills `MovementClassData`. Per-unit live values live in
  the UDS instance on the component; the mode reads them back (see §4 hydrate).
- **No steered/vehicle base loop in the framework.** The base ships only `Basic` + `BasicUnit` loops.
  Concrete vehicle modes remain a *designer or Movement+* concern — we build a maximally general
  product and stay deliberately blind to the future extension. (Consistent with
  [[movement-base-not-anticipatory]].)

## 3. What already exists (do not rebuild)
- **Class selection + fallback:** `FSeinMovementComponent::MovementClass` (`FSoftClassPath`,
  MetaClass-filtered to `USeinMovement`), resolved at runtime, silent fallback to `USeinBasicMovement`.
  Per-unit instances in `USeinMovementSubsystem`. `SeinMovementComponent.h`.
- **Auto-inserted tuning container ALREADY WORKS for C++ modes:** `MovementClassData` is an
  `FInstancedStruct` with `meta=(SeinDataStructFromClass="MovementClass,GetMovementDataStruct")`;
  the editor restricts + auto-fills it on class change. `SeinInstancedStructDetails.cpp`. It just
  keys off a C++ virtual today — §6 Phase B makes it key off a BP mode too.
- **UDS authoring:** `USeinSimComponentFactory` (Right-click → Component) stamps `SeinDeterministic`;
  `FSeinDeterministicStructValidator` strips non-deterministic fields on edit (`IsPinTypeDeterministic`
  whitelist). `SeinComponentEligibility::IsEntityComponentStruct` is the shared eligibility rule.
- **Full fixed-point BP math:** `UMathBPFL` (arithmetic, trig, vectors, rotators, quats, lerp/clamp).
  Deterministic steering math in a BP graph is fully available today. `Lib/MathBPFL.h`.
- **The steering toolkit (C++):** `USeinMovement` protected statics — `ResolveLookAheadPoint`,
  `AdvanceWaypointAlongPath`, `KinematicArrivalSpeedCap`, `StepSpeedToward`, `EffectiveTopSpeed`,
  `ApplyAvoidanceSteer`, `ResolveNavCollision`, `IsFootprintPassable`, `ApplyGroundSnapAndAltitude`,
  `ComputeSlopePitch/Roll`, `SmoothAngleToward`, `ShortestAngleDelta`, `YawFromRotation`,
  `IsOvershootArrival`, `ShouldAutoReverse`. `SeinMovement.h:245-660`. Phase D wraps these as BP nodes.

## 4. Target architecture

**The load-bearing constraint:** per-unit tuning MUST live in `MovementClassData` (the hashed sim
component), because the `USeinMovement` instance is shared/borrowed and holds only transient state
(runtime reads `MovementData->MovementClassData.GetPtr<T>()`). BP variables on the mode sit on the
shared CDO — not per-unit, not hashed. So the generated UDS in `MovementClassData` is the only
correct home for per-unit tuning. The BP variables are the **schema + defaults**; the per-unit
instance is **hydrated** from `MovementClassData` at `OnMoveBegin` so the designer's graph reads its
own variables and they're per-unit-correct (resolves the "my variables aren't live" wrinkle).

**Three tiers on `USeinMovement` (all backed by C++ defaults → shipped modes untouched):**

```
Tier 0  pick a mode + author tuning. No graph.
Tier 1  override decision hooks (fixed-point in/out). Default Tick loop calls them.
Tier 2  override BP_Tick itself. Default impl is the integration loop; override bypasses it.
```

**The dispatch reshape (forced, because FSeinMovementContext is not a USTRUCT):**
`FSeinMovementContext` holds `FSeinEntity&` + raw pointers, so it cannot be a `BlueprintNativeEvent`
parameter. Therefore:
- The sim-facing `Tick(const FSeinMovementContext&)` / `OnMoveBegin` / `OnMoveEnd` / `TickIdle` stay
  **virtual C++ entries** and become **dispatchers**: point a reusable `USeinMoverHandle` at the live
  context, call the matching `BlueprintNativeEvent` (`BP_Tick(handle)` etc.), return.
- **Non-breaking:** call sites in `SeinMoveToAction.cpp` (`:396` OnMoveBegin, `:811` Tick, `:977`
  OnMoveEnd) and the Movement+ vehicle modes that override `Tick(Ctx)` directly are **untouched** —
  they bypass the dispatcher (a C++ override of `Tick(Ctx)` simply doesn't reach `BP_Tick`).

**`USeinMoverHandle`** — a transient `BlueprintType` UObject wrapping `FSeinMovementContext*`
(repointed per dispatch, valid only during the call). BP-facing accessors: transform get/set,
velocity get/set, path/waypoint read + advance, delta / acceptance / terrain-speed, movement-authoring
reads (TopSpeed/Effective/Accel/Decel/TurnRate/reverse), tuning accessor, ground-Z sample. Phase D
extends it with toolkit wrappers + integration helpers. One handle per movement instance, reused.

**Hook set (V1) — only the decisions `BasicUnit` actually makes, so defaults are byte-identical:**
- `FFixedPoint ComputeDesiredSpeed(USeinMoverHandle*)` — returns the **cruise target** (default
  `EffectiveTopSpeed`). The base loop then applies the kinematic arrival cap + `StepSpeedToward` ramp.
  (Override target = a turn-braked / arrival-shaped cruise.)
- `FFixedPoint ComputeSteer(USeinMoverHandle*, FFixedVector DesiredMoveDir, FFixedPoint CurrentYaw)` —
  returns the **post-turn yaw** (default = face-velocity, `CurrentYaw + clamp(ΔYaw, ±TurnRate·dt)`).
  The base loop then applies slope pitch/roll + sets the rotation. (Override = bicycle/tank steering.)
- Reverse is NOT a base-loop hook in V1 (BasicUnit is forward-only). Reverse stays a power-route /
  vehicle concern via the exposed `ShouldAutoReverse` helper (Phase D). Keeps the default loop honest.
- Also make BP-overridable: `GetMovementDataStruct`, `GetAltitude`, `GetMinTurnRadius`,
  `BypassPathfinding`, `QueryReferenceZ`.

## 5. Byte-identical strategy (the only behavior-critical change)
The base `BP_Tick_Implementation` becomes the **hoisted `BasicUnit` loop** (`SeinBasicUnitMovement.cpp`
verbatim), with its two inline decisions delegated to `ComputeDesiredSpeed` / `ComputeSteer` whose
default `_Implementation`s are exactly the code they replace. Then:
- `USeinBasicUnitMovement` → **empty** (its behavior == base default). Verify identical.
- `USeinBasicMovement` → **unchanged** (keeps overriding `Tick(Ctx)` verbatim — the cheap fallback,
  never reaches `BP_Tick`). Zero risk.
- Movement+ vehicle modes → **unchanged** (keep overriding `Tick(Ctx)`).
Verification: PIE state-hash agreement / behavior diff on a `BasicUnit` unit before vs after — must be
bit-identical. This is the sacred no-regression gate (§guardrails).

## 6. Phase ladder (all in scope; each gate = build-green + a PIE checkpoint)

- **Phase A — Logic seam.** `USeinMoverHandle` (.h/.cpp); BNE surface + dispatchers on `USeinMovement`;
  `BP_Tick_Implementation` = hoisted loop calling the two hooks; `BasicUnit` → empty; `Basic`/Movement+
  unchanged. *Gate:* `BasicUnit` byte-identical; a BP override of `ComputeSteer` changes feel in PIE.
- **Phase B — Tuning pipeline.** Promote `IsPinTypeDeterministic` to a shared util; build the
  `UBlueprint::NewVariables` → UDS export (create/sync a sibling UDS stamped `SeinDeterministic,
  SeinSubData`; `FStructureEditorUtils::AddVariable`/retype/remove to match; copy defaults; run on BP
  compile + a manual button). Stamp the generated UDS onto a CDO `UScriptStruct*` so
  `SeinDataStructFromClass` auto-fill works for BP modes. Per-unit hydrate at `OnMoveBegin`. Add a
  "Get Movement Tuning" unwrap node. *Gate:* add a var → compile → UDS field appears → `MovementClassData`
  auto-fills → per-unit edit reaches the graph.
- **Phase C — Factory + scaffolding.** `USeinMovementModeFactory` (mirror `USeinSimComponentFactory`):
  "Create Movement Mode" → BP parented to `USeinMovement` + paired tuning UDS, pre-linked, starter
  graph (hook stubs reading the tuning node). *Gate:* one click → a working mode in the picker.
- **Phase D — Power-route toolkit.** Wrap the steering toolkit + integration (apply velocity / set
  facing / write-back) + key nav queries as deterministic nodes on `USeinMoverHandle` / a toolkit BPFL.
  *Gate:* a BP mode overriding `BP_Tick` (not the hooks) drives a unit end-to-end via exposed nodes only.
- **Phase E — Determinism lint (mandatory for Tier 2).** Tag deterministic BPFLs (`MathBPFL`, toolkit,
  accessors) with a `SeinDeterministic` UFUNCTION meta; a Blueprint-compile pass over
  `USeinMovement`-derived graphs allows pure flow + `SeinDeterministic`-tagged calls + handle/tuning
  accessors, errors on everything else (float `FMath`, `FVector`, engine random, world delta). Emit to
  the compiler results log. Whitelist-by-meta — mirrors the struct validator. *Gate:* a float node
  errors; a clean fixed-point graph compiles silently.

## 7. Determinism guardrails & open audits
- **No-regression:** §5's byte-identical gate is non-negotiable (state-hash agreement).
- **Sim-tick BP cost:** calling the BP VM per unit per tick is opt-in — C++ defaults cost nothing;
  only overridden hooks/`BP_Tick` pay. Acceptable; nativization is out of scope.
- **Instance-state audit:** while formalizing the per-unit instance model, confirm transient
  instance state (e.g. Movement+ Wheeled's `CurrentSteer`) is reconstructed identically across clients
  (reset in `OnMoveBegin` today) and isn't an unhashed authoritative-state leak. Not introduced here.
- **Handle lifetime:** `USeinMoverHandle` is transient scratch (not hashed); its `Ctx*` is valid only
  during a single dispatch — BP must not stash it across ticks (document on the node).

## 8. Status log
- 2026-06-17 — Plan consolidated. Direction locked with RJ (both routes + lint now; BP-vars→UDS;
  no steered base loop in framework). Codebase mapped (movement modes, tuning auto-swap, UDS tooling,
  nav/steering seam, fixed-point BP math). Reshape designed: dispatch via `USeinMoverHandle`, hoist
  `BasicUnit` loop to base `BP_Tick_Implementation` factored through `ComputeDesiredSpeed`/`ComputeSteer`;
  non-breaking for call sites + Movement+. Phase A started.
- 2026-06-17 — **Phase A core landed + build-green** (compile + link, editor closed → full verify).
  • New `USeinMoverHandle` (`Movement/SeinMoverHandle.h/.cpp`) — BlueprintType context façade
    (transform/velocity/kinematics/path/per-tick reads), stores `const FSeinMovementContext*`,
    repointed per dispatch. Nav queries + toolkit + typed tuning accessor deferred to Phases D/B.
  • `USeinMovement`: `Tick(Ctx)` is now a sealed virtual **dispatcher** → `BP_Tick` (BlueprintNativeEvent);
    `BP_Tick_Implementation` = the RTS loop hoisted verbatim from `USeinBasicUnitMovement::Tick`, with
    its two decisions routed through `ComputeDesiredSpeed` (default = `EffectiveTopSpeed`) and
    `ComputeSteer` (default = face-velocity clamp) — both BlueprintNativeEvents whose `_Implementation`
    reproduces the original inline logic. Reusable `CachedHandle` (Transient UPROPERTY) created lazily.
  • `USeinBasicUnitMovement` → empty (inherits the base default). `USeinBasicMovement` + Movement+
    vehicles **unchanged** — they still override `Tick(Ctx)` directly and bypass `BP_Tick`.
  • Caught one bug en route: the `Tick` docstring edit orphaned the prior comment's `*/`, breaking the
    class parse (cascaded into "Tick is not a member" everywhere) — fixed; rebuild green.
  **PIE-PENDING (RJ):** the no-regression gate — a `BasicUnit`-class unit must move BIT-IDENTICALLY
  pre/post (state-hash agreement). Build-verified only; behavior not yet PIE-checked.
  **Remaining Phase A:** lifecycle BNEs (`OnMoveBegin`/`OnMoveEnd`/`TickIdle` → `BP_*`). Then Phase B.
- 2026-06-17 — **Phase A COMPLETE + Phase B runtime seam landed, build-green.**
  • Lifecycle BNEs: `OnMoveBegin`/`OnMoveEnd`/`TickIdle` dispatch to `BP_OnMoveBegin`/`BP_OnMoveEnd`/
    `BP_TickIdle`. `USeinMoverHandle` gained an **entity-only mode** (`SetEntityOnly`) so `BP_OnMoveEnd`
    (only an `FSeinEntity`, no live context) can still bind transform reads — `IsValidMover()` is false
    there; transform accessors switched to an `EntityPtr` valid in both modes. `TickIdle`'s body moved
    into `BP_TickIdle_Implementation`.
  • Phase B runtime seam: `USeinMovement::TuningStruct` (EditDefaultsOnly `UScriptStruct*`) +
    `GetMovementDataStruct` now returns it → a BP mode's tuning UDS auto-fills `MovementClassData` via
    the existing `SeinDataStructFromClass` path, and a UDS can be **linked by hand today**. Per-unit
    hydrate `HydrateTuningFromData` (called in the OnMoveBegin dispatch) reflection-copies
    `MovementClassData` → same-named instance UPROPERTYs, matched via `UStruct::GetAuthoredNameForField`
    (handles UDS GUID-name mangling — caught + fixed a first cut that matched raw FNames). No-op without
    tuning → BasicUnit unaffected.
  **Remaining Phase B (editor automation):** `UBlueprint::NewVariables` → UDS export on compile, then
  the "Create Movement Mode" factory (Phase C). Planned tuning-var convention: **instance-editable +
  deterministic-typed** BP vars → tuning fields (adjustable). This is the editor-API-heavy, version-
  sensitive piece and the natural point for RJ's first in-editor pass.
- 2026-06-17 — **Phase B COMPLETE (editor automation), build-green.**
  • Shared rule promoted to `Util/SeinDeterminismRules.h` (`SeinDeterminism::IsPinTypeDeterministic`);
    validator refactored onto it (one source of truth).
  • Export `Util/SeinMovementTuningExport.{h,cpp}` — `SyncTuningStructForBlueprint(UBlueprint*)`: gathers
    **Instance-Editable + deterministic** `NewVariables`, get-or-creates the paired `<Name>_Tuning` UDS
    (stamped SeinDeterministic + SeinSubData), syncs fields BY NAME via `FStructureEditorUtils`
    (add/remove/retype + best-effort defaults), stamps the UDS onto the CDO `TuningStruct` by reflection,
    marks the BP modified. Zero tuning vars → clears the link, no asset. **Decoupled from Movement**
    (base class by path, CDO prop by reflection) — no Build.cs change.
  • UI `Details/SeinMovementModeDetails` — Class-Defaults "Tuning" category + "Sync Tuning Struct" button,
    shown only for BP-generated movement modes; registered by class NAME "SeinMovement".
  • **DESIGN CHOICE (reshape latitude):** user-triggered button, NOT auto-on-compile — a
    `UBlueprintCompilerExtension` would mutate the paired UDS + CDO DURING compile (reentrancy/instability
    I can't validate headlessly). Auto-on-compile deferred as optional polish.
  • Incidental: build briefly red on a PRE-EXISTING 3-arg `SeinComputeFormationPreview` call in the
    in-progress formation/drag-order work (`SeinFormationPreviewSubsystem.cpp`); RJ's concurrent edit
    resolved it to the 5-arg signature. Not movement-related; not touched by me.
  **Remaining:** Phase C (Create Movement Mode factory) · Phase D (power-route toolkit nodes) · Phase E
  (determinism lint). Then RJ's one full-pipeline test.
- 2026-06-17 — **Phases C + D COMPLETE, build-green.**
  • C — `Factories/SeinMovementModeFactory.{h,cpp}`: auto-discovered (bCreateNew UFactory, no
    registration) Content-Browser entry under the SeinARTS category — creates a Blueprint pre-parented
    to USeinMovement (resolved by path, no Movement dep) so designers skip the generic class picker.
  • D — power-route toolkit on `USeinMoverHandle`: 15 fixed-point wrappers over USeinMovement's steering
    helpers (EffectiveTopSpeed, StepSpeedToward, KinematicArrivalSpeedCap, ComputeAdaptiveLookAhead,
    ResolveLookAheadPoint, AdvanceWaypoint, ShortestAngleDelta, SmoothAngleToward, ApplyAvoidanceSteer,
    ResolveNavCollision, ApplyGroundSnapAndAltitude, ComputeSlopePitch/Roll, ShouldAutoReverse), each
    pre-bound to the dispatch context. `USeinMovement` grants `friend class USeinMoverHandle` so the
    toolkit stays OUT of the public C++ API; the handle reaches its owner via `GetTypedOuter`. A Tier-2
    `BP_Tick` override can now be authored entirely from nodes.
  **Remaining: Phase E only** — determinism lint. Plan: tag `UMathBPFL` + `USeinMoverHandle` with a
  class-level `SeinDeterministic` meta; a `UEditorValidatorBase` (DataValidation module) walks a
  movement-mode BP's graphs and warns on non-whitelisted `UK2Node_CallFunction` targets (warn, not
  hard-error, for V1 — escalatable). The authoring PIPELINE (Tiers 0/1/2 + tuning + factory) is complete
  + build-green; the lint is orthogonal safety, so RJ's full-pipeline test can happen before or after it.
- 2026-06-17 — **Phase E COMPLETE → initiative CODE-COMPLETE, build-green.**
  `USeinMovementDeterminismValidator` (`UEditorValidatorBase`, DataValidation plugin — confirmed
  enabled): auto-gathered at editor start; runs on save + "Validate Assets". `CanValidateAsset` gates to
  movement-mode BPs; `ValidateLoadedAsset` walks every `UK2Node_CallFunction`
  (`FBlueprintEditorUtils::GetAllNodesOfClass`) and **warns** on any target whose function OR owning
  class lacks the `SeinDeterministic` meta. Whitelist seeded by class-meta on `UMathBPFL` +
  `USeinMoverHandle`. V1 = warnings (`AssetWarning` + `AssetPasses`), escalatable to `AssetFails`. Added
  `"DataValidation"` to SeinARTSEditor.Build.cs. (Agent first claimed UEditorValidatorBase didn't exist
  in 5.7 — wrong; it's in `Engine/Plugins/Editor/DataValidation`, real header read for exact signatures.)
  **ALL PHASES A–E BUILD-GREEN. Initiative code-complete.** PIE-PENDING: RJ's one full-pipeline test.
  V1 deferrals (all flagged, none blocking): export is button-triggered not auto-on-compile; UDS field
  DEFAULT-value copy is best-effort (struct round-trip may not carry); validator emits warnings not errors;
  whitelist seeded with MathBPFL + MoverHandle only (other deterministic Sein libs would warn until tagged).
- 2026-06-17 — **PIE FIX (RJ hit it at test step 2): "Sync Tuning Struct" found 0 vars.** Root cause: the
  export required variables to be **Instance-Editable**, but new BP vars aren't by default → the designer's
  freshly-added FixedPoint var was filtered out, `Desired` was empty, and the link was silently cleared
  (no UDS, no feedback). `FFixedPoint` IS `SeinDeterministic`-marked, so the type check was fine. **Fix:**
  dropped the Instance-Editable gate — **every deterministic-typed `NewVariables` entry now exports** (matches
  "my variables are my tuning"); and added **success/empty notifications** to the Sync button so it can never
  silently no-op. Convention is now simply: deterministic var ⇒ tuning field (internal scratch of a
  deterministic type exports too — per-var opt-out deferred). Build-green (compile + link).
  **PIE check 2026-06-17 (RJ):** byte-identical BasicUnit **movement** + **idle** ground-snap/settle
  confirmed OK (gate items 1–2) — the no-regression gate holds for shipped behavior. The BP-seam live
  test (item 3) is deferred to ONE full-pipeline test after Phases B–E land.
- 2026-06-17 — **QoL/UX pass (RJ feedback); Instance-Editable gate RESTORED. Build-green.**
  Supersedes the "dropped Instance-Editable" decision above (that was a wrong turn during the PIE fix).
  • **Instance-Editable requirement is back** — RJ: it IS the right intent signal (separates tuning knobs
    from internal scratch). The earlier 0-vars failure was tester error (var not marked Instance Editable).
    Kept the success/empty toasts; the empty toast now points at the eye icon.
  • `TuningStruct` is now **VisibleAnywhere (read-only)** (was EditDefaultsOnly) — RJ flagged that exposing a
    generated, button-managed link as an editable class picker is a foot-gun. Manual-link path dropped.
  • **UDS naming unified** to `<Name>TuningData` (no underscore; was `<Name>_Tuning`) to match the
    `…MovementData` family; button relabeled **"Generate Tuning Data Structure"**; help/tooltip/toasts/
    factory-tooltip updated.
  **Editor RESTART required** — the TuningStruct specifier flip is a reflection change (Live Coding won't
  catch it). Delete any `<Name>_Tuning` test UDS made earlier and regenerate as `<Name>TuningData`.
- 2026-06-22 — **Bug fix (RJ): renaming a mode BP didn't rename its tuning struct.** Cause: `SyncFields`
  only updates fields (never the asset name), and `ResolveExistingTuningUDS` returns the already-linked
  struct via the CDO link → it kept its stale name (and the legacy `_Tuning` name). Fix: `RenameTuningUDS`
  (`IAssetTools::RenameAssets`) renames + relocates the existing struct to `<BPName>TuningData` in the BP's
  current folder on every Generate — tracks renames AND folder-moves AND migrates legacy `_Tuning` naming.
  Best-effort (leaves a redirector at the old path → "Fix Up Redirectors" to clean). Build-green.

## Phase F — Close ALL custom-movement authoring gaps (planned 2026-06-22)
Goal: make the FULL mode range BP-authorable (ground *feel* already ships + verified; add nav-integrated
kinematic shape, airborne, reverse, custom planning). Sequenced **easy-batch → major-single**. Every group
ends build-green + an RJ PIE gate. Two invariants hold throughout: **byte-identical** for the shipped C++
modes (Basic/BasicUnit), and **non-breaking for Movement+** (its overrides migrate plain-virtual →
`_Implementation`, behaviour identical).

**Cross-cutting note (applies to every virtual→BNE conversion below):** each Movement+ override
(`USeinWheeled/Tracked/Hover/FlightMovement`) flips `Foo() override` → `Foo_Implementation() override` —
mechanical + behaviour-identical, but must build-green with Movement+ enabled. C++ defaults stay native
(zero VM cost); only a BP-*overridden* mode pays the VM call.

### Group F1 — Easy wins (batch; additive, low risk)
- **F1.1 Authoring guide** — `Movement_Authoring_Guide.md` (designer-facing): the 3 tiers, hook signatures,
  the Mover Handle node catalog (accessors + toolkit), the tuning workflow (Instance-Editable vars →
  Generate → auto-fill → hydrate), determinism rules, and the worked "spin" recipe. LIVING doc — extended
  as F2–F4 land. [markdown; no code risk]
- **F1.2 Airborne flags** — `BypassPathfinding` → `BlueprintNativeEvent` (bool); add `UsesWalkableGroundGate`
  `BlueprintNativeEvent` (bool, default true) driving the `QueryReferenceZ` ground-snap gate (a flyer returns
  false → snaps to terrain-top, not walkable-only). Defaults reproduce current behaviour. Migrate Movement+
  Flight/Hover. ⇒ a BP flyer opts out of A* + clears ground obstacles without C++. (Full custom Z-source
  override stays deferred to F4 — the flag covers the standard flight case.) [small; Movement+ ripple]
- **F1.3 Validator whitelist breadth** — tag any extra deterministic helper libs a mode legitimately calls
  with the `SeinDeterministic` class meta; document how to whitelist more + how to escalate warnings→errors.
  [additive meta]

### Group F2 — Hydrate-timing + numeric shape virtuals (cohesive refactor)
The shape virtuals read per-class tuning, but hydrate only runs at `OnMoveBegin` today — so a virtual called
at plan-time / idle would read un-hydrated (default) values. Fix timing FIRST, then expose the virtuals.
- **F2.1 Hydrate at instance creation** (+ re-hydrate on `MovementClassData` change) in
  `USeinMovementSubsystem`'s instance path, so a BP mode's vars are populated whenever ANY virtual runs
  (Tick, TickIdle, queries). Keep the OnMoveBegin refresh. Byte-identical for C++ modes. [moderate]
- **F2.2 `GetAltitude` → BNE** (BP reads its hydrated cruise-altitude var; default 0 = ground). Migrate
  Movement+ Hover/Flight. ⇒ BP hover/flight hold altitude. [small after F2.1; per-tick BNE only when overridden]
- **F2.3 `GetMinTurnRadius` → BNE** (BP computes from hydrated wheelbase/steer vars; default 0). Migrate
  Movement+ Wheeled/Tracked; verify nav corner-rounding + curvature throttle read the BP value at plan time.
  ⇒ BP wheeled gets nav-rounded paths. [small after F2.1]

### Group F3 — Reverse as a Tier-1 hook (single item)
- **F3.1** Add `ShouldReverse` BNE (default = the existing `ShouldAutoReverse` 3-gate logic) and make the
  base default loop honour reverse, **gated by `bCanReverse` (default false → forward-only → byte-identical
  for every current unit)**. Lets a BP vehicle reverse without a full Tick override. [medium; the one behaviour
  addition to the shared loop — PIE-diff a forward-only unit AND a reversing one]

### Group F4 — Custom path planning (major refactor, single item)
- **F4.1** Make `PlanPath` BP-overridable: a plan-time façade (a handle like `USeinMoverHandle` exposing
  destination + nav queries + footprint) + path-builder nodes (append waypoint / straight-line / request A*)
  + `PlanPath` → BNE. Enables designer-authored planners (curve-fit driving lines, 3D flight corridors), and
  is where a full custom `QueryReferenceZ` Z-source would land. [LARGE — new façade + node surface mirroring
  the Tick exposure; rare need → last]

### Group F5 — Tier-2 state-determinism hardening (optional, single item)
- **F5.1** A Tier-2 BP Tick storing *persistent authoritative* state in loose (non-tuning) member vars puts
  it outside the hashed sim state → desync risk. Deliver: a firm doc rule (persistent state → tuning/
  `MovementClassData`; loose vars = per-tick scratch only) + an OPTIONAL validator heuristic warning when a
  mode BP has member vars not in its tuning struct. [doc-certain; heuristic fuzzy → optional]

**PIE gates:** F1 → a BP flyer bypasses A* + holds height. F2 → BP wheeled shows nav-rounded corners, BP
hover holds altitude while idle. F3 → a reversing vehicle backs into close rear targets while forward-only
units stay bit-identical. F4 → a BP custom planner emits a non-A* path.

### Phase F execution log
- 2026-06-22 — **F1.2 + F1.3 + F2 + F3 DONE, build-green. Movement-side only — ZERO nav edits** (as scoped).
  Paused before F4 + F5 per RJ.
  • **Approach for all four query-virtual exposures:** keep the C++ virtual, route its base body to a new
    `BP_*`/hook `BlueprintNativeEvent`, default reproduces prior behaviour. ⇒ **Movement+ needed ZERO changes**
    (it keeps overriding the C++ virtuals; no `_Implementation` rename ripple after all).
  • **F1.2** `BypassPathfinding` → `BP_BypassPathfinding`; new `UsesWalkableGroundGate` hook (default true) read
    by the default `QueryReferenceZ` gate. BP flyers bypass A* + snap to terrain-top over obstacles.
  • **F1.3** validator: added a signature-deterministic allowance — a call passes if SeinDeterministic-tagged
    OR every param/return is a deterministic type (auto-permits engine int/bool/name/enum pure ops; still
    flags float/vector). Residual blind spot (deterministic-signature stateful calls e.g. unseeded random) documented.
  • **F2.1** hydrate at instance creation (`USeinMovementSubsystem` friended → calls protected
    `HydrateTuningFromData`) so shape virtuals read correct tuning at plan-time/idle, not just post-OnMoveBegin.
  • **F2.2/F2.3** `GetAltitude`/`GetMinTurnRadius` → `BP_GetAltitude`/`BP_GetMinTurnRadius` (read hydrated vars).
    Turn radius still flows mode → `FSeinPathRequest` → nav (nav untouched).
  • **F3** reverse: `ShouldReverse` hook (default = ShouldAutoReverse 3-gate) latched once at OnMoveBegin into
    `bReversingThisOrder`; loop caps cruise at ReverseTopSpeed + faces away from travel when reversing. Gated by
    `bCanReverse` (default false) → byte-identical for all current units.
  **Editor RESTART required** (new UFUNCTIONs = reflection change; Live Coding won't add them).
  **PIE-PENDING (RJ):** BP flyer (bypass A* + altitude) · BP wheeled corner-rounding · reversing vehicle backs
  into a close rear target WHILE forward-only units stay bit-identical (state-hash) · validator no longer
  false-warns on int/bool nodes.
- 2026-06-22 — **F4 DONE (custom path planning), build-green. Movement-side only — ZERO nav edits**
  (confirmed: `ESeinPathResult` is already `UENUM(BlueprintType)`; `RequestPath`/`FSeinPath`/`FSeinPathRequest`
  are public). Editor had to be closed for the rebuild (new UClass + UFUNCTIONs = structural, not Live-Codeable).
  • New `USeinPlannerHandle` (`Movement/SeinPlannerHandle.{h,cpp}`) — plan-time façade: inputs (start /
    destination / footprint / min-turn-radius); path-builder (Clear / Add Waypoint / Finalize Path /
    Build Straight Line Path / Request Nav Path); result read (count / at); nav probes (Nav Raycast /
    Sample Ground Z). All forwards are to PUBLIC nav / USeinMovement API — no friend needed.
  • `PlanPath(Ctx,OutPath)` is now the sealed dispatcher → `BP_PlanPath` (const BNE). Default
    `BP_PlanPath_Implementation` = `BypassPathfinding ? BuildStraightLinePath : RequestNavPath` — the two
    branches RELOCATED into the handle as the single source (byte-identical default). Reusable
    `CachedPlannerHandle` bound via a localized const_cast (PlanPath is const). Movement+ untouched.
  **PIE-PENDING (RJ):** default modes path identically (no regression); a BP mode overriding Plan Path that
  does Request Nav Path → reads its waypoints → re-emits a modified polyline produces the modified path.
  **Phase F now: F1.2/F1.3/F2/F3/F4 ALL DONE + build-green. Only F5 (determinism hardening) remains — optional.**
- 2026-06-22 — **API cleanup + clarity pass DONE, build-green (two chunks: handles, then movement hooks). RJ
  signed off the rename table + the gate→property idea first; F5 skipped (deemed non-essential).** No functionality
  change — pure renames / property conversions / new convenience nodes / tooltip rewrites / comment slimming.
  • **Renames (DisplayName + C++ symbol where it's a hook; underlying USeinMovement statics kept their dev names —
    the handle wrappers are the BP face, mapping 1:1):** `Compute Desired Speed`→`Compute Speed` (hook symbol
    `ComputeDesiredSpeed`→`ComputeSpeed`); `Kinematic Arrival Speed Cap`→`Get Arrival Speed Cap`; `Compute Adaptive
    Look Ahead`→`Get Adaptive Look Ahead Distance`; `Resolve Look Ahead Point`→`Get Look Ahead Point`; `Resolve Nav
    Collision`→`Clamp To Navigation`; `Should Auto Reverse`→`Get Default Reverse Decision`; `Get Acceptance Radius
    Sq`→`Get Acceptance Radius Squared`; `Get Distance To Final`→`Get Distance To Final Waypoint`; `Sample Ground
    Z`→`Sample Ground Height`. KEPT: `Compute Steer` (tooltip clarifies it returns FINAL facing yaw, not a delta),
    `Tick` (kept over "Tick Movement" — pairs with `Tick Idle`, unambiguous on a Sein Movement object), `Apply
    Avoidance Steer`, `Effective Top Speed`, all plain accessors + planner nodes.
  • **Two flag-hooks → bool properties** (RJ's call on the gate; extended to Bypass for consistency — both are
    per-MODE constants, not per-unit tuning, so a checkbox beats a graph hook): `Uses Walkable Ground Gate` (BNE) →
    **`Snaps To Ground`** `bSnapsToGround` (EditDefaultsOnly bool, default true; read by base `QueryReferenceZ`);
    `BP_BypassPathfinding` (BNE) → **`Bypass Pathfinding`** `bBypassPathfinding` (default false; base
    `BypassPathfinding()` returns it). Movement+ untouched (still overrides the C++ virtuals / `QueryReferenceZ`).
    A conditional-bypass mode is still expressible via a custom `Plan Path`. (Altitude / Min Turn Radius stay HOOKS —
    they read per-unit tuning.)
  • **Gap-fill nodes on `USeinMoverHandle`** (mirroring the planner handle): `Nav Raycast`, `Sample Ground Height`,
    and a non-squared `Get Acceptance Radius` (alongside the squared one).
  • **Arrival braking answer (now in the `Compute Speed` tooltip):** the hook returns CRUISE speed; the loop then
    floors it by the kinematic arrival cap (from `Deceleration`) so the unit always stops cleanly — so Tier-1 tunes
    braking via `Deceleration` (or by returning a lower cruise), but the brake CURVE itself is a Tier-2 job (the
    `Get Arrival Speed Cap` node hands you the same math to apply or skip in a custom Tick).
  • **Every BP-facing tooltip rewritten** to the agreed format (1–3 sentence ELI5, blank line, full detail; plain
    text — no markdown, since UE tooltips don't parse it) across `SeinMovement.h` (13 hooks/props), `SeinMoverHandle.h`
    (~35 nodes), `SeinPlannerHandle.h` (~16 nodes). Verbose internal comments slimmed where low-risk.
  **Editor RESTART required** (renames + new UFUNCTIONs + property changes = reflection, not Live-Codeable).
  **PIE-PENDING (RJ):** spin-test BP still works after the `Compute Steer` signature is unchanged; a flyer authored
  via the new `Bypass Pathfinding` + `Snaps To Ground` checkboxes behaves as the old hooks did; tooltips read well in-graph.
- 2026-06-22 — **Post-signoff rescan + "cheap batch" cleanup DONE, build-green (54 actions).** Three parallel audits
  (movement runtime/API, nav, editor tooling) → a prioritized gap list (below). The low-risk batch landed:
  • **Stale comments fixed (code-over-comments traps):** `ESeinRepathMode::OffPathOnly` no longer claims "deferred/no-op"
    (it's fully built — `SeinMoveToAction`); `CellCost` doc (`SeinNavigationAStar.h`) now states 1..254 IS the terrain
    cost multiplier A* reads (VERIFIED in code: `StepCost = NeighborCost * CellCost`, bake writes `GetTerrainNavCost`)
    — the old "binary, unbuilt" note was wrong; dangling Reeds-Shepp/`FitVehicleCurve` refs softened (`SeinNavigation.h`,
    `SeinNavigationAStar.cpp`, `PluginSettings.h`); module docstrings + `Sein.Show.Steering` help no longer promise
    carrot/tangent viz that doesn't exist (now describe what's actually drawn); `SeinBasicUnitMovement.h` brief now says
    "marker class, loop lives in base"; dropped a retired-doc ref (`API_Cleanup_Pass.md`).
  • **Dead output made live:** `FSeinPath::TotalCost` (was always 0) now = planar world-space path length, summed in
    `DeriveSegmentsFromWaypoints` (covers nav + planner-handle + straight-line paths); `Get Path Cost`/`Get Path Length`
    BPFL docs clarify length-vs-count.
  • **Log hygiene:** removed the always-on 1/sec `LogTemp "PATH VIZ DRAW"` spam; demoted the 0-segment-vehicle
    diagnostic to `Verbose`.
  • **Robustness/consistency:** `Set Current Waypoint Index` now clamps to the path range (was unclamped → could steer
    to world origin); `Get Arrival Speed Cap` tooltip documents the decel≤0 "no-cap" sentinel; **`USeinPlannerHandle`
    tagged `SeinDeterministic`** so the determinism validator stops false-warning on `BP_PlanPath` graphs (matches the
    Mover handle).
  **Held back (NOT cleanup — net-new code):** the `MovementClass`-resolves validator.
- 2026-06-22 — **Backlog #1 (handle completeness + slope-tilt) + #2 (steering viz) DONE, build-green.** Both
  movement-side; shipped Basic/BasicUnit byte-identical (the slope step was extracted, not changed).
  • **#1 Mover-handle completeness:** added `Get Min Turn Radius` / `Get Footprint Radius` / `Get Altitude` reads to
    `USeinMoverHandle` (a steering Tick can now read its own mode shape, not just the Planner handle), forwarding to the
    owner virtuals + `ResolveCollisionRadius` (the mover context already carries World/SelfHandle/NavData).
  • **#1 slope-smoothing fix (the hole in the shipped API):** extracted the loop's slope tilt + 60°/sec pitch/roll
    smoothing into a shared `USeinMovement::ApplySlopeTilt(Pos, Yaw, MovementData, Nav, dt)` helper; the default loop now
    calls it (byte-identical); exposed it as the `Apply Slope Tilt` handle node so a Tier-2 `BP_Tick` that sets rotation
    keeps the cross-tick smoothing instead of popping on slopes.
  • **#2 steering viz:** delivered as **BP-callable debug-draw nodes** on the Mover handle — `Draw Debug Line / Arrow /
    Sphere / Circle` (gated `UE_ENABLE_DEBUG_DRAWING`, no-op + zero-cost in shipping, never touch sim state). An author
    SEES what their graph computes (carrot, steer vector, turn-radius circle). Chose this over a built-in carrot/arc in
    `DrawSteeringDebugViz`: that viz is a static with only component data — a built-in arc would need the debug ticker to
    resolve each unit's mode instance, and the base loop has no carrot anyway; the node is general (any mode draws its
    own). A built-in `DrawSteeringDebugViz` min-turn arc remains an optional future nicety.
  **Editor RESTART required** (new UFUNCTIONs). **PIE-PENDING (RJ):** a Tier-2 BP_Tick using Apply Slope Tilt sits flat
  on slopes (no pop); Draw Debug nodes render from a BP_Tick; shipped Basic/BasicUnit unchanged (state-hash).
- 2026-06-22 — **Backlog: determinism-validator hardening + GUID-stable tuning DONE, build-green (62 actions).** Both
  editor-side (SeinARTSEditor); no runtime/sim change.
  • **GUID-stable tuning** (`SeinMovementTuningExport.cpp`): `SyncFields` now matches BP var → UDS field by the source
    BP-var GUID stamped in each field's metadata (`SeinSourceVarGuid`), not by friendly name. Renaming a tuning var now
    RENAMES its UDS field — preserving every per-unit authored value (UDS instances serialize by the field's own GUID,
    which a rename keeps) — instead of drop + re-add. Legacy fields are adopted by name on the first sync, then
    GUID-tracked. (Caveat: a rename done BEFORE the first post-update sync — before any field is stamped — still can't be
    recognized; one-time migration boundary only.)
  • **Validator hardening** (`SeinMovementDeterminismValidator.cpp`): (a) recurses into **macro instances** (their
    graphs live in other assets — the direct walk missed them; cycle-guarded); (b) a **denylist** flags engine RNG with
    a deterministic signature (`RandomInteger` / `RandomIntegerInRange` / `RandomBool` / `RandRange` / …) that the
    signature gate wrongly passed; (c) **opt-in escalate-to-error** — new `USeinARTSCoreSettings::bMovementDeterminismIsError`
    (Project Settings → SeinARTS → Movement, default off) makes findings blocking `AssetFails` errors instead of warnings.
    Verified FFixedPoint carries `SeinDeterministic`, so fixed-point math + handle nodes already pass clean — a broad
    BPFL-tagging sweep to kill the residual tag/handle-BPFL false-positives was NOT done (different module; error mode is
    opt-in and assumes clean graphs).
  **Editor RESTART required** (new setting UPROPERTY + validator reflection). **PIE/editor-PENDING (RJ):** rename a
  tuning var + Generate → per-unit values survive; a macro hiding a non-det call now warns; toggling the setting makes
  it a blocking Data Validation error.

- 2026-06-23 — **Backlog: nav BP-query parity DONE, build-green.** Verified first (RJ doubted the gap): projection IS
  already exposed — `USeinFormation::ProjectToNavigable` / `ProjectPositionsToNavigable` (BP, SeinDeterministic). The
  genuine gaps were `GetTerrainTypeAt` / `IsWorldPositionClear` / `GetCellSize` (exposed nowhere) and `GetCellHeightAt`
  (only on the movement handles, not the nav BPFL). Added:
  • `USeinNavigationBPFL`: Get Cell Height At, Get Terrain Type At, Is Position Clear, Get Cell Size (forward to the
    active nav via the subsystem); ALL its tooltips rewritten to the ELI5-then-detail format.
  • `USeinPlannerHandle`: Project To Nav (raw nav snap for waypoint-building) + Get Terrain Type At / Is Position Clear /
    Get Cell Size.
  • `USeinMoverHandle`: Get Terrain Type At + Is Position Clear (parity; it already had Nav Raycast + Sample Ground Height).
  Nav class stays C++-only (queries exposed, not the pathfinder). Determinism-clean (read-only fixed-point/int).
  **Get Terrain Tag At** QoL node added on all three surfaces (index→tag via `USeinARTSCoreSettings::GetTerrainTag`;
  `GameplayTags` promoted to a public dep of SeinARTSMovement since the handle headers now expose `FGameplayTag`).
  **Also started `STYLE_GUIDE.md`**
  (project root): tooltip format + naming + BP-exposure + determinism + settings + comment conventions. **Editor RESTART
  required** (new UFUNCTIONs).

- 2026-06-23 — **Authoring/tuning gap pass: tuning ergonomics (#1 + #3) + mode render cue (#2) DONE, build-green.**
  • **#1 Tuning metadata propagation** (`SeinMovementTuningExport.cpp`): the exporter now copies a whitelist of the BP
    tuning var's display/validation metadata (tooltip / ClampMin / ClampMax / UIMin / UIMax / Units / Category) onto its
    UDS field, so the per-unit MovementClassData editor shows help, clamps, slider ranges, units, and grouping instead of
    bare fields. Applied to matched + new fields via a shared `ApplyFieldMetaAndDefault`; stale keys cleared.
  • **#3 Default-value re-sync**: the BP var's default is re-applied to the UDS field on every Generate (was
    creation-only), gated on actual change — the BP var is authoritative for the tool-managed UDS. (Caveat: a per-unit
    instance sitting at the OLD default follows the new one — correct for "didn't override," inherent to UDS.)
  • **#2 Mode render cue** — `Emit Movement Cue` on the Mover Handle: a mode emits a one-way sim→render cue (skid / dust /
    rev) from its Tick. Non-prescriptive: a generic `ESeinVisualEventType::MovementCue` (appended, no value shift)
    discriminated by a designer-chosen `Tag`; the framework enumerates no cues. Fires at the unit's location with a free
    `Value` payload via `World->EnqueueVisualEvent`. Deterministic (fixed-point, in-Tick, whitelisted on the handle).
  **HELD (RJ):** #4 authoring scaffold + in-editor test + `MovementClass`-resolves validator; #5 authoring guide/example.
  **Lower/deferred (explained):** fixed-point curve tuning (new core primitive + editor — big lift); extensible custom
  anim-state output (overlaps #2's render channel); a Tier-1.5 `Compute Arrival Speed Cap` hook (cheap, if arrival feel
  becomes a tuning target). **Editor RESTART required** (enum + new UFUNCTIONs).

- 2026-06-23 — **#4c `MovementClass`-resolves validator DONE, build-green.** New `USeinMovementClassValidator`
  (`UEditorValidatorBase`, auto-registered) over `ASeinActor` BPs: walks the entity bridge's `ComponentData` for
  `FSeinMovementComponent` and warns when a set `MovementClass` won't load (stale path / stripped plugin), is abstract,
  or isn't a `USeinMovement` subclass — all of which silently fall back to Basic at runtime. Warnings only; `USeinMovement`
  resolved by path (no Movement link dep). Editor restart to register. RJ: **held 4a** (scaffold — fine hintless in beta,
  docs site will cover movement authoring), **killed 4b** (no in-editor test harness), **held #5**.
  **Deferred lower trio scoped (priority / difficulty):** (1) Tier-1.5 `Compute Arrival Speed Cap` hook — **SMALL**,
  closes custom-brake-without-owning-Tick (one BNE hook, default = the kinematic formula); (2) extensible render/anim
  state channel — **MEDIUM**, bespoke modes (bank/tread/gait), keep it non-hashed render-only, synergizes with the
  `Emit Movement Cue` channel; (3) fixed-point curve tuning (`FFixedCurve` + editor) — **LARGE**, deepest vehicle-feel
  lever, interim = array-of-(input,output) fixed pairs + deterministic `Sample` (SMALL–MEDIUM, ~80% of the value).

- 2026-06-23 — **Deferred lower trio DONE, build-green (all three).**
  • **#1 Tier-1.5 arrival hook** — `Compute Arrival Speed Cap` (BNE) on `USeinMovement`; the default loop now floors
    cruise via this hook (default = the kinematic formula from `Deceleration` → byte-identical for shipped modes).
    Override to reshape JUST the brake (hard stop / two-stage / creep) without owning the whole Tick.
  • **#2 Render/anim state channel** — `Set Render Value(Slot, Value)` on the Mover Handle writes indexed fixed-point
    render slots (`FSeinMovementComponent::RenderState`, 64-slot cap), read via `Get Movement Render Value` BPFL (as
    float). RENDER-ONLY: deliberately NOT in the deterministic state hash (cosmetic output the mode computes
    deterministically). Indexed (not named) to stay hash-safe — FName isn't machine-stable. Continuous-state sibling to
    `Emit Movement Cue`.
  • **#3 Fixed-point curve tuning (interim array form)** — new `FFixedCurve` (SeinARTSCore, `SeinDeterministic`) = a
    sorted array of (X=input, Y=output) `FFixedVector2D` keys + a deterministic piecewise-linear `Sample`; `Sample Fixed
    Curve` BPFL on `UMathBPFL` (whitelisted). Because it's `SeinDeterministic` + `BlueprintType`, a designer can make it
    a tuning variable → it exports to the UDS → per-unit-editable as an array of key pairs, sampled in the mode graph.
    Edited as a plain key array (no curve-editor canvas yet — the ~80% interim; a Slate widget can layer on later).
  **Editor RESTART required** (new USTRUCT + UFUNCTIONs).

- 2026-06-23 — **Point-2 fix (Tier-2 state determinism) + native curve tuning DONE, build-green.**
  • **Point 2 — member-variable determinism check.** The shared `USeinBlueprintDeterminismValidator` now also walks the
    BP's `NewVariables` and warns on any **non-deterministic-typed member variable** — the call-walk only caught
    non-deterministic CALLS, not STATE. A mode instance persists per-unit in the sim, so a loose float/vector/object
    member is the desync footgun; tuning vars (deterministic-typed, hydrated) pass. Shared by movement + formation;
    respects the opt-in escalate-to-error.
  • **Native curve tuning.** `FFixedCurve` rewritten to wrap a native `FRuntimeFloatCurve` (full curve-editor UX —
    points / tangents / interp modes); `Sample` evaluates it DETERMINISTICALLY in fixed-point from the authored keys
    (Constant / Linear / Cubic matched via FRichCurve's Hermite basis). Lockstep-safe: curve data is authored content
    (identical on all clients), `FromFloat` is assert-free, and the eval is fixed-point — the same "editor-authored →
    deterministic at runtime" pattern as cover-slot scatter. Relocated SeinARTSCore → SeinARTSCoreEntity (FRuntimeFloatCurve
    needs Engine). Verified the state-hash guard DROPS-with-a-dev-warning (never asserts) on the curve's float keys, and
    authored content can't diverge — safe. `Sample Fixed Curve` BPFL unchanged.
  **Editor RESTART required.**

### Rescan backlog (prioritized, 2026-06-22) — gaps surfaced by the audit, NOT yet done
- **[DONE 2026-06-22] Mover-handle completeness + slope-smoothing node.** Added the three reads + `Apply Slope Tilt`
  (shared helper extracted from the loop). See the execution-log entry above.
- **[DONE 2026-06-22] Steering debug viz** — delivered as BP-callable `Draw Debug Line/Arrow/Sphere/Circle` nodes on
  the Mover handle (author draws their own carrot/steer/turn-radius). A built-in `DrawSteeringDebugViz` min-turn arc
  (would need the debug ticker to resolve each unit's mode instance) remains an optional future nicety. See above.
- **[DONE 2026-06-22] Determinism validator hardening** — macro-instance recursion + RNG denylist + opt-in
  escalate-to-error (`bMovementDeterminismIsError`). Residual: broad BPFL-tagging sweep for tag/handle-BPFL
  false-positives (left to a CoreEntity-side pass; FFixedPoint already tagged so the common cases pass). See above.
- **[DONE 2026-06-22] GUID-stable tuning reconciliation** — `SyncFields` matches by stamped source-var GUID; rename
  now preserves per-unit values. See the execution-log entry above.
- **[small] `MovementClass`-resolves validator** — a 2nd `UEditorValidatorBase` over unit BPs warning when
  `MovementClass` fails to load / is abstract / has empty-or-mismatched `MovementClassData` (today: silent fallback to
  Basic). High designer value.
- **[DONE 2026-06-23] Nav BP-query parity** — added Get Terrain Type At / Is Position Clear / Get Cell Size / Get Cell
  Height At to the nav BPFL + planner/mover handles, plus Project To Nav on the planner handle; nav class stays C++-only.
  (Projection was already exposed via `USeinFormation`; world-distance path length shipped earlier as Get Path Cost.)
  See execution-log above.
- **[large] Nav dynamic-cost / weighted-region API** — runtime per-region cost layered over baked `CellCost`,
  re-read by A* (threat avoidance / influence maps / dynamic road preference). Biggest genuinely-missing nav capability;
  natural home for any future hierarchical pathing (async is off the table by design).
- **[medium] Authoring scaffold + in-editor "test this mode".** "Create Movement Mode" yields a blank BP; seed an
  example override graph (or one-click "add Compute Steer override") and a preview/test affordance to kill the cold-start
  cliff and the author-a-whole-unit-then-PIE ritual.
- Smaller/rough (present-but-rough): no single "tick every frame regardless of order" hook (hover-bob authored twice in
  `BP_Tick` + `BP_TickIdle`); `IsReachable` ignores `BlockedTerrainTags` (reachability vs path can disagree);
  `MoveToProxy` has no self-Cancel verb; `IsPlacementValid` base default is center-only (custom-nav trap).
