/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActor.cpp
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of base actor class for simulation-backed entities.
 */

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSim, Log, All);

ASeinActor::ASeinActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// The ONLY default subobject. Holds the entity handle + transform sync +
	// visual event forwarding (old "bridge" role) AND the unified authoring
	// array (bIsAbstract + ComponentData). Variable + subobject names align
	// as `EntityBridge` so the components panel shows
	// "SeinARTS Entity Bridge (EntityBridge)".
	EntityBridge = CreateDefaultSubobject<USeinEntityComponent>(TEXT("EntityBridge"));

	// All other legacy default subobjects (the pre-Phase-5 ArchetypeDefinition, TagsComponent,
	// typed-wrapper sim ACs) have been excised. Sim data + tags live on
	// the entity bridge: BaseTags UPROPERTY for tags, ComponentData
	// (TArray<FInstancedStruct>) for component authoring.
}

void ASeinActor::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void ASeinActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Snapshot the actor's world location AND rotation into deterministic
	// fixed-point values at edit time. The conversion runs once in the
	// editor process; the result is serialized to the .umap. At runtime,
	// all clients (PC, Mac Apple Silicon, mobile, console) read the same
	// int64 bits from disk, no float conversion in the chain. Cross-arch
	// lockstep safe.
	//
	// Both location AND rotation must be baked — `SpawnEntityFromPlacedActor`
	// uses these to construct the sim entity's FFixedTransform. Skipping
	// rotation here would let the sim transform default to identity, and
	// the entity component would re-apply identity to the AActor every frame,
	// visually snapping the placed actor's rotation to zero at PIE start.
	//
	// `bFinished` is false during a drag, true on release. Snapshotting on
	// every intermediate doesn't hurt (cheap), and ensures Ctrl+Z / undo
	// also lands a fresh snapshot.
	PlacedSimLocation = FFixedVector::FromVector(GetActorLocation());
	bSimLocationBaked = true;

	PlacedSimRotation = FFixedQuaternion::FromQuat(GetActorQuat());
	bSimRotationBaked = true;

	if (bFinished)
	{
		MarkPackageDirty();
	}
}
#endif

void ASeinActor::InitializeWithEntity(FSeinEntityHandle Handle)
{
	if (!EntityBridge)
	{
		UE_LOG(LogSeinSim, Error, TEXT("ASeinActor::InitializeWithEntity - EntityBridge is null!"));
		return;
	}

	EntityBridge->SetEntityHandle(Handle);

	// Fire Blueprint event
	ReceiveEntityInitialized();

	// Verbose: per-actor on map travel; spammy. Re-enable via
	// `Log LogSeinSim Verbose` when diagnosing entity-init races.
	UE_LOG(LogSeinSim, Verbose, TEXT("ASeinActor initialized with entity %s"), *Handle.ToString());
}

FSeinEntityHandle ASeinActor::GetEntityHandle() const
{
	if (!EntityBridge)
	{
		return FSeinEntityHandle::Invalid();
	}

	return EntityBridge->GetEntityHandle();
}

bool ASeinActor::HasValidEntity() const
{
	if (!EntityBridge)
	{
		return false;
	}

	return EntityBridge->HasValidEntity();
}
