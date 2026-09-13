# Construction and entity presentation

Implementation contract, 2026-09-12. Lifecycle and work fields live on the existing Sein Construction component. The separate work authoring component and payload have been removed at RJ's direction.

## Construction gameplay

An entity keeps the same actor and identity from placement through completion. Sein Construction supplies lifecycle data; it does not assume a timer, HP rule, worker scheduler, or visual class.

**Start Queued for Construction** defaults off. A level-placed or normally spawned entity starts Complete without applying a completion effect. Enabling the setting starts an unfinished Queued site waiting for work. Spawn Construction Site explicitly initializes Queued or Building before passive abilities and spawn observers. Injection Enabled still controls whether the authoring component contributes its payload at all.

| Node | Contract |
|---|---|
| Queue Construction | Mark an existing entity unfinished and return a job handle. Repeated queue on an unfinished site preserves the current job. |
| Start Construction | Queued or Paused becomes Building. |
| Pause Construction | Building becomes Paused; work and stage are retained. |
| Complete Construction | Complete any unfinished job when the calling ability's rules permit it. Apply its captured completion effect once and release only the framework-owned UnderConstruction grant. |
| Set Construction Stage | Set or clear a designer milestone tag independently of lifecycle phase. This does not grant an entity tag. |
| Get Construction Status | Read Valid, Construction Handle, State and Stage. |

Mutations require simulation authority. Queue takes an entity; subsequent operations take the returned handle, containing entity generation and a monotonic job ID. Old worker actions cannot change a replacement job on the same entity. Complete is latched until a new Queue operation: damage after completion does not silently reopen construction.

Completion Effect is captured when queued. Runtime completion uses the normal effect pipeline; it schedules the effect for the next PreTick. Independent UnderConstruction tag grants survive completion. Placement graphs must use the construction API instead of manually granting that tag.

Games choose their own completion rules in abilities: accumulate HP and complete at a chosen threshold; track materials; combine work with HP; pause on damage; or use a timer. Health, damage, repair, refunds and cancellation remain game-authored. Presentation callbacks never supply simulation authority.

## Work fields on Sein Construction

**Sein Construction** contains Required Work, Progress and Job Required Work alongside lifecycle and stage. Required Work defaults to 10 and uses units chosen by the build ability. Queue captures it as Job Required Work and resets Progress for the new job. Repeated Queue/Start preserves the active job; editing Required Work affects the next job.

**Add Construction Work** accepts positive fixed-point work while Building and clamps to the captured requirement. It returns **Work Complete** without changing lifecycle phase. **Get Construction Work Status** reports Valid, Progress, Required Work, Percent and Work Complete. Its Valid output means a job has been queued, including a retained completed job. **Get Construction Work Percent** is the fill query.

Complete Construction accepts any unfinished current job without requiring or filling work. For a separate construction bar, use these work fields and complete at the threshold. For HP-based construction, ignore them, add to the game's Health field in the build ability, and call Complete Construction when the game's HP rule is satisfied. There is no additional component or opt-in flag to configure.

The serialized ReadyToComplete result identifier displays as Work Complete. The old lifecycle enumerator is hidden and never entered. Force Complete is a deprecated alias of Complete; older Add Construction Progress/Finish Construction nodes remain deprecated adapters. TimeToCompletion is a hidden legacy authoring value; Required Work is the current field.

## Entity binding and presentation

**Sein Entity Binding** binds an arbitrary actor explicitly to an entity actor. On an entity actor it binds to itself. Get Entity Handle and Get Entity Actor avoid assumptions about attachment hierarchy. Binding subscribes before reading current state; it refreshes after initialization and save restoration even when lifecycle/stage values do not change. Rebind and teardown remove old subscriptions.

**Sein Entity Presentation** controls named groups of explicitly assigned scene components. Each member must belong to one group within the component. Children are not inferred; visibility changes do not propagate. Components outside the groups, including widgets, are untouched. Original component visibility is restored when the binding is released.

A group may optionally specify any Actor class. Its Owner is the entity actor, and native Entity Binding components receive context before BeginPlay; an explicit context component is injected when none exists. Blueprint-created binding components adopt that context before their BeginPlay consumers run. The managed actor attaches to the presentation owner's root. Hiding the group, rebinding, entity destruction or parent EndPlay destroys it. No particular Blueprint superclass or interface is required. Managed creation and destruction tolerate Blueprint callbacks that refresh, rebind or tear down presentation.

