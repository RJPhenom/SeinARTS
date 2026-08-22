/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		PluginSettings.cpp
 * @date:		1/17/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of global plugin settings.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Settings/PluginSettings.h"
#include "Settings/SeinConfigFingerprintRegistry.h"
#include "Data/SeinReplayHeader.h"
#include "Formations/SeinBoxFormation.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SeinARTSCoreEntityLog.h"
#include "Misc/Crc.h"
#include "UObject/UnrealType.h"
// NOTE: `DefaultBrokerResolverClass` is a soft-class-path (no hard dependency). It now defaults
// EXPLICITLY to the framework's plain `USeinDefaultCommandBrokerResolver` (WYSIWYG: an EMPTY/None
// value means loose-unit group dispatch is OFF, so the default must be a real class or a fresh
// project would ship with group orders disabled). Projects wanting cover-aware loose-unit dispatch
// point this at the Cover Extension's resolver (this repo's DefaultGame.ini does exactly that). The
// base settings still name no cover class — full extension decoupling.

// NOTE: the constructor's member-initializer order below MUST match the field
// DECLARATION order in PluginSettings.h (which follows the Project Settings
// category order: Simulation → Level Data → Terrain → Economy → Collision →
// Navigation → Fog Of War → Network → UI → Debug Visualization → Editor
// Preferences) to avoid -Wreorder. Members with in-class initializers (the
// avoidance dials, the minimap defaults, etc.) are not listed here.
USeinARTSCoreSettings::USeinARTSCoreSettings()
	// Simulation.
	: SimulationTickRate(30)
	, MaxTicksPerFrame(5)
	, EffectCountWarningThreshold(256)
	// Level Data. Substrate class defaults EXPLICITLY to the shipped grid; the save folder is
	// /Game/LevelData by convention (regenerable, gitignored). Both are soft paths so this module
	// does not depend on SeinARTSLevelData. Empty LevelDataClass now means the substrate is OFF
	// (WYSIWYG), so the ctor must name the default or a fresh project would boot with no level bake.
	, LevelDataClass(FSoftClassPath(TEXT("/Script/SeinARTSLevelData.SeinLevelDataDefault")))
	, LevelDataSaveFolder({TEXT("/Game/LevelData")})
	// Collision resolver defaults to the shipped Gauss-Seidel resolver. Like
	// NavigationClass / FogOfWarClass it's a soft-class-path string rather than a
	// StaticClass() call — the target lives in THIS module (SeinARTSCoreEntity),
	// so the path could resolve in-module, but the soft path keeps the declaration
	// uniform with the other pluggable-class settings and lets a designer clear it
	// back to the default. (Declared in the Collision section, before Navigation.)
	, CollisionResolverClass(FSoftClassPath(TEXT("/Script/SeinARTSCoreEntity.SeinCollisionResolverDefault")))
	// Navigation. NavigationClass defaults to the shipped A* reference. Hard-coded
	// as a soft-class path string (not a StaticClass() call) because this module
	// (SeinARTSCoreEntity) intentionally does NOT depend on SeinARTSNavigation —
	// the decoupling is the whole point of the pluggable nav architecture.
	, NavigationClass(FSoftClassPath(TEXT("/Script/SeinARTSNavigation.SeinNavigationAStar")))
	, CellSize(FFixedPoint::FromInt(100))
	, MaxStepHeight(FFixedPoint::FromInt(50))
	// Path-pipeline tunable. Budget=32 covers typical RTS group sizes (≤32
	// units) without any per-tick stagger — a 20-unit move resolves all paths
	// same tick. (A* heuristic weight + iteration cap moved to USeinNavigationAStar's CDO.)
	, PathRequestsPerTickBudget(32)
	, NavReachabilityProfileCacheCapacity(8)
	// Nav projection tunables — see PluginSettings.h for rationale on each.
	// 100cm tolerance covers typical curb / step deltas without crossing
	// platform-height boundaries; 30-cell ring radius is ~30m on a 100cm grid,
	// plenty for off-edge slot snapping without sweeping huge blocked regions.
	, NavProjectionElevationTolerance(FFixedPoint::FromInt(100))
	, NavProjectionMaxRingRadius(30)
	// Nav bake island-prune threshold. 16 cells matches the long-standing hardcoded
	// default — small enough to keep legitimate platforms, large enough to drop cube
	// tops / floating slivers. Gated to the shipped A* in the editor (IsUsingShippedAStar).
	, NavMinWalkableIslandCells(16)
	// Default broker resolver + avoidance model default EXPLICITLY to their shipped classes. Empty
	// means the system is OFF (WYSIWYG), so a fresh project would otherwise boot with loose-unit
	// dispatch / avoidance disabled. Both soft paths (no hard dep on the owning module). This repo's
	// DefaultGame.ini overrides the broker to the Cover-aware resolver.
	, DefaultBrokerResolverClass(FSoftObjectPath(TEXT("/Script/SeinARTSCoreEntity.SeinDefaultCommandBrokerResolver")))
	, AvoidanceClass(FSoftClassPath(TEXT("/Script/SeinARTSMovement.SeinAvoidanceDefault")))
	// Formation preview actor defaults to the shipped mesh-quad preview. Soft path (no hard dep on
	// SeinARTSFramework). Empty now means the preview is OFF (WYSIWYG), so the ctor must name the
	// default or a fresh project would render no destination preview.
	, FormationPreviewActorClass(FSoftClassPath(TEXT("/Script/SeinARTSFramework.SeinFormationPreviewActor")))
	// Fog of War. Soft-class-path default follows the same nav/relay decoupling.
	, FogOfWarClass(FSoftClassPath(TEXT("/Script/SeinARTSFogOfWar.SeinFogOfWarDefault")))
	, VisionCellSize(FFixedPoint::FromInt(100))
	, VisionTickInterval(3)
	, FogRenderTickRate(10.0f)
	// Command protocol. None is fail-closed, so the shipped owner+grant policy
	// is named explicitly rather than recovered as a hidden fallback.
	, CommandAuthorityPolicyClass(FSoftClassPath(TEXT("/Script/SeinARTSCoreEntity.SeinDefaultCommandAuthorityPolicy")))
	, MaxCommandsPerSubmission(256)
	// Network defaults — see PluginSettings.h for rationale on each. Soft path
	// for the relay class follows the established nav/fog decoupling: this module
	// deliberately does NOT depend on SeinARTSNet.
	, bNetworkingEnabled(true)
