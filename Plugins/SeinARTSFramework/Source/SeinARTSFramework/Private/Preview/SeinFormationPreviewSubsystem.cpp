/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file    SeinFormationPreviewSubsystem.cpp
 * @author  RJ Macklem
 * @created 21 Aug 2026
 * @latest  02 Sep 2026
 * @brief   Base destination-preview coordinator (ported from the Cover extension;
 *          cover-quality query replaced by the generic PreviewQualityProvider hook
 *          on USeinWorldSubsystem, which the Cover extension binds).
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Preview/SeinFormationPreviewSubsystem.h"
#include "Preview/SeinFormationPreviewActor.h"
#include "Preview/SeinFormationPreviewComponent.h"
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
#include "HAL/IConsoleManager.h"
#include "Stats/Stats.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFormationPreviewSubsystem, Log, All);

/** Dev kill switch for the marker drawing only — computation and the frozen
 *  destination artifact are unaffected. Opt-in itself is authored per unit by
 *  adding a Formation Preview Component to its Blueprint. */
static TAutoConsoleVariable<int32> CVarSeinPreviewDisable(
	TEXT("Sein.Preview.Disable"),
	0,
	TEXT("1 = draw no on-ground destination preview markers (render-only dev toggle)."),
	ECVF_Default);

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

	for (TPair<TObjectPtr<UClass>, TObjectPtr<ASeinFormationPreviewActor>>& Pair : PreviewActors)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->Destroy();
		}
	}
	PreviewActors.Empty();

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
	if (CVarSeinPreviewDisable.GetValueOnGameThread() != 0)
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
	TArray<UClass*> RenderClasses;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_ResolveSelection);
		Members = ResolveSelectionToMembers(PC, RenderClasses);
	}
	if (Members.Num() == 0)
	{
		HidePreview();
		return;
	}

	// Render opt-in is authored per unit (Formation Preview Component). When no
	// selected member opts in there is nothing to draw, so skip the layout solve
	// entirely — the commit computes its own artifact for such selections, exactly
	// as it does for any click made while no preview is displayed.
	const bool bAnyMemberRenders = RenderClasses.ContainsByPredicate(
		[](const UClass* RenderClass) { return RenderClass != nullptr; });
	if (!bAnyMemberRenders)
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

	// Fan the full parallel arrays out per resolved renderer class: each pooled
	// actor draws its own member subset with its own change guards, so instanced
	// backends still batch per unit type. Pooled actors whose class drew nobody
	// this refresh are hidden in place. Only the DRAWING is filtered — the
	// artifact bookkeeping below stays full-membership.
	bool bAnyDrawn = false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_FormationPreview_PushActors);
		const int32 MemberCount = FMath::Min(RenderClasses.Num(), WorldPositions.Num());
		const bool bHaveQualities = CachedQualities.Num() == WorldPositions.Num();
		const bool bHaveRadii = RadiiUU.Num() == WorldPositions.Num();
		const bool bHaveStyles = Styles.Num() == WorldPositions.Num();

		TSet<UClass*> ActiveClasses;
		for (int32 Index = 0; Index < MemberCount; ++Index)
		{
			if (RenderClasses[Index])
			{
				ActiveClasses.Add(RenderClasses[Index]);
			}
		}

		TArray<FVector> GroupPositions;
		TArray<FGameplayTag> GroupQualities;
		TArray<float> GroupRadii;
		TArray<FSeinFormationPreviewElementStyle> GroupStyles;
		for (UClass* RenderClass : ActiveClasses)
		{
			GroupPositions.Reset();
			GroupQualities.Reset();
			GroupRadii.Reset();
			GroupStyles.Reset();
			for (int32 Index = 0; Index < MemberCount; ++Index)
			{
				if (RenderClasses[Index] != RenderClass) continue;
				GroupPositions.Add(WorldPositions[Index]);
				if (bHaveQualities) GroupQualities.Add(CachedQualities[Index]);
				if (bHaveRadii) GroupRadii.Add(RadiiUU[Index]);
				if (bHaveStyles) GroupStyles.Add(Styles[Index]);
			}

			ASeinFormationPreviewActor* GroupActor = EnsurePreviewActorForClass(RenderClass);
			if (!GroupActor) continue;
			GroupActor->SetPositions(GroupPositions, GroupQualities, GroupRadii, GroupStyles);
			GroupActor->SetActorHiddenInGame(false);
			bAnyDrawn = true;
		}

		for (TPair<TObjectPtr<UClass>, TObjectPtr<ASeinFormationPreviewActor>>& Pair : PreviewActors)
		{
			if (IsValid(Pair.Value) && !ActiveClasses.Contains(Pair.Key))
			{
				Pair.Value->HideAll();
				Pair.Value->SetActorHiddenInGame(true);
			}
		}
	}
	if (!bAnyDrawn)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Warning,
			TEXT("Refresh: bail — no preview actor could be spawned for the opted-in members."));
		HidePreview();
		return;
	}
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
	for (TPair<TObjectPtr<UClass>, TObjectPtr<ASeinFormationPreviewActor>>& Pair : PreviewActors)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->HideAll();
			Pair.Value->SetActorHiddenInGame(true);
		}
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

