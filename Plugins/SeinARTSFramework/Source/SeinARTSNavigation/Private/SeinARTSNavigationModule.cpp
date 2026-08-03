/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNavigationModule.cpp
 * @brief   Module startup + nav debug toggle.
 *
 *          `Sein.Nav.Show [0|1|on|off]` toggles UE's per-
 *          viewport `ShowFlags.Navigation` flag (same bit as the 'P' hotkey
 *          and `showflag.navigation`). That flag drives:
 *            - Cell viz via `USeinNavDebugComponent`'s scene proxy
 *              (`GetViewRelevance` gates on `EngineShowFlags.Navigation`)
 *            - Per-active-move path cell highlights + waypoint lines, drawn
 *              by the SeinARTSMovement module's ticker — that ticker gates on
 *              `UE::SeinARTSNavigation::IsNavigationShowFlagOnForWorld` so a
 *              single toggle drives both visuals.
 *
 *          `Sein.Nav.Show.Layer <bit>` filters the nav debug viz to only
 *          render blockers that affect the selected agent layer. No-args
 *          resets to "show all". Storage is module-public via
 *          `TryGetDebugNavLayerOverride` so impl `CollectDebugBlockerCells`
 *          calls can honor the filter.
 *
 *          Shipping strip: the ticker registration, console command, and all
 *          helper draw functions are gated on UE_ENABLE_DEBUG_DRAWING. Shipping
 *          still registers the native simulation-content contributor, but no
 *          console command is registered.
 */

#include "SeinARTSNavigationModule.h"
#include "SeinARTSNavigationLog.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Serialization/SeinNavigationCanonicalStateProvider.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UObjectIterator.h"

// Module-shared log categories (declared in SeinARTSNavigationLog.h) — defined
// once here so they register at module load and always show in the Output Log
// category filter.
DEFINE_LOG_CATEGORY(LogSeinNavSubsystem);
DEFINE_LOG_CATEGORY(LogSeinNavigationAStar);
DEFINE_LOG_CATEGORY(LogSeinNavDebug);
DEFINE_LOG_CATEGORY(LogSeinNavBlockerStamp);

namespace
{
	FSeinSimulationContentDiscoveryRoot MakePackageDiscoveryRoot(
		const UClass* RootClass)
	{
		check(RootClass);

		FSeinSimulationContentDiscoveryRoot Root;
		Root.RootClassPath = RootClass->GetPathName();
		Root.StableRecordKindId =
			FSeinSimulationContentManifestCodec::GetCurrentRecordKindId();
		Root.RecordRevision =
			FSeinSimulationContentManifestCodec::CurrentRecordRevision;
		return Root;
	}
}

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinNavDebugComponent.h"
#include "Volumes/SeinLevelVolume.h"

#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "ShowFlags.h"
#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "Editor.h"
#endif
#endif // UE_ENABLE_DEBUG_DRAWING

IMPLEMENT_MODULE(FSeinARTSNavigationModule, SeinARTSNavigation)

#if UE_ENABLE_DEBUG_DRAWING
namespace UE::SeinARTSNavigation
{
	// Layer override for the nav debug viz. -1 = no override (every blocker
	// renders). 0..7 = pinned layer bit; CollectDebugBlockerCells filters to
	// blockers whose BlockedNavLayerMask has that bit set. Lets a designer
	// audit "what blocks my Amphibious unit?" by selecting its layer bit.
	// Storage lives in the named namespace (not anonymous) so the public
	// TryGetDebugNavLayerOverride helper below can read it.
	int32 GDebugNavLayerOverride = -1;

	bool TryGetDebugNavLayerOverride(int32& OutBitIndex)
	{
		if (GDebugNavLayerOverride >= 0 && GDebugNavLayerOverride <= 7)
		{
			OutBitIndex = GDebugNavLayerOverride;
			return true;
		}
		return false;
	}

	/** True iff some viewport rendering `World` currently has
	 *  `ShowFlags.Navigation` enabled.
	 *
	 *  Per-world AND multi-context: matches only viewports whose
	 *  `GetWorld() == World`, but enumerates every `FWorldContext`'s
	 *  GameViewport so multi-client PIE works (each PIE instance has its
	 *  own GameViewport; `GEngine->GameViewport` only points at one of
	 *  them). This restores symmetry with the cell-quad scene proxy's
	 *  `GetViewRelevance` check, which is per-view: when the user toggles
	 *  the flag off on their visible viewport, both the cell quads and
	 *  the ticker draws disappear together.
	 *
	 *  `DrawDebug*` primitives bind to a world's line batcher and render
	 *  for any viewport showing that world. So if any viewport rendering
	 *  this world has the flag on, drawing for this world is correct —
	 *  the user sees the draws in that viewport. If no viewport rendering
	 *  this world has the flag, returning false skips the draws entirely. */
	bool IsNavigationShowFlagOnForWorld(UWorld* World)
	{
		if (!World) return false;

#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && Vp->GetWorld() == World && Vp->EngineShowFlags.Navigation) return true;
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport
				    && Ctx.GameViewport->GetWorld() == World
				    && Ctx.GameViewport->EngineShowFlags.Navigation)
				{
					return true;
				}
			}
		}
		return false;
	}
}

