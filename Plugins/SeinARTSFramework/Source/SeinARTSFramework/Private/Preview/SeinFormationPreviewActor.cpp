/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewActor.cpp
 * @brief   Destination-preview renderer: backend-agnostic orchestrator (SetPositions /
 *          HideAll + per-element change guards + quality→tint resolution) plus the default
 *          flat-mesh-quad backend. Decal / ISM variants live in sibling files and override
 *          the render hooks.
 */

#include "Preview/SeinFormationPreviewActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFormationPreview, Log, All);

namespace SeinFormationPreviewLocal
{
	/** Engine unit Plane (100x100uu, faces +Z) — the default quad for the mesh backends. */
	static UStaticMesh* GetFallbackPlaneMesh()
	{
		static TWeakObjectPtr<UStaticMesh> Cached;
		if (UStaticMesh* Existing = Cached.Get()) { return Existing; }
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (Mesh) { Cached = Mesh; }
		return Mesh;
	}

	/** Neutral engine material so the mesh backend renders SOMETHING before the designer
	 *  assigns PreviewMaterial. It has no Tint/OuterRad parameters, so styling is a no-op
	 *  while it's in use. */
	static UMaterialInterface* GetFallbackMeshMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (UMaterialInterface* Existing = Cached.Get()) { return Existing; }
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (Mat) { Cached = Mat; }
		return Mat;
	}
}

ASeinFormationPreviewActor::ASeinFormationPreviewActor()
{
	// No tick — state is pushed by the subsystem.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;        // render-side only

	// Bare scene root — render elements attach here.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
	SetRootComponent(Root);

	// CoverQualityTints intentionally ships EMPTY in the base framework — the base has no
	// quality vocabulary of its own. A project's preview BP (or the Cover extension's BP
	// subclass) populates it for whatever quality tags it uses.
}

// ====================================================================================================
// Backend-agnostic orchestrator
// ====================================================================================================

void ASeinFormationPreviewActor::SetPositions(const TArray<FVector>& WorldPositions, const TArray<FGameplayTag>& CoverQualities, const TArray<float>& Radii, const TArray<FSeinFormationPreviewElementStyle>& Styles)
{
	const int32 Count = WorldPositions.Num();
	EnsureElementCount(Count);

	const int32 Pool = NumElements();

	// Keep change-guard arrays sized to the pool. Explicitly sentinel new POSITION slots
	// (SetNum leaves FVector uninitialized) so the first update after growth always fires:
	// the position guard alone forces UpdateElement, and tint/radius/style sync on that call.
	if (LastWorldPositions.Num() < Pool)
	{
		const int32 OldNum = LastWorldPositions.Num();
		LastWorldPositions.SetNum(Pool);
		LastTints.SetNum(Pool);
		LastRadii.SetNum(Pool);
		LastStyles.SetNum(Pool);
		for (int32 i = OldNum; i < Pool; ++i)
		{
			LastWorldPositions[i] = FVector(TNumericLimits<float>::Max());
		}
	}

	const FSeinFormationPreviewElementStyle DefaultStyle;
	for (int32 i = 0; i < Pool; ++i)
	{
		if (i < Count)
		{
			const FSeinFormationPreviewElementStyle& Style =
				Styles.IsValidIndex(i) ? Styles[i] : DefaultStyle;

			// Style size override (full diameter) replaces the member's footprint radius so
			// ResolveFootprintSize lands exactly on MarkerSizeUU in every backend.
			float RadiusUU = Radii.IsValidIndex(i) ? Radii[i] : -1.f;
			if (Style.MarkerSizeUU > 0.f)
			{
				RadiusUU = Style.MarkerSizeUU * 0.5f;
			}

			const FGameplayTag Quality = CoverQualities.IsValidIndex(i) ? CoverQualities[i] : FGameplayTag();
			const FLinearColor Tint = ResolveTintForQuality(Quality) * Style.StyleTint;
			const FVector WorldPos = WorldPositions[i] + FVector(0.f, 0.f, ZOffsetUU);

			// Guard: only touch the render proxy when something about this element changed.
			const bool bChanged =
				   !WorldPos.Equals(LastWorldPositions[i], 0.01f)
				|| Tint != LastTints[i]
				|| !FMath::IsNearlyEqual(RadiusUU, LastRadii[i])
				|| Style != LastStyles[i];

			if (bChanged)
			{
				UpdateElement(i, WorldPos, Tint, RadiusUU, Style);
				LastWorldPositions[i] = WorldPos;
				LastTints[i]          = Tint;
				LastRadii[i]          = RadiusUU;
				LastStyles[i]         = Style;
			}

			SetElementVisible(i, true);
		}
		else
		{
			SetElementVisible(i, false);
			// Invalidate the guard so a future reuse of this slot always re-places (covers
			// backends that drop transform state when hidden, e.g. rebuilt ISM instances).
			LastWorldPositions[i] = FVector(TNumericLimits<float>::Max());
		}
	}

	CommitElements(Count);

	UE_LOG(LogSeinFormationPreview, VeryVerbose,
		TEXT("SetPositions: showing %d elements (pool %d, %d quality tags)"),
		Count, Pool, CoverQualities.Num());
}

void ASeinFormationPreviewActor::HideAll()
{
	const int32 Pool = NumElements();
	for (int32 i = 0; i < Pool; ++i)
	{
		SetElementVisible(i, false);
		if (LastWorldPositions.IsValidIndex(i))
		{
			LastWorldPositions[i] = FVector(TNumericLimits<float>::Max());
		}
	}
	CommitElements(0);
}

// ====================================================================================================
// Shared helpers
// ====================================================================================================

