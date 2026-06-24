# SeinARTS — Balance Table Generator (Source Plan)

**Date:** 2026-06-23 · **Status:** active — design locked (forks + feasibility verified), pre-build ·
**Scope:** an editor tool that collects tunable fields off a designer-chosen set of entity classes
into one flat, CSV-friendly DataTable, edited in a grid, and pushed back into the source Blueprints.

## 0. How to use this document
Self-contained source of truth for the balance-table pass. A fresh session can execute from *this file
+ the code*. Line citations are **starting coordinates** — re-ground against live code before editing.
Obeys the project-root guardrails (**no worktrees**; **git is RJ's** — disk only; **build via the
PowerShell tool then read the log**, not just the exit code; **determinism is sacred**;
**destination-preview-is-sacred** untouched — this feature never touches sim or movement code paths).

## 1. Goal — the desired end state
A designer points a **balance profile** at a class hierarchy (e.g. "all Units, minus these two"),
optionally narrows it to the components/fields they care about, and clicks **Gather**. Out comes a
single **flat `UDataTable`** — one row per unit, one column per tunable stat — that they edit directly
in the grid *or* export to CSV, balance in a spreadsheet, and re-import. **Push** writes the edited
values back into the source Blueprints. That is the whole loop: *scope → gather → tune → push.*

The **end state** the v1 build must stay compatible with (not necessarily implement up front):

- **All tunable surfaces, not just top-level component fields.** Columns can also come from
  **nested sub-data** (e.g. `FSeinMovementComponent::MovementClassData`, an `FInstancedStruct`),
  from **abilities** (production **cost** lives on the ability's `ResourceCost`, *not*
  `FSeinProducibleComponent` — so a true cost column reaches into abilities), and from **identity**
  (`DisplayName` / `IdentityTag` as read-only label/grouping columns).
- **Focused tables via multiple profiles.** Because each profile chooses its own component set, a
  designer makes *one profile per concern* — "vehicle movement stats", "infantry combat stats" — each
  yielding a dense, low-sparsity table. This is how the single-union-table model scales without a
  per-component-table mode (see §2).
- **Robust, non-destructive round-trip.** Regenerating after a field rename/add must preserve unrelated
  edited values; the tool surfaces **drift** (table vs source out of sync), name collisions, and
  per-column provenance.
- **Designer-first & BP-aware.** Native *and* designer-authored UDS components are first-class targets
  (one reflection path covers both). Consistent with [[prefer-bp-exposure-with-utility]].
- **Optional future: runtime authority.** v1 is write-back (the Blueprints stay the source of truth).
  The architecture must not *preclude* a later flagged mode where entities read tuning from the table
  at spawn, making it the shippable/hot-patchable balance artifact (see §5.4) — but that is a non-goal
  now.

## 2. Decisions locked (RJ, 2026-06-23)
- **Round-trip = write-back to Blueprints.** The table is a bulk **editing view**, not runtime data.
  *Gather* pulls authored `ComponentData` in; *Push* writes edited values back into each unit Blueprint
  and re-saves. No new runtime/determinism surface; "the Blueprint **is** the unit" stays true.
  Snapshot-only and runtime-authority were both considered and declined for v1 (runtime authority kept
  alive as a future mode only, §5.4).
- **Surface = flat-column `UDataTable` with a synthesized UDS row struct.** Verified feasible — see §3
  and [[datatable-row-struct-uds]]. Gives the flat grid **and** CSV import/export for free.
- **One union table per profile.** Columns are the union of the profile's tracked fields, namespaced
  `Component.Field`. Units lacking a component get default cells (sparse but honest). Sparsity is
  managed by *scoping profiles*, not by splitting into per-component tables. (Revisit only if a real
  profile proves unavoidably sparse.)
- **v1 scope boundary (deferred, not abandoned — see §6 Phase E+):** top-level fields of tracked
  components only. **Out of v1:** nested sub-data (MovementClassData), ability-derived columns
  (cost/cooldown), veterancy/effect modifiers. The column-provider spine (§4) is built so these slot in
  without reshaping anything.

## 3. What already exists (do not rebuild)
- **The authoring surface to read/write.** `USeinEntityComponent::ComponentData` — a
  `TArray<FInstancedStruct>` on the entity bridge (`SeinEntityComponent.h`, ~`:164`). At spawn
  `InjectAuthoredComponents` walks it and copies each entry into reflection-backed storage
  (`SeinEntityComponent.cpp:51-114`). **Confirms the CDO bridge's `ComponentData` is the canonical
  read/write target** — Gather reads it, Push writes it. Native and UDS entries are handled identically.