**Sein Construction Render** is a thin adapter that selects Finished or Construction groups from lifecycle state. Configure their component membership explicitly. On Construction State Changed and On Construction Stage Changed deliver ordered old/new values for actual simulation transitions. Snapshot refresh silently establishes current appearance; it does not invent gameplay transitions or rerun effects. Completion delegates run before the optional construction actor is destroyed.

Cursor placement holograms remain separate from an already-placed construction site's presentation.

## Widgets

**Sein Entity Widget Component** provides explicit entity context to a world widget, including widgets on managed actors. Entity Widget subclasses initialize automatically. Arbitrary widget classes can consume On Widget Entity Context and use the component's current getters/Initialize Widget Context when subscribing after BeginPlay.

**Sein Entity Widget** exposes Bound Entity and a Blueprint-overridable Get Progress Display returning visibility, normalized percent and label. This supports construction, health or other game-authored displays. It clears invalid contexts and refreshes presentation independently of the construction visual group. The supplied **Sein Construction Work Progress Widget** is an example policy using the construction component's work fields; it shows queued/building/paused work and hides completed jobs. Its percentage query has no visibility side effects.

## Demo and verification

The existing placement/build ability graphs use Spawn Construction Site, a captured job handle, Start, Add Work and Complete. The second migration configures Barracks/Factory with explicit Finished and Construction meshes on the stable actor, work fields on Sein Construction, and a separate Entity Widget Component. It reparents the existing progress widget to the supplied work policy while preserving its layout.

Migration commandlets back up the current working binary assets under Saved/ConstructionRefactor/Backups before saving. No public Docs pages are changed by this implementation; their affected contracts are listed in PUBLIC_DOCS_BACKLOG.md.

Development evidence for this second refactor is retained under Saved/ConstructionRefactor. Earlier Saved/Construction results apply to the preceding implementation only. Required acceptance includes default-off placed spawn, atomic queued spawn, work-free completion, stale jobs, effect-once/tag ownership, ordered transitions, binding readiness and reentrancy, widget reuse, explicit group cleanup and fresh-world restore. PIE/Details appearance and animation feel remain human checks, separate from automated runtime evidence.

### Compatibility

Framework behavior epoch is now `SeinARTS.Replay.10`, reflecting consolidation of the work payload into construction. It distinguishes the new compiled construction semantics in config parity, replay and snapshot admission, including ordinary editor-session mode where no saved payload/content records are included. The unrelated CoreEntity discovery-contract revision remains unchanged. Earlier runtime/saved inputs must be rejected rather than interpreted as equivalent construction behavior.

### Previous refactor evidence (before work consolidation)

- Final epoch-9 test-enabled editor build: Saved/Build/c83a4286a29a4a599966a476c6e09ce9/build-result.json. Ordinary editor restored by Saved/Build/70c4f287740f47b09f20b516df222ca9/build-result.json.
- Saved/ConstructionRefactor/Compatibility/SeinARTS.Sim.json: 101/101 passed, including 12 construction tests. Tests cover work-free completion, independent work thresholds, widget reuse/clearing, same-phase work restore without synthetic transitions, ordered state/stage events, completion-listener rebind, managed actor BeginPlay context through normal component lookup, and migrated building references/baked defaults.
- Saved/ConstructionRefactor/Compatibility/SeinARTS.Integration.json: 22/22 passed with rendering enabled (required by an existing canvas integration test).
- Saved/ConstructionRefactor/Compatibility/SeinARTS.Determinism.json: 45/45 passed. Fresh serial/parallel processes match all 120 canonical-root and raw-pose frames: Saved/ConstructionRefactor/Compatibility/DeterminismAB/ab-result.json. This proves the collision workload, not every gameplay workload.
- Saved/ConstructionRefactor/Compatibility/SeinARTS.Unit.json: 531/535 passed. Remaining pre-existing failures are AgentProjectionMatchesTerrainWallPadding, AgentTerrainParticipatesInFootprintClearance, AgentTerrainPolicyOwnsPathAndReachabilityTopology and AuthoritativeDestinationCannotOverrideAgentHazards. The framework-epoch assertion now passes.
- Simulation preset invocation: Saved/Validation/2d35026f89844f4fa3cb00aa0d85c5ef/validation-result.json. Whitespace passed; Unit initially stopped the preset on the four navigation assertions and a stale epoch literal. Final suites were invoked directly after versioning this refactor and updating its epoch test; see Compatibility receipts above.
- Migration: Saved/ConstructionRefactor/migrate.log. Current working assets backed up to Saved/ConstructionRefactor/Backups/4166AEAB432DA7E8245808A91F57F136. Barracks, Factory and WBP_ConstructionProgressBar saved successfully. Fresh-process recompile/audit: Saved/ConstructionRefactor/verify.log and asset-audit.txt, zero warnings/errors. Both buildings have Start Queued off and Work Required 10.
- Independent review found and resolved callback reentrancy, observer-authority and managed binding readiness defects, and confirmed the framework behavior epoch is the correct compatibility boundary.
- git diff --check HEAD and newly added source whitespace checks pass. The earlier tag EOF whitespace blocker was cleared by intervening workspace work.
- Commandlet execution required guarding existing editor variable-customization registration from loading Kismet before GEditor initialization. This narrow fix preserves the unrelated activation-input implementation.

