/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActor.cpp
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of base actor class for simulation-backed entities.
 */

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinIdentityComponent.h"      // IdentityTag surfaced as an asset tag
#include "Core/SeinAssetTagKeys.h"
#include "UObject/AssetRegistryTagsContext.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"                    // CheckForErrors → Map Check log
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#endif

#include "SeinARTSCoreEntityLog.h"  // LogSeinSim (module-shared)

ASeinActor::ASeinActor()
{
	// ASeinActor has no Tick implementation. Per-frame render interpolation is
	// owned by USeinEntityBridgeComponent, so registering an actor tick for every RTS
	// entity is pure scheduler overhead.
	PrimaryActorTick.bCanEverTick = false;

	// The ONLY default subobject. Holds the entity handle + transform sync +
	// visual event forwarding (old "bridge" role) AND the unified authoring
	// array (bIsAbstract + ComponentData). Variable + subobject names align
	// as `EntityBridge` so the components panel shows
	// "SeinARTS Entity Bridge (EntityBridge)".
	EntityBridge = CreateDefaultSubobject<USeinEntityBridgeComponent>(TEXT("EntityBridge"));

	// All other legacy default subobjects (the pre-Phase-5 ArchetypeDefinition, TagsComponent,
	// typed-wrapper sim ACs) have been excised. Sim data + tags live on
	// the entity bridge: BaseTags UPROPERTY for tags, ComponentData
	// (TArray<FInstancedStruct>) for component authoring.
}

void ASeinActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// The level-editor world registers every placed unit too. Applying these
	// policies only from BeginPlay protects standalone/cooked worlds but leaves
	// the editor's duplicate 200-unit scene contributing animated BLAS memory
	// throughout PIE. This hook runs after the actor's complete component set is
	// registered in both editor and runtime worlds, before the renderer can keep
	// treating ordinary RTS crowd meshes as ray-traced geometry.
	if (EntityBridge)
	{
		EntityBridge->ApplyRayTracingGeometryPolicy();
		EntityBridge->ApplySkeletalMeshPerformancePolicy();
	}
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

void ASeinActor::CheckForErrors()
{
	Super::CheckForErrors();

	const USeinEntityBridgeComponent* Bridge = EntityBridge;
	if (!Bridge)
	{
		return;
	}

	// The same shared validator match bootstrap runs — but against THIS placed
	// instance's authored array, which the Blueprint pre-compile gate (class
	// defaults only) can never see.
	TArray<FSeinComponentDataIssue> Issues;
	USeinEntityBridgeComponent::ValidateComponentData(Bridge->ComponentData, Issues);
	for (const FSeinComponentDataIssue& Issue : Issues)
	{
		// Error, not Warning: these entries abort match bootstrap outright.
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::FromString(
				Issue.Description + TEXT(". A match bootstrapping this level will fail on this entry."))));
	}

	if (Bridge->HasStructuralComponentDataOverride()
		&& !Bridge->IsComponentDataShapeExplainedByAuthoring())
	{
		// Shape divergence fully explained by this instance's authoring data
		// components (e.g. a per-instance Injection Enabled toggle) is intent,
		// not staleness — nagging it would advise a revert the next bake
		// undoes.
		FMessageLog("MapCheck").Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::FromString(TEXT(
				"ComponentData differs in shape from the class default (stale or deliberate structural override) and no longer tracks Blueprint ComponentData updates. If unintended, revert the Component Data property on this actor to re-sync."))));
	}
}
#endif

void ASeinActor::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	// Surface the entity's IdentityTag onto the asset's FAssetData so the editor
	// auto-tag collision check reads it without loading this CDO. The tag lives
	// nested in an FSeinIdentityComponent inside the bridge's ComponentData
	// array. One identity per entity is the runtime contract (ComponentData is
	// keyed by struct type at spawn), so the first valid entry wins. Bare
	// ToString() form, matching the USeinAbility / USeinEffect tags.
	if (const USeinEntityBridgeComponent* Bridge = EntityBridge)
	{
		for (const FInstancedStruct& Entry : Bridge->ComponentData)
		{
			if (!Entry.IsValid()) continue;
			if (Entry.GetScriptStruct() != FSeinIdentityComponent::StaticStruct()) continue;
			const FSeinIdentityComponent& Identity = Entry.Get<FSeinIdentityComponent>();
			if (Identity.IdentityTag.IsValid())
			{
				Context.AddTag(FAssetRegistryTag(
					SeinAssetTagKeys::IdentityTag(),
					Identity.IdentityTag.ToString(),
					FAssetRegistryTag::TT_Alphabetical));
			}
			break;
		}
	}
}

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