FLinearColor ASeinFormationPreviewActor::ResolveTintForQuality(const FGameplayTag& QualityTag) const
{
	if (!QualityTag.IsValid()) return NoCoverTint;
	if (const FLinearColor* Found = CoverQualityTints.Find(QualityTag))
	{
		return *Found;
	}
	return NoCoverTint;
}

float ASeinFormationPreviewActor::ResolveFootprintSize(float RadiusUU) const
{
	return (RadiusUU > 0.f) ? (RadiusUU * 2.f) : GroundSizeUU;
}

UMaterialInterface* ASeinFormationPreviewActor::ResolvePreviewMaterial() const
{
	return PreviewMaterial ? ToRawPtr(PreviewMaterial) : SeinFormationPreviewLocal::GetFallbackMeshMaterial();
}

UStaticMesh* ASeinFormationPreviewActor::ResolvePreviewMesh() const
{
	return PreviewMesh ? ToRawPtr(PreviewMesh) : SeinFormationPreviewLocal::GetFallbackPlaneMesh();
}

UStaticMesh* ASeinFormationPreviewActor::ResolveElementMesh(const FSeinFormationPreviewElementStyle& Style) const
{
	return Style.MarkerMesh ? ToRawPtr(Style.MarkerMesh) : ResolvePreviewMesh();
}

UMaterialInterface* ASeinFormationPreviewActor::ResolveElementMaterial(const FSeinFormationPreviewElementStyle& Style) const
{
	return Style.MarkerMaterial ? ToRawPtr(Style.MarkerMaterial) : ResolvePreviewMaterial();
}

// ====================================================================================================
// Default MESH backend — a pool of flat quads. A moving quad reprojects under TAA (masked
// materials write velocity; translucent ones can use Responsive AA), so it does not ghost the
// way a moving deferred decal does.
// ====================================================================================================

void ASeinFormationPreviewActor::EnsureElementCount_Implementation(int32 Count)
{
	UStaticMesh* Mesh = ResolvePreviewMesh();
	UMaterialInterface* SourceMat = ResolvePreviewMaterial();

	while (MeshPool.Num() < Count)
	{
		const int32 NewIndex = MeshPool.Num();
		const FName CompName = *FString::Printf(TEXT("PreviewMesh_%d"), NewIndex);
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, CompName);
		if (!Comp)
		{
			UE_LOG(LogSeinFormationPreview, Warning,
				TEXT("EnsureElementCount: failed to allocate mesh component at index %d"), NewIndex);
			break;
		}

		Comp->SetupAttachment(GetRootComponent());
		Comp->SetMobility(EComponentMobility::Movable);
		if (Mesh) { Comp->SetStaticMesh(Mesh); }
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->bReceivesDecals = false;
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetVisibility(false);     // start hidden — SetPositions shows

		// One MID per element so each holds its own tint.
		UMaterialInstanceDynamic* MID = SourceMat ? UMaterialInstanceDynamic::Create(SourceMat, this) : nullptr;
		if (MID) { Comp->SetMaterial(0, MID); }

		Comp->RegisterComponent();

		MeshPool.Add(Comp);
		MeshMIDs.SetNum(MeshPool.Num());
		MeshMIDs[NewIndex] = MID;
		MeshMIDSources.SetNum(MeshPool.Num());
		MeshMIDSources[NewIndex] = SourceMat;
	}
}

void ASeinFormationPreviewActor::UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU, const FSeinFormationPreviewElementStyle& Style)
{
	if (!MeshPool.IsValidIndex(Index)) return;
	UStaticMeshComponent* Comp = MeshPool[Index];
	if (!Comp) return;

	Comp->SetWorldLocation(WorldPos);

	// Per-element mesh: the style's marker mesh, else the backend default. Cheap no-op
	// compare — mesh changes only when the pool slot's member styling changes.
	if (UStaticMesh* WantMesh = ResolveElementMesh(Style))
	{
		if (Comp->GetStaticMesh() != WantMesh)
		{
			Comp->SetStaticMesh(WantMesh);
		}
	}

	// Per-element material: rebuild this element's MID only when its source material
	// changed (a style override appearing/going away, or a different override).
	UMaterialInterface* WantSource = ResolveElementMaterial(Style);
	if (MeshMIDSources.IsValidIndex(Index) && MeshMIDSources[Index] != WantSource)
	{
		UMaterialInstanceDynamic* NewMID = WantSource ? UMaterialInstanceDynamic::Create(WantSource, this) : nullptr;
		if (NewMID) { Comp->SetMaterial(0, NewMID); }
		MeshMIDs[Index] = NewMID;
		MeshMIDSources[Index] = WantSource;
	}

	// Engine Plane is 100uu square; scale so the quad spans the footprint diameter.
	// (Style marker meshes are authored to the same 100uu base footprint.)
	const float Side  = ResolveFootprintSize(RadiusUU);
	const float Scale = Side / 100.f;
	Comp->SetWorldScale3D(FVector(Scale, Scale, 1.f));

	// Only the tint is driven from C++ (quality tint × style tint, composed by the
	// orchestrator); the ring's shape (inner/outer radius) is owned entirely by the
	// material. Per-member radius is reflected by the quad scale above.
	if (UMaterialInstanceDynamic* MID = MeshMIDs.IsValidIndex(Index) ? MeshMIDs[Index] : nullptr)
	{
		MID->SetVectorParameterValue(TintParameterName, Tint);
	}
}

void ASeinFormationPreviewActor::SetElementVisible_Implementation(int32 Index, bool bVisible)
{
	if (MeshPool.IsValidIndex(Index) && MeshPool[Index])
	{
		MeshPool[Index]->SetVisibility(bVisible);
	}
}

int32 ASeinFormationPreviewActor::NumElements_Implementation() const
{
	return MeshPool.Num();
}