Manifest regeneration was attempted: Saved/ConstructionRefactor/manifest.log. It remains blocked because SU_InfantrySquad references the missing /SeinARTSFramework/Demo/Blueprints/UI/WBP_UnitBanner asset. No unrelated asset was recreated or modified to bypass admission.

Remaining human acceptance: inspect Start Queued for Construction and group component pickers in Details; place a completed Barracks in PIE, build a queued Barracks through the migrated ability, pause/resume it, and observe its progress bar and stage animations. Automated actor/widget assertions do not establish visual appearance or animation feel.

### Work consolidation

Required Work was copied from the removed component into the existing Sein Construction authoring component. Barracks/Factory Blueprint SCS nodes and baked work payloads were removed. Sandbox and DemoSkirmish placed actors were loaded through the editor map loader, their authored requirements captured before Blueprint recompilation, then migrated and rebaked. All captured values were 10. The original working assets are backed up under Saved/ConstructionMerge/Backups/831660D14EE5612E25CC898979AAD887; migration receipt is Saved/ConstructionMerge/migrate-maps.log.

The old native authoring class and payload header are deleted. Scanning project/plug-in assets and maps finds no references to either removed type. The one-time merge implementation was removed after migration. Final consolidated-source evidence is retained under Saved/ConstructionMerge; prior refactor receipts above describe the earlier schema.


Final work-consolidation evidence:

- Removed-type test-enabled build passed: Saved/Build/8b81ef025364472882adc39ae45561ca/build-result.json; ordinary editor build passed: Saved/Build/845973ec7a2744fc9881ee8ef1182107/build-result.json.
- Saved/ConstructionMerge/sim.json: 101/102 Sim tests passed. All 17 construction-related tests passed (13 lifecycle tests plus four economy tests), including queue-time requirement capture, fresh-world continuation, work-free completion and migrated authoring defaults. The failure was InvalidResearchIsRejectedAtEnqueueAndCannotConsumeFunding at ProductionCostOwnershipTests.cpp:1074 (resource-payer cleanup), outside this consolidation.
- Saved/ConstructionMerge/verify.log: fresh-process Blueprint verification and editor map reload passed with zero warnings/errors. Both Sandbox buildings and the DemoSkirmish Barracks retain Required Work 10 after the old reflected types were removed.
- Saved/ConstructionMerge/removed-type-scan.json: 316 assets/maps scanned, zero removed-class or removed-payload references. Source retains only an intentional test assertion that the removed authoring class is absent.
- Simulation preset rebuilt after unrelated production source changes: Saved/Validation/a39078d38eb44c02ad40ee3883017768/validation-result.json. Unit stopped on the four navigation failures listed above and SnapshotV18EnvelopeHasFrozenBigEndianFramingAndCanonicalOrder (actual version 19 versus expected 18). The latter accompanies separate snapshot/production changes in the shared checkout. Test-enabled build 484d8bbc0f6a480e930b8e82afb1ac58 and ordinary editor build b813de8ca1084a2ba4ef892ab31e6a23 passed.
- Additional shared production-source edits during that run invalidated build provenance again. Follow-up Sim, Integration, Determinism and fresh-process A/B were refused by guarded SkipBuild; receipts are under Saved/ConstructionMerge. These are incomplete checks, not passes. Do not reuse the earlier schema's broader passing receipts as proof of this consolidation.
- Independent focused review found no concrete consolidation defect. git diff --check HEAD passed. No public Docs pages were edited; authoring/API documentation impact is recorded in PUBLIC_DOCS_BACKLOG.md.
- Manifest regeneration was attempted again and remains blocked by the missing WBP_UnitBanner dependency from SU_InfantrySquad: Saved/ConstructionMerge/manifest.log. No unrelated asset was created to bypass it.

Remaining acceptance: reopen the editor and inspect Required Work on the existing Sein Construction component; actual Details presentation and PIE construction/health-bar behavior remain unverified. Broader validation needs a stable shared-source snapshot and resolution of the unrelated suite/manifest blockers above.
