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
#include "Formations/SeinFormation.h"
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
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		// WYSIWYG. None/empty => broker dispatch is OFF → return null; callers already treat a null
		// resolver as "no formation spread / no dispatch", so preview and commit stay consistent. A
		// set-but-unloadable/abstract class falls back to the shipped default (a mistake is not off).
		if (!Settings || Settings->DefaultBrokerResolverClass.IsNull())
		{
			return nullptr;
		}
		TSubclassOf<USeinCommandBrokerResolver> ResolverClass = Settings->DefaultBrokerResolverClass.LoadSynchronous();
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
	FFixedVector ClickTarget,
	const TArray<FFixedVector>& GuidePoints,
	FGameplayTag FormationTag,
	TArray<FFixedQuaternion>& OutFacings)
{
	// Lay the brokers (squads) out as ELEMENTS of the gesture formation: each squad is ONE element,
	// sized by its FormationRadius (its whole footprint) through the SAME footprint-aware
	// ResolveFormationLayout loose units use, so a multi-squad order takes the chosen shape
	// (Ring/Wedge/Grid/Box/...) instead of a hardcoded box/row. Each squad then lays out its own
	// members around the returned anchor (USeinSlotFormation). Shared by the commit
	// (USeinWorldSubsystem::ProcessCommands) and the preview, so the two can never drift.
	const int32 N = Brokers.Num();
	TArray<FFixedVector> Anchors;
	Anchors.Init(ClickTarget, N);
	OutFacings.Reset();
	OutFacings.Init(FFixedQuaternion::Identity, N);
	if (N == 0) { return Anchors; }

	// The parent layout runs through the project's default resolver CDO (it owns the formation map +
	// the shaping passes). No resolver (nav-less tests) -> leave every anchor at ClickTarget.
	USeinCommandBrokerResolver* Resolver = SeinFormationPreviewLocal::ResolveDefaultResolverCDO();
	if (!Resolver) { return Anchors; }

	// Selection centroid (XY, RTS plane) -> the formation rotates its facing from centroid to target.
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

	// Anti-cross re-match flags read off the default resolver CDO (the same source the within-formation
	// slot re-match uses), so a moving squad group does not cross to worse-ranked anchors.
	bool bLateral = true;
	bool bDepth = true;
	if (const USeinDefaultCommandBrokerResolver* DefCDO = Cast<USeinDefaultCommandBrokerResolver>(Resolver))
	{
		bLateral = DefCDO->bReassignSlotsLateral;
		bDepth = DefCDO->bReassignSlotsDepth;
	}

	// Each broker is ONE element; ResolveFormationLayout sizes it via the broker-aware
	// GetFootprintRadius (FormationRadius). ClickTarget is the drag midpoint, so a lone squad centres.
	FSeinOrderTarget Target;
	Target.Anchor = ClickTarget;
	Target.GuidePoints = GuidePoints;
	Target.FormationTag = FormationTag;
	Target.CurrentCentroid = SelCentroid;
	Target.CurrentFacing = FFixedQuaternion::Identity;

	const FSeinFormationLayout Layout = Resolver->ResolveFormationLayout(&World, Brokers, Target, bLateral, bDepth);
	for (int32 i = 0; i < N; ++i)
	{
		Anchors[i]    = Layout.Positions.IsValidIndex(i) ? Layout.Positions[i] : ClickTarget;
		OutFacings[i] = Layout.Facings.IsValidIndex(i)   ? Layout.Facings[i]   : Layout.Facing;
	}
	return Anchors;
}

