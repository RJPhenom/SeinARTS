/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverSubsystem.cpp
 */

#include "System/SeinCoverSubsystem.h"
#include "Templates/SubclassOf.h"
#include "System/SeinCoverSystem.h"
#include "System/SeinCoverDefault.h"

#include "Components/SeinCoverComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Lib/SeinCoverAssignmentPlanner.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "SeinARTSFogOfWarModule.h"   // ResolveLocalObserverPlayerID for the preview quality hook

#include "Engine/Engine.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCoverSubsystem, Log, All);

namespace
{
	constexpr int32 MaxCoverStateContributors = 64;
	constexpr uint32 AuthoritativeDestinationBehaviorRevision = 1;
	constexpr uint32 SelectionDestinationPlanBehaviorRevision = 1;
	const TCHAR* AuthoritativeDestinationProviderId =
		TEXT("seinarts.cover.authoritative-destination");
	const TCHAR* SelectionDestinationPlanProviderId =
		TEXT("seinarts.cover.selection-destination-plan");

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}
}

void USeinCoverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(USeinWorldSubsystem::StaticClass());
	bStateBindingFrozen = false;
	StateBindingFailureReason.Reset();
	FrozenStateBindingFrame.Reset();
	AuthoritativeDestinationProviderToken = 0;
	SelectionDestinationPlanProviderToken = 0;

	// Resolve the configured class. FSoftClassPath drives the picker — same
	// pattern as NavigationClass / FogOfWarClass / RelayActorClass — so this
	// module doesn't need to be loaded for the settings to resolve from disk,
	// and game teams can swap in a custom impl without touching the framework.
	TSubclassOf<USeinCoverSystem> CoverClass;
	bool bConfiguredCoverClass = false;
	if (const USeinARTSCoverSettings* Settings = GetDefault<USeinARTSCoverSettings>())
	{
		if (Settings->CoverSystemClass.IsValid())
		{
			bConfiguredCoverClass = true;
			CoverClass = Settings->CoverSystemClass.TryLoadClass<USeinCoverSystem>();
		}
	}
	if (!CoverClass || CoverClass->HasAnyClassFlags(CLASS_Abstract))
	{
		// A set-but-unloadable/abstract class is a mistake, not an off-switch
		// (same convention as the other pluggable pickers): fall back to the
		// shipped default LOUDLY — the state-coverage gate then certifies the
		// default, not the configured class, and a silently swapped custom
		// implementation would otherwise be invisible.
		if (bConfiguredCoverClass)
		{
			UE_LOG(LogSeinCoverSubsystem, Error,
				TEXT("Initialize: configured CoverSystemClass could not be loaded (or is abstract); falling back to the shipped default cover system. Fix the class path in SeinARTS Cover settings."));
		}
		CoverClass = USeinCoverDefault::StaticClass();
	}

	CoverSystem = NewObject<USeinCoverSystem>(this, CoverClass, NAME_None, RF_Transient);
	if (!CoverSystem)
	{
		UE_LOG(LogSeinCoverSubsystem, Warning,
			TEXT("Initialize: failed to instantiate cover system class %s"),
			*GetNameSafe(CoverClass));
		return;
	}

	USeinWorldSubsystem* WorldSub = GetWorld() ? GetWorld()->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	CoverSystem->OnCoverSystemInitialized(WorldSub);

	// Hook entity spawn/destroy events — auto-registers entities with
	// FSeinCoverComponent in storage as cover providers. Replaces the
	// pre-Phase-5 USeinCoverProviderComponent AC's OnEntitySpawnedNative
	// hook (the AC is gone; events are how render-side systems learn about
	// sim-side component changes now).
	HookSimWorldEvents();

	UE_LOG(LogSeinCoverSubsystem, Log,
		TEXT("USeinCoverSubsystem initialized — active cover system: %s"),
		*GetNameSafe(CoverSystem));
}

void USeinCoverSubsystem::Deinitialize()
{
	ReleaseModuleOwnedStateForModuleUnload();
	Super::Deinitialize();
}

void USeinCoverSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	HookSimWorldEvents();
}

void USeinCoverSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim =
				World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sim->TerminateAndReleaseForModuleUnload(
				FName(TEXT("SeinARTSCover")),
				TEXT("cover state and deterministic destination resolvers are unloading"));
		}
	}

	if (CachedSimWorld)
	{
		if (SpawnedHandle.IsValid())   CachedSimWorld->OnEntitySpawned.Remove(SpawnedHandle);
		if (DestroyedHandle.IsValid()) CachedSimWorld->OnEntityDestroyed.Remove(DestroyedHandle);
		if (RestoredHandle.IsValid())
		{
			CachedSimWorld->OnAuthoritativeStateRestored.Remove(RestoredHandle);
		}
		// Core's terminal release already severed every registered callback. The
		// token is world-local and must not survive into a replacement world.
		AuthoritativeDestinationProviderToken = 0;
		SelectionDestinationPlanProviderToken = 0;
		if (CachedSimWorld->PreviewQualityProvider.IsBoundToObject(this))
		{
			CachedSimWorld->PreviewQualityProvider.Unbind();
		}
		SpawnedHandle.Reset();
		DestroyedHandle.Reset();
		RestoredHandle.Reset();
		CachedSimWorld = nullptr;
	}

	if (CoverSystem)
	{
		CoverSystem->OnCoverSystemDeinitialized();
		CoverSystem = nullptr;
	}
	bStateBindingFrozen = false;
	StateBindingFailureReason.Reset();
	FrozenStateBindingFrame.Reset();
}