ASeinFormationPreviewActor* USeinFormationPreviewSubsystem::EnsurePreviewActorForClass(UClass* ActorClass)
{
	if (!ActorClass) return nullptr;
	if (TObjectPtr<ASeinFormationPreviewActor>* Existing = PreviewActors.Find(ActorClass))
	{
		// IsValid also rejects a pooled actor whose world was torn down (map
		// travel) before GC nulls the reference; the Add below replaces it.
		if (IsValid(*Existing)) return *Existing;
	}

	UWorld* World = GetWorld();
	if (!World) return nullptr;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags = RF_Transient;
	ASeinFormationPreviewActor* Spawned = World->SpawnActor<ASeinFormationPreviewActor>(
		ActorClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);

	if (!Spawned)
	{
		UE_LOG(LogSeinFormationPreviewSubsystem, Warning,
			TEXT("EnsurePreviewActorForClass: failed to spawn preview actor of class %s"),
			*GetNameSafe(ActorClass));
		return nullptr;
	}

	Spawned->SetActorHiddenInGame(true);     // start hidden until first SetPositions

	PreviewActors.Add(ActorClass, Spawned);
	UE_LOG(LogSeinFormationPreviewSubsystem, Log,
		TEXT("Spawned formation preview actor %s (%s)"),
		*GetNameSafe(Spawned), *GetNameSafe(ActorClass));
	return Spawned;
}

UClass* USeinFormationPreviewSubsystem::ResolveRenderClassForActor(const AActor* Actor)
{
	if (!Actor) return nullptr;
	const USeinFormationPreviewComponent* Component =
		Actor->FindComponentByClass<USeinFormationPreviewComponent>();
	// No component = the unit is not opted into the preview: nothing is drawn for
	// it. Component presence is THE enable — there is no settings-level switch.
	if (!Component) return nullptr;

	// Component class → project default → framework base. A set-but-unloadable or
	// abstract entry is a mistake, not an off switch: log and fall through.
	if (!Component->PreviewActorClass.IsNull())
	{
		UClass* Class = Component->PreviewActorClass.LoadSynchronous();
		if (Class
			&& !Class->HasAnyClassFlags(CLASS_Abstract)
			&& Class->IsChildOf(ASeinFormationPreviewActor::StaticClass()))
		{
			return Class;
		}
		UE_LOG(LogSeinFormationPreviewSubsystem, Error,
			TEXT("Formation Preview Component on %s names class '%s' that could not be loaded or is abstract — using the project default."),
			*GetNameSafe(Actor), *Component->PreviewActorClass.ToString());
	}
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		if (!Settings->FormationPreviewActorClass.IsNull())
		{
			UClass* Class = Settings->FormationPreviewActorClass.TryLoadClass<ASeinFormationPreviewActor>();
			if (Class && !Class->HasAnyClassFlags(CLASS_Abstract))
			{
				return Class;
			}
			UE_LOG(LogSeinFormationPreviewSubsystem, Error,
				TEXT("FormationPreviewActorClass '%s' could not be loaded or is abstract — falling back to the framework default."),
				*Settings->FormationPreviewActorClass.ToString());
		}
	}
	return ASeinFormationPreviewActor::StaticClass();
}

TArray<FSeinEntityHandle> USeinFormationPreviewSubsystem::ResolveSelectionToMembers(
	ASeinPlayerController* PC, TArray<UClass*>& OutRenderClasses) const
{
	TArray<FSeinEntityHandle> Out;
	OutRenderClasses.Reset();
	if (!PC) return Out;

	UWorld* World = PC->GetWorld();
	USeinWorldSubsystem* WorldSub = World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (!WorldSub) return Out;
	USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();

	// Squads expand to live members; lone units return their own handle. A member
	// with NO navigation component has no destination opinion, and the commit
	// resolves such a selection through the artifact-less path — there is nothing
	// exact to display, so it suppresses the whole preview.
	//
	// Render opt-in is separate, per member, and NEVER affects membership: the
	// parallel OutRenderClasses entry carries the renderer class from the member's
	// Formation Preview Component (the one on a squad's actor covers all its
	// members), or null for members that draw no marker. The layout and frozen
	// destination artifact always cover every returned member.
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

		// Path A: squad-level dispatch. The component on the squad's own actor opts
		// the whole squad in with one renderer; without it, each member's actor
		// decides for itself (a member whose bridge actor hasn't spawned yet simply
		// draws nothing this refresh).
		if (const FSeinSquadPayload* SquadData = WorldSub->GetComponent<FSeinSquadPayload>(Handle))
		{
			UClass* SquadRenderClass = ResolveRenderClassForActor(Actor);
			for (const FSeinEntityHandle& Member : SquadData->GetLiveMembers())
			{
				const FSeinNavigationPayload* MemberNav =
					WorldSub->GetComponent<FSeinNavigationPayload>(Member);
				if (!MemberNav)
				{
					OutRenderClasses.Reset();
					return {};
				}
				UClass* MemberRenderClass = SquadRenderClass;
				if (!MemberRenderClass && Bridge)
				{
					MemberRenderClass =
						ResolveRenderClassForActor(Bridge->GetActorForEntity(Member));
				}
				Out.Add(Member);
				OutRenderClasses.Add(MemberRenderClass);
			}
			continue;
		}

		// Path B: lone-unit dispatch.
		const FSeinNavigationPayload* NavComp =
			WorldSub->GetComponent<FSeinNavigationPayload>(Handle);
		if (!NavComp)
		{
			OutRenderClasses.Reset();
			return {};
		}
		Out.Add(Handle);
		OutRenderClasses.Add(ResolveRenderClassForActor(Actor));
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
