# Demo guide progress

Scope: the demo-tutorial authoring task only. Its desktop restrictions, stop points, and specific
approvals apply when resuming that task; they do not govern unrelated framework work. Preserve this
active handoff. Dated implementation details are evidence for that task, not a replacement for the
current root/plugin contracts or current user instructions.

## Authoritative working agreement

Build the example from ZERO in a new Blank Unreal Engine project. Existing plugin Demo assets are reference evidence of RJ's intended result, never prerequisites or assets to copy. Teach plugin installation/enabling, required project settings, every custom gameplay class, input action/mapping/handler graph, faction, level, unit, ability, material, and UI before referencing it. Native framework classes and nodes are legitimate building blocks. A provided SU_Soldier or prewired demo controller is not.

RJ approved deleting or replacing the unreviewed guide content while preserving styling. RJ superseded the stop-at-unfinished-work process on 2026-09-07: finish the guides autonomously with concrete MVP choices; RJ will follow and edit them. Source-check the recipes and provide complete instructions for prerequisites, including any required game-side C++, without claiming the saved demo already implements the new chapters. Never control the desktop/editor; RJ is working concurrently.

This task is scoped to the docs site; main-site requests belong in RJ's separate chat and must not be acted on here. Preserve unrelated checkout work. Distinguish source-checked documentation drafts from interactive authoring and runtime verification. RJ explicitly authorized one framework cleanup here: remove the unused SeinHUD layout-widget properties and their automatic BeginPlay creation path, and update the guide accordingly.

## Restart state

The previous supplied-scene setup chapter was rejected and removed. Its map-manifest warning is not the current starting task: fixing RJ's existing map does not reconstruct the project from zero.

New opening chapter: `Docs/src/content/docs/guides/creating-the-demo-project.md`. It covers source acquisition, new Blank C++ host project, installing the framework, dependencies, and required Unreal project settings. No provided demo asset is required. Its endpoint is editor integration, not a working scene. RJ removed the native-defaults audit and upfront folder scaffold: create folders only when authoring their first assets. Chapter 2 now creates Demo/Blueprints with the gameplay classes and Input with the input actions; UI, Data, Materials, and Assets folders are not created in advance.

Removed the five unreviewed old guides and their sidebar/homepage links. Public navigation now starts at the new chapter. Corrected stale entity-bridge terminology on the two adjacent introductory pages. Styling is unchanged.

## Resolved starting-content assumption

RJ explicitly instructed: assume the tutorial project contains no demo content. The shipped demo IS the completed guides. Do not change Blueprint assets, shipping layout, installed content, or project tag conventions to solve coexistence with the completed example. The reader authors the result from scratch. Keep Tag Prefix SeinARTS and normal names such as SU_Soldier and SA_Move. There is no pending namespace/distribution decision.

## Current chapters and stop

1. Create the Demo Project: updated to this assumption and required Unreal settings, without auditing native defaults or scaffolding folders upfront.
2. Create the Gameplay Classes and Input: new local draft. Creates four Blueprint classes from native parents, nine basic Input Actions, mapping context, local-player BeginPlay setup, camera axis bindings, select/command press-release-cancel paths, and modifier latches.
3. Create the First Level: starts from Unreal's Basic template with its existing floor and lighting, replaces any ordinary Player Start with a native slot-1 Sein Player Start, fits the volume to the template floor, bakes, and tests the scene. Its checkpoint uses only camera and marquee; no supplied or already-created Soldier is assumed.

The chapter 3 map-registration/manifest blocker was resolved on 2026-09-06. Config/DefaultGame.ini now registers /SeinARTSFramework/Demo/LVL_DemoSkirmish in AvailableMaps with SlotCount=1 and TeamCount=1. The editor log records successful manifest generation/save at 09:06:57 local time, and the saved manifest contains the demo map path. The guide itself uses its newly created /Game/Demo/LVL_DemoSkirmish map; RJ's existing reference map is /SeinARTSFramework/Demo/LVL_DemoSkirmish. Do not conflate their paths. The latest inspected log does not establish a new PIE camera/marquee pass.

