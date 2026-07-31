/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapTransaction.cpp
 */

#include "GameMode/SeinMatchBootstrapTransaction.h"

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Core/SeinPlayerState.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMode/SeinPlayerStart.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	FSeinDeterministicValueDigestOptions MakeDigestOptions()
	{
		FSeinDeterministicValueDigestOptions Options;
#if !WITH_METADATA
		Options.bTrustCookedTypesWithoutMetadata = true;
#endif
		return Options;
	}

	FString GuidString(const FGuid& Guid)
	{
		return Guid.ToString(EGuidFormats::Digits);
	}
}

FSeinMatchBootstrapTransaction::FSeinMatchBootstrapTransaction(
	UWorld& InWorld,
	USeinWorldSubsystem& InWorldSubsystem,
	USeinActorBridgeSubsystem& InActorBridge)
	: World(&InWorld)
	, WorldSubsystem(&InWorldSubsystem)
	, ActorBridge(&InActorBridge)
{
}

bool FSeinMatchBootstrapTransaction::Materialize(
	const FSeinMatchSettings& Settings,
	const FGuid& InAuthorizationContextDigest,
	FSeinMatchBootstrapReceipt& OutReceipt,
	FString& OutError)
{
	OutReceipt = FSeinMatchBootstrapReceipt();
	OutError.Reset();

	USeinWorldSubsystem* Subsystem = WorldSubsystem.Get();
	if (!World.IsValid() || !Subsystem || !ActorBridge.IsValid())
	{
		return Fail(TEXT("Framework bootstrap dependencies are no longer valid."), OutError);
	}
	if (!InAuthorizationContextDigest.IsValid())
	{
		return Fail(TEXT("Framework bootstrap requires a valid authorization context."), OutError);
	}

	FGameplayTag RejectionReason;
	CanonicalSettings = Settings;
	FSeinDeterministicValueDigestError SettingsDigestError;
	if (!Subsystem->ValidateMatchSettings(CanonicalSettings, RejectionReason))
	{
		return Fail(FString::Printf(
			TEXT("Framework bootstrap rejected match settings (%s)."),
			*RejectionReason.ToString()), OutError);
	}
	if (!SeinCanonicalizeAndDigestMatchSettings(
		CanonicalSettings, ContractDigest, &SettingsDigestError))
	{
		return Fail(FString::Printf(
			TEXT("Framework bootstrap could not digest match settings (%s: %s)."),
			*SettingsDigestError.FieldPath, *SettingsDigestError.Message), OutError);
	}
	AuthorizationContextDigest = InAuthorizationContextDigest;
	if (Subsystem->GetMatchBootstrapState()
			!= ESeinMatchBootstrapState::Applying
		|| Subsystem->GetMatchBootstrapAuthorizationContextDigest()
			!= AuthorizationContextDigest)
	{
		OutError =
			TEXT("Core did not open the Framework materializer's canonical bootstrap transaction.");
		return false;
	}

	if (!Preflight(OutError)
		|| !ComputePlanDigest(OutError)
		|| !VerifyFrozenPlan(OutError)
		|| !Apply(OutError))
	{
		const FString Failure = OutError;
		return Fail(Failure, OutError);
	}

	if (!Subsystem->SealLocalMatchBootstrap(PlanDigest, OutReceipt, OutError))
	{
		const FString Failure = OutError;
		return Fail(Failure, OutError);
	}
	return true;
}

