/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinISMFormationPreviewActor.cpp
 * @brief   Instanced-Static-Mesh render backend (see header). One ISM component, per-instance
 *          custom-data tint, batched flush in CommitElements so a whole formation update is a
 *          single render-state refresh.
 */

#include "Preview/SeinISMFormationPreviewActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinISMPreview, Log, All);

namespace
{
	// Near-zero scale used to "hide" an instance (ISM has no per-instance visibility flag).
	constexpr float HiddenScale = 1.e-4f;
}

void ASeinISMFormationPreviewActor::EnsureElementCount_Implementation(int32 Count)
{
	if (!ISMComp)
	{
		ISMComp = NewObject<UInstancedStaticMeshComponent>(this, TEXT("PreviewISM"));
		if (!ISMComp)
		{
			UE_LOG(LogSeinISMPreview, Warning, TEXT("EnsureElementCount: failed to allocate ISM component"));
			return;
		}

		ISMComp->SetupAttachment(GetRootComponent());
		ISMComp->SetMobility(EComponentMobility::Movable);
		if (UStaticMesh* Mesh = ResolvePreviewMesh()) { ISMComp->SetStaticMesh(Mesh); }
		ISMComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISMComp->SetCastShadow(false);
		ISMComp->bReceivesDecals = false;
		ISMComp->SetCanEverAffectNavigation(false);
		// 4 floats per instance: RGBA tint, consumed via a PerInstanceCustomData material node.
		ISMComp->NumCustomDataFloats = 4;

		// Assign the look material. ISM can't carry a per-instance MID — per-instance tint is
		// written to custom-data floats 0–3; the ring's shape is owned entirely by the material.
		if (UMaterialInterface* SourceMat = ResolvePreviewMaterial())
		{
			ISMMID = UMaterialInstanceDynamic::Create(SourceMat, this);
			if (ISMMID) { ISMComp->SetMaterial(0, ISMMID); }
		}

		ISMComp->RegisterComponent();
	}

	// Grow to Count instances. New instances start hidden (near-zero scale); SetPositions
	// places the active ones the same pass before CommitElements flushes.
	const FTransform HiddenXform(FQuat::Identity, FVector::ZeroVector, FVector(HiddenScale));
	while (ISMComp->GetInstanceCount() < Count)
	{
		ISMComp->AddInstance(HiddenXform, /*bWorldSpace=*/true);
	}
}

void ASeinISMFormationPreviewActor::UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU)
{
	if (!ISMComp || Index < 0 || Index >= ISMComp->GetInstanceCount()) return;

	// Engine Plane is 100uu square; scale so the quad spans the footprint diameter.
	const float Scale = ResolveFootprintSize(RadiusUU) / 100.f;
	const FTransform Xform(FQuat::Identity, WorldPos, FVector(Scale, Scale, 1.f));
	ISMComp->UpdateInstanceTransform(Index, Xform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);

	// Per-instance tint → custom data floats 0–3 (RGBA). Flushed in CommitElements.
	ISMComp->SetCustomDataValue(Index, 0, Tint.R, /*bMarkRenderStateDirty=*/false);
	ISMComp->SetCustomDataValue(Index, 1, Tint.G, false);
	ISMComp->SetCustomDataValue(Index, 2, Tint.B, false);
	ISMComp->SetCustomDataValue(Index, 3, Tint.A, false);
}

void ASeinISMFormationPreviewActor::SetElementVisible_Implementation(int32 Index, bool bVisible)
{
	if (!ISMComp || Index < 0 || Index >= ISMComp->GetInstanceCount()) return;

	// No per-instance visibility flag — a visible element is already placed by UpdateElement;
	// hide by collapsing the instance to a near-zero scale at its current location.
	if (!bVisible)
	{
		FTransform Xform;
		ISMComp->GetInstanceTransform(Index, Xform, /*bWorldSpace=*/true);
		Xform.SetScale3D(FVector(HiddenScale));
		ISMComp->UpdateInstanceTransform(Index, Xform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
	}
}

int32 ASeinISMFormationPreviewActor::NumElements_Implementation() const
{
	return ISMComp ? ISMComp->GetInstanceCount() : 0;
}

void ASeinISMFormationPreviewActor::CommitElements_Implementation(int32 /*ActiveCount*/)
{
	// All per-instance updates above used bMarkRenderStateDirty=false; flush them in one refresh.
	if (ISMComp)
	{
		ISMComp->MarkRenderStateDirty();
	}
}