- **UDS field-synthesis with stable identity — ~80% of the engine we need.**
  `SeinMovementTuningExport.{h,cpp}` (`Source/SeinARTSEditor/Private/Util/`) builds/syncs a UDS from a
  source schema, mapping fields by a stamped **source GUID** so renames don't orphan authored values
  (uses `FStructureEditorUtils::AddVariable`/retype/rename/remove). Generalize from "one BP's variables"
  to "the union of N component structs"; swap the `SeinSourceVarGuid` stamp for a structured
  **column-source descriptor** (§4). Precedent path: [[movement-mode-bp-authoring]].
- **Component eligibility filter.** `SeinComponentEligibility::IsEntityComponentStruct` (used by the
  `ComponentData` picker / `SeinComponentNodeMenuCache`) is the shared rule for "is this a valid Sein
  component struct" (native `FSeinComponent` subclass *or* `SeinEntityComponent`-meta UDS). Reuse it to
  filter the `TrackedComponents` picker.
- **Subclass enumeration (incl. unloaded).** `SeinFactionService.cpp:70-86` —
  `IAssetRegistry::GetAssetsByClass(Class->GetClassPathName(), Out, /*bSearchSubClasses*/ true)`. This
  is the "opt in a parent, all children appear" mechanism.
- **Fixed-point editing seam.** `FFixedPoint` stores `int64 Value` (32.32); `FromFloat`/`ToFloat` are
  flagged non-deterministic, editor-only (`FixedPoint.h`). `FSeinFixedPointDetails`
  (`Source/SeinARTSEditor/Public/Details/`) already presents fixed-point as a **decimal field**. So
  `FFixedPoint` columns are tuned as plain numbers and stored fixed — **no new determinism work.**
- **Editor-tooling conventions to mirror.** Factory = `USeinMovementModeFactory` (`UFactory`,
  `GetMenuCategories` → `FSeinARTSEditorModule::GetAssetCategoryBit`). AssetTypeActions registered in
  `SeinARTSEditorModule.cpp` (`RegisterAssetTypeActions` + `RegisterAdvancedAssetCategory`).
  **Action buttons = `IDetailCustomization`** (the house pattern — see `FSeinMovementModeDetails`,
  `AddCustomRow` + `SButton`, registered via `PropertyModule.RegisterCustomClassLayout`), *not*
  `CallInEditor`. Validators subclass `UEditorValidatorBase` (auto-discovered). Icons via
  `FSeinARTSEditorStyle` (`ClassIcon.X`/`ClassThumbnail.X` + `Resources/BrandKit` PNGs).
- **Engine fact (verified in UE 5.7 source) — the feasibility unblocker.** A `UDataTable`'s `RowStruct`
  is a plain `TObjectPtr<UScriptStruct>` and does **not** need to inherit `FTableRowBase`:
  `FDataTableEditorUtils::IsValidTableStruct` (`DataTableEditorUtils.cpp:918-928`) accepts
  `IsChildOf(FTableRowBase) || IsA<UUserDefinedStruct>()`. The "must inherit FTableRowBase" line in
  `DataTable.h:92` is a stale comment. `AddRow(FName, const uint8*, const UScriptStruct*)` /
  `CreateTableFromRawData(TMap<FName,const uint8*>, UScriptStruct*)` (`DataTable.h:287,344`) populate
  from any struct; the editor renders UDS rows as flat columns (`DataTableEditor.cpp:995`) and CSV/JSON
  iterate the row's `FProperty`s generically (`GetColumnTitles`/`GetTableData`/`GetTablePropertyArray`).
  Full detail in [[datatable-row-struct-uds]].

## 4. Target architecture

