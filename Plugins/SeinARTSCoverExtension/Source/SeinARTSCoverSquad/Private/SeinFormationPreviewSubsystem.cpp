/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewSubsystem.cpp
 */

#include "SeinFormationPreviewSubsystem.h"
#include "Preview/SeinFormationPreviewActor.h"

#include "Settings/SeinARTSCoverSettings.h"

#include "Player/SeinPlayerController.h"
#include "Player/SeinTargeterSubsystem.h"
#include "Actor/SeinActor.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Lib/SeinCommandBrokerBPFL.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Core/SeinEntityHandle.h"
#include "SeinARTSFogOfWarModule.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"
#include "Types/Vector.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFormationPreviewSubsystem, Log, All);

namespace SeinFormationPreviewLocal
{
	/** Helper — local PC accessor. Returns nullptr until the PC has been
	 *  spawned (subsystems initialize before PC for the first map load). */
	static ASeinPlayerController* GetSeinPC(const ULocalPlayer* LP, UWorld* World)
	{
		if (!LP || !World) return nullptr;
		return Cast<ASeinPlayerController>(LP->GetPlayerController(World));
	}
}

void USeinFormationPreviewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Try an immediate hook — covers the case where the subsystem is created
	// after the PC already exists (post-PIE, sub-PIE worlds, etc.). When the
	// PC isn't ready yet, the post-load-map delegate below catches it on the
	// first map load.
	HookPlayerControllerDelegates();

	// Re-hook on every map load. Travel destroys the prior PC; the new one
	// won't carry our subscriptions, so we rebind unconditionally and let the
	// idempotent unhook handle the now-dead prior PC. Cover providers in the
	// new world are unrelated to whatever the cache held — flip dirty so the
	// next refresh re-queries.
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(this,
		[this](UWorld* /*LoadedWorld*/)
		{
			HookPlayerControllerDelegates();
			bCoverTintDirty = true;
			CachedCoverQualities.Reset();
		});

	UE_LOG(LogSeinFormationPreviewSubsystem, Log,
		TEXT("USeinFormationPreviewSubsystem initialized (LP=%s) — bound to PC = %s"),
		*GetNameSafe(GetLocalPlayer()), *GetNameSafe(BoundPC.Get()));
}

void USeinFormationPreviewSubsystem::Deinitialize()
{
	UnhookPlayerControllerDelegates();
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	if (PreviewActor)
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}

	bIsVisible = false;
	BoundPC = nullptr;

	Super::Deinitialize();
}

void USeinFormationPreviewSubsystem::HookPlayerControllerDelegates()
{
	UWorld* World = GetWorld();
	ASeinPlayerController* PC = SeinFormationPreviewLocal::GetSeinPC(GetLocalPlayer(), World);

	// PC not ready yet (subsystem created before PC spawn). Bail — the
	// FTickableGameObject Tick path will retry every frame until PC is up.
	if (!PC)
	{
		// Verbose because Tick calls this every frame until bound — we don't
		// want Log-level spam during the first 1-2 seconds of map load while
		// the PC pipeline is still warming up.
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose,
			TEXT("HookPlayerControllerDelegates: PC not ready; will retry next tick"));
		return;
	}

	// If we're already bound to this exact PC, no-op. Avoids double-add when
	// PostLoadMapWithWorld fires on a sub-load that doesn't actually replace
	// the PC.
	if (BoundPC.Get() == PC) return;

	// Drop subscriptions on the prior PC (if it survives) before binding new.
	UnhookPlayerControllerDelegates();

	BoundPC = PC;
	PC->OnSelectionChanged.AddDynamic(this, &USeinFormationPreviewSubsystem::HandleSelectionChanged);
	PC->OnCursorUpdated.AddDynamic(this, &USeinFormationPreviewSubsystem::HandleCursorUpdated);

	UE_LOG(LogSeinFormationPreviewSubsystem, Log,
		TEXT("Bound formation preview to PC %s"), *GetNameSafe(PC));
}

void USeinFormationPreviewSubsystem::UnhookPlayerControllerDelegates()
{
	if (ASeinPlayerController* PC = BoundPC.Get())
	{
		PC->OnSelectionChanged.RemoveDynamic(this, &USeinFormationPreviewSubsystem::HandleSelectionChanged);
		PC->OnCursorUpdated.RemoveDynamic(this, &USeinFormationPreviewSubsystem::HandleCursorUpdated);
	}
	BoundPC = nullptr;
}