The pre-resume commit/push and subsequent docs-layout changes are complete at 53a5e28. RJ explicitly resumed guide writing on 2026-09-06 after the manifest checkpoint. Images will be added by RJ later and are not a blocker. Do not require a Soldier as a prerequisite to the guide's camera/marquee checkpoint. No clean-project playthrough is claimed.

4. Create the Soldier: new local draft at `Docs/src/content/docs/guides/creating-the-soldier.md`, linked from the first-level guide, sidebar, and homepage. Creates SU_Soldier through the native entity factory, adds identity, imports Unreal's Third Person mannequin content, authors an initial ABP_Soldier idle graph, adds an extents capsule and sight, assigns the Soldier as Spawn Entity on the slot-1 Sein Player Start and checks selection using the native observer-command overlay. Animation folder is created only when the Animation Blueprint is authored. Locomotion is introduced with movement next; no prebuilt demo asset is a prerequisite.

RJ corrected the map workflow: tutorial skirmish units must flow from the player's Spawn Entity and subsequent gameplay production, not manually placed unit actors. SU_Soldier is the starting entity at this stage; no decision has been made to replace it with a Barracks later. Chapter 3 now places the start's origin at the floor surface. Chapter 4 assigns Spawn Entity and explains that bootstrap uses the saved start transform and slot ownership directly.

RJ explicitly approved a native sight default: USeinVisionComponent's constructor adds one default FSeinVisionStamp (enabled radial, radius 1000, normal layer). FSeinVisionPayload itself remains empty by default; WritePayload copies the authored array without adding or repairing entries. Chapter 4 now describes the supplied stamp. This is an authoring default change, not a runtime fallback or asset migration. Previously saved components that inherited the old empty native default may inherit the new default; do not claim all pre-existing empty configurations are preserved. No Blueprint assets have been edited or resaved.

Vision-default validation on 2026-09-06: after RJ closed the editor, native Development/Shipping builds and headless default/explicit-empty tests passed. Interactive compile/save/reload of authored arrays remains a human editor check. No Blueprint assets were edited or resaved. Do not close or control RJ's editor.

Current work: the complete 14-chapter walkthrough is drafted. RJ will follow and edit it. There is no pending combat approval or asset-save gate; see the autonomous completion handoff below.

Read-only asset inspection used UnrealEditor-Cmd with PythonScriptPlugin enabled only on its command line and ObjectExporterT3D. It exported the Soldier, movement ability, combat component, animation Blueprint, locomotion Blend Space, and attribution asset to ignored `Saved/Automation/DemoGuideAuthoring`. No assets or project settings were saved. The actual Soldier includes identity, movement, abilities, navigation, extents, vision, navigation renderer, and SC_DemoCombatComponent. The combat component currently contains Damage and RateOfFire, both zero; inspect its intended combat contract together when that chapter is reached.

Source checks for chapter 4: native entity factory supplies Generic Entity/SeinActor and name-derived identity; authoring components seed from matching bridge payloads; extents default capsule is radius 40 and height 180 above LocalOffset.Z; a new vision stamp is enabled radial sight radius 1000; placed actors own EditInstanceOnly PlayerSlot and baked transforms. Selection log commands are toggles, and observer commands are filtered out by default. The UE 5.8 Third Person feature-pack manifest includes Characters, and the installed template resources contain SKM_Manny_Simple, SK_Mannequin, and MM_Idle.

Chapter 4 validation: npm run build passed with 10 pages; new route HTTP 200; desktop and 390px-wide mobile rendered without horizontal overflow; sidebar link verified after refreshing the dev server's config watcher. No fresh-project Unreal playthrough was performed. Layout styles, source code, Blueprint assets, and config remain untouched by this chapter.

## Source checks