#if WITH_EDITORONLY_DATA
	, bAutoStartMultiplayerPIE(true)
#endif
	, TurnRate(10)
	, InputDelayTurns(3)
	, MaxPlayers(8)
	, RelayActorClass(FSoftClassPath(TEXT("/Script/SeinARTSNet.SeinNetRelay")))
	, bDeterminismChecksEnabled(true)
	, DeterminismCheckIntervalTurns(10)
	// Replay recording policy. These bound local memory/disk work only and are
	// deliberately absent from ComputeConfigFingerprint's sim-affecting field list.
	, ReplayCheckpointIntervalTurns(3000)
	, ReplayTurnBatchSize(64)
	, ReplayMaxFileSizeMiB(16384)
	// Drop-in/drop-out: BasicAI policy + 30s grace period default. Ships
	// `USeinNullAIController` as the framework no-op fallback so the auto-spawn
	// path is exercised end-to-end even before designers wire their own AI
	// subclass — same "minimal reference impl" pattern as the shipped A* nav and
	// default fog. Soft-class-path string because this module deliberately can't
	// reach into project code.
	, SlotDropPolicy(ESeinSlotDropPolicy::BasicAI)
	, DefaultAIControllerClass(FSoftClassPath(TEXT("/Script/SeinARTSCoreEntity.SeinNullAIController")))
	, DroppedToAITakeoverSeconds(30.0)
	, DebugFixedSessionSeed(0)
	// Lobby defaults — soft paths empty by default (designer ships their own
	// preset / maps). FactionServiceClass falls back to the framework default
	// `USeinFactionService` (AssetRegistry scan) when empty. Reconnect grace 60s
	// is the sweet spot for network blips without leaving slots reserved long
	// enough to block re-joining players in casual lobbies.
	, FactionServiceClass(FSoftClassPath(TEXT("/Script/SeinARTSCoreEntity.SeinFactionService")))
	, LobbyReconnectGraceSeconds(60.0f)
	// Debug visualization defaults. 10000 / 50000 / true:
	//   - 100m view distance covers a typical zoom-out shot of active combat
	//   - 50000-cell-per-bucket cap renders a generous in-view grid even on fine
	//     cell sizes (50cm) at typical zoom levels; teams can lower for stricter
	//     caps or raise for unbounded viz.
	, DebugDrawMaxDistance(10000.0f)
	, DebugDrawMaxEntities(50000)
	, bDebugDrawFrustumCullEnabled(true)
	// Tag Semantics defaults — auto-tag generation on, "SeinARTS" namespace,
	// layering on. PrefixCategoryMappings is body-initialized below since
	// TArray<USTRUCT> + ctor-initialization is verbose.
	, bEnableAutoTagGeneration(true)
	, TagPrefix(TEXT("SeinARTS"))
	, bAllowTagLayering(true)
