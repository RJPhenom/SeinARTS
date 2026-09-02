/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewSubsystem.cpp
 * @brief   Base destination-preview coordinator (ported from the Cover extension;
 *          cover-quality query replaced by the generic PreviewQualityProvider hook
 *          on USeinWorldSubsystem, which the Cover extension binds).
 */

#include "Preview/SeinFormationPreviewSubsystem.h"
#include "Preview/SeinFormationPreviewActor.h"
#include "Preview/SeinFormationPreviewStyleComponent.h"

#include "Player/SeinPlayerController.h"
#include "Player/SeinTargeterSubsystem.h"
#include "Actor/SeinActor.h"
#include "Components/SeinNavigationPayload.h"
#include "Components/SeinSquadPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "Lib/SeinCommandBrokerBPFL.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Vector.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
	bModuleUnloadStateReleased = false;

	// Immediate hook (covers subsystem-after-PC creation); the post-load-map
	// delegate + the Tick lazy-bind catch the PC-not-ready-yet case.
	HookPlayerControllerDelegates();

	FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(this,
		[this](UWorld* /*LoadedWorld*/)
		{
			HookPlayerControllerDelegates();
			bQualityDirty = true;
			bLayoutDirty = true;
			CachedQualities.Reset();
			CachedMemberStyles.Reset();
		});

	UE_LOG(LogSeinFormationPreviewSubsystem, Log,
		TEXT("USeinFormationPreviewSubsystem initialized (LP=%s) — bound to PC = %s"),
		*GetNameSafe(GetLocalPlayer()), *GetNameSafe(BoundPC.Get()));
}

void USeinFormationPreviewSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinFormationPreviewSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	bModuleUnloadStateReleased = true;
	UnhookPlayerControllerDelegates();
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	if (PreviewActor)
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}

	bIsVisible = false;
	BoundPC = nullptr;
	CachedQualities.Reset();
	CachedMemberStyles.Reset();
	bLastCursorValid = false;
	bQualityDirty = true;
	bLayoutDirty = true;
	LastLayoutSimTick = MIN_int32;
	LastLayoutDragPointCount = INDEX_NONE;
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
	// New selection → different member count / footprint → cached qualities + styles stale.
	bQualityDirty = true;
	bLayoutDirty = true;
	CachedMemberStyles.Reset();

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

	// The player controller broadcasts at render rate, while authoritative unit
	// positions advance at the fixed simulation rate. If the cursor and gesture
	// are also unchanged, the complete preview input is identical and repeating
	// the footprint separation + anti-cross assignment is wasted work.
	if (bValidTrace && !bLayoutDirty)
	{
		ASeinPlayerController* PC = BoundPC.Get();
		USeinWorldSubsystem* WorldSub = PC && PC->GetWorld()
			? PC->GetWorld()->GetSubsystem<USeinWorldSubsystem>()
			: nullptr;
		const int32 SimTick = WorldSub ? WorldSub->GetCurrentTick() : MIN_int32;
		const int32 DragPointCount = PC ? PC->CommandDragPath.Num() : INDEX_NONE;
		const bool bCommandDrag = PC && PC->bIsCommandDragging;
		const bool bGestureUnchanged =
			DragPointCount == LastLayoutDragPointCount
			&& bCommandDrag == bLastLayoutWasCommandDrag
			&& CursorWorld.Equals(LastLayoutCursor, 0.01f);
		if (SimTick == LastLayoutSimTick && bGestureUnchanged)
		{
			return;
		}
		// A sim tick advanced but the gesture is unchanged. If every displayed
		// member's pose is also value-identical, the layout inputs the preview
		// can observe are the same and the (dense-cover Hungarian) re-solve is
		// wasted — a stationary cursor over settled units otherwise pays the
		// full solve 30x/second. Sim state the pose hash cannot see (a foreign
		// reservation admitted this tick, a provider spawned) still surfaces:
		// the skip is capped at a few ticks past the last real solve, and the
		// click path always recomputes the exact artifact regardless.
		if (bGestureUnchanged
			&& WorldSub
			&& !DisplayedArtifactMembers.IsEmpty()
			&& LastLayoutSimTick != MIN_int32
			&& SimTick - LastLayoutSimTick < 5)
		{
			uint32 PoseHash = 0;
			bool bAllAlive = true;
			for (const FSeinEntityHandle& Member : DisplayedArtifactMembers)
			{
				const FSeinEntity* Entity = WorldSub->GetEntity(Member);
				if (!Entity)
				{
					bAllAlive = false;
					break;
				}
				PoseHash = HashCombine(
					PoseHash, GetTypeHash(Entity->Transform));
			}
			if (bAllAlive && PoseHash == LastLayoutMemberPoseHash)
			{
				return;
			}
		}
	}
	RefreshPreview();
}