- Framework descriptor declares EnhancedInput and GameplayTagsEditor; optional SeinARTS extensions are not core prerequisites.
- UEngine.WorldSettingsClassName is restart-required, under DefaultClasses, displayed as World Settings Class. Use settings search; do not repeat the old unverified Maps & Modes breadcrumb.
- UInputSettings exposes DefaultPlayerInputClass and DefaultInputComponentClass. Host uses EnhancedPlayerInput and EnhancedInputComponent.
- Engine GameplayTags settings default ImportTagsFromConfig to true.
- Native PluginSettings supplies the implementation defaults; the guide no longer asks readers to audit them.
- Native SeinPlayerController explicitly leaves mapping-context installation and event bindings to a Blueprint subclass.
- Current saved controller references native selection/command cancellation handlers as well as the basic camera/selection handlers.
- Current standalone demo runs with Player(1), Faction(0), Team 0 and no faction assets. Native RegisterPlayer seeds the resource catalog defaults before any optional faction kit overrides. Do not claim a faction data asset is mandatory for the local bootstrap or base economy merely from stale header comments.

## Next authoring sequence

Complete the current Basic-level camera/marquee checkpoint, then create Soldier data and visuals; script movement; complete combat data and attack; Minerals income; HUD; buildings and construction; production and research; Truck and transport; fog/minimap; lobby; clean packaged verification. Expand foundational steps as needed so every prerequisite is created before use.

No clean editor bootstrap has been executed by this task. Existing-project PIE evidence must not be presented as clean-project verification.

Documentation impact: public + private-agent. The explicitly authorized HUD cleanup removes HUDLayoutWidgetClass, HUDLayoutWidget, the now-unneeded BeginPlay override, and the widget include/forward declaration. Existing selection and category WIP is preserved. No config or Blueprint assets changed.

## Validation for restart

- npm run build passed for all 9 pages, including the three opening guides. Internal link destinations and rendered guide tables/checkpoints checked successfully.
- Generated-page internal link destination check: passed.
- git diff --check: passed.
- No CSS or shared component edits; no Blueprint, host config, engine, or editor changes.
- Prior npm run check found nine TypeScript errors in untouched GitHubStar.astro; this restart did not fix or reclassify them as passing.
- No clean-project editor run and no website deployment performed.

## HUD removal and Basic-level revision validation

- Scripts/Build.ps1 passed; SeinHUD.cpp and generated framework code compiled and UnrealEditor-SeinARTSFramework.dll linked successfully.
- Headless CompileAllBlueprints with the project/framework content allow list checked 67 Blueprints, including SHUD_Play and HUD_DemoSkirmishHUD: zero compilation errors, warnings, or failed loads. Log: Saved/Automation/HUDLayoutRemoval/CompileFilteredBlueprints.log. The first invocation had a malformed unquoted allow-list argument and was stopped; only the corrected filtered run is the validation result.
- Docs npm run build passed for all nine pages. Both edited guides return HTTP 200 with the removed field absent and the Basic template instructions present.
- No Blueprint assets were saved by the commandlet. No interactive editor or clean-project PIE playthrough was performed.


## Automatic compatibility workflow (implemented 2026-09-06)

RJ requires zero manual manifest setup or routine regeneration. This supersedes the historical manifest checkpoints above. Never tell tutorial readers to regenerate after editing assets, saving a map, or creating a project. Available Maps is a lobby catalog concern, not a prerequisite to ordinary Play. Both affected guides now follow this rule.

Ordinary uncooked editor/development sessions use a reserved editor-session compatibility profile independently of the configured recovery manifest. Normal Play does not save source packages, write manifest settings, or calculate saved-package hashes. Compiled unsaved Blueprint pool classes are admitted from the live editor/Asset Registry; snapshot capture and restore retain their state. This profile checks active code contracts, not saved asset parity, and is deliberately incompatible with release evidence. Strict saved-content testing remains explicit through Sein.SimulationContent.RequireFreshManifestForPIE=1.

By-the-book cook now builds target-filtered compatibility evidence automatically in its sandbox at Content/SeinARTS/SimulationCompatibility.bin. It includes discovered simulation inputs and actual cooked authored maps plus their source dependencies, without requiring duplicate map-catalog registration. Source assets/config are never written. Missing inputs, invalid contracts, cook errors, incomplete cooks, or changed source hashes prevent publication; old evidence is invalidated first. UAT stages the binary as UFS data into the package. Cooked runtime requires this evidence and the exact active contributor set. Existing peer/replay/snapshot digest checks remain in place. Builder revision is now 2, so older saved evidence must be rebuilt when explicitly using strict recovery mode.