bool FSeinMatchBootstrapTransaction::ComputeStandaloneAuthorizationContextDigest(
	UWorld& InWorld,
	USeinWorldSubsystem& Subsystem,
	const FSeinMatchSettings& Settings,
	FGuid& OutDigest,
	FString& OutError)
{
	OutDigest.Invalidate();
	OutError.Reset();

	FSeinMatchSettings Canonical = Settings;
	FGuid MatchSettingsDigest;
	FSeinDeterministicValueDigestError SettingsError;
	if (!SeinCanonicalizeAndDigestMatchSettings(
		Canonical, MatchSettingsDigest, &SettingsError))
	{
		OutError = FString::Printf(
			TEXT("Standalone bootstrap settings digest failed (%s: %s)."),
			*SettingsError.FieldPath, *SettingsError.Message);
		return false;
	}

	const FGuid CommandProtocolDigest = Subsystem.GetCommandProtocolDigest();
	const FGuid SimulationContentDigest =
		Subsystem.GetSimulationContentDigest();
	if (!CommandProtocolDigest.IsValid()
		|| !SimulationContentDigest.IsValid())
	{
		OutError =
			TEXT("Standalone bootstrap cannot identify its command protocol or simulation content.");
		return false;
	}

	FSeinStandaloneBootstrapContext Context;
	Context.Domain = TEXT("SeinARTS.Bootstrap.Standalone.v2");
	Context.MapPackageName = UWorld::RemovePIEPrefix(
		InWorld.GetOutermost()->GetName());
	Context.MatchSettingsDigest = GuidString(MatchSettingsDigest);
	Context.CommandProtocolDigest = GuidString(CommandProtocolDigest);
	Context.SimulationContentDigest = GuidString(SimulationContentDigest);
	Context.ConfigFingerprint = Subsystem.GetConfigFingerprint();
	Context.SessionSeed = 0;

	FSeinDeterministicValueDigestError DigestError;
	if (FSeinDeterministicValueDigest::Compute(
		FSeinStandaloneBootstrapContext::StaticStruct(), &Context,
		OutDigest, &DigestError, MakeDigestOptions())
		!= ESeinDeterministicValueDigestResult::Success)
	{
		OutError = FString::Printf(
			TEXT("Standalone bootstrap context digest failed (%s: %s)."),
			*DigestError.FieldPath, *DigestError.Message);
		return false;
	}
	return OutDigest.IsValid();
}