namespace
{
	IConsoleCommand* GShowNavCmd = nullptr;
	IConsoleCommand* GLayerPerspectiveCmd = nullptr;

	// Console-command intent for the Navigation showflag. Console toggles set
	// this AND the live viewport flags. The PIE-start hook reads this when a
	// new game viewport spins up so toggling Nav-on before clicking Play
	// carries through into PIE — the game viewport doesn't exist at toggle
	// time, so the live-viewport set in SetNavigationShowFlag has no effect
	// on it. Without the intent + hook, designers see nothing in PIE despite
	// stamping working correctly.
	bool GShowNavigationIntent = false;
#if WITH_EDITOR
	FDelegateHandle GPostPIEStartedHandle;
#endif

	static void MarkAllNavDebugProxiesDirty();

	static void SetNavigationShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					Vp->EngineShowFlags.SetNavigation(bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		// Iterate every FWorldContext's GameViewport — multi-client PIE has
		// multiple game viewports and only one is GEngine->GameViewport. The
		// previous (only-GameViewport) version applied the toggle to one PIE
		// window, leaving others stuck at whatever default the showflag
		// initialized to.
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport)
				{
					Ctx.GameViewport->EngineShowFlags.SetNavigation(bEnable);
				}
			}
		}

		// Persist intent so a future PIE startup picks it up even though
		// the GameViewport above didn't exist at toggle time.
		GShowNavigationIntent = bEnable;

		// Hidden proxies intentionally ignore nav mutation broadcasts. Refresh
		// every proxy when the viewer is enabled so the first visible frame uses
		// current static and blocker data.
		if (bEnable)
		{
			MarkAllNavDebugProxiesDirty();
		}
	}

	static bool IsNavigationShowFlagOn()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && Vp->EngineShowFlags.Navigation) return true;
			}
		}
#endif
		// All FWorldContexts (multi-client PIE has multiple).
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport && Ctx.GameViewport->EngineShowFlags.Navigation) return true;
			}
		}
		return false;
	}

	static void OnShowNavigationCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		bool bEnable = !IsNavigationShowFlagOn();
		if (Args.Num() > 0)
		{
			const FString& A = Args[0];
			if (A == TEXT("0") || A.Equals(TEXT("off"),  ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bEnable = false;
			}
			else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase) || A.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bEnable = true;
			}
		}
		SetNavigationShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("Sein.Nav.Show = %s (ShowFlags.Navigation)"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
	}

	/** Force every nav debug proxy to rebuild — picks up the new layer
	 *  filter immediately instead of waiting for the next blocker mutation. */
	static void MarkAllNavDebugProxiesDirty()
	{
		for (TObjectIterator<USeinNavDebugComponent> It; It; ++It)
		{
			if (IsValid(*It))
			{
				It->MarkRenderStateDirty();
			}
		}
	}

	static void OnLayerPerspectiveCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		if (Args.Num() == 0)
		{
			UE::SeinARTSNavigation::GDebugNavLayerOverride = -1;
			UE_LOG(LogTemp, Log, TEXT("Sein.Nav.Show.Layer = ALL (reset)"));
			MarkAllNavDebugProxiesDirty();
			return;
		}
		const int32 Parsed = FCString::Atoi(*Args[0]);
		if (Parsed < 0 || Parsed > 7)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Sein.Nav.Show.Layer: expected 0-7 (0=Default, 1..7=N0..N6), got %s"),
				*Args[0]);
			return;
		}
		UE::SeinARTSNavigation::GDebugNavLayerOverride = Parsed;
		static const TCHAR* LayerNames[] = {
			TEXT("Default"),
			TEXT("N0"), TEXT("N1"), TEXT("N2"), TEXT("N3"), TEXT("N4"), TEXT("N5"), TEXT("N6")
		};
		UE_LOG(LogTemp, Log,
			TEXT("Sein.Nav.Show.Layer = bit %d (%s). Use no-args to reset to ALL."),
			Parsed, LayerNames[Parsed]);
		MarkAllNavDebugProxiesDirty();
	}
}
#endif // UE_ENABLE_DEBUG_DRAWING