The installation diagnostic and consumer/release tooling now follow automatic cook too: no generated recovery asset prerequisite, no bootstrap error suppression, and no pre/post-map regeneration. Consumer qualification checks for cook-owned output and records AutomaticCook mode. The Windows PowerShell 5.1 diagnostic selftest passes absent/None/broken recovery references plus existing adverse installation fixtures. Script parsing passed; the full release consumer matrix was not rerun in this task.

Contributor descriptors now require OwnerModule for target plugin admission. Every production contributor supplies it. The field controls build membership; it is not itself a semantic digest field. External contributors must also populate it. The manual Rebuild Compatibility Data action and its saved-asset fields are under advanced Compatibility Recovery.

Cooked Blueprint pool discovery resolves actual generated classes rather than editor-only Asset Registry tags. Source-directory rescan fallback is restricted to uncooked sessions: forcing a rescan in IoStore was deleting registry entries between worlds and breaking snapshot admission. A standalone Development compilation issue in steering debug labels was also fixed by retaining GetDisplayNameText in editor builds and using GetName otherwise.

Validation:
- Development editor, standalone Development, and Shipping builds passed. Final Shipping log: Saved/Logs/ManifestShippingBuild.log.
- Five editor compatibility tests passed, including an unsaved compiled ability with state 42, snapshot restore, four future canonical roots, and preserving the dirty package without saving. Vision authoring starts with one stamp; explicit empty arrays survive duplication and payload writing; raw runtime payload stays empty by default.
- Fresh-process serial/parallel editor runs also matched all four future canonical roots (Saved/Logs/ManifestAB0.log and ManifestAB1.log). Each run passed the unsaved ability snapshot test.
- Four runtime mode tests, five manifest/registry codec tests, and five existing snapshot regressions passed. Reports/logs are under Saved/Automation and Saved/Logs/Manifest*.
- Direct cook with SimulationContentManifest=None passed: 161 source packages and ten contributors, including /Game/Sandbox even though that map is not catalogued. No /Engine records. Full and iterative cook produced the same digest, 3785F8F4ECD59CA21AEA2DB6C2C182D5. Negative cook with a missing additional source failed and removed pre-existing output evidence.
- UAT cook, pak, IoStore, and stage passed with no configured manifest. Final restaging of current Development binaries passed (ManifestUATStageFinal.log). UFS staging includes SimulationCompatibility.bin.
- Hidden NullRHI packaged demo startup, snapshot capture, and fresh-process restore passed: six entities, ten component storages, ten ability pool instances. Serial/parallel startup canonical roots and the restored root agree at tick 1: 789722999E1DF9CB44C2D69DF104A369. Legacy local fingerprints differ across processes and are not canonical determinism evidence.
- Docs build passed for ten pages; manual manifest instructions are absent from current guides.

Limits: interactive clean-project authoring, Blueprint save/reload UI, multiplayer PIE, Cook on the Fly, and multi-worker cook were not exercised. Cook on the Fly development clients use development evidence. DLC cook fails explicitly until base-profile composition is implemented. Cook reuses saved Asset Registry hashes but still rediscovers/revalidates inputs and rewrites build evidence each cook; no separate cross-cook discovery cache is claimed. Do not present these headless host tests as a clean-project tutorial playthrough or broad multiplayer determinism proof.

The guide can resume at the movement chapter after the framework validation handoff. The old ability-flag checkpoint below has been superseded by the approved Default Move Ability refactor. Do not edit the Blueprint silently. No Blueprint or host config files were modified by this work.

## Interrupted-run completion (2026-09-07)