**The config asset — `USeinBalanceProfile : public UDataAsset`** (runtime module; all generation logic
`WITH_EDITOR`-guarded, like `ASeinLevelVolume`'s bake). Plain `UDataAsset`, not `PrimaryDataAsset` —
matches `USeinFaction`/`USeinInputConfig`; the asset is never read at runtime, so Asset-Manager
registration buys nothing (trivial to promote later if wanted). Name is a proposal — open to a rename.
Category `SeinARTS|Balance`; DisplayName drops the `Sein` prefix per house rules.

```
// Targeting
TArray<TSoftClassPtr<ASeinActor>> IncludedRoots     // parent → all descendants (registry walk)
TArray<TSoftClassPtr<ASeinActor>> ExcludedClasses    // exclude class + its branch
bool bIncludeAbstract = false                        // skip abstract bridges by default
// Tracking
TArray<TObjectPtr<UScriptStruct>> TrackedComponents  // eligibility-filtered picker; empty = all present
// Output
FDirectoryPath OutputDir                             // optional; default = this asset's folder
TSoftObjectPtr<UDataTable> GeneratedTable            // filled on first Gather
```

`TObjectPtr<UScriptStruct>` references **both** UDS (asset) and native structs (`/Script/...` path)
cleanly — one field type covers both target kinds.

**The spine: a column-provider abstraction.** Everything routes through providers so the end-state
surfaces (§1) slot in without reshaping Gather/Push:

```
ISeinBalanceColumnProvider
  DescribeColumns(targets)        -> [FSeinBalanceColumnDesc]   // namespaced name + source descriptor + UE field type
  ReadCell(target, columnDesc)    -> raw value                  // for Gather (read CDO ComponentData)
  WriteCell(target, columnDesc, value)                          // for Push  (write CDO ComponentData)

FSeinBalanceColumnDesc.Source = { Kind, ... }   // Kind ∈ { Component, Identity, NestedComponent, Ability, Computed }
```

v1 ships **Component** (top-level deterministic fields of tracked components) and **Identity**
(`DisplayName`/`IdentityTag`, read-only-ish labels). The `Kind` discriminator + structured source
descriptor is the **one decision that keeps the end state reachable** — Phase E adds `NestedComponent`
/ `Ability` providers and nothing else changes.

**Row-UDS synthesis.** `DescribeColumns` across all providers → the union column set → synthesize/sync
one `UUserDefinedStruct` (generalized `SeinMovementTuningExport` engine): one UDS field per column,
authored name = namespaced `Component.Field`, **field type cloned from the source `FProperty`** (so
`FFixedPoint` stays `FFixedPoint` → inherits the decimal editor + determinism), each field **stamped
with its `FSeinBalanceColumnDesc.Source`** (replacing `SeinSourceVarGuid`). Set the UDS as the
DataTable's `RowStruct`; create the table in `OutputDir` (or the profile's folder).

**Gather (Pull) data flow:** resolve targets (registry subclass walk − exclusions − abstract) → sync
the row UDS → for each target, `provider.ReadCell` over every column from the **CDO bridge
`ComponentData`** → `AddRow`. Missing component ⇒ default cell (sparse). Row key = a stable handle back
to the Blueprint (see §5.2).

**Push (Apply) data flow:** for each row → resolve the source Blueprint from the row key → for each
column, decode its `Source` and `provider.WriteCell` into the BP CDO's `ComponentData` → mark dirty +
save. Conflicts (missing component, duplicate-of-type, stale/ambiguous key) are collected and reported,
never silently applied. (§5.1 is the gating risk for this step.)