void FSeinARTSNavigationModule::StartupModule()
{
	CanonicalStateRegistrationHandle.Reset();
	SimulationContentRegistrationHandle.Reset();

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.StableContributorId = TEXT("seinarts.navigation");
	ContentDescriptor.ContributorRevision = 1;
	ContentDescriptor.DiscoveryRoots = {
		MakePackageDiscoveryRoot(USeinNavigation::StaticClass()),
	};

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinNavSubsystem,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}

	FString StateRegistrationError;
	CanonicalStateRegistrationHandle =
		SeinRegisterNavigationCanonicalStateProvider(
			StateRegistrationError);
	if (!CanonicalStateRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinNavSubsystem,
			Error,
			TEXT("Canonical-state contributor 'seinarts.navigation.async-path-continuation' failed to register: %s"),
			*StateRegistrationError);
	}

#if UE_ENABLE_DEBUG_DRAWING
	// Host the nav cell viz on the unified level volume. ASeinLevelVolume can't
	// link this module (dependencies point the other way), so it exposes a
	// debug-component registry: register our component class here and the
	// volume attaches one transient instance in PostRegisterAllComponents.
	// Same guard as the component's scene-proxy availability.
	ASeinLevelVolume::RegisterDebugComponentClass(USeinNavDebugComponent::StaticClass());

	if (!GShowNavCmd)
	{
		GShowNavCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Nav.Show"),
			TEXT("Toggle ShowFlags.Navigation across all viewports (same bit as UE's 'P' hotkey and `showflag.navigation`). Drives USeinNavDebugComponent cell viz + per-action path highlights (path overlay lives in SeinARTSMovement). Usage: Sein.Nav.Show [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowNavigationCommand),
			ECVF_Default);
	}
	if (!GLayerPerspectiveCmd)
	{
		GLayerPerspectiveCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Nav.Show.Layer"),
			TEXT("Filter the nav debug viewer to render only blockers that affect a specific agent layer bit. Usage: Sein.Nav.Show.Layer <bit 0-7>. 0 = Default, 1..7 = N0..N6 (names from NavLayers settings). No args → reset to ALL (every blocker rendered). Useful for auditing 'what blocks my Amphibious unit?' by selecting that layer's bit."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnLayerPerspectiveCommand),
			ECVF_Default);
	}

#if WITH_EDITOR
	// PIE-start hook: when a fresh PIE session spins up, the GameViewport
	// has just been created and any console-toggled Nav showflag intent
	// hasn't reached it yet (the toggle ran when GameViewport was null).
	// Apply the stored intent here so toggling Nav-on in the editor before
	// hitting Play correctly carries through into PIE.
	GPostPIEStartedHandle = FEditorDelegates::PostPIEStarted.AddLambda(
		[](const bool /*bIsSimulating*/)
		{
			if (!GShowNavigationIntent || !GEngine) return;
			// Apply intent to every PIE GameViewport — multi-client PIE has
			// one per client + one per server, all created during/after this
			// delegate fires.
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport)
				{
					Ctx.GameViewport->EngineShowFlags.SetNavigation(true);
				}
			}
		});
#endif
#endif
}

void FSeinARTSNavigationModule::PreUnloadCallback()
{
	check(IsInGameThread());

#if UE_ENABLE_DEBUG_DRAWING
	// Destroys live components and drains their scene proxies before this
	// module's component vtable can disappear.
	ASeinLevelVolume::UnregisterDebugComponentClass(
		USeinNavDebugComponent::StaticClass());
#endif

	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSNavigation"),
				TEXT("navigation systems and canonical continuation state are unloading"));
		}
	}

	for (TObjectIterator<USeinNavigationSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}

	// Frozen worlds retain only tokens, while the process registry owns
	// module TFunctions. Withdraw this exact generation before code unload.
	CanonicalStateRegistrationHandle.Reset();
	SimulationContentRegistrationHandle.Reset();
}

void FSeinARTSNavigationModule::ShutdownModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	ASeinLevelVolume::UnregisterDebugComponentClass(USeinNavDebugComponent::StaticClass());

	if (GShowNavCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowNavCmd);
		GShowNavCmd = nullptr;
	}
	if (GLayerPerspectiveCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GLayerPerspectiveCmd);
		GLayerPerspectiveCmd = nullptr;
	}

#if WITH_EDITOR
	if (GPostPIEStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(GPostPIEStartedHandle);
		GPostPIEStartedHandle.Reset();
	}
#endif
#endif

	CanonicalStateRegistrationHandle.Reset();
	SimulationContentRegistrationHandle.Reset();
}