Resumed after usage-limit interruption. Current checkout includes independent docs-footer and native debug fixes at 33f4ebf; preserve them and unrelated WIP. The automatic compatibility changes remain uncommitted. Final independent source review found no blocking defects in cook invalidation/completion, target contributor filtering, cooked Blueprint resolution/scan restriction, or ConsumerMatrix/Release/Diagnostics changes; relevant UE 5.8 cooker code was checked. Diagnostics selftest, four PowerShell syntax checks, the ten-page docs build, and diff whitespace check passed again. The current Development editor and Shipping builds passed (Saved/Logs/ManifestResumeEditorBuild.log and ManifestResumeShippingBuild.log). All five refreshed editor compatibility tests passed (ManifestResumeEditorTests.log; report SeinARTS.Editor.SimulationContent-20260907-113249-94ca5edc). Startup briefly waited for the SDK-validation child process to acquire the build lock, then completed normally. No asset/config saves or desktop control performed.

Ready to return to the guides. Interactive clean-project PIE and the full release matrix remain unrun. Changes remain uncommitted; no push or deployment was performed during the resumed work.

## Movement chapter preflight (2026-09-07)

RJ requested the next guide. A fresh read-only Unreal Python commandlet loaded the saved SA_Move generated-class CDO and confirmed is_move_ability=False (Saved/Logs/DemoGuideMoveInspect.log). Requested that RJ enable Is Move Ability, compile, and save before proceeding, following the agreed stop-at-first-unfinished-setting workflow. No chapter was published past this checkpoint and no Blueprint/config was saved. The temporary inspection script was removed.

Source/reference preflight: SA_Move already wires OnActivate to Move To using TargetLocation; Completed, Failed, and Cancelled lead to End Ability. Soldier has Basic Unit movement, navigation, and granted SA_Move with a default-command mapping requiring RightClick and Target.Ground. These are evidence for authoring instructions from zero, not reader prerequisites.

## Explicit default movement (2026-09-07)

RJ approved replacing the ability-level Is Move Ability flag with Default Move Ability on Sein Movement. The selection names one already-granted concrete, non-passive Point ability with a valid unique tag. Empty disables automatic movement; there is no implicit grant or first-available fallback. Production rally, AutoMoveThen command handling, and availability queries use the same exact-grant resolver. Rally now stores PredeterminedAbilityTag as well, so it does not require an unrelated Default Commands mapping. Queued orders keep their captured tag when the default changes.

The editor picker reads live authoring grants, including inherited components; validation rejects stale, ineligible, or ambiguous selections. The reflected movement state includes the class reference and the core simulation contributor revision is 5. Consumer fixture generation schema is 9. No silent Blueprint migration: existing assets start with an empty default. For the current Soldier checkpoint, select SA_Move in Sein Movement > Default Move Ability, then compile and save. Keep SA_Move in Granted Abilities. The guide must teach creating and granting SA_Move before selecting it, and must never ask readers to set the removed flag or regenerate a manifest.

Validation:
- Focused explicit-grant/revocation/invalid-tag/rally tests, fresh-world snapshot plus four future ticks, and live/inherited/child-override authoring checks passed. The existing AutoMoveThen funding test now changes and clears the default after prefix creation: the queued movement still activates, while new out-of-range availability is disabled. The four affected test helpers' old diagnostic names were updated from SeinAbilityComponent to SeinAbilityPayload.
- Final All-profile simulation run: 93/94 passed, including the changed production/payer tests. The remaining MovingCoverFrozenDestinationTests.cpp:298 assertion checks input command type before automatic movement resolution; that file was not changed. Receipt: Saved/Validation/7c8cd7c7a36140a799243fdf6d43ed94/All-SeinARTS.Sim.json.
- All-profile determinism: 58/58 passed. Editor DefaultMove: 1/1 passed, including the child-owned inherited component override context. Receipts: Saved/Validation/DefaultMoveFinal-SeinARTS.Determinism.json and DefaultMoveFinal-SeinARTS.Editor.DefaultMove.json.
- Fresh-process Framework collision A/B: all 120 canonical-root and raw-pose frames match. This qualifies that workload, not all simulation behavior. Receipt: Saved/Validation/DefaultMoveCollisionAB/ab-result.json.
- Broad Unit: 548/553 passed. Remaining failures are the replay epoch expectation (7 versus live 8) and four agent-terrain navigation checks; no related implementation was changed here. Receipt: Saved/Validation/0d5a604689d04dbdb1a06b698bd69547/All-SeinARTS.Unit.json.
- Broad Integration aborted in the existing SteeringCanvasShowsIdleMotionAndOwnsItsViewBudget test at MoveToLifecycleTests.cpp:748: NullRHI supplied no RenderTarget to Canvas. No full integration pass is claimed. Receipt: Saved/Validation/DefaultMoveFinal-SeinARTS.Integration.json.
- Independent source review found no remaining blocking defect. Its followup verified that the rally fix carries PredeterminedAbilityTag through broker creation and dispatch without a default-command mapping. The new rally regression exposed and verifies that fix.
- Development editor and Shipping builds passed. Final receipts: Saved/Validation/DefaultMoveEditor.json and DefaultMoveShipping.json; Shipping rebuilt the affected core ability/world code and linked successfully. Diff whitespace and consumer-matrix script parsing passed. No full consumer/release matrix or manual Slate/PIE playthrough was run.

