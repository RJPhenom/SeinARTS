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
	FFixedVector ClickTarget,
	const TArray<FFixedVector>& GuidePoints)
{
	// The SHARED multi-broker lateral-spacing helper, called by BOTH the commit
	// (USeinWorldSubsystem::ProcessCommands) and the preview, so the two can never
	// diverge. 0 brokers: empty. 1 broker (single squad): centered on the drag (handled
	// just below). >1 brokers: laterally spread / boxed (further below).
	const int32 N = Brokers.Num();
	TArray<FFixedVector> Anchors;
	Anchors.Init(ClickTarget, N);
	if (N <= 1)
	{
		// A single squad CENTERS on the drag (the guide midpoint) like loose units do via the
		// box spanning the guide. ClickTarget is the drag START, so without this a lone squad
		// anchored at the start of the line. No drag guide (plain click) -> the click point.
		if (N == 1 && GuidePoints.Num() >= 2)
		{
			const FFixedVector A = GuidePoints[0];
			const FFixedVector B = GuidePoints.Last();
			Anchors[0] = FFixedVector((A.X + B.X) / FFixedPoint::Two,
			                          (A.Y + B.Y) / FFixedPoint::Two,
			                          (A.Z + B.Z) / FFixedPoint::Two);
		}
		return Anchors;
	}

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

	// DRAG → BOX OF SQUADS. With a guide line (right-click-drag), lay the squad anchors
	// out as a box: the front rank spans the drag, extra squads stack into ranks behind
	// (toward the selection centroid). The unit-box layout one level up — each cell is a
	// whole squad (uniform avg-width spacing). No guide / degenerate line → the single
	// side-by-side row below (plain-click behaviour, unchanged).
	if (GuidePoints.Num() >= 2)
	{
		FFixedVector LineVec = GuidePoints.Last() - GuidePoints[0];
		LineVec.Z = FFixedPoint::Zero;
		if (!LineVec.IsNearlyZero())
		{
			const FFixedVector LStart = GuidePoints[0];
			const FFixedVector LEnd   = GuidePoints.Last();
			const FFixedPoint  LLen   = LineVec.Size();
			const FFixedVector LayoutDir = FFixedVector::GetSafeNormal(LineVec);
			const FFixedVector LineMid((LStart.X + LEnd.X) / FFixedPoint::Two,
			                           (LStart.Y + LEnd.Y) / FFixedPoint::Two,
			                           FFixedPoint::Zero);

			// Depth axis: BEHIND the front rank (the drag line is the front edge). With this
			// handedness the back side is +FaceDir, matching the box and each squad's slot
			// facing, so the squad-box sits behind the line and faces out over it.
			const FFixedVector FaceDir(FFixedPoint::Zero - LayoutDir.Y, LayoutDir.X, FFixedPoint::Zero);
			const FFixedVector DepthDir = FaceDir;

			// Cell spacing = average squad width; columns = how many fit across the drag.
			const FFixedPoint AvgW = TotalBrokerWidth / FFixedPoint::FromInt(N);
			const FFixedPoint CellW = (AvgW > FFixedPoint::Zero) ? AvgW : MinBrokerWidth;
			int32 Cols = 1;
			{
				FFixedPoint Accum = CellW;
				while (Accum <= LLen && Cols < N) { Accum = Accum + CellW; ++Cols; }
			}
			if (Cols < 1) { Cols = 1; }
			if (Cols > N) { Cols = N; }

			// Anti-cross: order squads by current position along the drag (left→right),
			// gated on the formation-level lateral flag (same as the row path below).
			bool bLat = true;
			if (const USeinDefaultCommandBrokerResolver* DefCDO =
				Cast<USeinDefaultCommandBrokerResolver>(SeinFormationPreviewLocal::ResolveDefaultResolverCDO()))
			{
				bLat = DefCDO->bReassignSlotsLateral;
			}
			TArray<FFixedPoint> AlongCoord; AlongCoord.SetNum(N);
			TArray<int32> Order2; Order2.Reserve(N);
			for (int32 i = 0; i < N; ++i)
			{
				const FSeinEntity* BrokerEnt = World.GetEntity(Brokers[i]);
				const FFixedVector Pos = BrokerEnt ? BrokerEnt->Transform.GetLocation() : ClickTarget;
				AlongCoord[i] = FFixedVector::DotProduct(Pos, LayoutDir);
				Order2.Add(i);
			}
			if (bLat)
			{
				Order2.Sort([&AlongCoord, &Brokers](int32 A, int32 B)
				{
					if (AlongCoord[A] != AlongCoord[B]) return AlongCoord[A] < AlongCoord[B];
					return Brokers[A].Index < Brokers[B].Index;
				});
			}

			const FFixedPoint ColDenom = (Cols > 1) ? FFixedPoint::FromInt(Cols - 1) : FFixedPoint::One;
			const FFixedVector LineDelta = LEnd - LStart;
			for (int32 k = 0; k < N; ++k)
			{
				const int32 i = Order2[k];
				const int32 col = k % Cols;
				const int32 row = k / Cols;
				FFixedVector FrontPt;
				if (Cols <= 1)
				{
					FrontPt = LineMid;
				}
				else
				{
					const FFixedPoint T = FFixedPoint::FromInt(col) / ColDenom;
					FrontPt = FFixedVector(LStart.X + LineDelta.X * T,
					                       LStart.Y + LineDelta.Y * T,
					                       LStart.Z + LineDelta.Z * T);
				}
				const FFixedPoint RowOff = FFixedPoint::FromInt(row) * CellW;
				Anchors[i] = FFixedVector(FrontPt.X + DepthDir.X * RowOff,
				                          FrontPt.Y + DepthDir.Y * RowOff,
				                          FrontPt.Z + DepthDir.Z * RowOff);
			}
			return Anchors;
		}
	}

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

	// 3.5 ANTI-CROSS: order the brokers by their CURRENT position along RightAxis (left→right), so the
	// leftmost squad fills the leftmost anchor and squads don't cross to a worse-ranked slot — the
	// formation-level equivalent of the within-squad slot re-match. Without this, brokers fill anchors
	// by array index, so a selection whose array order doesn't match its live left/right layout crosses
	// (the X when moving a group of squads). Deterministic: perpendicular coordinate, then entity-handle
	// index tie-break (TOTAL order → no unstable-sort desync). Commit and preview both call this, so they
	// stay byte-identical.
	// Gated on the formation-level OPT-OUT lateral flag (default true) read off the default resolver
	// CDO — the squad-group is a 1-D row, so only the lateral toggle is meaningful here.
	bool bLateralReassign = true;
	if (const USeinDefaultCommandBrokerResolver* DefCDO =
		Cast<USeinDefaultCommandBrokerResolver>(SeinFormationPreviewLocal::ResolveDefaultResolverCDO()))
	{
		bLateralReassign = DefCDO->bReassignSlotsLateral;
	}

	TArray<FFixedPoint> BrokerPerp; BrokerPerp.SetNum(N);
	TArray<int32> RankOrder; RankOrder.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FSeinEntity* BrokerEnt = World.GetEntity(Brokers[i]);
		const FFixedVector Pos = BrokerEnt ? BrokerEnt->Transform.GetLocation() : ClickTarget;
		BrokerPerp[i] = FFixedVector::DotProduct(Pos, RightAxis);
		RankOrder.Add(i);
	}
	if (bLateralReassign)
	{
		// Leftmost-by-current-position fills the leftmost anchor. Skip → RankOrder stays identity
		// (raw array/index order, the pre-anti-cross behavior).
		RankOrder.Sort([&BrokerPerp, &Brokers](int32 A, int32 B)
		{
			if (BrokerPerp[A] != BrokerPerp[B]) return BrokerPerp[A] < BrokerPerp[B];
			return Brokers[A].Index < Brokers[B].Index;
		});
	}

	// 4. Walk anchors along RightAxis around ClickTarget, in left→right RANK order — each broker keeps
	// its own width, and the anchor is stored back at the broker's ORIGINAL index so the returned array
	// stays index-aligned with the input Brokers.
	FFixedPoint Cursor = -HalfSpan;
	for (int32 k = 0; k < N; ++k)
	{
		const int32 i = RankOrder[k]; // the k-th broker from the left
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

	// Per-squad laterally-offset anchors — the SAME helper the commit uses, so the
	// squads spread side-by-side in the preview exactly as when ordered.
	const TArray<FFixedVector> SquadAnchors =
		ComputeMultiBrokerAnchors(*World, SquadOrder, TargetLocation, ProjectedGuide);

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
			SquadAnchors.IsValidIndex(s) ? SquadAnchors[s] : TargetLocation;

		FSeinOrderTarget SquadTarget;
		SquadTarget.Anchor          = SquadAnchor;
		SquadTarget.GuidePoints     = ProjectedGuide;   // orient the squad to the drag (no tag → stays SlotFormation), matching commit
		SquadTarget.CurrentCentroid = Centroid;
		SquadTarget.CurrentFacing   = Facing;
		const FSeinFormationLayout SquadLayout = Resolver->ResolveFormationLayout(
			World, SquadMembers, SquadTarget, bReassignLateral, bReassignDepth);

		// Scatter the squad's positions back to the ORIGINAL member indices.
		for (int32 k = 0; k < Indices.Num(); ++k)
		{
			Out.Positions[Indices[k]] = SquadLayout.Positions.IsValidIndex(k)
				? SquadLayout.Positions[k] : SquadAnchor;
		}
		// Representative facing for any consumer that draws a facing arrow (the
		// preview renders per-cell decals from Positions; Facing is advisory).
		if (s == 0) { Out.Facing = SquadLayout.Facing; }
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

			// Non-squad selection → the default resolver's formation-level opt-OUT flags (default both
			// on → 2-D). Mirrors the commit, where the ephemeral broker's default resolver reads the
			// same instance flags.
			bool bLooseLateral = true, bLooseDepth = true;
			if (const USeinDefaultCommandBrokerResolver* DefCDO = Cast<USeinDefaultCommandBrokerResolver>(Resolver))
			{
				bLooseLateral = DefCDO->bReassignSlotsLateral;
				bLooseDepth   = DefCDO->bReassignSlotsDepth;
			}
			FSeinOrderTarget LooseTarget;
			LooseTarget.Anchor          = TargetLocation;
			LooseTarget.GuidePoints     = ProjectedGuide;
			LooseTarget.FormationTag    = FormationTag;
			LooseTarget.CurrentCentroid = LooseCentroid;
			LooseTarget.CurrentFacing   = FFixedQuaternion::Identity;
			const FSeinFormationLayout LooseLayout = Resolver->ResolveFormationLayout(
				World, LooseMembers, LooseTarget, bLooseLateral, bLooseDepth);

			for (int32 k = 0; k < LooseIndices.Num(); ++k)
			{
				Out.Positions[LooseIndices[k]] = LooseLayout.Positions.IsValidIndex(k)
					? LooseLayout.Positions[k] : TargetLocation;
			}
			if (SquadOrder.Num() == 0) { Out.Facing = LooseLayout.Facing; }
		}
	}

	return Out;
}
