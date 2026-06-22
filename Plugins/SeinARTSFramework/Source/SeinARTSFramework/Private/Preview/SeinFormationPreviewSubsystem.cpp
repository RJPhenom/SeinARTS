/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewSubsystem.cpp
 * @brief   Base destination-preview coordinator (ported from the Cover extension;
 *          cover-quality query replaced by the generic PreviewQualityProvider hook
 *          on USeinWorldSubsystem, which the Cover extension binds).
 */

#include "Preview/SeinFormationPreviewSubsystem.h"
#include "Preview/SeinFormationPreviewActor.h"

#include "Player/SeinPlayerController.h"
#include "Player/SeinTargeterSubsystem.h"
#include "Actor/SeinActor.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Lib/SeinCommandBrokerBPFL.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Vector.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFormationPreviewSubsystem, Log, All);

namespace SeinFormationPreviewLocal
{
	/** Local PC accessor. Null until the PC has spawned (subsystems init before PC
	 *  on the first map load). */
	static ASeinPlayerController* GetSeinPC(const ULocalPlayer* LP, UWorld* World)
	{
		if (!LP || !World) return nullptr;
		return Cast<ASeinPlayerController>(LP->GetPlayerController(World));
	}
}

void USeinFormationPreviewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Immediate hook (covers subsystem-after-PC creation); the post-load-map
	// delegate + the Tick lazy-bind catch the PC-not-ready-yet case.
	HookPlayerControllerDelegates();

	FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(this,
		[this](UWorld* /*LoadedWorld*/)
		{
			HookPlayerControllerDelegates();
			bQualityDirty = true;
			CachedQualities.Reset();
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

	if (!PC)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Verbose,
			TEXT("HookPlayerControllerDelegates: PC not ready; will retry next tick"));
		return;
	}

	if (BoundPC.Get() == PC) return;

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
	// New selection → different member count / footprint → cached qualities stale.
	bQualityDirty = true;

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
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (Settings && !Settings->bEnableFormationPreview)
	{
		HidePreview();
		return;
	}

	ASeinPlayerController* PC = BoundPC.Get();
	if (!PC)
	{
		HidePreview();
		return;
	}

	// Targeter takes precedence — its preview owns the cursor while active.
	if (USeinTargeterSubsystem* Targeter = PC->GetTargeterSubsystem())
	{
		if (Targeter->IsActive())
		{
			HidePreview();
			return;
		}
	}

	if (!bLastCursorValid)
	{
		HidePreview();
		return;
	}

	if (PC->GetSelectionCount() == 0)
	{
		HidePreview();
		return;
	}

	const TArray<FSeinEntityHandle> Members = ResolveSelectionToMembers(PC);
	if (Members.Num() == 0)
	{
		HidePreview();
		return;
	}

	// Build the order the current drag/cursor state would commit (anchor + gesture
	// guide + nominated formation) so the preview reflects the ACTUAL formation: a
	// right-click-drag previews the line, a click previews the blob. The PC runs the
	// same gesture the commit uses → preview === commit (root CLAUDE invariant #6).
	FVector OrderAnchor;
	TArray<FVector> OrderGuide;
	FGameplayTag OrderFormationTag;
	PC->BuildPreviewOrder(LastCursorWorld, OrderAnchor, OrderGuide, OrderFormationTag);

	const FFixedVector AnchorFixed = FFixedVector::FromVector(OrderAnchor);
	TArray<FFixedVector> GuideFixed;
	GuideFixed.Reserve(OrderGuide.Num());
	for (const FVector& G : OrderGuide) { GuideFixed.Add(FFixedVector::FromVector(G)); }

	const FSeinFormationLayout Layout = USeinCommandBrokerBPFL::SeinComputeFormationPreview(
		PC, Members, AnchorFixed, GuideFixed, OrderFormationTag);

	if (Layout.Positions.Num() == 0)
	{
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

	// Per-member footprint radii (fixed → world cm) so each preview dot sizes to its
	// unit's footprint. Empty when the formation didn't emit radii → uniform dots.
	TArray<float> RadiiUU;
	RadiiUU.Reserve(Layout.Radii.Num());
	for (const FFixedPoint& R : Layout.Radii) { RadiiUU.Add(R.ToFloat()); }

	// Optional per-cell quality tags from an extension (e.g. Cover supplies cover
	// quality via USeinWorldSubsystem::PreviewQualityProvider). Unbound → neutral.
	// Throttled: re-query only on selection change, cursor move past a threshold,
	// or cell-count change; otherwise reuse the cache.
	const float QualityCursorThresholdSq = 25.f * 25.f;     // 625 cm² = 25cm move
	const bool bCursorMoved = (LastCursorWorld - LastQualityQueryCursor).SizeSquared()
		> QualityCursorThresholdSq;
	const bool bCellCountChanged = (CachedQualities.Num() != Layout.Positions.Num());

	if (bQualityDirty || bCursorMoved || bCellCountChanged)
	{
		CachedQualities.Reset();
		if (USeinWorldSubsystem* WorldSub = PC->GetWorld() ? PC->GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr)
		{
			if (WorldSub->PreviewQualityProvider.IsBound())
			{
				CachedQualities = WorldSub->PreviewQualityProvider.Execute(Layout.Positions);
			}
		}
		LastQualityQueryCursor = LastCursorWorld;
		bQualityDirty = false;
	}

	PreviewActor->SetPositions(WorldPositions, CachedQualities, RadiiUU);
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

	// Class from the base settings, falling back to the framework default.
	TSubclassOf<ASeinFormationPreviewActor> ActorClass;
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
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

	// ALL-OR-NOTHING preview opt-in: any opted-out member suppresses the WHOLE
	// preview. Squads expand to live members (squad must opt in + every member must
	// have a nav component and opt in); lone units return their own handle if they
	// have a nav component and opt in. No nav component = no destination opinion =
	// opted out.
	for (const TWeakObjectPtr<ASeinActor>& Weak : PC->SelectedActors)
	{
		ASeinActor* Actor = Weak.Get();
		if (!Actor) continue;
		const FSeinEntityHandle Handle = Actor->GetEntityHandle();
		if (!Handle.IsValid()) continue;

		// Path A: squad-level dispatch.
		if (const FSeinSquadComponent* SquadData = WorldSub->GetComponent<FSeinSquadComponent>(Handle))
		{
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
// FTickableGameObject — lazy-bind to PC every frame until bound.
// ====================================================================================================

void USeinFormationPreviewSubsystem::Tick(float /*DeltaTime*/)
{
	HookPlayerControllerDelegates();
}

TStatId USeinFormationPreviewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USeinFormationPreviewSubsystem, STATGROUP_Tickables);
}

bool USeinFormationPreviewSubsystem::IsTickable() const
{
	return !IsTemplate();
}

UWorld* USeinFormationPreviewSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}