#if WITH_EDITORONLY_DATA
	, bShowAbilityInBasicCategory(true)
	, bShowComponentInBasicCategory(false)
	, bShowEffectInBasicCategory(true)
	, bShowEntityInBasicCategory(true)
	, bShowWidgetInBasicCategory(false)
#endif
{
	// Vision layers: exactly 6 designer-configurable slots (N0..N5). All start
	// disabled + unnamed — the framework-default "Normal" layer is reserved as
	// the V bit and is always present without needing a slot here. Designers
	// opt in by naming + enabling slots for game-specific channels (Stealth,
	// Thermal, etc.). EditFixedSize prevents add/remove — rename or toggle only.
	// Note: we SetNum + seed here, but config load after the ctor can stomp
	// this — PostInitProperties reconciles.
	VisionLayers.SetNum(6);

	// Nav layers: 7 slots (bits 1..7). Default is reserved as bit 0. Same
	// stability + opt-in story as vision layers.
	NavLayers.SetNum(7);

	// Tag prefix → category mappings. Ships with SeinARTS conventions.
	// Downstream teams add or replace entries (e.g. MA→Ability for a
	// MyGame-namespaced project). Order doesn't matter at lookup time
	// (linear scan, first match wins) but the table is short.
	PrefixCategoryMappings.Reset();
	PrefixCategoryMappings.Add(FSeinTagPrefixMapping(TEXT("SA"),  TEXT("Ability")));
	PrefixCategoryMappings.Add(FSeinTagPrefixMapping(TEXT("SU"),  TEXT("Unit")));
	PrefixCategoryMappings.Add(FSeinTagPrefixMapping(TEXT("SE"),  TEXT("Effect")));
	PrefixCategoryMappings.Add(FSeinTagPrefixMapping(TEXT("SR"),  TEXT("Research")));
	PrefixCategoryMappings.Add(FSeinTagPrefixMapping(TEXT("ST"),  TEXT("Tech")));
	PrefixCategoryMappings.Add(FSeinTagPrefixMapping(TEXT("SBP"), TEXT("Entity")));

	// Collision channels default EMPTY — the editable array holds only ADDITIONAL
	// designer channels. The reserved "Default" channel lives OUTSIDE the array
	// (see GetAllCollisionChannels), so it's always present and can't be removed.

	// Project default order formation = the framework Box (a rank-and-file block). Set in the
	// ctor body (not the init list) so the header needs only a forward-declared USeinFormation.
	DefaultFormation = USeinBoxFormation::StaticClass();
}

namespace
{
	// Canonical per-slot debug colors. Authored as sRGB hex and converted
	// via `FLinearColor(FColor)` so the picker in settings shows the exact
	// hex the user intended (hex is sRGB by convention; UE's display pipe
	// gamma-corrects linear → sRGB when rendering, so going hex → FColor
	// → FLinearColor preserves the visual intent).
	//  Slot 0 (N0) 0000FF · Slot 1 (N1) 9100FF · Slot 2 (N2) E700D6
	//  Slot 3 (N3) FF5A72 · Slot 4 (N4) D4FF83 · Slot 5 (N5) 00FFA1
	static const FLinearColor DefaultLayerColors[6] = {
		FLinearColor(FColor::FromHex(TEXT("0000FF"))),
		FLinearColor(FColor::FromHex(TEXT("9100FF"))),
		FLinearColor(FColor::FromHex(TEXT("E700D6"))),
		FLinearColor(FColor::FromHex(TEXT("FF5A72"))),
		FLinearColor(FColor::FromHex(TEXT("D4FF83"))),
		FLinearColor(FColor::FromHex(TEXT("00FFA1"))),
	};

