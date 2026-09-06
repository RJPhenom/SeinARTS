# Demo guide progress

## Authoritative working agreement

Build the example from ZERO in a new Blank Unreal Engine project. Existing plugin Demo assets are reference evidence of RJ's intended result, never prerequisites or assets to copy. Teach plugin installation/enabling, required project settings, every custom gameplay class, input action/mapping/handler graph, faction, level, unit, ability, material, and UI before referencing it. Native framework classes and nodes are legitimate building blocks. A provided SU_Soldier or prewired demo controller is not.

RJ approved deleting or replacing the unreviewed guide content while preserving styling. Work sequentially. Stop at the first unfinished mechanism or required decision, resolve it with RJ, then continue at the same point. Do not collect deferrals. RJ may implement C++ in another session. Transport/garrisoning ownership is undecided until reached. Never control the desktop/editor; RJ is working concurrently.

This task is scoped to the docs site; main-site requests belong in RJ's separate chat and must not be acted on here. Preserve unrelated checkout work. Do not publish unverified chapters. RJ explicitly authorized one framework cleanup here: remove the unused SeinHUD layout-widget properties and their automatic BeginPlay creation path, and update the guide accordingly.

## Restart state

The previous supplied-scene setup chapter was rejected and removed. Its map-manifest warning is not the current starting task: fixing RJ's existing map does not reconstruct the project from zero.

New opening chapter: `Docs/src/content/docs/guides/creating-the-demo-project.md`. It covers source acquisition, new Blank C++ host project, installing the framework, dependencies, and required Unreal project settings. No provided demo asset is required. Its endpoint is editor integration, not a working scene. RJ removed the native-defaults audit and upfront folder scaffold: create folders only when authoring their first assets. Chapter 2 now creates Demo/Blueprints with the gameplay classes and Input with the input actions; UI, Data, Materials, and Assets folders are not created in advance.

Removed the five unreviewed old guides and their sidebar/homepage links. Public navigation now starts at the new chapter. Corrected stale entity-bridge terminology on the two adjacent introductory pages. Styling is unchanged.

## Resolved starting-content assumption

RJ explicitly instructed: assume the tutorial project contains no demo content. The shipped demo IS the completed guides. Do not change Blueprint assets, shipping layout, installed content, or project tag conventions to solve coexistence with the completed example. The reader authors the result from scratch. Keep Tag Prefix SeinARTS and normal names such as SU_Soldier and SA_Move. There is no pending namespace/distribution decision.

## Current chapters and stop

1. Create the Demo Project: updated to this assumption and required Unreal settings, without auditing native defaults or scaffolding folders upfront.
2. Create the Gameplay Classes and Input: new local draft. Creates four Blueprint classes from native parents, nine basic Input Actions, mapping context, local-player BeginPlay setup, camera axis bindings, select/command press-release-cancel paths, and modifier latches.
3. Create the First Level: starts from Unreal's Basic template with its existing floor and lighting, replaces any ordinary Player Start with a native slot-1 Sein Player Start, fits the volume to the template floor, bakes, registers Available Maps and generates the first manifest. Its checkpoint uses only camera and marquee; no supplied or already-created Soldier is assumed.

The chapter 3 map-registration/manifest blocker was resolved on 2026-09-06. Config/DefaultGame.ini now registers /SeinARTSFramework/Demo/LVL_DemoSkirmish in AvailableMaps with SlotCount=1 and TeamCount=1. The editor log records successful manifest generation/save at 09:06:57 local time, and the saved manifest contains the demo map path. The guide itself uses its newly created /Game/Demo/LVL_DemoSkirmish map; RJ's existing reference map is /SeinARTSFramework/Demo/LVL_DemoSkirmish. Do not conflate their paths. The latest inspected log does not establish a new PIE camera/marquee pass.

RJ requested committing and pushing all current SeinARTS repository work, then standing by for an explicit order to proceed. Do not begin the Soldier chapter until that order arrives. Images will be added by RJ later and are not a blocker. Do not require a Soldier as a prerequisite to the guide's camera/marquee checkpoint. No clean-project playthrough is claimed.

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
