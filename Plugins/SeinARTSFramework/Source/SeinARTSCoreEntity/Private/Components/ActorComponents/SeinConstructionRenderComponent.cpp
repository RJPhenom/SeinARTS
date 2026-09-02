/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConstructionRenderComponent.cpp
 */

#include "Components/ActorComponents/SeinConstructionRenderComponent.h"

#include "Actor/SeinEntityBridgeComponent.h"
#include "Events/SeinVisualEvent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinConstructionRender, Log, All);

USeinConstructionRenderComponent::USeinConstructionRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USeinConstructionRenderComponent::BeginPlay()
{
	Super::BeginPlay();

	// Locate the entity bridge on the owning actor and subscribe to its
	// per-entity visual event delegate. The bridge is the single channel
	// through which sim → render events reach this component — no inheritance
	// from a SeinARTS base class needed.
	AActor* Owner = GetOwner();
	if (!Owner) return;

	USeinEntityBridgeComponent* Bridge = Owner->FindComponentByClass<USeinEntityBridgeComponent>();
	if (!Bridge)
	{
		UE_LOG(LogSeinConstructionRender, Warning,
			TEXT("[%s] BeginPlay: no USeinEntityBridgeComponent on the owning actor — "
				 "visual events won't reach this component. Add the Entity Bridge."),
			*GetNameSafe(Owner));
		return;
	}

	Bridge->OnVisualEvent.AddDynamic(this, &USeinConstructionRenderComponent::HandleVisualEvent);
	CachedBridge = Bridge;
}

void USeinConstructionRenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean unsubscribe — the delegate uses weak owner refs but explicit
	// removal keeps the multicast list tight if the bridge outlives us
	// (it shouldn't on actor teardown, but be defensive).
	if (USeinEntityBridgeComponent* Bridge = CachedBridge.Get())
	{
		Bridge->OnVisualEvent.RemoveDynamic(this, &USeinConstructionRenderComponent::HandleVisualEvent);
	}
	CachedBridge = nullptr;

	// Defensive cleanup of any spawned construction visuals — Unreal's GC
	// would catch them eventually but explicit teardown avoids visible
	// orphan frames on rapid actor destroy / level transition.
	ExitConstructionState();

	Super::EndPlay(EndPlayReason);
}

void USeinConstructionRenderComponent::HandleVisualEvent(const FSeinVisualEvent& Event)
{
	if (Event.Type != ESeinVisualEventType::ConstructionStateChanged)
	{
		return;
	}

	// The bridge already filters by PrimaryEntity (it routes events to the
	// owning actor of that entity), so we don't double-check the handle here.

	const bool bUnderConstruction = (Event.Value > FFixedPoint::Zero);
	if (bUnderConstruction)
	{
		EnterConstructionState();
	}
	else
	{
		ExitConstructionState();
	}
}