bool USeinCoverSubsystem::FreezeCanonicalStateBinding(
	bool bCommit,
	FString& OutFrame,
	FString& OutError)
{
	OutFrame.Reset();
	OutError.Reset();
	if (!StateBindingFailureReason.IsEmpty())
	{
		OutError = StateBindingFailureReason;
		return false;
	}

	FString CandidateFrame =
		TEXT("SeinARTS.Cover.WorldBinding\n");
	AppendFramed(CandidateFrame, TEXT("1"));

	if (!CoverSystem)
	{
		// No live cover implementation for this world (instantiation failed or
		// module-owned state already released). Mirror navigation's disabled
		// branch: an explicit stable frame — never a silent skip — so a cover
		// implementation appearing later is a detected contract change.
		AppendFramed(CandidateFrame, TEXT("disabled"));
		AppendFramed(CandidateFrame, TEXT("<disabled>"));
		AppendFramed(CandidateFrame, TEXT("seinarts.cover.disabled"));
		AppendFramed(CandidateFrame, TEXT("1"));
		AppendFramed(CandidateFrame, TEXT("1"));
		AppendFramed(CandidateFrame, TEXT("stateless"));
		AppendFramed(CandidateFrame, TEXT("0"));
	}
	else
	{
		FSeinCoverStateCoverageClaim Coverage;
		bool bCoverageValid =
			CoverSystem->ComputeStateCoverageClaim(
				Coverage, OutError)
			&& !Coverage.StableImplementationId.IsEmpty()
			&& Coverage.StableImplementationId
				== Coverage.StableImplementationId.TrimStartAndEnd()
			&& Coverage.BehaviorRevision != 0
			&& Coverage.CoverageRevision != 0;

		FString CoverageKind;
		TArray<FString> CanonicalRequiredKeys;
		if (bCoverageValid)
		{
			switch (Coverage.StateCoverage)
			{
			case ESeinCoverStateCoverage::Stateless:
				CoverageKind = TEXT("stateless");
				if (!Coverage.RequiredCanonicalStateContributors.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Stateless cover implementation '%s' names supplemental canonical-state contributors."),
						*CoverSystem->GetClass()->GetPathName());
					bCoverageValid = false;
				}
				break;

			case ESeinCoverStateCoverage::CanonicalStateContributors:
				CoverageKind = TEXT("canonical-state-contributors");
				if (Coverage.RequiredCanonicalStateContributors.IsEmpty()
					|| Coverage.RequiredCanonicalStateContributors.Num()
						> MaxCoverStateContributors)
				{
					OutError = FString::Printf(
						TEXT("Stateful cover implementation '%s' names an invalid supplemental canonical-state contributor count."),
						*CoverSystem->GetClass()->GetPathName());
					bCoverageValid = false;
					break;
				}

				if (const USeinWorldSubsystem* Sim =
						GetWorld()
							? GetWorld()->GetSubsystem<
								USeinWorldSubsystem>()
							: nullptr)
				{
					CanonicalRequiredKeys.Reserve(
						Coverage.RequiredCanonicalStateContributors.Num());
					for (const FSeinCanonicalStateKey& Required :
						Coverage.RequiredCanonicalStateContributors)
					{
						const FString CanonicalKey =
							FSeinCanonicalStateRegistry::CanonicalKey(
								Required);
						if (CanonicalKey.IsEmpty())
						{
							OutError = FString::Printf(
								TEXT("Cover implementation '%s' names an invalid supplemental canonical-state contributor."),
								*CoverSystem->GetClass()->GetPathName());
							bCoverageValid = false;
							break;
						}
						if (!Sim->HasFrozenCanonicalStateContributor(
								Required,
								ESeinCanonicalStateRole::Authoritative))
						{
							OutError = FString::Printf(
								TEXT("Cover implementation '%s' requires missing authoritative canonical-state contributor '%s'."),
								*CoverSystem->GetClass()->GetPathName(),
								*CanonicalKey);
							bCoverageValid = false;
							break;
						}
						CanonicalRequiredKeys.Add(CanonicalKey);
					}
				}
				else
				{
					OutError = FString::Printf(
						TEXT("Cover implementation '%s' cannot verify supplemental canonical-state contributors without a frozen Core world schema."),
						*CoverSystem->GetClass()->GetPathName());
					bCoverageValid = false;
				}

				CanonicalRequiredKeys.Sort();
				for (int32 Index = 1;
					bCoverageValid
						&& Index < CanonicalRequiredKeys.Num();
					++Index)
				{
					if (CanonicalRequiredKeys[Index - 1]
						== CanonicalRequiredKeys[Index])
					{
						OutError = FString::Printf(
							TEXT("Cover implementation '%s' names duplicate canonical-state contributor '%s'."),
							*CoverSystem->GetClass()->GetPathName(),
							*CanonicalRequiredKeys[Index]);
						bCoverageValid = false;
					}
				}
				break;

			case ESeinCoverStateCoverage::Unspecified:
			default:
				OutError = FString::Printf(
					TEXT("Cover implementation '%s' did not declare whether its mutable state is stateless or restored by canonical contributors."),
					*CoverSystem->GetClass()->GetPathName());
				bCoverageValid = false;
				break;
			}
		}

		if (!bCoverageValid)
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Cover implementation '%s' returned an invalid exact-state coverage claim."),
					*CoverSystem->GetClass()->GetPathName());
			}
			if (bStateBindingFrozen)
			{
				InvalidateCommittedCanonicalStateBinding(OutError);
			}
			return false;
		}

		AppendFramed(CandidateFrame, TEXT("enabled"));
		AppendFramed(
			CandidateFrame, CoverSystem->GetClass()->GetPathName());
		AppendFramed(
			CandidateFrame, Coverage.StableImplementationId);
		AppendFramed(
			CandidateFrame,
			LexToString(Coverage.BehaviorRevision));
		AppendFramed(
			CandidateFrame,
			LexToString(Coverage.CoverageRevision));
		AppendFramed(CandidateFrame, CoverageKind);
		AppendFramed(
			CandidateFrame,
			LexToString(CanonicalRequiredKeys.Num()));
		for (const FString& RequiredKey : CanonicalRequiredKeys)
		{
			AppendFramed(CandidateFrame, RequiredKey);
		}
	}

	if (bStateBindingFrozen
		&& FrozenStateBindingFrame != CandidateFrame)
	{
		InvalidateCommittedCanonicalStateBinding(
			TEXT("The cover implementation or its state-coverage claim changed after the match StateContract froze."));
		OutError = StateBindingFailureReason;
		return false;
	}

	if (bCommit)
	{
		bStateBindingFrozen = true;
		FrozenStateBindingFrame = CandidateFrame;
	}
	OutFrame = MoveTemp(CandidateFrame);
	return true;
}