bool FSeinMatchBootstrapTransaction::Preflight(FString& OutError)
{
	UWorld* CurrentWorld = World.Get();
	USeinWorldSubsystem* Subsystem = WorldSubsystem.Get();
	if (!CurrentWorld || !Subsystem)
	{
		OutError = TEXT("Framework bootstrap world disappeared during preflight.");
		return false;
	}

	const TArray<FSeinPlayerID> ExistingPlayers = Subsystem->GetRegisteredPlayerIDs();
	if (ExistingPlayers.Num() != 1 || !ExistingPlayers[0].IsNeutral())
	{
		OutError = TEXT("Framework bootstrap requires pristine neutral-only player state.");
		return false;
	}

	Plan = FSeinBootstrapPlanDigestData();
	PlayerStarts.Reset();
	PlacedActors.Reset();

	TMap<int32, TArray<TWeakObjectPtr<ASeinPlayerStart>>> StartsBySlot;
	for (TActorIterator<ASeinPlayerStart> It(CurrentWorld); It; ++It)
	{
		ASeinPlayerStart* Start = *It;
		if (Start && Start->PlayerSlot > 0)
		{
			StartsBySlot.FindOrAdd(Start->PlayerSlot).Add(Start);
		}
	}

	TSet<FSeinPlayerID> ActivePlayerIDs;
	for (const FSeinMatchSlot& Slot : CanonicalSettings.Slots)
	{
		if (Slot.State != ESeinSlotState::Human
			&& Slot.State != ESeinSlotState::AI)
		{
			continue;
		}
		// Factions are an OPT-IN catalog: a factionless project fields
		// occupied slots with the invalid/zero FactionID and the sim
		// registers them as Faction(0) — deterministic, digest-covered.
		// Faction-required is a game-mode policy, not a bootstrap invariant.
		if (Slot.SlotIndex <= 0 || Slot.SlotIndex > MAX_uint8)
		{
			OutError = FString::Printf(
				TEXT("Active slot %d requires a representable player ID."),
				Slot.SlotIndex);
			return false;
		}

		const TArray<TWeakObjectPtr<ASeinPlayerStart>>* MatchingStarts =
			StartsBySlot.Find(Slot.SlotIndex);
		if (!MatchingStarts || MatchingStarts->Num() != 1
			|| !(*MatchingStarts)[0].IsValid())
		{
			OutError = FString::Printf(
				TEXT("Active slot %d requires exactly one valid SeinPlayerStart; found %d."),
				Slot.SlotIndex, MatchingStarts ? MatchingStarts->Num() : 0);
			return false;
		}

		ASeinPlayerStart* Start = (*MatchingStarts)[0].Get();
		if (!Start->bSimTransformBaked)
		{
			OutError = FString::Printf(
				TEXT("PlayerStart %s for active slot %d has no baked fixed transform; re-save the level."),
				*Start->GetPathName(), Slot.SlotIndex);
			return false;
		}
		if (Start->PlacedSimTransform.Scale.X == FFixedPoint::Zero
			|| Start->PlacedSimTransform.Scale.Y == FFixedPoint::Zero
			|| Start->PlacedSimTransform.Scale.Z == FFixedPoint::Zero)
		{
			OutError = FString::Printf(
				TEXT("PlayerStart %s for active slot %d has a degenerate baked scale."),
				*Start->GetPathName(), Slot.SlotIndex);
			return false;
		}

		if (Start->SpawnEntity)
		{
			TArray<const USeinEntityComponent*> EntityComponents;
			AActor::GetActorClassDefaultComponents<USeinEntityComponent>(
				Start->SpawnEntity, EntityComponents);
			if (!ValidateEntityComponentData(
				EntityComponents, Start->SpawnEntity->GetPathName(), OutError))
			{
				return false;
			}
		}

		FSeinBootstrapPlayerPlanEntry& Entry = Plan.Players.AddDefaulted_GetRef();
		Entry.PlayerID = FSeinPlayerID(static_cast<uint8>(Slot.SlotIndex));
		Entry.FactionID = Slot.FactionID;
		Entry.TeamID = Slot.TeamID;
		Entry.SlotState = Slot.State;
		Entry.bHasSpawnEntity = Start->SpawnEntity != nullptr;
		Entry.SpawnClassPath = Start->SpawnEntity
			? Start->SpawnEntity->GetPathName()
			: FString();
		Entry.SpawnTransform = Start->PlacedSimTransform;
		PlayerStarts.Add(Start);
		ActivePlayerIDs.Add(Entry.PlayerID);
	}

	struct FPendingPlacedActor
	{
		FSeinBootstrapPlacedActorPlanEntry Entry;
		TWeakObjectPtr<ASeinActor> Actor;
	};
	TArray<FPendingPlacedActor> PendingPlacedActors;
	for (TActorIterator<ASeinActor> It(CurrentWorld); It; ++It)
	{
		ASeinActor* Actor = *It;
		if (!Actor || Actor->HasValidEntity())
		{
			continue;
		}
		if (!Actor->bSimLocationBaked || !Actor->bSimRotationBaked)
		{
			OutError = FString::Printf(
				TEXT("Placed actor %s has no complete baked fixed transform; re-save the level."),
				*Actor->GetPathName());
			return false;
		}
		if (Actor->PlayerSlot < 0 || Actor->PlayerSlot > MAX_uint8)
		{
			OutError = FString::Printf(
				TEXT("Placed actor %s has invalid owner slot %d."),
				*Actor->GetPathName(), Actor->PlayerSlot);
			return false;
		}

		const FSeinPlayerID Owner = Actor->PlayerSlot > 0
			? FSeinPlayerID(static_cast<uint8>(Actor->PlayerSlot))
			: FSeinPlayerID::Neutral();
		if (Owner.IsValid() && !ActivePlayerIDs.Contains(Owner))
		{
			OutError = FString::Printf(
				TEXT("Placed actor %s is owned by inactive slot %u."),
				*Actor->GetPathName(), Owner.Value);
			return false;
		}

		TArray<USeinEntityComponent*> MutableComponents;
		Actor->GetComponents<USeinEntityComponent>(MutableComponents);
		TArray<const USeinEntityComponent*> EntityComponents;
		EntityComponents.Reserve(MutableComponents.Num());
		for (const USeinEntityComponent* Component : MutableComponents)
		{
			EntityComponents.Add(Component);
		}
		if (!ValidateEntityComponentData(
			EntityComponents, Actor->GetPathName(), OutError))
		{
			return false;
		}

		FPendingPlacedActor& Pending = PendingPlacedActors.AddDefaulted_GetRef();
		Pending.Entry.StableKey = BuildPlacedActorStableKey(*Actor);
		Pending.Entry.ActorClassPath = Actor->GetClass()->GetPathName();
		Pending.Entry.BakedTransform = FFixedTransform(
			Actor->PlacedSimLocation, Actor->PlacedSimRotation);
		Pending.Entry.OwnerPlayerID = Owner;
		Pending.Actor = Actor;
	}

	PendingPlacedActors.Sort([](const FPendingPlacedActor& A, const FPendingPlacedActor& B)
	{
		return A.Entry.StableKey < B.Entry.StableKey;
	});
	for (int32 Index = 0; Index < PendingPlacedActors.Num(); ++Index)
	{
		if (Index > 0
			&& PendingPlacedActors[Index - 1].Entry.StableKey
				== PendingPlacedActors[Index].Entry.StableKey)
		{
			OutError = FString::Printf(
				TEXT("Placed actor bootstrap key is duplicated: %s."),
				*PendingPlacedActors[Index].Entry.StableKey);
			return false;
		}
		Plan.PlacedActors.Add(PendingPlacedActors[Index].Entry);
		PlacedActors.Add(PendingPlacedActors[Index].Actor);
	}

	return true;
}