void USeinConstructionRenderComponent::EnterConstructionState()
{
	if (bInConstructionState) return;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogSeinConstructionRender, Warning, TEXT("EnterConstructionState: AC has no owning actor"));
		return;
	}

	// Hide all currently-visible mesh components — the "final" visuals. Track
	// only what we hid so the inverse on Exit touches the same set
	// (preserves designer-driven invisibility on unrelated meshes).
	HiddenMainMeshes.Reset();
	TArray<UMeshComponent*> AllMeshes;
	Owner->GetComponents<UMeshComponent>(AllMeshes);
	for (UMeshComponent* Mesh : AllMeshes)
	{
		if (Mesh && Mesh->IsVisible())
		{
			Mesh->SetVisibility(false, /*bPropagateToChildren=*/false);
			HiddenMainMeshes.Add(Mesh);
		}
	}

	// Spawn the configured placement visual. None = leave the (now-hidden)
	// main mesh as-is; useful for "ground-stamp + decal only" minimal previews.
	USceneComponent* Root = Owner->GetRootComponent();
	switch (PlacementVisualType)
	{
	case ESeinConstructionPlacementVisualType::None:
		break;

	case ESeinConstructionPlacementVisualType::StaticMesh:
		if (PlacementStaticMesh)
		{
			UStaticMeshComponent* SMC = NewObject<UStaticMeshComponent>(Owner);
			SMC->SetStaticMesh(PlacementStaticMesh);
			SMC->SetMobility(EComponentMobility::Movable);
			SMC->RegisterComponent();
			if (Root)
			{
				SMC->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
			}
			SpawnedPlacementStaticMesh = SMC;
		}
		else
		{
			UE_LOG(LogSeinConstructionRender, Warning,
				TEXT("EnterConstructionState[%s]: PlacementVisualType=StaticMesh but PlacementStaticMesh is null"),
				*Owner->GetName());
		}
		break;

	case ESeinConstructionPlacementVisualType::SkeletalMesh:
		if (PlacementSkeletalMesh)
		{
			USkeletalMeshComponent* SKMC = NewObject<USkeletalMeshComponent>(Owner);
			SKMC->SetSkeletalMesh(PlacementSkeletalMesh);
			SKMC->SetMobility(EComponentMobility::Movable);
			SKMC->RegisterComponent();
			if (Root)
			{
				SKMC->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
			}
			SpawnedPlacementSkeletalMesh = SKMC;
		}
		else
		{
			UE_LOG(LogSeinConstructionRender, Warning,
				TEXT("EnterConstructionState[%s]: PlacementVisualType=SkeletalMesh but PlacementSkeletalMesh is null"),
				*Owner->GetName());
		}
		break;

	case ESeinConstructionPlacementVisualType::BlueprintActor:
		if (PlacementBlueprint)
		{
			if (UWorld* World = Owner->GetWorld())
			{
				FActorSpawnParameters Params;
				Params.Owner = Owner;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AActor* Spawned = World->SpawnActor<AActor>(PlacementBlueprint, Owner->GetActorTransform(), Params);
				if (Spawned)
				{
					Spawned->AttachToActor(Owner, FAttachmentTransformRules::KeepWorldTransform);
					SpawnedPlacementActor = Spawned;
				}
			}
		}
		else
		{
			UE_LOG(LogSeinConstructionRender, Warning,
				TEXT("EnterConstructionState[%s]: PlacementVisualType=BlueprintActor but PlacementBlueprint is null"),
				*Owner->GetName());
		}
		break;
	}

	// Optional ground-stamp decal — orthogonal to the placement visual choice.
	if (GroundStampDecal)
	{
		UDecalComponent* Decal = NewObject<UDecalComponent>(Owner);
		Decal->SetDecalMaterial(GroundStampDecal);
		Decal->DecalSize = GroundStampDecalSize;
		// Decal projects along its local +X by default — rotate to point straight
		// down onto the ground beneath the actor.
		Decal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
		Decal->RegisterComponent();
		if (Root)
		{
			Decal->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		}
		SpawnedDecalComponent = Decal;
	}

	bInConstructionState = true;

	UE_LOG(LogSeinConstructionRender, Verbose,
		TEXT("EnterConstructionState[%s]: hid %d main meshes, spawned visual type=%d"),
		*Owner->GetName(), HiddenMainMeshes.Num(), static_cast<int32>(PlacementVisualType));
}

void USeinConstructionRenderComponent::ExitConstructionState()
{
	if (!bInConstructionState) return;

	// Tear down spawned scene primitives. Each pointer is independent — only
	// the populated branches do work.
	if (SpawnedPlacementStaticMesh)
	{
		SpawnedPlacementStaticMesh->DestroyComponent();
		SpawnedPlacementStaticMesh = nullptr;
	}
	if (SpawnedPlacementSkeletalMesh)
	{
		SpawnedPlacementSkeletalMesh->DestroyComponent();
		SpawnedPlacementSkeletalMesh = nullptr;
	}
	if (SpawnedPlacementActor)
	{
		SpawnedPlacementActor->Destroy();
		SpawnedPlacementActor = nullptr;
	}
	if (SpawnedDecalComponent)
	{
		SpawnedDecalComponent->DestroyComponent();
		SpawnedDecalComponent = nullptr;
	}

	// Restore visibility on the meshes we hid. Weak ptrs silently skip any
	// meshes destroyed during the construction window.
	for (TWeakObjectPtr<UMeshComponent>& MeshPtr : HiddenMainMeshes)
	{
		if (UMeshComponent* Mesh = MeshPtr.Get())
		{
			Mesh->SetVisibility(true, /*bPropagateToChildren=*/false);
		}
	}
	HiddenMainMeshes.Reset();

	bInConstructionState = false;

	if (const AActor* Owner = GetOwner())
	{
		UE_LOG(LogSeinConstructionRender, Verbose,
			TEXT("ExitConstructionState[%s]: restored main mesh visibility, destroyed placement visuals"),
			*Owner->GetName());
	}
}