void USeinFormationPreviewSubsystem::RefreshPreview()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_Refresh);
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

	TArray<FSeinEntityHandle> Members;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_ResolveSelection);
		Members = ResolveSelectionToMembers(PC);
	}
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
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_BuildOrder);
		PC->BuildPreviewOrder(LastCursorWorld, OrderAnchor, OrderGuide, OrderFormationTag);
	}

	const FFixedVector AnchorFixed = FFixedVector::FromVector(OrderAnchor);
	TArray<FFixedVector> GuideFixed;
	GuideFixed.Reserve(OrderGuide.Num());
	for (const FVector& G : OrderGuide) { GuideFixed.Add(FFixedVector::FromVector(G)); }

	FSeinFormationLayout Layout;
	TArray<FSeinFrozenDestination> DestinationArtifact;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_ComputeLayout);
		Layout = USeinCommandBrokerBPFL::ComputeFormationDestinationArtifact(
			PC,
			Members,
			AnchorFixed,
			GuideFixed,
			OrderFormationTag,
			PC->SeinPlayerID,
			PC->IsQueueModifierHeld(),
			DestinationArtifact);
	}

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
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_QueryQuality);
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

	// Per-member marker styles (from each unit's Formation Preview Style Component),
	// index-aligned with Members and therefore with Layout.Positions. Cache-backed:
	// the bridge/component lookup runs once per member per selection.
	TArray<FSeinFormationPreviewElementStyle> Styles;
	BuildMemberStyles(Members, Styles);
	if (Styles.Num() != WorldPositions.Num())
	{
		// Defensive: a layout that isn't member-aligned must not style the wrong markers.
		Styles.Reset();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_PushActor);
		PreviewActor->SetPositions(WorldPositions, CachedQualities, RadiiUU, Styles);
	}
	PreviewActor->SetActorHiddenInGame(false);
	bIsVisible = true;
	DisplayedDestinationArtifact = MoveTemp(DestinationArtifact);
	DisplayedArtifactMembers = Members;
	// Pose fingerprint of the exact displayed members, for the unchanged-input
	// re-solve skip in HandleCursorUpdated.
	LastLayoutMemberPoseHash = 0;
	if (USeinWorldSubsystem* WorldSub = PC->GetWorld()
		? PC->GetWorld()->GetSubsystem<USeinWorldSubsystem>()
		: nullptr)
	{
		for (const FSeinEntityHandle& Member : DisplayedArtifactMembers)
		{
			if (const FSeinEntity* Entity = WorldSub->GetEntity(Member))
			{
				LastLayoutMemberPoseHash = HashCombine(
					LastLayoutMemberPoseHash, GetTypeHash(Entity->Transform));
			}
		}
	}
	DisplayedArtifactTarget = AnchorFixed;
	DisplayedArtifactGuidePoints = GuideFixed;
	DisplayedArtifactFormationTag = OrderFormationTag;
	bDisplayedArtifactQueueCommand = PC->IsQueueModifierHeld();
	bLayoutDirty = false;
	LastLayoutCursor = LastCursorWorld;
	if (USeinWorldSubsystem* WorldSub = PC->GetWorld()
		? PC->GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr)
	{
		LastLayoutSimTick = WorldSub->GetCurrentTick();
	}
	else
	{
		LastLayoutSimTick = MIN_int32;
	}
	LastLayoutDragPointCount = PC->CommandDragPath.Num();
	bLastLayoutWasCommandDrag = PC->bIsCommandDragging;
}

void USeinFormationPreviewSubsystem::HidePreview()
{
	if (PreviewActor)
	{
		PreviewActor->HideAll();
		PreviewActor->SetActorHiddenInGame(true);
	}
	bIsVisible = false;
	DisplayedDestinationArtifact.Reset();
	DisplayedArtifactMembers.Reset();
	DisplayedArtifactGuidePoints.Reset();
	DisplayedArtifactFormationTag = FGameplayTag();
	bDisplayedArtifactQueueCommand = false;
	// A later valid cursor/targeter exit must rebuild even if it happens in the
	// same sim tick at the same world location as the last visible layout.
	bLayoutDirty = true;
}