void USeinFormationPreviewSubsystem::HandleSelectionChanged()
{
	// Selection change invalidates the cached cover-quality array — the
	// new selection has a different member count and formation footprint,
	// so the previous tints are stale even if the cursor hasn't moved.
	bCoverTintDirty = true;

	// Selection changes that empty the list need to immediately hide the decals
	// (otherwise the player sees stale decals between deselect-frame and the
	// next cursor-tick). Non-empty changes get refreshed with the current
	// cursor position so decals follow new formations within the same frame.
	if (BoundPC.IsValid() && BoundPC->GetSelectionCount() == 0)
	{
		HidePreview();
	}
	else
	{
		RefreshPreview();
	}
}

void USeinFormationPreviewSubsystem::HandleCursorUpdated(FVector CursorWorld, bool bValidTrace)
{
	LastCursorWorld = CursorWorld;
	bLastCursorValid = bValidTrace;
	RefreshPreview();
}

void USeinFormationPreviewSubsystem::RefreshPreview()
{
	const USeinARTSCoverSettings* Settings = GetDefault<USeinARTSCoverSettings>();
	if (Settings && !Settings->bEnableFormationPreview)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose, TEXT("Refresh: bail — bEnableFormationPreview = false"));
		HidePreview();
		return;
	}

	ASeinPlayerController* PC = BoundPC.Get();
	if (!PC)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose, TEXT("Refresh: bail — BoundPC null (delegates not hooked)"));
		HidePreview();
		return;
	}

	// Targeter takes precedence — its preview owns the cursor while active.
	// Without this gate, the formation decals would render on top of building
	// holograms / smoke reticles / etc., which conflicts with the targeter's
	// own visualization.
	if (USeinTargeterSubsystem* Targeter = PC->GetTargeterSubsystem())
	{
		if (Targeter->IsActive())
		{
			UE_LOG(LogSeinFormationPreviewSubsystem, Verbose, TEXT("Refresh: bail — targeter active"));
			HidePreview();
			return;
		}
	}

	if (!bLastCursorValid)
	{
		// Cursor off the world (panned past the map edge, etc.) — hide rather
		// than projecting decals at (0,0,0).
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose, TEXT("Refresh: bail — cursor trace invalid"));
		HidePreview();
		return;
	}

	if (PC->GetSelectionCount() == 0)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose, TEXT("Refresh: bail — selection empty"));
		HidePreview();
		return;
	}

	const TArray<FSeinEntityHandle> Members = ResolveSelectionToMembers(PC);
	if (Members.Num() == 0)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose,
			TEXT("Refresh: bail — selection of %d actors resolved to 0 members (no valid SeinEntityHandle / no squad expansion)"),
			PC->GetSelectionCount());
		HidePreview();
		return;
	}

	const FFixedVector TargetFixed = FFixedVector::FromVector(LastCursorWorld);
	const FSeinFormationLayout Layout = USeinCommandBrokerBPFL::SeinComputeFormationPreview(
		PC, Members, TargetFixed);

	if (Layout.Positions.Num() == 0)
	{
		// Resolver returned empty — likely no world subsystem yet (very early
		// in PIE) or a misconfigured selection. Don't render stale decals.
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose,
			TEXT("Refresh: bail — SeinComputeFormationPreview returned 0 positions for %d members at cursor (%.1f, %.1f, %.1f)"),
			Members.Num(), LastCursorWorld.X, LastCursorWorld.Y, LastCursorWorld.Z);
		HidePreview();
		return;
	}

	EnsurePreviewActorSpawned();
	if (!PreviewActor)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Warning,
			TEXT("Refresh: bail — preview actor failed to spawn (check Settings->FormationPreviewActorClass)"));
		HidePreview();
		return;
	}

	const TArray<FVector> WorldPositions = ConvertPositions(Layout.Positions);

	// Cover-quality query per position: ask the cover system for the best
	// quality tag at each formation cell. The preview actor maps tags →
	// tint colors. No cover at the cell → invalid tag → preview uses
	// NoCoverTint. Skip the entire query loop when the cover system isn't
	// available (cover module loaded but subsystem not initialized for this
	// world) — preview falls back to neutral decals.
	//
	// Throttled: the per-cell loop is O(Members × Providers) and was the
	// dominant per-frame cost on cover-rich maps. We re-query only when:
	//   (a) selection changed (HandleSelectionChanged sets bCoverTintDirty)
	//   (b) cursor moved more than ~25cm since the last query
	//   (c) layout cell count changed (defensive — squad gains/loses a member)
	// Otherwise we reuse the previous frame's CachedCoverQualities array.
	// A 25cm threshold is below the per-decal visual radius, so the lag in
	// tint updates is imperceptible.
	const float CoverTintCursorThresholdSq = 25.f * 25.f;     // 625 cm² = 25cm move
	const bool bCursorMoved = (LastCursorWorld - LastTintQueryCursor).SizeSquared()
		> CoverTintCursorThresholdSq;
	const bool bCellCountChanged = (CachedCoverQualities.Num() != Layout.Positions.Num());

	if (bCoverTintDirty || bCursorMoved || bCellCountChanged)
	{
		CachedCoverQualities.Reset(Layout.Positions.Num());
		if (USeinCoverSystem* CoverSys = USeinCoverSubsystem::GetCoverSystemForWorld(this))
		{
			// Pass the local observer so cover the player can't see doesn't
			// tint the preview (would leak information about unscouted cover).
			// Resolved via the same helper the FoW visibility subsystem uses
			// to identify the local viewer.
			const FSeinPlayerID Observer = UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(GetWorld());
			for (const FFixedVector& Pos : Layout.Positions)
			{
				CachedCoverQualities.Add(CoverSys->QueryBestCoverQualityAt(Pos, Observer));
			}
		}
		LastTintQueryCursor = LastCursorWorld;
		bCoverTintDirty = false;
	}
	const TArray<FGameplayTag>& CoverQualities = CachedCoverQualities;

	UE_LOG(LogSeinFormationPreviewSubsystem, Verbose,
		TEXT("Refresh: showing %d decals at cursor (%.1f, %.1f, %.1f), Members=%d, CoverTags=%d"),
		WorldPositions.Num(), LastCursorWorld.X, LastCursorWorld.Y, LastCursorWorld.Z, Members.Num(),
		CoverQualities.Num());
	PreviewActor->SetPositions(WorldPositions, CoverQualities);
	PreviewActor->SetActorHiddenInGame(false);
	bIsVisible = true;
}