Documentation impact: private-agent plus current editor tooltips; no existing public guide required the removed flag. That setting checkpoint was subsequently completed; see the movement chapter handoff below. No main-site, Blueprint asset, host config, commit, or push changes in this refactor. Preserve the separate automatic-compatibility WIP.

## Movement chapter and field presentation (2026-09-07)

RJ confirmed SA_Move is selected. Fresh read-only Unreal exports verify SU_Soldier's saved DefaultMoveAbility points to SA_Move; no Blueprint or map was saved by the inspection. The editor display name is now Movement Ability and DisplayAfter=TurnRate positions it before Movement Class without renaming or relocating the serialized member. The tooltip explains automatic movement, eligibility, no implicit grant/fallback, and captured queued orders. Related user-facing diagnostics use the same label.

New public Add Movement chapter teaches movement/navigation components, creating Generic Ability SA_Move, Move To terminal handling, grants and Movement Ability selection, right-click ground mapping, a directional mannequin Blend Space, and animation state driven by simulation movement. It is linked from Create the Soldier, the sidebar, and the homepage. Native metadata/editor validation passed (Saved/Validation/6e0174fe5cf847c9928a88697e1dd536/validation-result.json); the docs build passed with 11 pages. No fresh-project interactive authoring or PIE playthrough is claimed.

Next checkpoint: the fresh SC_DemoCombatComponent CDO contains Damage=0 and RateOfFire=0. Its own authored data has no health field; inspect the Soldier's existing attribute setup before deciding where health belongs. This historical stop was superseded by RJ's subsequent instruction to finish the guides autonomously. Read-only exports are in Saved/Automation/MovementGuideCurrent; log Saved/Logs/MovementGuideInspect.log. The temporary inspection script was removed. Preserve RJ's saved Soldier/map changes and all prior compatibility/refactor WIP. No main-site changes, commit, push, or deployment in this turn.

## Autonomous guide completion (2026-09-07)

RJ explicitly replaced the step-by-step approval/unfinished-asset gates with autonomous completion: finish all guides so RJ can follow and edit them. The public walkthrough now has 14 linked chapters. Nine added chapters cover combat, mineral income, HUD, buildings/construction, production/research, transport, fog/minimap, lobby, and packaging. The sidebar, homepage, Getting Started, and movement continuation link include them. Concrete demo choices: Soldier Health 100/Damage 3/RateOfFire 10 (600 RPM), with harvesting using the same values for 30 Minerals/second; Minerals start 500; deposits 1000; Barracks 100, Factory 200; Soldier production 50/5s, Factory unlock 100/10s, Truck 150/8s; self-completing construction 10s; four-seat instant-load transport; same roster with two human slots and no bot AI. Gameplay is a skirmish sandbox without a victory rule.

Source checks covered entity-component factory/baking, typed getters, fixed-time ability callbacks and cancellation arbitration, Apply Field Delta plus combat notifications, income ownership, placement targeters/footprint gates, construction completion, production funding transfer and player effect tags, containment admission/exit, UI models, fog material parameter bindings, lobby requests and roster materialization. The research Can Activate recipe checks all owned Barracks queues before accepting a duplicate. Gameplay data and scripts remain designer-authored in the tutorial.