bool USeinFormationPreviewSubsystem::TryGetDisplayedDestinationArtifact(
	const TArray<FSeinEntityHandle>& Members,
	const FFixedVector& TargetLocation,
	const TArray<FFixedVector>& GuidePoints,
	FGameplayTag FormationTag,
	bool bQueueCommand,
	TArray<FSeinFrozenDestination>& OutArtifact) const
{
	OutArtifact.Reset();
	if (!bIsVisible
		|| DisplayedDestinationArtifact.Num() != Members.Num()
		|| DisplayedArtifactMembers != Members
		|| DisplayedArtifactTarget != TargetLocation
		|| DisplayedArtifactGuidePoints != GuidePoints
		|| DisplayedArtifactFormationTag != FormationTag
		|| bDisplayedArtifactQueueCommand != bQueueCommand)
	{
		return false;
	}
	OutArtifact = DisplayedDestinationArtifact;
	return true;
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
		// WYSIWYG. None/empty => the preview actor is OFF: spawn nothing, so no on-ground destination
		// markers render (PreviewActor stays null; callers already treat that as "not spawned"). A
		// set-but-unloadable/abstract class is a mistake, not an off-switch: fall back to the default.
		if (Settings->FormationPreviewActorClass.IsNull())
		{
			USeinARTSCoreSettings::ReportDisabledSystem(TEXT("Formation Preview"),
				TEXT("No on-ground destination markers are drawn for move orders."), /*bHighSeverity*/ false);
			return;
		}
		ActorClass = Settings->FormationPreviewActorClass.TryLoadClass<ASeinFormationPreviewActor>();
		if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			UE_LOG(LogSeinFormationPreviewSubsystem, Error,
				TEXT("FormationPreviewActorClass '%s' could not be loaded or is abstract — falling back to the default."),
				*Settings->FormationPreviewActorClass.ToString());
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
	const bool bFocusedSelection =
		PC->ActiveFocusIndex >= 0
		&& PC->SelectedActors.IsValidIndex(PC->ActiveFocusIndex);
	const int32 FirstSelectionIndex = bFocusedSelection
		? PC->ActiveFocusIndex
		: 0;
	const int32 LastSelectionIndex = bFocusedSelection
		? PC->ActiveFocusIndex + 1
		: PC->SelectedActors.Num();
	for (int32 SelectionIndex = FirstSelectionIndex;
		SelectionIndex < LastSelectionIndex;
		++SelectionIndex)
	{
		const TWeakObjectPtr<ASeinActor>& Weak =
			PC->SelectedActors[SelectionIndex];
		ASeinActor* Actor = Weak.Get();
		if (!Actor) continue;
		const FSeinEntityHandle Handle = Actor->GetEntityHandle();
		if (!Handle.IsValid()) continue;

		// Path A: squad-level dispatch.
		if (const FSeinSquadPayload* SquadData = WorldSub->GetComponent<FSeinSquadPayload>(Handle))
		{
			if (!SquadData->bShowFormationPreview) return {};
			for (const FSeinEntityHandle& Member : SquadData->GetLiveMembers())
			{
				const FSeinNavigationPayload* MemberNav =
					WorldSub->GetComponent<FSeinNavigationPayload>(Member);
				if (!MemberNav || !MemberNav->bShowNavigationPreview) return {};
				Out.Add(Member);
			}
			continue;
		}

		// Path B: lone-unit dispatch.
		const FSeinNavigationPayload* NavComp =
			WorldSub->GetComponent<FSeinNavigationPayload>(Handle);
		if (!NavComp || !NavComp->bShowNavigationPreview) return {};
		Out.Add(Handle);
	}

	return Out;
}

void USeinFormationPreviewSubsystem::BuildMemberStyles(const TArray<FSeinEntityHandle>& Members,
	TArray<FSeinFormationPreviewElementStyle>& OutStyles)
{
	OutStyles.Reset();
	OutStyles.Reserve(Members.Num());

	UWorld* World = GetWorld();
	USeinActorBridgeSubsystem* Bridge = World ? World->GetSubsystem<USeinActorBridgeSubsystem>() : nullptr;

	for (const FSeinEntityHandle& Member : Members)
	{
		if (const FSeinFormationPreviewElementStyle* Cached = CachedMemberStyles.Find(Member))
		{
			OutStyles.Add(*Cached);
			continue;
		}

		// Default style (member handle only) unless the member's actor carries a style
		// component. Actor-less members (abstract entities) keep the default look.
		FSeinFormationPreviewElementStyle Style;
		Style.MemberHandle = Member;
		if (Bridge)
		{
			if (ASeinActor* Actor = Bridge->GetActorForEntity(Member))
			{
				if (const USeinFormationPreviewStyleComponent* StyleComp =
					Actor->FindComponentByClass<USeinFormationPreviewStyleComponent>())
				{
					Style = StyleComp->BuildElementStyle(Member);
				}
			}
		}
		CachedMemberStyles.Add(Member, Style);
		OutStyles.Add(Style);
	}
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
	return !IsTemplate() && !bModuleUnloadStateReleased;
}

UWorld* USeinFormationPreviewSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}
