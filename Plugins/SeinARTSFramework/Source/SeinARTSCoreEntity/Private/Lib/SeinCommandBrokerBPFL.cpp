/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerBPFL.cpp
 */

#include "Lib/SeinCommandBrokerBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Input/SeinCommand.h"
#include "Settings/PluginSettings.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Types/Entity.h"
#include "StructUtils/InstancedStruct.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBroker, Log, All);

USeinWorldSubsystem* USeinCommandBrokerBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

bool USeinCommandBrokerBPFL::SeinGetBrokerData(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle, FSeinCommandBrokerData& OutData)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return false;
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	if (!Data)
	{
		UE_LOG(LogSeinBroker, Warning, TEXT("GetBrokerData: handle %s is not a broker"), *BrokerHandle.ToString());
		return false;
	}
	OutData = *Data;
	return true;
}

FSeinEntityHandle USeinCommandBrokerBPFL::SeinGetCurrentBroker(const UObject* WorldContextObject, FSeinEntityHandle Member)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FSeinEntityHandle::Invalid();
	const FSeinBrokerMembershipData* Memb = Sub->GetComponent<FSeinBrokerMembershipData>(Member);
	if (!Memb) return FSeinEntityHandle::Invalid();
	return Memb->CurrentBrokerHandle;
}

TArray<FSeinEntityHandle> USeinCommandBrokerBPFL::SeinGetBrokerMembers(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return {};
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->Members : TArray<FSeinEntityHandle>{};
}

FFixedVector USeinCommandBrokerBPFL::SeinGetBrokerCentroid(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FFixedVector();
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->Centroid : FFixedVector();
}

FGameplayTagContainer USeinCommandBrokerBPFL::SeinGetBrokerActiveOrderContext(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return FGameplayTagContainer();
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->CurrentOrderContext : FGameplayTagContainer();
}

int32 USeinCommandBrokerBPFL::SeinGetBrokerQueueLength(const UObject* WorldContextObject, FSeinEntityHandle BrokerHandle)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub) return 0;
	const FSeinCommandBrokerData* Data = Sub->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
	return Data ? Data->OrderQueue.Num() : 0;
}

void USeinCommandBrokerBPFL::SeinIssueBrokerOrder(
	const UObject* WorldContextObject,
	FSeinPlayerID PlayerID,
	const TArray<FSeinEntityHandle>& Members,
	const FGameplayTagContainer& CommandContext,
	FSeinEntityHandle TargetEntity,
	FFixedVector TargetLocation,
	bool bQueueCommand,
	FFixedVector FormationEnd)
{
	USeinWorldSubsystem* Sub = GetWorldSubsystem(WorldContextObject);
	if (!Sub)
	{
		UE_LOG(LogSeinBroker, Warning, TEXT("IssueBrokerOrder: no world subsystem"));
		return;
	}
	if (Members.Num() == 0)
	{
		UE_LOG(LogSeinBroker, Warning, TEXT("IssueBrokerOrder: empty member list"));
		return;
	}

	FSeinBrokerOrderPayload Payload;
	Payload.CommandContext = CommandContext;
	Payload.FormationEnd = FormationEnd;

	FSeinCommand Cmd;
	Cmd.PlayerID = PlayerID;
	Cmd.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
	Cmd.TargetEntity = TargetEntity;
	Cmd.TargetLocation = TargetLocation;
	Cmd.EntityList = Members;
	Cmd.bQueueCommand = bQueueCommand;
	Cmd.Payload = FInstancedStruct::Make(Payload);

	Sub->EnqueueCommand(Cmd);
}

namespace SeinFormationPreviewLocal
{
	/** Walk Members, return the squad entity that all Members are a part of, or
	 *  Invalid if Members come from different squads / mix squad and non-squad
	 *  entities. Used by the preview to decide whether to dispatch to the squad's
	 *  pooled resolver instance (single-squad case) or fall through to the
	 *  default resolver CDO (everything else). */
	static FSeinEntityHandle FindCommonSquadParent(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members)
	{
		if (!World || Members.Num() == 0) return FSeinEntityHandle::Invalid();

		FSeinEntityHandle CommonSquad;
		for (const FSeinEntityHandle& Member : Members)
		{
			const FSeinSquadMemberComponent* MemberData = World->GetComponent<FSeinSquadMemberComponent>(Member);
			if (!MemberData || !MemberData->SquadEntity.IsValid())
			{
				return FSeinEntityHandle::Invalid();    // not a squad member — mixed selection
			}
			if (!CommonSquad.IsValid())
			{
				CommonSquad = MemberData->SquadEntity;
			}
			else if (CommonSquad != MemberData->SquadEntity)
			{
				return FSeinEntityHandle::Invalid();    // members come from different squads
			}
		}
		return CommonSquad;
	}

	/** Resolve the framework's default broker resolver class — settings override
	 *  if present, USeinDefaultCommandBrokerResolver::StaticClass() otherwise.
	 *  Returns the CDO of the chosen class for stateless preview dispatch.
	 *  Mirrors the same fallback chain SeinWorldSubsystem::ProcessCommands uses
	 *  when spawning a fresh broker. */
	static USeinCommandBrokerResolver* ResolveDefaultResolverCDO()
	{
		TSubclassOf<USeinCommandBrokerResolver> ResolverClass;
		if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
		{
			if (!Settings->DefaultBrokerResolverClass.IsNull())
			{
				ResolverClass = Settings->DefaultBrokerResolverClass.LoadSynchronous();
			}
		}
		if (!ResolverClass || ResolverClass->HasAnyClassFlags(CLASS_Abstract))
		{
			ResolverClass = USeinDefaultCommandBrokerResolver::StaticClass();
		}
		return ResolverClass->GetDefaultObject<USeinCommandBrokerResolver>();
	}
}