FSeinFormationLayout USeinCommandBrokerBPFL::SeinComputeFormationPreview(
	const UObject* WorldContextObject,
	const TArray<FSeinEntityHandle>& Members,
	FFixedVector TargetLocation,
	const TArray<FFixedVector>& GuidePoints,
	FGameplayTag FormationTag)
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

	// Project the gesture guide points too — the commit projects them in
	// ProcessCommands, so a drag-line preview must lay out on the same reachable
	// cells as the committed order (preview === commit). Loose group only (below);
	// squads ignore the gesture guide here, matching the squad dispatch guard.
	TArray<FFixedVector> ProjectedGuide = GuidePoints;
	if (World->NavProjectResolver.IsBound())
	{
		for (FFixedVector& G : ProjectedGuide)
		{
			FFixedVector P;
			if (World->NavProjectResolver.Execute(G, P)) { G = P; }
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
	Out.Radii.SetNum(Members.Num()); // carry per-member footprint radii for preview dot sizing

	// Per-squad anchors via the SAME helper the commit uses, so squads take the gesture formation
	// (Ring/Wedge/Box/...) in the preview exactly as when ordered. Each squad then lays its own
	// members at its anchor below (USeinSlotFormation) — preview === commit.
	// A2: ONE unified formation over ALL elements - squad handles + loose unit handles, co-equal, each
	// sized by its footprint (squad = FormationRadius). Squads are the FIRST elements, loose follow;
	// element index e >= FirstLooseElement maps to LooseIndices[e - FirstLooseElement]. SAME helper the
	// commit calls so preview == commit.
	TArray<FSeinEntityHandle> Elements = SquadOrder;
	const int32 FirstLooseElement = Elements.Num();
	for (const int32 Idx : LooseIndices) { Elements.Add(Members[Idx]); }
	TArray<FFixedQuaternion> ElementFacings;
	const TArray<FFixedVector> ElementPositions =
		ComputeMultiBrokerAnchors(*World, Elements, TargetLocation, ProjectedGuide, FormationTag, ElementFacings);

	for (int32 s = 0; s < SquadOrder.Num(); ++s)
	{
		const FSeinEntityHandle Squad = SquadOrder[s];
		const TArray<int32>& Indices = SquadMemberIndices[s];

		TArray<FSeinEntityHandle> SquadMembers;
		SquadMembers.Reserve(Indices.Num());
		for (const int32 Idx : Indices) { SquadMembers.Add(Members[Idx]); }

		// Identical reads to USeinSquadDispatchResolver::ResolveDispatch: the squad's
		// own pooled resolver, broker centroid + facing, and per-squad re-match flags.
		const FSeinCommandBrokerData* Broker = World->GetComponent<FSeinCommandBrokerData>(Squad);
		const FSeinSquadComponent* SquadData = World->GetComponent<FSeinSquadComponent>(Squad);

		USeinCommandBrokerResolver* Resolver = Broker
			? World->GetCommandBrokerResolver(Broker->ResolverID) : nullptr;
		FFixedVector Centroid = Broker ? Broker->Centroid : FFixedVector::ZeroVector;
		const FFixedQuaternion Facing = Broker ? Broker->AnchorFacing : FFixedQuaternion::Identity;
		const bool bReassignLateral = SquadData ? SquadData->bReassignSlotsLateral : false;
		const bool bReassignDepth   = SquadData ? SquadData->bReassignSlotsDepth   : false;

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
			ElementPositions.IsValidIndex(s) ? ElementPositions[s] : TargetLocation;

		// Inner-layout target via the shared constructor: the squad's members around the anchor the parent
		// formation gave it, in the squad's OWN compact shape. By construction it carries NO gesture
		// guide/tag, so the drag can't re-expand each squad (which overlapped them into one). The PARENT
		// element facing (radial in a ring, drag-perp in a box, move-dir on a click) is handed down here.
		// SAME constructor the commit (USeinSquadDispatchResolver::ResolveDispatch) uses → preview === commit.
		const FFixedQuaternion SquadFacing = ElementFacings.IsValidIndex(s) ? ElementFacings[s] : Facing;
		const FSeinOrderTarget SquadTarget = USeinFormation::MakeInnerLayoutTarget(
			SquadAnchor, Centroid, SquadFacing,
			SquadData ? SquadData->FormationClass : TSoftClassPtr<USeinFormation>());
		const FSeinFormationLayout SquadLayout = Resolver->ResolveFormationLayout(
			World, SquadMembers, SquadTarget, bReassignLateral, bReassignDepth);

		// Scatter the squad's positions (+ footprint radii) back to the ORIGINAL member indices.
		for (int32 k = 0; k < Indices.Num(); ++k)
		{
			Out.Positions[Indices[k]] = SquadLayout.Positions.IsValidIndex(k)
				? SquadLayout.Positions[k] : SquadAnchor;
			Out.Radii[Indices[k]] = SquadLayout.Radii.IsValidIndex(k)
				? SquadLayout.Radii[k] : FFixedPoint::Zero;
		}
		// Representative facing for any consumer that draws a facing arrow (the
		// preview renders per-cell decals from Positions; Facing is advisory).
		if (s == 0) { Out.Facing = SquadLayout.Facing; }
	}

	// Loose (non-squad) members: each is its OWN element in the unified formation above - scatter its
	// element position straight to its dot (its slot in the SINGLE shape), sized by its footprint.
	// Mirrors the commit's pre-placed loose dispatch so preview == commit.
	for (int32 j = 0; j < LooseIndices.Num(); ++j)
	{
		const int32 MemberIdx = LooseIndices[j];
		const int32 ElemIdx = FirstLooseElement + j;
		Out.Positions[MemberIdx] = ElementPositions.IsValidIndex(ElemIdx) ? ElementPositions[ElemIdx] : TargetLocation;
		Out.Radii[MemberIdx] = USeinFormation::GetFootprintRadius(World, Members[MemberIdx]);
	}
	if (SquadOrder.Num() == 0 && ElementFacings.IsValidIndex(FirstLooseElement))
	{
		Out.Facing = ElementFacings[FirstLooseElement];
	}
	return Out;
}