**Determinism:** columns are restricted to deterministic field types (non-deterministic fields are
never offered as tracked columns — they can't be safely tuned anyway). Nothing new enters the sim.

## 5. Risks & behavior-critical questions (what the plan must nail)

**5.1 Write-back persistence to BP CDOs — the #1 gating spike (prove before building Push).**
Authored `ComponentData` on a Blueprint *subclass*: confirm exactly where it lives (CDO subobject vs
SCS template), and that writing the value + marking dirty + saving **persists to the `.uasset` and
propagates to existing instances** (and survives a recompile). Precedent: `SeinMovementTuningExport`
already mutates BP assets safely (it stamps the CDO's tuning `UScriptStruct*`). Phase C **must** open
with a one-field/one-unit spike that round-trips through save+reload+PIE before the batch path is built.

**5.2 Row key ↔ Blueprint mapping.** Must be stable *and* readable. Proposal: row `FName` = asset name
(e.g. `SU_BasicUnit1`); store the full soft class path for write-back (aux column or a side map on the
profile); warn on cross-folder asset-name collisions.

**5.3 Secondary correctness traps.** Duplicate components of the same struct type on one entity make
column→entry mapping ambiguous → detect and warn (v1 assumes one-per-type). Non-deterministic tracked
field → excluded with a notice. Drift (someone edits the BP after Gather) → Phase D staleness check.

**5.4 Runtime-authority — explicitly deferred, deliberately not foreclosed.** If the project ever wants
the table to be the shipped balance source, the write-back design already produces a correct table;
you'd add a flagged spawn-time apply that reads it (loaded deterministically like `RegisteredFactions`)
and overrides `ComponentData`. This is a *future mode*, not v1 — recorded so a later pivot is informed.

## 6. Phase ladder (each gate = build-green + an in-editor/PIE checkpoint)

- **Phase A — Profile asset + targeting + action skeleton.** ✅ **Build-green 2026-06-23** (both modules
  relinked clean); in-editor gate pending RJ. `USeinBalanceProfile` (runtime `SeinARTSCoreEntity`,
  `Balance/`) + `USeinBalanceProfileFactory` + `FAssetTypeActions_SeinBalanceProfile`; registry subclass
  walk via `GetDerivedClassNames` resolving `IncludedRoots − ExcludedClasses(+subtree) − abstract`
  (`ResolveTargetClasses`); `FSeinBalanceProfileDetails : IDetailCustomization` with **Preview** (live)
  + **Gather**/**Push** (honest no-op stubs); registered in `SeinARTSEditorModule`.
  **Deviations (noted in-code):** `TrackedComponents` uses `meta=(MetaStruct=FSeinComponent)` for now
  (native components; the eligibility-filtered viewer incl. designer UDS lands with Phase B's columns),
  and asset categories are plain `Targeting`/`Tracking`/`Output` (no redundant `SeinARTS|Balance|`
  prefix, per the settings-lean rule). *Gate (in-editor, pending):* create a profile, set an Included
  Root → Preview lists exactly the right unit BPs; Gather/Push appear as no-ops.
- **Phase B — Row-UDS synthesis + Gather.** ✅ **Build-green 2026-06-23** (first-pass; both modules
  relinked); in-editor gate pending RJ. `ISeinBalanceColumnProvider` spine (`SeinBalanceColumn.h`) with
  `FComponentColumnProvider` + `FIdentityColumnProvider`; `SeinBalanceTableExport::GatherToTable`
  synthesizes a row UDS (source-stamped fields, rename-safe sync via the tuning-export idiom; field
  types cloned from each source `FProperty` via `ConvertPropertyToPinType`, except **FFixedPoint → `float`
  columns** for grid/CSV readability — converted fixed↔float at gather/push), sets it as the table
  `RowStruct`, and populates from each target CDO bridge's `ComponentData`. **Re-Gather = destructive
  regenerate** (RJ 2026-06-23): a modal warns, then it resolves the existing assets, empties the table,
  rewrites the row-UDS schema, and repopulates from source (discarding manual in-table edits). The
  in-place `CleanBefore`/`RestoreAfterStructChange` migration was abandoned — mutating a UDS whose
  DataTable rows are still live froze, then crashed, the editor (see the memory gotcha).
  **Notes:** Gather emits TWO assets — `DT_<Profile>` + paired `<Profile>_Row` UDS (a DataTable RowStruct
  must be a persistent UScriptStruct). Designer **UDS components ARE gathered in track-all mode** (the
  eligibility filter runs over the CDO's real ComponentData); only *explicitly* listing a UDS in
  TrackedComponents still awaits the eligibility-filtered viewer. Assets left dirty for the user to save.
  *Gate:* first Gather CONFIRMED in-editor 2026-06-23 (correct flat `Component_Field` columns + per-unit
  values, e.g. TopSpeed 500); re-Gather CONFIRMED safe (modal-guarded destructive rebuild — no freeze/crash).
  Float-column readability + CSV round-trip are build-green, pending a final in-editor eyeball.
- **Phase C — Write-back spike → Push.** *Gate 0:* the §5.1 one-field spike persists through
  save/reload/PIE. Then Push over all rows/columns via providers, with conflict reporting. *Gate:* edit
  a cell → Push → the unit BP's authored `ComponentData` shows the new value in-editor **and** at spawn.
- **Phase D — Polish.** Asset icon/thumbnail ✅ (landed early in Phase A — `SeinDataIcon` 16/92);
  `USeinBalanceProfileValidator :
  UEditorValidatorBase` (excluded ⊄ included, empty targets, bad output path, non-deterministic tracked
  field, duplicate component types); CSV ergonomics; **drift/staleness** indicator in Details. *Gate:* a
  misconfigured profile flags in Data Validation; icon shows in the content browser.
- **Phase E+ — End-state expansions (post-v1 roadmap; the spine already supports these).**
  - **E1 — Nested sub-data columns.** `NestedComponentColumnProvider` descends into `FInstancedStruct`
    sub-data (e.g. `MovementClassData`).
  - **E2 — Ability-derived columns.** `AbilityColumnProvider` surfaces production **cost**
    (`ResourceCost`), cooldowns, etc. via the entity's `FSeinAbilityComponent` → ability assets.
  - **E3 — Advanced round-trip.** Per-cell provenance, conflict-resolution UX, multi-profile management.
  - **E4 — (optional) Runtime-authority mode.** The flagged spawn-time apply of §5.4, only if wanted.

## 7. Naming & conventions checklist (write these before the body — [[node-names-must-match-scope]])
- USTRUCT `FSeinBalanceColumnDesc`; UObject config `USeinBalanceProfile`; validator
  `USeinBalanceProfileValidator`; provider interface `ISeinBalanceColumnProvider`. Editor-only types in
  `SeinARTSEditor`; the config `UCLASS` in a runtime module with `WITH_EDITOR` gen.
- BP-visible `Category = "SeinARTS|Balance"`; `DisplayName` drops the `Sein` prefix; settings (if any)
  stay lean under the shared SeinARTS page, no extension page ([[settings-org-lean]]).
- The two buttons do **exactly** what their names say — **Gather** only reads into the table, **Push**
  only writes back. No hidden "gather also re-saves Blueprints" scope creep.