void USeinCoverSubsystem::InvalidateCanonicalStateBinding(
	const FString& Reason)
{
	if (StateBindingFailureReason.IsEmpty())
	{
		StateBindingFailureReason = Reason.IsEmpty()
			? TEXT("The frozen cover state-coverage contract became invalid.")
			: Reason;
		UE_LOG(LogSeinCoverSubsystem, Error, TEXT("%s"),
			*StateBindingFailureReason);
	}
}

void USeinCoverSubsystem::InvalidateCommittedCanonicalStateBinding(
	const FString& Reason)
{
	InvalidateCanonicalStateBinding(Reason);
	if (UWorld* World = GetWorld())
	{
		if (USeinWorldSubsystem* Sim =
				World->GetSubsystem<USeinWorldSubsystem>())
		{
			Sim->InvalidateDeterministicExecutionContract(
				StateBindingFailureReason);
		}
	}
}

void USeinCoverSubsystem::HookSimWorldEvents()
{
	UWorld* World = GetWorld();
	if (!World) return;
	USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
	if (!WorldSub)
	{
		// Defensive fallback for an unusual world that declined the declared
		// subsystem dependency. Retry on world begin play.
		UE_LOG(LogSeinCoverSubsystem, Verbose,
			TEXT("HookSimWorldEvents: sim subsystem not ready; will rebind in OnWorldBeginPlay"));
		return;
	}
	if (CachedSimWorld == WorldSub
		&& SpawnedHandle.IsValid()
		&& DestroyedHandle.IsValid()
		&& RestoredHandle.IsValid()
		&& AuthoritativeDestinationProviderToken != 0
		&& SelectionDestinationPlanProviderToken != 0)
	{
		return;
	}

	CachedSimWorld = WorldSub;
	if (!SpawnedHandle.IsValid())
	{
		SpawnedHandle = WorldSub->OnEntitySpawned.AddUObject(
			this, &USeinCoverSubsystem::HandleEntitySpawned);
	}
	if (!DestroyedHandle.IsValid())
	{
		DestroyedHandle = WorldSub->OnEntityDestroyed.AddUObject(
			this, &USeinCoverSubsystem::HandleEntityDestroyed);
	}
	if (!RestoredHandle.IsValid())
	{
		RestoredHandle = WorldSub->OnAuthoritativeStateRestored.AddUObject(
			this, &USeinCoverSubsystem::ReconcileProviderRegistry);
	}

	// Stable-keyed destination authority composes with other deterministic
	// providers and is bound into the match StateContract by Core. No FoW gate:
	// authored standing-position validity is independent of observer knowledge.
	if (AuthoritativeDestinationProviderToken == 0)
	{
		FSeinAuthoritativeDestinationProviderResolver DestinationResolver;
		DestinationResolver.BindWeakLambda(this,
			[this](const FSeinAuthoritativeDestinationQuery& Query) -> bool
			{
				if (!CoverSystem) return false;
				const FFixedPoint Eps = FFixedPoint::FromInt(10);
				return CoverSystem->FindNearbySlots(
					Query.WorldPosition,
					Eps,
					FSeinPlayerID()).Num() > 0;
			});
		FString DestinationProviderError;
		if (!WorldSub->RegisterAuthoritativeDestinationProvider(
				AuthoritativeDestinationProviderId,
				AuthoritativeDestinationBehaviorRevision,
				MoveTemp(DestinationResolver),
				AuthoritativeDestinationProviderToken,
				&DestinationProviderError))
		{
			UE_LOG(LogSeinCoverSubsystem, Error,
				TEXT("HookSimWorldEvents: authoritative-destination provider registration failed: %s"),
				*DestinationProviderError);
			return;
		}
	}

	// Build one cover assignment across the complete flattened selection. The
	// artifact stores slot provenance for diagnostics, but its world position is
	// the command value: provider motion/destruction never turns it into follow.
	if (SelectionDestinationPlanProviderToken == 0)
	{
		FSeinSelectionDestinationPlanProviderResolver PlanResolver;
		PlanResolver.BindWeakLambda(this,
			[this, WorldSub](
				const FSeinSelectionDestinationPlanQuery& Query,
				TArray<FSeinFrozenDestination>& Destinations) -> bool
			{
				if (!CoverSystem) return true;
				const USeinARTSCoverSettings* Settings =
					GetDefault<USeinARTSCoverSettings>();
				const FFixedPoint SnapRadius = Settings
					? Settings->CoverSnapRadius
					: FFixedPoint::FromInt(500);
				if (SnapRadius <= FFixedPoint::Zero) return true;

				TArray<FSeinCoverSlotCandidate> Slots =
					CoverSystem->FindNearbySlots(
						Query.TargetLocation,
						SnapRadius,
						Query.OrderingPlayer);
				FFixedPoint MaxMemberRadius = FFixedPoint::Zero;
				for (const FSeinFrozenDestination& Destination : Destinations)
				{
					if (Destination.FootprintRadius > MaxMemberRadius)
					{
						MaxMemberRadius = Destination.FootprintRadius;
					}
				}
				if (MaxMemberRadius <= FFixedPoint::Zero) return true;
				const TConstArrayView<FSeinEntityHandle> IgnoredMembers =
					Query.bQueueCommand
						? TConstArrayView<FSeinEntityHandle>()
						: Query.Members;
				Slots.RemoveAll([WorldSub, IgnoredMembers, MaxMemberRadius](
					const FSeinCoverSlotCandidate& Slot)
				{
					return WorldSub->IsDestinationFootprintReserved(
						Slot.WorldPosition,
						MaxMemberRadius,
						IgnoredMembers);
				});
				if (MaxMemberRadius
					> FFixedPoint::MaxValue - MaxMemberRadius)
				{
					if (Slots.Num() > 1) Slots.SetNum(1);
				}
				else
				{
					const FFixedPoint MinimumSeparation =
						MaxMemberRadius + MaxMemberRadius;
					TArray<FSeinCoverSlotCandidate> NonOverlappingSlots;
					NonOverlappingSlots.Reserve(Slots.Num());
					for (const FSeinCoverSlotCandidate& Slot : Slots)
					{
						if (!NonOverlappingSlots.ContainsByPredicate(
								[&Slot, MinimumSeparation](
									const FSeinCoverSlotCandidate& Existing)
								{
									return FFixedVector::IsPlanarDistanceWithin(
										Slot.WorldPosition,
										Existing.WorldPosition,
										MinimumSeparation);
								}))
						{
							NonOverlappingSlots.Add(Slot);
						}
					}
					Slots = MoveTemp(NonOverlappingSlots);
				}
				if (Slots.IsEmpty()) return true;

				TArray<FFixedVector> DesiredPositions;
				DesiredPositions.Reserve(Destinations.Num());
				for (const FSeinFrozenDestination& Destination : Destinations)
				{
					DesiredPositions.Add(Destination.WorldPosition);
				}
				TArray<FSeinEntityHandle> Members;
				Members.Append(Query.Members.GetData(), Query.Members.Num());
				const FSeinCoverAssignmentPlan Plan =
					FSeinCoverAssignmentPlanner::PlanForMembers(
						WorldSub,
						Members,
						DesiredPositions,
						Slots,
						Query.TargetLocation,
						SnapRadius);
				for (const FSeinCoverSlotAssignment& Assignment : Plan.Assignments)
				{
					if (!Destinations.IsValidIndex(Assignment.MemberIndex)
						|| !Slots.IsValidIndex(Assignment.SlotCandidateIndex))
					{
						return false;
					}
					FSeinFrozenDestination& Destination =
						Destinations[Assignment.MemberIndex];
					const FSeinCoverSlotCandidate& Slot =
						Slots[Assignment.SlotCandidateIndex];
					Destination.WorldPosition = Slot.WorldPosition;
					Destination.bReserveFootprint = true;
					Destination.SourceEntity = Slot.ProviderHandle;
					Destination.SourceIndex = Slot.SlotIndex;
				}
				return true;
			});
		FString PlanProviderError;
		if (!WorldSub->RegisterSelectionDestinationPlanProvider(
				SelectionDestinationPlanProviderId,
				SelectionDestinationPlanBehaviorRevision,
				MoveTemp(PlanResolver),
				SelectionDestinationPlanProviderToken,
				&PlanProviderError))
		{
			UE_LOG(LogSeinCoverSubsystem, Error,
				TEXT("HookSimWorldEvents: selection-destination provider registration failed: %s"),
				*PlanProviderError);
			return;
		}
	}

		// Destination-preview quality provider: supply per-cell cover quality for the
		// BASE preview's decal tints, FoW-observer-gated (preview can't leak cover the
		// local player hasn't scouted). The preview subsystem + actor live in the base
		// framework now; Cover augments them through this hook. Unbound when Cover is
		// absent -> neutral preview.
		WorldSub->PreviewQualityProvider.BindWeakLambda(this,
			[this](const TArray<FFixedVector>& Positions) -> TArray<FGameplayTag>
			{
				TArray<FGameplayTag> Out;
				if (!CoverSystem) return Out;
				Out.Reserve(Positions.Num());
				const FSeinPlayerID Observer = UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(GetWorld());
				for (const FFixedVector& Pos : Positions)
				{
					Out.Add(CoverSystem->QueryBestCoverQualityAt(Pos, Observer));
				}
				return Out;
			});

	// Initialization order is intentionally unconstrained across plugins. If
	// the sim world already contains authored or restored providers, bring the
	// replaceable cover implementation to the same derived state immediately.
	ReconcileProviderRegistry();

	UE_LOG(LogSeinCoverSubsystem, Log,
		TEXT("HookSimWorldEvents: subscribed to entity lifecycle + authoritative restore"));
}

