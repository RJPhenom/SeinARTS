/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandBrokerBPFL.cpp
 */

#include "Lib/SeinCommandBrokerBPFL.h"
#include "Engine/Engine.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Formations/SeinFormation.h"
#include "Data/SeinWorldSnapshot.h"
#include "Input/SeinCommand.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Settings/PluginSettings.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Types/Entity.h"
#include "StructUtils/InstancedStruct.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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

	Sub->SubmitLocalCommandDraft(Cmd);
}

namespace SeinFormationPreviewLocal
{
	/** Resolve the framework's default broker resolver class — settings override
	 *  if present, USeinDefaultCommandBrokerResolver::StaticClass() otherwise.
	 *  Returns the read-only CDO of the chosen class as scratch-clone source.
	 *  Mirrors the same fallback chain SeinWorldSubsystem::ProcessCommands uses
	 *  when spawning a fresh broker. */
	static const USeinCommandBrokerResolver* ResolveDefaultResolverCDO()
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

	/** Render-rate preview isolation. The preview must never invoke the LIVE
	 *  pooled resolver (or a shared CDO): its captured reflected state feeds
	 *  the canonical world root, so a designer resolver subclass that writes
	 *  a member property during layout would diverge that root — or the
	 *  process-global CDO — from purely local mouse hover. The preview
	 *  instead runs on a transient scratch clone materialized through the
	 *  pool-object codec, so "captured state" keeps exactly one definition.
	 *  The clone refreshes only when the source's captured bytes change; a
	 *  caller finalizes the clone after layout: unchanged scratch may be reused;
	 *  mutated scratch is discarded so state never leaks between previews.
	 *  A codec failure invalidates the deterministic execution contract rather
	 *  than invoking a pooled resolver or CDO through an unsafe fallback.
	 *
	 *  Lifecycle: the scratch is outered to the TRANSIENT PACKAGE (never to a
	 *  world — a rooted chain into a dead world would fail map-travel world
	 *  cleanup), the cache is keyed per (source, world) so PIE clients never
	 *  share a scratch (the settings-default CDO is one source shared by
	 *  every world), and entries whose source OR world died are pruned on
	 *  every call. */
	struct FPreviewScratchEntry
	{
		TWeakObjectPtr<const USeinCommandBrokerResolver> Source;
		TWeakObjectPtr<const UWorld> World;
		TStrongObjectPtr<USeinCommandBrokerResolver> Scratch;
		TArray<uint8> SourceCapturedBytes;
	};
	static TArray<FPreviewScratchEntry> PreviewScratchCache;