	// Per-slot canonical nav-layer colors. Warm / pink-red / earth-tone
	// spectrum — intentionally avoids greens and cyans (those clash with the
	// vision-layer palette and the static nav cell green/red). Each slot is
	// distinct enough at-a-glance for layered debug viz to read clearly.
	//  Slot 0 (N0) FFA500 · Slot 1 (N1) FFD600 · Slot 2 (N2) FA8072
	//  Slot 3 (N3) DDA0DD · Slot 4 (N4) FF6347 · Slot 5 (N5) C71585
	//  Slot 6 (N6) 8B4513
	static const FLinearColor DefaultNavLayerColors[7] = {
		FLinearColor(FColor::FromHex(TEXT("FFA500"))),
		FLinearColor(FColor::FromHex(TEXT("FFD600"))),
		FLinearColor(FColor::FromHex(TEXT("FA8072"))),
		FLinearColor(FColor::FromHex(TEXT("DDA0DD"))),
		FLinearColor(FColor::FromHex(TEXT("FF6347"))),
		FLinearColor(FColor::FromHex(TEXT("C71585"))),
		FLinearColor(FColor::FromHex(TEXT("8B4513"))),
	};
}

void USeinARTSCoreSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Config load (DefaultEngine.ini) runs AFTER the ctor. An older INI can
	// shrink the array or leave DebugColor at the struct default (white).
	// Force exactly 6 slots and re-seed any color that's still plain white
	// with the per-slot canonical value — idempotent on fresh projects,
	// corrective on legacy ones.
	if (VisionLayers.Num() != 6)
	{
		VisionLayers.SetNum(6);
	}
	for (int32 i = 0; i < 6; ++i)
	{
		if (VisionLayers[i].DebugColor == FLinearColor::White)
		{
			VisionLayers[i].DebugColor = DefaultLayerColors[i];
		}
	}

	// Same reconcile pattern for nav layers — enforce slot count, re-seed
	// any white DebugColor to the canonical per-slot value.
	if (NavLayers.Num() != 7)
	{
		NavLayers.SetNum(7);
	}
	for (int32 i = 0; i < 7; ++i)
	{
		if (NavLayers[i].DebugColor == FLinearColor::White)
		{
			NavLayers[i].DebugColor = DefaultNavLayerColors[i];
		}
	}

	// DefaultBrokerResolverClass is left at whatever the config loaded (the ctor
	// default is now the plain framework resolver; empty/None means loose-unit
	// group dispatch is OFF). No cover-flavored reseed here — cover-aware dispatch
	// is an opt-in the project wires via the setting, keeping the base framework
	// free of any cover class reference.

	// Push the simulation-performance toggles (now resolved from config) to their
	// backing cvars so the sim systems see the project's chosen defaults from boot.
	ApplySimPerformanceCvars();
}

void USeinARTSCoreSettings::ApplySimPerformanceCvars() const
{
	IConsoleManager& CM = IConsoleManager::Get();

	// ECVF_SetByProjectSetting: a later manual console command (ECVF_SetByConsole, higher
	// priority) still overrides these — so live A/B testing via the console keeps working.
	if (IConsoleVariable* CV = CM.FindConsoleVariable(TEXT("Sein.Sim.Parallel")))
	{
		CV->Set(bParallelSimulation ? 1 : 0, ECVF_SetByProjectSetting);
	}
	if (IConsoleVariable* CV = CM.FindConsoleVariable(TEXT("Sein.Sim.ParallelMinBatch")))
	{
		CV->Set(ParallelMinBatch, ECVF_SetByProjectSetting);
	}
	if (IConsoleVariable* CV = CM.FindConsoleVariable(TEXT("Sein.Sim.AsyncPathfinding")))
	{
		CV->Set(bAsyncPathfinding ? 1 : 0, ECVF_SetByProjectSetting);
	}
}