void USeinFormationPreviewSubsystem::HidePreview()
{
	if (PreviewActor)
	{
		PreviewActor->HideAll();
		PreviewActor->SetActorHiddenInGame(true);
	}
	bIsVisible = false;
}

void USeinFormationPreviewSubsystem::EnsurePreviewActorSpawned()
{
	if (PreviewActor) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Resolve the configured class — settings override or framework default.
	// FormationPreviewActorClass lives on USeinARTSCoverSettings (the Cover
	// Extension's own settings page), so the cover/squad bridge reads it
	// directly without any base-framework settings coupling.
	TSubclassOf<ASeinFormationPreviewActor> ActorClass;
	if (const USeinARTSCoverSettings* Settings = GetDefault<USeinARTSCoverSettings>())
	{
		if (Settings->FormationPreviewActorClass.IsValid())
		{
			ActorClass = Settings->FormationPreviewActorClass.TryLoadClass<ASeinFormationPreviewActor>();
		}
	}
	if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract))
	{
		ActorClass = ASeinFormationPreviewActor::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags = RF_Transient;
	PreviewActor = World->SpawnActor<ASeinFormationPreviewActor>(
		ActorClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);

	if (!PreviewActor)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Warning,
			TEXT("EnsurePreviewActorSpawned: failed to spawn preview actor of class %s"),
			*GetNameSafe(ActorClass));
		return;
	}

	PreviewActor->SetActorHiddenInGame(true);     // start hidden until first SetPositions

	UE_LOG(LogSeinFormationPreviewSubsystem, Log,
		TEXT("Spawned formation preview actor (%s)"), *GetNameSafe(PreviewActor));
}