bool FSeinMatchBootstrapTransaction::VerifyFrozenPlan(FString& OutError) const
{
	if (Plan.Players.Num() != PlayerStarts.Num()
		|| Plan.PlacedActors.Num() != PlacedActors.Num())
	{
		OutError = TEXT("Framework bootstrap plan/reference cardinality changed before apply.");
		return false;
	}

	for (int32 Index = 0; Index < Plan.Players.Num(); ++Index)
	{
		const ASeinPlayerStart* Start = PlayerStarts[Index].Get();
		const FSeinBootstrapPlayerPlanEntry& Entry = Plan.Players[Index];
		const FString CurrentClassPath = Start && Start->SpawnEntity
			? Start->SpawnEntity->GetPathName()
			: FString();
		if (!Start
			|| Start->PlayerSlot != static_cast<int32>(Entry.PlayerID.Value)
			|| !Start->bSimTransformBaked
			|| Start->PlacedSimTransform != Entry.SpawnTransform
			|| (Start->SpawnEntity != nullptr) != Entry.bHasSpawnEntity
			|| CurrentClassPath != Entry.SpawnClassPath)
		{
			OutError = FString::Printf(
				TEXT("PlayerStart plan entry %d changed between preflight and apply."), Index);
			return false;
		}
	}

	for (int32 Index = 0; Index < Plan.PlacedActors.Num(); ++Index)
	{
		const ASeinActor* Actor = PlacedActors[Index].Get();
		const FSeinBootstrapPlacedActorPlanEntry& Entry = Plan.PlacedActors[Index];
		const FSeinPlayerID CurrentOwner = Actor && Actor->PlayerSlot > 0
			? FSeinPlayerID(static_cast<uint8>(Actor->PlayerSlot))
			: FSeinPlayerID::Neutral();
		if (!Actor || Actor->HasValidEntity()
			|| !Actor->bSimLocationBaked || !Actor->bSimRotationBaked
			|| BuildPlacedActorStableKey(*Actor) != Entry.StableKey
			|| Actor->GetClass()->GetPathName() != Entry.ActorClassPath
			|| FFixedTransform(Actor->PlacedSimLocation, Actor->PlacedSimRotation)
				!= Entry.BakedTransform
			|| CurrentOwner != Entry.OwnerPlayerID)
		{
			OutError = FString::Printf(
				TEXT("Placed actor plan entry %d changed between preflight and apply."), Index);
			return false;
		}
	}
	return true;
}

