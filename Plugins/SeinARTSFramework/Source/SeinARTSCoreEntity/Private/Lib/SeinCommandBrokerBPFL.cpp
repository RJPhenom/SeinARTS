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

TArray<FFixedVector> USeinCommandBrokerBPFL::ComputeMultiBrokerAnchors(
	USeinWorldSubsystem& World,
	const TArray<FSeinEntityHandle>& Brokers,
	FFixedVector ClickTarget)
{
	// Exact replica of the multi-broker lateral spacing in
	// USeinWorldSubsystem::ProcessCommands (BrokerOrder path) — kept in this one
	// place so the commit and the preview can never diverge. Single / no broker →
	// anchor == click (no offset).
	const int32 N = Brokers.Num();
	TArray<FFixedVector> Anchors;
	Anchors.Init(ClickTarget, N);
	if (N <= 1) return Anchors;

	// 1. Per-broker lateral width from FormationWidth, clamped to a visible minimum.
	const FFixedPoint MinBrokerWidth = FFixedPoint::FromInt(300);
	TArray<FFixedPoint> BrokerWidths;
	BrokerWidths.Reserve(N);
	FFixedPoint TotalBrokerWidth = FFixedPoint::Zero;
	for (const FSeinEntityHandle& BrokerHandle : Brokers)
	{
		FFixedPoint Width = FFixedPoint::Zero;
		if (const FSeinCommandBrokerData* BrokerData = World.GetComponent<FSeinCommandBrokerData>(BrokerHandle))
		{
			Width = BrokerData->FormationWidth;
		}
		if (Width < MinBrokerWidth) { Width = MinBrokerWidth; }
		BrokerWidths.Add(Width);
		TotalBrokerWidth += Width;
	}

	// 2. Selection centroid + move direction (XY only — RTS plane).
	FFixedVector SelCentroid = FFixedVector::ZeroVector;
	int32 CentroidCount = 0;
	for (const FSeinEntityHandle& BrokerHandle : Brokers)
	{
		if (const FSeinEntity* BrokerEnt = World.GetEntity(BrokerHandle))
		{
			SelCentroid = SelCentroid + BrokerEnt->Transform.GetLocation();
			++CentroidCount;
		}
	}
	SelCentroid = (CentroidCount > 0) ? (SelCentroid / FFixedPoint::FromInt(CentroidCount)) : ClickTarget;

	FFixedVector MoveDir = ClickTarget - SelCentroid;
	MoveDir.Z = FFixedPoint::Zero;
	FFixedVector RightAxis;
	if (MoveDir.IsNearlyZero())
	{
		RightAxis = FFixedVector::RightVector;
	}
	else
	{
		const FFixedVector ForwardN = FFixedVector::GetSafeNormal(MoveDir);
		// UE convention: Right = Forward rotated +90 around +Z -> (-Y, X, 0)
		RightAxis = FFixedVector(-ForwardN.Y, ForwardN.X, FFixedPoint::Zero);
	}

	// 3. Gap budget: span = sum(widths) + (N-1)*gap; gap-per-edge = avg width / 2.
	const FFixedPoint AvgWidth = TotalBrokerWidth / FFixedPoint::FromInt(N);
	const FFixedPoint GapPerEdge = AvgWidth / FFixedPoint::Two;
	const FFixedPoint TotalSpan = TotalBrokerWidth + GapPerEdge * FFixedPoint::FromInt(N - 1);
	const FFixedPoint HalfSpan = TotalSpan / FFixedPoint::Two;

	// 4. Walk anchors along RightAxis around ClickTarget.
	FFixedPoint Cursor = -HalfSpan;
	for (int32 i = 0; i < N; ++i)
	{
		const FFixedPoint Width = BrokerWidths[i];
		const FFixedPoint AnchorOffset = Cursor + Width / FFixedPoint::Two;
		Anchors[i] = ClickTarget + RightAxis * AnchorOffset;
		Cursor = Cursor + Width + GapPerEdge;
	}
	return Anchors;
}