void USeinCoverSubsystem::HandleEntitySpawned(FSeinEntityHandle Handle)
{
	if (!CoverSystem || !CachedSimWorld) return;
	// Only register entities that actually have a cover component in storage —
	// the bridge's InjectAuthoredComponents puts it there if the designer
	// authored an FSeinCoverComponent entry on the ComponentData array.
	if (CachedSimWorld->GetComponent<FSeinCoverComponent>(Handle) != nullptr)
	{
		CoverSystem->RegisterAuthoritativeProvider(Handle);
		UE_LOG(LogSeinCoverSubsystem, Verbose,
			TEXT("HandleEntitySpawned: registered cover provider %s"), *Handle.ToString());
	}
}

void USeinCoverSubsystem::HandleEntityDestroyed(FSeinEntityHandle Handle)
{
	// Always call Unregister — the cover system's impl is idempotent
	// (Default impl's Find returns INDEX_NONE for unregistered handles and
	// no-ops). Lets us skip the per-entity "is this a provider?" check on
	// the hot destroy path.
	if (CoverSystem)
	{
		CoverSystem->UnregisterAuthoritativeProvider(Handle);
	}
}

void USeinCoverSubsystem::ReconcileProviderRegistry()
{
	if (!CoverSystem || !CachedSimWorld)
	{
		return;
	}

	TArray<FSeinEntityHandle> ProviderHandles;
	ProviderHandles.Reserve(
		CachedSimWorld->GetEntityPool().GetActiveCount());
	CachedSimWorld->GetEntityPool().ForEachEntity(
		[this, &ProviderHandles](
			FSeinEntityHandle Handle,
			const FSeinEntity& /*Entity*/)
		{
			if (CachedSimWorld->GetComponent<FSeinCoverComponent>(Handle))
			{
				ProviderHandles.Add(Handle);
			}
		});
	ProviderHandles.Sort();

	CoverSystem->RebuildProviderRegistry(ProviderHandles);
	UE_LOG(LogSeinCoverSubsystem, Verbose,
		TEXT("ReconcileProviderRegistry: rebuilt %d authoritative provider(s)"),
		ProviderHandles.Num());
}

USeinCoverSystem* USeinCoverSubsystem::GetCoverSystemForWorld(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;
	if (!World) return nullptr;
	const USeinCoverSubsystem* Sub = World->GetSubsystem<USeinCoverSubsystem>();
	return Sub ? Sub->GetCoverSystem() : nullptr;
}