bool FSeinMatchBootstrapTransaction::Apply(FString& OutError)
{
	UWorld* CurrentWorld = World.Get();
	USeinWorldSubsystem* Subsystem = WorldSubsystem.Get();
	USeinActorBridgeSubsystem* Bridge = ActorBridge.Get();
	if (!CurrentWorld || !Subsystem || !Bridge)
	{
		OutError = TEXT("Framework bootstrap dependencies disappeared before apply.");
		return false;
	}

	Subsystem->RegisterFactionsFromSettings();
	Subsystem->StartMatch(CanonicalSettings);
	if (Subsystem->GetMatchState() != ESeinMatchState::Starting
		|| Subsystem->GetMatchSettingsDigest() != ContractDigest)
	{
		OutError = TEXT("Framework bootstrap could not install its canonical match contract.");
		return false;
	}

	for (int32 Index = 0; Index < Plan.Players.Num(); ++Index)
	{
		const FSeinBootstrapPlayerPlanEntry& Entry = Plan.Players[Index];
		Subsystem->RegisterPlayer(Entry.PlayerID, Entry.FactionID, Entry.TeamID);
		const FSeinPlayerState* State = Subsystem->GetPlayerState(Entry.PlayerID);
		if (!State || State->FactionID != Entry.FactionID
			|| State->TeamID != Entry.TeamID)
		{
			OutError = FString::Printf(
				TEXT("Framework bootstrap failed to register canonical state for %s."),
				*Entry.PlayerID.ToString());
			return false;
		}

		if (Entry.bHasSpawnEntity)
		{
			ASeinPlayerStart* Start = PlayerStarts[Index].Get();
			if (!Start || !Subsystem->SpawnEntity(
				Start->SpawnEntity, Entry.SpawnTransform, Entry.PlayerID).IsValid())
			{
				OutError = FString::Printf(
					TEXT("Framework bootstrap failed to spawn %s for %s."),
					*Entry.SpawnClassPath, *Entry.PlayerID.ToString());
				return false;
			}
		}
	}

	for (int32 Index = 0; Index < Plan.PlacedActors.Num(); ++Index)
	{
		ASeinActor* Actor = PlacedActors[Index].Get();
		if (!Actor || !Bridge->RegisterPlacedActor(*Actor).IsValid())
		{
			OutError = FString::Printf(
				TEXT("Framework bootstrap failed to register placed actor %s."),
				*Plan.PlacedActors[Index].StableKey);
			return false;
		}
	}

	return true;
}

bool FSeinMatchBootstrapTransaction::ComputePlanDigest(FString& OutError)
{
	FSeinDeterministicValueDigestError DigestError;
	if (FSeinDeterministicValueDigest::Compute(
		FSeinBootstrapPlanDigestData::StaticStruct(), &Plan,
		PlanDigest, &DigestError, MakeDigestOptions())
		!= ESeinDeterministicValueDigestResult::Success)
	{
		OutError = FString::Printf(
			TEXT("Framework bootstrap plan digest failed (%s: %s)."),
			*DigestError.FieldPath, *DigestError.Message);
		return false;
	}
	return PlanDigest.IsValid();
}

bool FSeinMatchBootstrapTransaction::Fail(
	const FString& Reason,
	FString& OutError)
{
	OutError = Reason.IsEmpty()
		? TEXT("Framework bootstrap transaction failed without a diagnostic.")
		: Reason;
	return false;
}

FString FSeinMatchBootstrapTransaction::BuildPlacedActorStableKey(
	const ASeinActor& Actor)
{
	const ULevel* Level = Actor.GetLevel();
	const FString LevelPackage = Level
		? UWorld::RemovePIEPrefix(Level->GetOutermost()->GetName())
		: FString();
	return LevelPackage + TEXT(":") + Actor.GetName();
}

bool FSeinMatchBootstrapTransaction::ValidateEntityComponentData(
	TConstArrayView<const USeinEntityComponent*> Components,
	const FString& OwnerLabel,
	FString& OutError)
{
	if (Components.Num() != 1 || !Components[0])
	{
		OutError = FString::Printf(
			TEXT("Bootstrap entity %s must expose exactly one valid Sein entity component; found %d."),
			*OwnerLabel, Components.Num());
		return false;
	}

	TSet<const UScriptStruct*> SeenTypes;
	for (const FInstancedStruct& Entry : Components[0]->ComponentData)
	{
		const UScriptStruct* Type = Entry.GetScriptStruct();
		if (!Entry.IsValid() || !Type || !Entry.GetMemory())
		{
			OutError = FString::Printf(
				TEXT("Bootstrap entity %s contains an invalid authored component entry."),
				*OwnerLabel);
			return false;
		}
		if (SeenTypes.Contains(Type))
		{
			OutError = FString::Printf(
				TEXT("Bootstrap entity %s contains duplicate authored component type %s."),
				*OwnerLabel, *Type->GetPathName());
			return false;
		}
		SeenTypes.Add(Type);
	}
	return true;
}