FSeinFormationLayout USeinCommandBrokerBPFL::SeinComputeFormationPreview(
	const UObject* WorldContextObject,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector TargetLocation)
{
	FSeinFormationLayout Empty;
	USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (!World || Members.Num() == 0) return Empty;

	// Nav-project the cursor target ONCE up front. The commit projects
	// Order.TargetLocation before computing per-broker anchors, and every squad's
	// anchor derives from the projected click — so we project here too (not
	// per-squad) to stay byte-identical. No-nav games: resolver unbound, raw target
	// passes through, matching the commit's bypass.
	if (World->NavProjectResolver.IsBound())
	{
		FFixedVector ProjectedTarget;
		if (World->NavProjectResolver.Execute(TargetLocation, ProjectedTarget))
		{
			TargetLocation = ProjectedTarget;
		}
	}

	// Group members by owning squad, PRESERVING each member's original index so the
	// returned Positions stay index-aligned with the input Members (the
	// FSeinFormationLayout contract + the preview's decal mapping both rely on it).
	// Mirrors the commit's persistent-broker partition (ProcessCommands): each squad
	// is its own broker and lays out its members independently; non-squad members
	// form one residual "loose" group. A flat parallel array (not a TMap) keeps this
	// independent of FSeinEntityHandle hashability; squad counts are tiny.
	TArray<FSeinEntityHandle> SquadOrder;          // distinct squads, first-seen order
	TArray<TArray<int32>> SquadMemberIndices;       // parallel to SquadOrder
	TArray<int32> LooseIndices;
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		const FSeinSquadMemberComponent* MemberData =
			World->GetComponent<FSeinSquadMemberComponent>(Members[i]);
		if (MemberData && MemberData->SquadEntity.IsValid())
		{
			int32 SquadIdx = SquadOrder.IndexOfByKey(MemberData->SquadEntity);
			if (SquadIdx == INDEX_NONE)
			{
				SquadIdx = SquadOrder.Add(MemberData->SquadEntity);
				SquadMemberIndices.AddDefaulted();
			}
			SquadMemberIndices[SquadIdx].Add(i);
		}
		else
		{
			LooseIndices.Add(i);
		}
	}

	FSeinFormationLayout Out;
	Out.Positions.SetNum(Members.Num());

	// Per-squad laterally-offset anchors — the SAME helper the commit uses, so the
	// squads spread side-by-side in the preview exactly as when ordered.
	const TArray<FFixedVector> SquadAnchors =
		ComputeMultiBrokerAnchors(*World, SquadOrder, TargetLocation);

	for (int32 s = 0; s < SquadOrder.Num(); ++s)
	{
		const FSeinEntityHandle Squad = SquadOrder[s];
		const TArray<int32>& Indices = SquadMemberIndices[s];

		TArray<FSeinEntityHandle> SquadMembers;
		SquadMembers.Reserve(Indices.Num());
		for (const int32 Idx : Indices) { SquadMembers.Add(Members[Idx]); }

		// Identical reads to USeinSquadDispatchResolver::ResolveDispatch: the squad's
		// own pooled resolver, broker centroid + facing, and per-squad invert flag.
		const FSeinCommandBrokerData* Broker = World->GetComponent<FSeinCommandBrokerData>(Squad);
		const FSeinSquadComponent* SquadData = World->GetComponent<FSeinSquadComponent>(Squad);

		USeinCommandBrokerResolver* Resolver = Broker
			? World->GetCommandBrokerResolver(Broker->ResolverID) : nullptr;
		FFixedVector Centroid = Broker ? Broker->Centroid : FFixedVector::ZeroVector;
		const FFixedQuaternion Facing = Broker ? Broker->AnchorFacing : FFixedQuaternion::Identity;
		const bool bInvertWhenBackward = SquadData ? SquadData->bInvertSlotOrderWhenMovingBackward : false;

		// First-tick fallback centroid (broker centroid not yet computed).
		if (SquadData && (!Broker || Broker->Centroid.IsNearlyZero()))
		{
			const FSeinEntity* SquadEntity = World->GetEntity(Squad);
			const FFixedVector Fallback = SquadEntity
				? SquadEntity->Transform.GetLocation() : FFixedVector::ZeroVector;
			Centroid = SquadData->ComputeCentroid(Fallback);
		}

		if (!Resolver) { Resolver = SeinFormationPreviewLocal::ResolveDefaultResolverCDO(); }
		if (!Resolver) { continue; }

		const FFixedVector SquadAnchor =
			SquadAnchors.IsValidIndex(s) ? SquadAnchors[s] : TargetLocation;

		const FSeinFormationLayout SquadLayout = Resolver->ResolveFormationLayout(
			World, SquadMembers, Centroid, Facing, SquadAnchor, bInvertWhenBackward);

		// Scatter the squad's positions back to the ORIGINAL member indices.
		for (int32 k = 0; k < Indices.Num(); ++k)
		{
			Out.Positions[Indices[k]] = SquadLayout.Positions.IsValidIndex(k)
				? SquadLayout.Positions[k] : SquadAnchor;
		}
		// Representative facing for any consumer that draws a facing arrow (the
		// preview renders per-cell decals from Positions; Facing is advisory).
		if (s == 0) { Out.Facing = SquadLayout.Facing; Out.bAntiCrossReorder = SquadLayout.bAntiCrossReorder; }
	}

	// Loose (non-squad) members → default resolver, one grid at the click target —
	// mirrors the commit's ephemeral-broker path (loose units dispatch to
	// Order.TargetLocation, NOT a laterally-offset anchor).
	if (LooseIndices.Num() > 0)
	{
		if (USeinCommandBrokerResolver* Resolver = SeinFormationPreviewLocal::ResolveDefaultResolverCDO())
		{
			TArray<FSeinEntityHandle> LooseMembers;
			LooseMembers.Reserve(LooseIndices.Num());
			FFixedVector Sum = FFixedVector::ZeroVector;
			int32 Count = 0;
			for (const int32 Idx : LooseIndices)
			{
				LooseMembers.Add(Members[Idx]);
				if (const FSeinEntity* Entity = World->GetEntity(Members[Idx]))
				{
					Sum = Sum + Entity->Transform.GetLocation();
					++Count;
				}
			}
			const FFixedVector LooseCentroid = (Count > 0)
				? (Sum / FFixedPoint::FromInt(Count)) : TargetLocation;

			const FSeinFormationLayout LooseLayout = Resolver->ResolveFormationLayout(
				World, LooseMembers, LooseCentroid, FFixedQuaternion::Identity,
				TargetLocation, /*bInvertWhenBackward*/ false);

			for (int32 k = 0; k < LooseIndices.Num(); ++k)
			{
				Out.Positions[LooseIndices[k]] = LooseLayout.Positions.IsValidIndex(k)
					? LooseLayout.Positions[k] : TargetLocation;
			}
			if (SquadOrder.Num() == 0) { Out.Facing = LooseLayout.Facing; Out.bAntiCrossReorder = LooseLayout.bAntiCrossReorder; }
		}
	}

	return Out;
}