TArray<FSeinEntityHandle> USeinFormationPreviewSubsystem::ResolveSelectionToMembers(ASeinPlayerController* PC) const
{
	TArray<FSeinEntityHandle> Out;
	if (!PC) return Out;

	UWorld* World = PC->GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return Out;

	// Iterate the live selection (post-stale-purge), expanding squads to members.
	// ALL-OR-NOTHING preview opt-in: the moment ANY entity that would be in the
	// formation is opted out, return an empty list to suppress the WHOLE preview
	// (no mixed preview + non-preview members). "Opted out" means:
	//   - a selected squad with FSeinSquadComponent::bShowFormationPreview == false, OR
	//   - any member/lone unit with FSeinNavigationComponent::bShowNavigationPreview == false, OR
	//   - any member/lone unit with NO FSeinNavigationComponent (can't move, so no
	//     destination opinion — counts as opted out, per design).
	//   A. SQUAD: a selected entity carrying FSeinSquadComponent resolves to its
	//      live members (selecting any member resolves up to the squad).
	//   B. LONE-UNIT (no FSeinSquadComponent): a directly-selected movable unit.
	for (const TWeakObjectPtr<ASeinActor>& Weak : PC->SelectedActors)
	{
		ASeinActor* Actor = Weak.Get();
		if (!Actor) continue;
		const FSeinEntityHandle Handle = Actor->GetEntityHandle();
		if (!Handle.IsValid()) continue;

		// Path A: squad-level dispatch.
		if (const FSeinSquadComponent* SquadData = WorldSub->GetComponent<FSeinSquadComponent>(Handle))
		{
			// ALL-OR-NOTHING: the squad must opt in (bShowFormationPreview) AND
			// every live member must have a nav component and opt in
			// (bShowNavigationPreview). If any one fails, suppress the WHOLE
			// preview (return empty — RefreshPreview hides on an empty list). A
			// member with NO nav component counts as opted out: it can't move, so
			// it has no destination opinion.
			if (!SquadData->bShowFormationPreview) return {};
			for (const FSeinEntityHandle& Member : SquadData->GetLiveMembers())
			{
				const FSeinNavigationComponent* MemberNav =
					WorldSub->GetComponent<FSeinNavigationComponent>(Member);
				if (!MemberNav || !MemberNav->bShowNavigationPreview) return {};
				Out.Add(Member);
			}
			continue;
		}

		// Path B: lone-unit dispatch.
		// Lone unit — ALL-OR-NOTHING: must have a nav component AND opt in. No nav
		// component or opted out → suppress the whole preview.
		const FSeinNavigationComponent* NavComp =
			WorldSub->GetComponent<FSeinNavigationComponent>(Handle);
		if (!NavComp || !NavComp->bShowNavigationPreview) return {};
		Out.Add(Handle);
	}

	return Out;
}

TArray<FVector> USeinFormationPreviewSubsystem::ConvertPositions(const TArray<FFixedVector>& In)
{
	TArray<FVector> Out;
	Out.Reserve(In.Num());
	for (const FFixedVector& P : In)
	{
		Out.Add(P.ToVector());
	}
	return Out;
}

// ====================================================================================================
// FTickableGameObject — lazy-bind to PC every frame until bound. Once bound,
// HookPlayerControllerDelegates early-outs in O(1) on the BoundPC == PC check.
// ====================================================================================================

void USeinFormationPreviewSubsystem::Tick(float /*DeltaTime*/)
{
	// Fast path: already bound to current PC, nothing to do. HookPlayerControllerDelegates
	// does this check internally and bails immediately; calling it unconditionally each
	// frame keeps the binding logic in one place + handles map-travel rebinding for free
	// (after travel, BoundPC is stale and a new PC exists → hook fires re-bind).
	HookPlayerControllerDelegates();
}

TStatId USeinFormationPreviewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USeinFormationPreviewSubsystem, STATGROUP_Tickables);
}

bool USeinFormationPreviewSubsystem::IsTickable() const
{
	// Don't tick the CDO / class template. Otherwise tick freely — the work
	// done is one weak-pointer comparison once bound, plus the (occasional)
	// PC binding attempt while still unbound at startup.
	return !IsTemplate();
}

UWorld* USeinFormationPreviewSubsystem::GetTickableGameObjectWorld() const
{
	// Route ticks through our owning world (PIE / game). Without this override
	// the tick manager doesn't know which world we belong to and skips us.
	return GetWorld();
}
