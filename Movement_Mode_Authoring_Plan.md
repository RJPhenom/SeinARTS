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