FSeinFormationLayout USeinCommandBrokerBPFL::SeinComputeFormationPreview(
	const UObject* WorldContextObject,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector TargetLocation)
{
	FSeinFormationLayout Empty;
	USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (!World || Members.Num() == 0) return Empty;

	const FSeinEntityHandle CommonSquad =
		SeinFormationPreviewLocal::FindCommonSquadParent(World, Members);

	USeinCommandBrokerResolver* Resolver = nullptr;
	FFixedVector CurrentCentroid = FFixedVector::ZeroVector;
	FFixedQuaternion CurrentFacing = FFixedQuaternion::Identity;

	// Mirror the commit's invert flag — the preview is a read-only dry-run of the
	// dispatch, so it MUST feed ComputeFormationFacing the same input the real
	// order will. The squad's bInvertSlotOrderWhenMovingBackward toggle drives
	// backward-walk detection (keep current facing + mirror slot order when the
	// heading is behind the squad). USeinSquadDispatchResolver::ResolveDispatch
	// reads it from FSeinSquadComponent; if the preview passes a different value
	// it renders a forward layout for a move the squad will actually walk
	// backward into — the preview≠destinations bug. Set from SquadData below;
	// stays false for the non-squad / default-grid fallback, whose symmetric
	// grid makes the flag a no-op anyway (matching the base resolver's dispatch,
	// which also hardcodes false).
	bool bInvertWhenBackward = false;

	if (CommonSquad.IsValid())
	{
		// Single-squad selection — match the squad's authored slot offsets via
		// its pooled resolver instance. The squad's CommandBroker carries the
		// live centroid + facing; we use those to stay in lockstep with the
		// dispatch path's centroid/facing read.
		const FSeinCommandBrokerData* Broker = World->GetComponent<FSeinCommandBrokerData>(CommonSquad);
		const FSeinSquadComponent* SquadData = World->GetComponent<FSeinSquadComponent>(CommonSquad);

		if (Broker)
		{
			Resolver = World->GetCommandBrokerResolver(Broker->ResolverID);
			CurrentCentroid = Broker->Centroid;
			CurrentFacing = Broker->AnchorFacing;
		}
		if (SquadData)
		{
			// Same source the commit reads (ResolveDispatch fetches SquadData on
			// the broker handle, which IS this squad entity) — keeps preview and
			// commit in lockstep on backward-walk detection.
			bInvertWhenBackward = SquadData->bInvertSlotOrderWhenMovingBackward;

			// Fallback centroid for squads where the broker centroid hasn't been
			// computed yet (very-first-tick edge case): compute from live members.
			if (!Broker || Broker->Centroid.IsNearlyZero())
			{
				const FSeinEntity* SquadEntity = World->GetEntity(CommonSquad);
				const FFixedVector Fallback = SquadEntity
					? SquadEntity->Transform.GetLocation() : FFixedVector::ZeroVector;
				CurrentCentroid = SquadData->ComputeCentroid(Fallback);
			}
		}
	}

	if (!Resolver)
	{
		// Multi-entity / non-squad / unconfigured-squad fallback: default
		// resolver CDO. The default resolver's grid layout is stateless apart
		// from BlueprintReadWrite UPROPERTYs (InterUnitSpacing) which the CDO
		// carries verbatim.
		Resolver = SeinFormationPreviewLocal::ResolveDefaultResolverCDO();

		// Compute centroid from member transforms when no broker exists yet.
		// Identity facing is the right default — the formation will rotate to
		// face the target (since centroid != target in the non-degenerate case).
		FFixedVector Sum = FFixedVector::ZeroVector;
		int32 Count = 0;
		for (const FSeinEntityHandle& Member : Members)
		{
			const FSeinEntity* Entity = World->GetEntity(Member);
			if (!Entity) continue;
			Sum = Sum + Entity->Transform.GetLocation();
			++Count;
		}
		if (Count > 0)
		{
			CurrentCentroid = Sum / FFixedPoint::FromInt(Count);
		}
	}

	if (!Resolver)
	{
		UE_LOG(LogSeinBroker, Warning,
			TEXT("ComputeFormationPreview: failed to resolve a broker resolver instance — returning empty layout."));
		return Empty;
	}

	UE_LOG(LogSeinBroker, Verbose,
		TEXT("ComputeFormationPreview: resolver=%s, members=%d, common-squad=%s"),
		*GetNameSafe(Resolver), Members.Num(),
		CommonSquad.IsValid() ? TEXT("yes") : TEXT("no"));

	// Nav-project the cursor target to the nearest pathable cell — byte-for-byte
	// the snap ProcessCommands applies to a committed move order (the
	// NavProjectResolver call in the BrokerOrder path). A move order's real
	// destinations are computed from the PROJECTED target, so a preview built on
	// the raw cursor drifts whenever the cursor sits on or near a blocked cell.
	// No-nav games / tests: resolver unbound → raw target passes through,
	// matching the commit's bypass.
	if (World->NavProjectResolver.IsBound())
	{
		FFixedVector ProjectedTarget;
		if (World->NavProjectResolver.Execute(TargetLocation, ProjectedTarget))
		{
			TargetLocation = ProjectedTarget;
		}
	}

	const FSeinFormationLayout PreviewLayout = Resolver->ResolveFormationLayout(
		World, Members, CurrentCentroid, CurrentFacing,
		TargetLocation, bInvertWhenBackward);

	return PreviewLayout;
}
