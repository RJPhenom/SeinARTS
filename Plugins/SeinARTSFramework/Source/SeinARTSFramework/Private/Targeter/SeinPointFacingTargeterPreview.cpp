/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPointFacingTargeterPreview.cpp
 * @brief   Phase 3 building-placement hologram preview. Multi-mesh: clones
 *          each UStaticMeshComponent from the BuildingClass CDO so the
 *          hologram is 1:1 with the spawned building (rotations, offsets,
 *          scales, all preserved).
 */

#include "Targeter/SeinPointFacingTargeterPreview.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinPointFacingPreview, Log, All);

ASeinPointFacingTargeterPreview::ASeinPointFacingTargeterPreview()
{
	HologramMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HologramMesh"));
	RootComponent = HologramMesh;
	// Ghost meshes are non-interactive — disable collision so the cursor trace
	// passes through the preview (otherwise the preview would self-occlude its
	// own placement hit testing).
	HologramMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HologramMesh->SetCanEverAffectNavigation(false);
	HologramMesh->SetCastShadow(false);
}

void ASeinPointFacingTargeterPreview::InitializePreview(USeinTargeterSpec* InSpec, float InAreaRadiusWorld)
{
	// Base class assigns Spec + AreaRadiusWorld members. After that returns,
	// Spec is valid and we can drive mesh resolution off it. The targeter
	// subsystem uses SpawnActorDeferred + InitializePreview + FinishSpawning
	// so this runs BEFORE BeginPlay — meaning the hologram is fully built
	// before any tick / preview update logic fires.
	Super::InitializePreview(InSpec, InAreaRadiusWorld);
	BuildHologramMeshes();
}

void ASeinPointFacingTargeterPreview::BuildHologramMeshes()
{
	if (!GhostMaterial)
	{
		UE_LOG(LogSeinPointFacingPreview, Warning,
			TEXT("GhostMaterial unset on %s — hologram meshes will render with source mesh materials, no validity tinting. ")
			TEXT("Set GhostMaterial on the BP subclass for translucent ghost rendering."),
			*GetClass()->GetName());
	}

	const USeinPointFacingTargeterSpec* PFSpec = Cast<USeinPointFacingTargeterSpec>(Spec);
	if (!PFSpec)
	{
		UE_LOG(LogSeinPointFacingPreview, Warning,
			TEXT("BuildHologramMeshes: Spec is not USeinPointFacingTargeterSpec (got %s). No mesh built."),
			Spec ? *Spec->GetClass()->GetName() : TEXT("null"));
		return;
	}

	// Path 1: explicit single-mesh override on the spec — simplest case,
	// renders one mesh on the root HologramMesh component. Used when the
	// designer wants a pre-baked preview mesh different from the runtime
	// building (e.g. a simplified collision-shape mesh, an artist-authored
	// "ghost" version with cleaner topology).
	if (!PFSpec->PreviewMeshOverride.IsNull())
	{
		UStaticMesh* OverrideMesh = PFSpec->PreviewMeshOverride.LoadSynchronous();
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
	if (PFSpec->BuildingClass.IsNull())
	{
		// Fallback: leave HologramMesh's BP-set mesh (if any) alone, but try
		// to wrap it in ghost material. Edge case — designer set neither
		// override nor BuildingClass.
		ApplyGhostMaterialToComponent(HologramMesh);
		return;
	}

	UClass* BuildingClass = PFSpec->BuildingClass.LoadSynchronous();
	if (!BuildingClass)
	{
		UE_LOG(LogSeinPointFacingPreview, Warning,
			TEXT("BuildHologramMeshes: BuildingClass soft path failed to load (%s)."),
			*PFSpec->BuildingClass.ToSoftObjectPath().ToString());
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

			UStaticMeshComponent* HoloSM = NewObject<UStaticMeshComponent>(this);
			HoloSM->SetStaticMesh(MeshAsset);
			HoloMesh = HoloSM;
		}
		else if (const USkeletalMeshComponent* SourceSkel = Cast<USkeletalMeshComponent>(SourceComp))
		{
			USkeletalMesh* MeshAsset = SourceSkel->GetSkeletalMeshAsset();
			if (!MeshAsset) continue;

			USkeletalMeshComponent* HoloSkel = NewObject<USkeletalMeshComponent>(this);
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

		// Common setup for both types — attach to root, copy the source's
		// authored relative transform, kill collision/shadow/nav. NOTE: for
		// typical flat hierarchies (DefaultSceneRoot + sibling meshes) this
		// is correct out-of-the-box. For nested hierarchies (mesh A child of
		// mesh B), all clones still attach to the root rather than preserving
		// the parent chain — visual error only shows with deep nesting.
		// Documented limitation; revisit if it bites. Most building BPs flat.
		HoloMesh->SetupAttachment(HologramMesh);
		HoloMesh->SetRelativeTransform(SourceComp->GetRelativeTransform());
		HoloMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HoloMesh->SetCanEverAffectNavigation(false);
		HoloMesh->SetCastShadow(false);
		HoloMesh->RegisterComponent();

		ApplyGhostMaterialToComponent(HoloMesh);
		DynamicMeshes.Add(HoloMesh);
		AddedCount++;
	}

	UE_LOG(LogSeinPointFacingPreview, Verbose,
		TEXT("BuildHologramMeshes: cloned %d mesh components from %s into hologram."),
		AddedCount, *GetNameSafe(BuildingClass));
}

void ASeinPointFacingTargeterPreview::ApplyGhostMaterialToComponent(UMeshComponent* Comp)
{
	if (!Comp || !GhostMaterial) return;

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

void ASeinPointFacingTargeterPreview::TintComponent(UMeshComponent* Comp, const FLinearColor& Color)
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

void ASeinPointFacingTargeterPreview::OnPreviewUpdated_Implementation()
{
	if (!HologramMesh) return;

	// Position handling: while WaitingForCapture, base set the actor to the
	// cursor. While Dragging, the subsystem keeps cursor as the live mouse
	// but pushes a non-zero DragAnchorWorld — re-pin the actor to the anchor
	// and yaw to CurrentDragYawDegrees (the value the subsystem WILL CAPTURE
	// on confirm). Using the subsystem's authoritative yaw — already snapped
	// or free per spec — keeps the hologram visually identical to what the
	// player will get when they release. Computing yaw locally from cursor
	// here would diverge for snapped specs (preview rotates freely while
	// capture snaps), which is the bug we hit before.
	const bool bDragging = !CurrentDragAnchorWorld.IsNearlyZero();
	if (bDragging)
	{
		SetActorLocation(CurrentDragAnchorWorld);
		SetActorRotation(FRotator(0.0f, CurrentDragYawDegrees, 0.0f));
	}
	// Else: base class SetActorLocation(CursorWorld) already happened.

	// Validity tint — push TintColor into every dynamic material instance on
	// every visible mesh component (HologramMesh + every DynamicMesh clone).
	FLinearColor Tint = FLinearColor::White;
	switch (CurrentValidity)
	{
		case ESeinTargeterValidity::Valid:   Tint = FLinearColor(0.2f, 1.0f, 0.3f, 1.0f); break;
		case ESeinTargeterValidity::Warning: Tint = FLinearColor(1.0f, 0.85f, 0.2f, 1.0f); break;
		case ESeinTargeterValidity::Blocked: Tint = FLinearColor(1.0f, 0.2f, 0.2f, 1.0f); break;
	}

	TintComponent(HologramMesh, Tint);
	for (UMeshComponent* M : DynamicMeshes)
	{
		TintComponent(M, Tint);
	}
}