	static USeinCommandBrokerResolver* GetPreviewScratchResolver(
		USeinWorldSubsystem& World,
		const USeinCommandBrokerResolver& Source)
	{
		const UWorld* CacheWorld = World.GetWorld();
		PreviewScratchCache.RemoveAll([](const FPreviewScratchEntry& Entry)
		{
			return !Entry.Source.IsValid()
				|| !Entry.World.IsValid()
				|| !Entry.Scratch.IsValid();
		});

		FString Error;
		FSeinSnapshotPoolInstanceRecord Record;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_PreviewScratchCapture);
			if (!FSeinPoolObjectCodecRegistry::CaptureObject(
				World.GetPoolObjectCodecManifest(),
				Source,
				ESeinPoolObjectKind::CommandBrokerResolver,
				/*PoolId=*/0,
				Record,
				Error))
			{
				World.InvalidateDeterministicExecutionContract(
					FString::Printf(
						TEXT("Formation layout could not capture resolver '%s' for isolated evaluation: %s"),
						*Source.GetPathName(), *Error));
				return nullptr;
			}
		}

		for (FPreviewScratchEntry& Entry : PreviewScratchCache)
		{
			if (Entry.Source.Get() != &Source
				|| Entry.World.Get() != CacheWorld)
			{
				continue;
			}
			if (Entry.SourceCapturedBytes == Record.StateBytes)
			{
				return Entry.Scratch.Get();
			}
			break;
		}

		UObject* ScratchObject = nullptr;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_PreviewScratchMaterialize);
			ScratchObject = FSeinPoolObjectCodecRegistry::MaterializeObject(
				World.GetPoolObjectCodecManifest(),
				Record,
				ESeinPoolObjectKind::CommandBrokerResolver,
				*GetTransientPackage(),
				Error);
		}
		USeinCommandBrokerResolver* Scratch =
			Cast<USeinCommandBrokerResolver>(ScratchObject);
		if (!Scratch)
		{
			World.InvalidateDeterministicExecutionContract(
				FString::Printf(
					TEXT("Formation layout could not materialize an isolated resolver '%s': %s"),
					*Source.GetPathName(), *Error));
			return nullptr;
		}

		FPreviewScratchEntry* Entry = PreviewScratchCache.FindByPredicate(
			[&Source, CacheWorld](const FPreviewScratchEntry& Existing)
			{
				return Existing.Source.Get() == &Source
					&& Existing.World.Get() == CacheWorld;
			});
		if (!Entry)
		{
			Entry = &PreviewScratchCache.AddDefaulted_GetRef();
			Entry->Source = &Source;
			Entry->World = CacheWorld;
		}
		Entry->Scratch = TStrongObjectPtr<USeinCommandBrokerResolver>(Scratch);
		Entry->SourceCapturedBytes = MoveTemp(Record.StateBytes);
		return Entry->Scratch.Get();
	}

	/** Keep a scratch only when layout was observationally pure. Stateful
	 *  resolvers remain supported in their authoritative pool, but preview and
	 *  the ownerless outer-selection evaluator never persist their writes. */
	static void FinalizePreviewScratchResolver(
		USeinWorldSubsystem& World,
		const USeinCommandBrokerResolver& Source,
		const USeinCommandBrokerResolver& Scratch)
	{
		const UWorld* CacheWorld = World.GetWorld();
		FPreviewScratchEntry* Entry =
			PreviewScratchCache.FindByPredicate(
				[&Source, &Scratch, CacheWorld](
					const FPreviewScratchEntry& Candidate)
				{
					return Candidate.Source.Get() == &Source
						&& Candidate.World.Get() == CacheWorld
						&& Candidate.Scratch.Get() == &Scratch;
				});
		FString Error;
		FSeinSnapshotPoolInstanceRecord ScratchRecord;
		bool bCaptured = false;
		if (Entry)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_PreviewScratchRevalidate);
			bCaptured = FSeinPoolObjectCodecRegistry::CaptureObject(
				World.GetPoolObjectCodecManifest(), Scratch,
				ESeinPoolObjectKind::CommandBrokerResolver, 0,
				ScratchRecord, Error);
		}
		const bool bMatches = bCaptured
			&& Entry->SourceCapturedBytes == ScratchRecord.StateBytes;
		if (!bMatches)
		{
			// A mutation is legal for a pooled resolver but not reusable on this
			// non-authoritative evaluator. Force the next acquire to rematerialize
			// by dropping its strong cache entry.
			UE_LOG(LogSeinBroker, VeryVerbose,
				TEXT("Discarding mutated formation-layout scratch for '%s'%s%s."),
				*Source.GetPathName(),
				Error.IsEmpty() ? TEXT("") : TEXT(": "),
				*Error);
			PreviewScratchCache.RemoveAll(
				[&Source, &Scratch, CacheWorld](
					const FPreviewScratchEntry& Entry)
				{
					return Entry.Source.Get() == &Source
						&& Entry.World.Get() == CacheWorld
						&& Entry.Scratch.Get() == &Scratch;
				});
			if (!Entry || !Error.IsEmpty())
			{
				World.InvalidateDeterministicExecutionContract(
					FString::Printf(
						TEXT("Formation-layout scratch for resolver '%s' could not be revalidated: %s"),
						*Source.GetPathName(),
						Error.IsEmpty()
							? TEXT("cache ownership was lost")
							: *Error));
			}
		}
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
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_ComputeMultiBrokerAnchors);
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
	if (N == 0 || !World.IsExecutionTopologyValid())
	{
		return Anchors;
	}

	// The parent layout runs through the project's default resolver CDO (it owns the formation map +
	// the shaping passes). No resolver (nav-less tests) -> leave every anchor at ClickTarget.
	const USeinCommandBrokerResolver* ResolverSource =
		SeinFormationPreviewLocal::ResolveDefaultResolverCDO();
	if (!ResolverSource) { return Anchors; }
	USeinCommandBrokerResolver* Resolver =
		SeinFormationPreviewLocal::GetPreviewScratchResolver(
			World, *ResolverSource);
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
	SeinFormationPreviewLocal::FinalizePreviewScratchResolver(
		World, *ResolverSource, *Resolver);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Formation_ComputePreview);
	FSeinFormationLayout Empty;
	USeinWorldSubsystem* World = GetWorldSubsystem(WorldContextObject);
	if (!World
		|| !World->IsExecutionTopologyValid()
		|| Members.Num() == 0)
	{
		return Empty;
	}

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

		if (!Resolver)
		{
			Resolver = const_cast<USeinCommandBrokerResolver*>(
				SeinFormationPreviewLocal::ResolveDefaultResolverCDO());
		}
		if (!Resolver) { continue; }
		const USeinCommandBrokerResolver* ResolverSource = Resolver;
		// Never drive the live pooled instance (or a shared CDO) from this
		// render-rate path — preview on a captured-state scratch clone.
		Resolver = SeinFormationPreviewLocal::GetPreviewScratchResolver(
			*World, *Resolver);
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
		SeinFormationPreviewLocal::FinalizePreviewScratchResolver(
			*World, *ResolverSource, *Resolver);

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
