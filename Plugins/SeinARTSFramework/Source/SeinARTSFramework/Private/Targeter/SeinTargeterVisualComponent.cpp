/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTargeterVisualComponent.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Optional mesh and decal rendering with explicit validity materials.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Targeter/SeinTargeterVisualComponent.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
DEFINE_LOG_CATEGORY_STATIC(LogSeinPointFacingPreview, Log, All);

namespace
{
	FLinearColor LegacyTint(ESeinTargeterValidity State)
	{
		if (State == ESeinTargeterValidity::Blocked) return FLinearColor(1, .2f, .2f, 1);
		if (State == ESeinTargeterValidity::Warning) return FLinearColor(1, .85f, .2f, 1);
		return FLinearColor(.2f, 1, .3f, 1);
	}
}
UMaterialInterface* FSeinPreviewMaterials::Resolve(ESeinTargeterValidity State) const
{
	if (State == ESeinTargeterValidity::Blocked && Blocked) return Blocked;
	if (State == ESeinTargeterValidity::Warning && Warning) return Warning;
	return Valid;
}
void USeinTargeterMeshComponent::BindLegacy(UStaticMeshComponent* Mesh, UMaterialInterface* Material)
{
	HologramMesh = Mesh;
	GhostMaterial = Material;
}
void USeinTargeterMeshComponent::InitializeVisual(const FSeinTargeterPreviewContext& Context)
{
	if (bInitialized) return;
	bInitialized = true;
	if (SourceClass.IsNull()) SourceClass = Context.ActorClass;
	if (!HologramMesh)
	{
		HologramMesh = NewObject<UStaticMeshComponent>(GetOwner());
		HologramMesh->SetupAttachment(GetOwner()->GetRootComponent());
		HologramMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HologramMesh->SetCanEverAffectNavigation(false);
		HologramMesh->SetCastShadow(false);
		HologramMesh->RegisterComponent();
	}
	BuildHologramMeshes();
	UpdateVisual(Context);
}
void USeinTargeterMeshComponent::UpdateVisual(const FSeinTargeterPreviewContext& Context)
{
	if (bAppliedMaterial && AppliedValidity == Context.Validity) return;
	bAppliedMaterial = true;
	AppliedValidity = Context.Validity;
	const auto Apply = [&](UMeshComponent* Mesh)
	{
		if (!Mesh) return;
		if (auto* Material = Materials.Resolve(Context.Validity))
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) Mesh->SetMaterial(Slot, Material);
		else if (Materials.IsEmpty() && GhostMaterial) TintComponent(Mesh, LegacyTint(Context.Validity));
	};
	Apply(HologramMesh);
	for (UMeshComponent* Mesh : GeneratedMeshes) Apply(Mesh);
}
void USeinTargeterDecalComponent::InitializeVisual(const FSeinTargeterPreviewContext& Context)
{
	if (bInitialized) return;
	bInitialized = true;
	if (!Decal)
	{
		Decal = NewObject<UDecalComponent>(GetOwner());
		Decal->SetupAttachment(GetOwner()->GetRootComponent());
		Decal->SetRelativeRotation(FRotator(-90, 0, 0));
		Decal->RegisterComponent();
	}
	if (bLegacyTint && Materials.IsEmpty() && Decal->GetDecalMaterial())
		Decal->SetDecalMaterial(UMaterialInstanceDynamic::Create(Decal->GetDecalMaterial(), this));
	UpdateVisual(Context);
}
void USeinTargeterDecalComponent::UpdateVisual(const FSeinTargeterPreviewContext& Context)
{
	if (!Decal) return;
	const float Size = bUseAreaRadius && Context.AreaRadius > 0 ? Context.AreaRadius : Radius;
	Decal->DecalSize = FVector(ProjectionDepth, Size, Size);
	if (bAppliedMaterial && AppliedValidity == Context.Validity) return;
	bAppliedMaterial = true;
	AppliedValidity = Context.Validity;
	if (auto* Material = Materials.Resolve(Context.Validity)) Decal->SetDecalMaterial(Material);
	else if (bLegacyTint && Materials.IsEmpty())
		if (auto* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Decal->GetDecalMaterial()))
			DynamicMaterial->SetVectorParameterValue(TEXT("TintColor"), LegacyTint(Context.Validity));
}
void USeinTargeterMeshComponent::BuildHologramMeshes()
{
	// Path 1: explicit single-mesh override on the spec — simplest case,
	// renders one mesh on the root HologramMesh component. Used when the
	// designer wants a pre-baked preview mesh different from the runtime
	// building (e.g. a simplified collision-shape mesh, an artist-authored
	// "ghost" version with cleaner topology).
	if (!MeshOverride.IsNull())
	{
		UStaticMesh* OverrideMesh = MeshOverride.LoadSynchronous();
		if (OverrideMesh && HologramMesh)
		{
			HologramMesh->SetStaticMesh(OverrideMesh);
			ApplyGhostMaterialToComponent(HologramMesh);
		}
		return;
	}

	// Path 2: multi-mesh CDO walk. Clone every UStaticMeshComponent from the
	// BuildingClass into our actor as a dynamic child mesh component, copying
	// the source's relative transform so rotations, offsets, and scales are
	// preserved — the hologram is then visually 1:1 with the spawned building.
	if (SourceClass.IsNull())
	{
		// Fallback: leave HologramMesh's BP-set mesh (if any) alone, but try
		// to wrap it in ghost material. Edge case — designer set neither
		// override nor BuildingClass.
		ApplyGhostMaterialToComponent(HologramMesh);
		return;
	}

	UClass* BuildingClass = SourceClass.LoadSynchronous();
	if (!BuildingClass)
	{
		UE_LOG(LogSeinPointFacingPreview, Warning,
			TEXT("BuildHologramMeshes: BuildingClass soft path failed to load (%s)."),
			*SourceClass.ToSoftObjectPath().ToString());
		return;
	}

	// Walk for UMeshComponent (the abstract base) so we catch both
	// UStaticMeshComponent and USkeletalMeshComponent — game teams that author
	// buildings as skeletal meshes (animated structural parts, deformable
	// silos, etc.) get the correct preview type per source. Other UMeshComponent
	// subclasses (instanced static, procedural, sprite) are skipped with a
	// Verbose log — they need bespoke clone logic the framework doesn't ship.
	//
	// GetActorClassDefaultComponents (NOT FindComponentByClass on CDO) walks
	// both native components AND Blueprint-SCS-added components. The latter
	// are NOT on the CDO directly — they're SCS templates instantiated at
	// spawn time. Most designer-authored buildings have their meshes added
	// via the BP Components panel = SCS, so this is the correct walk.
	TArray<const UMeshComponent*> SourceMeshes;
	AActor::GetActorClassDefaultComponents<UMeshComponent>(BuildingClass, SourceMeshes);

	if (SourceMeshes.Num() == 0)
	{
		UE_LOG(LogSeinPointFacingPreview, Warning,
			TEXT("BuildHologramMeshes: 0 UMeshComponents on %s. Hologram will use HologramMesh's BP-set mesh as fallback (or be invisible if none set)."),
			*GetNameSafe(BuildingClass));
		ApplyGhostMaterialToComponent(HologramMesh);
		return;
	}

	// Hide HologramMesh's own mesh — we're building dynamic children that
	// supersede it. Designer-set mesh on HologramMesh BP CDO is treated as a
	// "no BuildingClass" fallback only.
	if (HologramMesh)
	{
		HologramMesh->SetStaticMesh(nullptr);
	}

	int32 AddedCount = 0;
	for (const UMeshComponent* SourceComp : SourceMeshes)
	{
		if (!SourceComp) continue;

		// Type-dispatch: pick the matching component class so the clone holds
		// the right kind of mesh asset. Common pattern is one type per
		// building, but mixed (static body + skeletal turret) works too.
		UMeshComponent* HoloMesh = nullptr;

		if (const UStaticMeshComponent* SourceSM = Cast<UStaticMeshComponent>(SourceComp))
		{
			UStaticMesh* MeshAsset = SourceSM->GetStaticMesh();
			if (!MeshAsset) continue;

			UStaticMeshComponent* HoloSM = NewObject<UStaticMeshComponent>(GetOwner());
			HoloSM->SetStaticMesh(MeshAsset);
			HoloMesh = HoloSM;
		}
		else if (const USkeletalMeshComponent* SourceSkel = Cast<USkeletalMeshComponent>(SourceComp))
		{
			USkeletalMesh* MeshAsset = SourceSkel->GetSkeletalMeshAsset();
			if (!MeshAsset) continue;

			USkeletalMeshComponent* HoloSkel = NewObject<USkeletalMeshComponent>(GetOwner());
			HoloSkel->SetSkeletalMeshAsset(MeshAsset);
			// Preview is a static silhouette — no need to evaluate animation
			// blueprints or play sequences. Custom mode + no anim instance
			// keeps the mesh in bind pose with zero animation overhead.
			// Designers needing animated previews (mobile-unit ability previews
			// in a future spec, etc.) can subclass and override.
			HoloSkel->SetAnimationMode(EAnimationMode::AnimationCustomMode);
			HoloMesh = HoloSkel;
		}
		else
		{
			UE_LOG(LogSeinPointFacingPreview, Verbose,
				TEXT("BuildHologramMeshes: skipping unsupported mesh component type %s on %s. ")
				TEXT("Framework supports UStaticMeshComponent and USkeletalMeshComponent; subclass + override if you need others (instanced static, procedural, etc.)."),
				*SourceComp->GetClass()->GetName(), *GetNameSafe(BuildingClass));
			continue;
		}

		// Flatten the source attachment chain into actor-local space.
		HoloMesh->SetupAttachment(HologramMesh);
		FTransform Relative = SourceComp->GetRelativeTransform();
		for (const USceneComponent* Parent = SourceComp->GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
			Relative = Relative * Parent->GetRelativeTransform();
		HoloMesh->SetRelativeTransform(Relative);
		for (int32 Slot = 0; Slot < SourceComp->GetNumMaterials(); ++Slot)
			HoloMesh->SetMaterial(Slot, SourceComp->GetMaterial(Slot));
		HoloMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HoloMesh->SetCanEverAffectNavigation(false);
		HoloMesh->SetCastShadow(false);
		HoloMesh->RegisterComponent();

		ApplyGhostMaterialToComponent(HoloMesh);
		GeneratedMeshes.Add(HoloMesh);
		AddedCount++;
	}

	UE_LOG(LogSeinPointFacingPreview, Verbose,
		TEXT("BuildHologramMeshes: cloned %d mesh components from %s into hologram."),
		AddedCount, *GetNameSafe(BuildingClass));
}

void USeinTargeterMeshComponent::ApplyGhostMaterialToComponent(UMeshComponent* Comp)
{
	if (!Comp || !GhostMaterial || !Materials.IsEmpty()) return;

	// Wrap each material slot in a dynamic instance so per-instance TintColor
	// changes don't bleed into the source asset. UMeshComponent's GetNumMaterials
	// + SetMaterial work uniformly across static + skeletal subclasses. Silent
	// no-op when the component has zero slots (e.g. HologramMesh with no static
	// mesh assigned).
	const int32 NumSlots = Comp->GetNumMaterials();
	for (int32 i = 0; i < NumSlots; ++i)
	{
		UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(GhostMaterial, this);
		Comp->SetMaterial(i, DynMat);
	}
}

void USeinTargeterMeshComponent::TintComponent(UMeshComponent* Comp, const FLinearColor& Color)
{
	if (!Comp) return;
	const int32 NumSlots = Comp->GetNumMaterials();
	for (int32 i = 0; i < NumSlots; ++i)
	{
		if (UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(i)))
		{
			DynMat->SetVectorParameterValue(TEXT("TintColor"), Color);
		}
	}
}