void USeinARTSCoreSettings::ReportDisabledSystem(const TCHAR* SystemName, const TCHAR* Detail, bool bHighSeverity)
{
#if !UE_BUILD_SHIPPING
	// Development-only, render-side notice — it must NEVER touch hashed sim state (and is compiled out
	// of shipping). Shown in game, PIE, and editor worlds alike (the editor Bake Level Data path
	// needs it), with the log deduped per system per session so transient/preview worlds can't spam.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (Settings && Settings->bSuppressDisabledSystemWarnings)
	{
		return;
	}
	const FString Message = FString::Printf(TEXT("SeinARTS: %s is OFF (its class is None). %s"), SystemName, Detail);

	// Dedupe the LOG line per system per session — some callers (broker / formation) resolve
	// per-order, so an undeduped UE_LOG would spam. This set is a pure render-side debug side
	// channel and never feeds back into hashed sim state. The on-screen message needs no set: its
	// stable per-system key refreshes the same line in place instead of stacking.
	const FName SysKey(SystemName);
	static TSet<FName> LoggedOnce;
	if (!LoggedOnce.Contains(SysKey))
	{
		LoggedOnce.Add(SysKey);
		UE_LOG(LogSeinSim, Warning, TEXT("%s  Suppress via Project Settings > SeinARTS > Debug Visualization."), *Message);
	}
	if (GEngine)
	{
		// Red for high-severity off-states (nav / level data / broker / net relay break a core flow),
		// orange (ff9900) for benign "no-X" modes.
		const FColor Colour = bHighSeverity ? FColor(255, 0, 0) : FColor(255, 153, 0);
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetTypeHash(SysKey)), 3600.0f, Colour, Message);
	}
#else
	(void)SystemName; (void)Detail; (void)bHighSeverity;
#endif
}