Containment authoring is a real source gap: FSeinContainmentData and FSeinContainmentMemberData are marked SeinSubData, excluded from the component-node menus, and have no native Add Component wrappers. The transport chapter includes complete game-side UDemoTransportComponent and UDemoPassengerComponent wrappers plus Build.cs and build steps; the existing bridge accepts their WritePayload output. It does not pretend those components already ship, alter the framework, or depend on copying demo Blueprints. Native schema/cook/runtime primitives are existing framework types. That example C++ was source-reviewed, not compiled in a fresh consumer project. Passenger mesh visibility is explicitly handled as presentation; ability Can Activate rules suppress contained units' actions.

Validation: site build passes for 20 pages, including all 14 guides. Fresh build command: node node_modules/astro/bin/astro.mjs build --force (inside Docs). A warm content-cache build left an old Expressive Code stylesheet reference on two existing pages; forcing Astro's content refresh resolved it without changing styling. Generated HTML audit checked 583 root-relative links/assets/anchors across 20 pages with zero failures. No new manual manifest generation steps, placeholder chapters, or supplied-demo prerequisites. Final documentation whitespace receipt recorded by Scripts/Validate.ps1. No clean-project Unreal authoring, transport-wrapper compilation, material compilation, PIE, or multiplayer/package playthrough was performed for these recipes. They are complete source-checked drafts for RJ's requested follow-and-edit review, not runtime-qualified demo assets.

Preserve RJ's concurrent movement-guide edits and all unrelated WIP. This completion changed only Docs content/navigation and this scoped handoff; no Blueprint saves, native source changes, main-site changes, commit, push, or deployment.

## Combat guide validator correction (2026-09-07)

Harvest follow-up: Grant Income also lacked SeinDeterministic function metadata. Added only that annotation after reviewing the existing authorization, atomic validation, and fixed-point projection path; extended the native guide-node regression to include Grant Income. Independent review found no defects. The focused preset stopped on unrelated Config/Tags/SeinGameplayTags.ini EOF whitespace; scoped diff checks passed and the underlying RunTests.ps1 rebuilt successfully and passed all 15 AbilityDeterminism tests (Saved/Automation/HarvestValidatorRegression.json). A separate commandlet compiled and validated the saved SA_Harvest with zero asset errors/warnings (Saved/Automation/HarvestAssetValidation.json); no saves, SHA256 unchanged. The guide requires no workaround. Documentation impact: private-agent. Reopen Unreal to load updated metadata, then continue Harvest point 5. No commit/push.

Guide-writing convention from RJ: author custom defaults once, then inherit them when adding the component. Instructions should name actual overrides, not ask readers to re-enter unchanged native defaults or reconfirm earlier values. The full walkthrough was audited against native initializers and preceding authoring steps; redundant setup was removed across the chapters. Runtime resets and intentional per-entity/test overrides remain. Mineral introduction and HUD expectations now consistently use 3 per hit.

RJ completed Add Combat through point 6. SA_Attack validation rejected Check Target, Apply Field Delta, Notify Damage Applied, Notify Death, and the local TryShot helper. The four native functions now carry function-specific SeinDeterministic metadata after checking their existing query/mutation/notification implementations. The shared validator accepts generated/skeleton local helpers only when their function graphs are in the current Blueprint's scan, validates signatures with the shared deterministic container/type rules, and checks local variables. Unsafe body calls, float storage/signatures, and external Blueprint helper calls still fail. Independent review found the initial safe-container false rejection; that was corrected and regression-covered.

Validation passed 15 focused AbilityDeterminism tests with fresh builds: Saved/Validation/f73393a8588444b2900e709758e22d12/validation-result.json. A separate background Python commandlet compiled and validated the saved SA_Attack successfully with zero asset errors/warnings; Saved/Automation/CombatGuideValidation.json and Saved/Logs/CombatGuideValidation.log contain the evidence. No asset was saved and its SHA256 remained unchanged. The temporary inspection script was deleted. No gameplay/peer determinism claim follows from these editor checks. Public guide instructions remain valid; documentation impact is private-agent and native validator contract comments. RJ can reopen Unreal and continue at point 7. No commit or push.