int32 USeinARTSCoreSettings::ComputeConfigFingerprint() const
{
	// SIM-AFFECTING settings that MUST be byte-identical across every client in a lockstep session.
	// EXCLUDES render / transport / lobby / editor / debug fields (they may legitimately differ per
	// machine — e.g. FormationPreviewActorClass, RelayActorClass, the minimap/debug-viz knobs). The
	// list order is fixed and each value is exported by reflection (ExportText → a value-based string:
	// the path for a soft-class, the element list for an array), so the hash is deterministic across
	// machines/builds. Keep this list in sync when adding a sim-affecting setting. NOTE: tuning that
	// lives on a pluggable class's CDO (the A* search knobs on USeinNavigationAStar, the avoidance
	// model knobs on USeinAvoidanceDefault) is intentionally NOT listed here — its determinism is
	// carried by the class-PATH token (NavigationClass / AvoidanceClass) plus identical compiled/asset
	// content, exactly as Formation's tuning is. Only tuning that still lives on THIS settings object
	// belongs in this list.
	static const TCHAR* const Fields[] = {
		TEXT("SimulationTickRate"), TEXT("TurnRate"), TEXT("InputDelayTurns"), TEXT("bAsyncPathfinding"),
		TEXT("CommandAuthorityPolicyClass"), TEXT("CommandHandlerClasses"), TEXT("MaxCommandsPerSubmission"),
		TEXT("NavigationClass"), TEXT("CollisionResolverClass"), TEXT("AvoidanceClass"), TEXT("FogOfWarClass"),
		TEXT("LevelDataClass"), TEXT("DefaultBrokerResolverClass"), TEXT("DefaultFormation"),
		TEXT("CellSize"), TEXT("MaxStepHeight"), TEXT("CollisionMassRatioCutoff"), TEXT("PathRequestsPerTickBudget"),
		TEXT("NavProjectionElevationTolerance"), TEXT("NavProjectionMaxRingRadius"), TEXT("NavMinWalkableIslandCells"),
		TEXT("AvoidanceMovingSpeedFloor"), TEXT("AvoidanceBendCapCos"), TEXT("AvoidanceIdleDodgeStepSpeed"),
		TEXT("bSettleToFormationFacing"),
		TEXT("bIdleReseek"), TEXT("ReseekDisplacementThreshold"),
		TEXT("ReseekWatchInterval"), TEXT("ReseekReleaseInterval"), TEXT("ReseekMaxEpisodeSeconds"),
		TEXT("VisionCellSize"), TEXT("VisionTickInterval"),
		TEXT("NavLayers"), TEXT("TerrainTypes"), TEXT("CollisionChannels"), TEXT("VisionLayers"),
		TEXT("ResourceCatalog"), TEXT("RegisteredFactions"),
	};

	FString Fp;
	Fp.Reserve(2048);
	// Compiled deterministic behavior is part of live admission, not only
	// snapshot/replay compatibility. Bumping the epoch makes mixed binaries
	// fail the existing config-parity barrier before lockstep starts.
	Fp += TEXT("FrameworkBehaviorEpoch=");
	Fp += SeinReplayCompatibility::GetFrameworkVersion();
	Fp += TEXT(";");
	const UClass* Cls = GetClass();
	for (const TCHAR* FieldName : Fields)
	{
		Fp += FieldName;
		Fp += TEXT("=");
		if (const FProperty* Prop = FindFProperty<FProperty>(Cls, FieldName))
		{
			FString Value;
			Prop->ExportText_InContainer(0, Value, this, nullptr, nullptr, PPF_None);
			Fp += Value;
		}
		Fp += TEXT(";");
	}
	// Recipe composition is a semantic set: execution is canonically ordered by
	// stable contributor ID, so a harmless Project Settings reorder must not
	// change lockstep compatibility.
	TArray<FString> CanonicalRecipePaths;
	CanonicalRecipePaths.Reserve(CanonicalStateRecipes.Num());
	for (const TSoftClassPtr<USeinCanonicalStateRecipe>& Recipe :
		CanonicalStateRecipes)
	{
		CanonicalRecipePaths.Add(
			Recipe.ToSoftObjectPath().ToString());
	}
	CanonicalRecipePaths.Sort();
	Fp += TEXT("CanonicalStateRecipes=");
	for (const FString& RecipePath : CanonicalRecipePaths)
	{
		Fp += FString::Printf(
			TEXT("%d:%s;"),
			RecipePath.Len(),
			*RecipePath);
	}
	// Fold in extension-registered sim-affecting settings (squad, etc.) AFTER the core
	// section. AppendContributors sorts by stable id internally, so the value is
	// independent of module load / registration order (see the registry docstring).
	FSeinConfigFingerprintRegistry::AppendContributors(Fp);
	return static_cast<int32>(FCrc::StrCrc32(*Fp));
}

TArray<FSeinCollisionChannelDefinition> USeinARTSCoreSettings::GetAllCollisionChannels() const
{
	TArray<FSeinCollisionChannelDefinition> Result;

	// Reserved, always-present "Default" channel — NOT stored in the editable
	// CollisionChannels array (so it can't be removed), mirroring the nav
	// "Default" layer / vision "Normal" layer. Block-responds by default.
	FSeinCollisionChannelDefinition DefaultChannel;
	DefaultChannel.Name            = GetDefaultCollisionChannelName();
	DefaultChannel.DefaultResponse = ESeinCollisionResponse::Block;
	Result.Add(DefaultChannel);

	// Additional designer channels — skip unnamed entries and any duplicate of
	// the reserved "Default" name.
	const FName DefaultName = GetDefaultCollisionChannelName();
	for (const FSeinCollisionChannelDefinition& Channel : CollisionChannels)
	{
		if (Channel.Name.IsNone() || Channel.Name == DefaultName) continue;
		Result.Add(Channel);
	}
	return Result;
}

FName USeinARTSCoreSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
void USeinARTSCoreSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Cheap + safe to re-apply on any property change — keeps the perf cvars in sync the
	// moment a designer toggles Parallel Simulation / Async Pathfinding in Project Settings.
	ApplySimPerformanceCvars();
}

FText USeinARTSCoreSettings::GetSectionText() const
{
	return NSLOCTEXT("SeinARTSCore", "SeinARTSCoreSettingsSection", "SeinARTS");
}

FText USeinARTSCoreSettings::GetSectionDescription() const
{
	return NSLOCTEXT("SeinARTSCore", "SeinARTSCoreSettingsDescription",
		"Configure SeinARTS simulation, editor, and content creation settings.");
}
#endif
