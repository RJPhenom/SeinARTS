/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinISMFormationPreviewActor.cpp
 * @brief   Instanced-Static-Mesh render backend (see header). One ISM component per
 *          distinct look (mesh + material), per-instance custom-data tint, batched flush
 *          in CommitElements. The hooks write a logical per-element cache; commit either
 *          updates instances in place (stable grouping — the cursor-drag hot path) or
 *          rebuilds each group's instance list (grouping changed — selection/style edits).
 */

#include "Preview/SeinISMFormationPreviewActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinISMPreview, Log, All);

int32 ASeinISMFormationPreviewActor::FindOrCreateGroup(UStaticMesh* Mesh, UMaterialInterface* Material)
{
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		if (Groups[GroupIndex].Mesh == Mesh && Groups[GroupIndex].Material == Material)
		{
			return GroupIndex;
		}
	}

	const FName CompName = *FString::Printf(TEXT("PreviewISM_%d"), Groups.Num());
	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this, CompName);
	if (!Comp)
	{
		UE_LOG(LogSeinISMPreview, Warning, TEXT("FindOrCreateGroup: failed to allocate ISM component"));
		return INDEX_NONE;
	}

	Comp->SetupAttachment(GetRootComponent());
	Comp->SetMobility(EComponentMobility::Movable);
	if (Mesh) { Comp->SetStaticMesh(Mesh); }
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);
	Comp->bReceivesDecals = false;
	Comp->SetCanEverAffectNavigation(false);
	// 4 floats per instance: RGBA tint, consumed via a PerInstanceCustomData material node.
	Comp->NumCustomDataFloats = 4;
	// ISM can't carry a per-instance MID — per-instance tint is custom-data floats 0–3; the
	// ring's shape is owned entirely by the material.
	if (Material) { Comp->SetMaterial(0, Material); }
	Comp->RegisterComponent();

	FSeinISMFormationPreviewGroup& Group = Groups.AddDefaulted_GetRef();
	Group.Comp = Comp;
	Group.Mesh = Mesh;
	Group.Material = Material;
	return Groups.Num() - 1;
}

void ASeinISMFormationPreviewActor::EnsureElementCount_Implementation(int32 Count)
{
	// Logical elements only — instances materialize in CommitElements, grouped by look.
	if (Elements.Num() < Count)
	{
		Elements.SetNum(Count);
	}
}

void ASeinISMFormationPreviewActor::UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU, const FSeinFormationPreviewElementStyle& Style)
{
	if (!Elements.IsValidIndex(Index)) return;
	FElementState& Element = Elements[Index];

	// Engine Plane is 100uu square; scale so the quad spans the footprint diameter.
	// (Style marker meshes are authored to the same 100uu base footprint.)
	const float Scale = ResolveFootprintSize(RadiusUU) / 100.f;
	Element.Xform = FTransform(FQuat::Identity, WorldPos, FVector(Scale, Scale, 1.f));
	Element.Tint = Tint;
	Element.bDirty = true;

	const int32 WantGroup = FindOrCreateGroup(ResolveElementMesh(Style), ResolveElementMaterial(Style));
	if (WantGroup != Element.GroupIndex)
	{
		Element.GroupIndex = WantGroup;
		bGroupingDirty = true;
	}
}

void ASeinISMFormationPreviewActor::SetElementVisible_Implementation(int32 Index, bool bVisible)
{
	if (!Elements.IsValidIndex(Index)) return;

	// Visibility = presence in a group's instance list, so a flip regroups on commit.
	if (Elements[Index].bVisible != bVisible)
	{
		Elements[Index].bVisible = bVisible;
		bGroupingDirty = true;
	}
}

int32 ASeinISMFormationPreviewActor::NumElements_Implementation() const
{
	return Elements.Num();
}

void ASeinISMFormationPreviewActor::CommitElements_Implementation(int32 /*ActiveCount*/)
{
	if (bGroupingDirty)
	{
		// Grouping changed (selection/style/visibility edits — rare next to cursor ticks):
		// rebuild every group's instance list from the logical cache. Hidden elements are
		// simply absent, so there is no per-instance visibility hack.
		for (FSeinISMFormationPreviewGroup& Group : Groups)
		{
			if (Group.Comp) { Group.Comp->ClearInstances(); }
		}
		for (FElementState& Element : Elements)
		{
			Element.LocalInstanceIndex = INDEX_NONE;
			if (!Element.bVisible || !Groups.IsValidIndex(Element.GroupIndex)) continue;
			UInstancedStaticMeshComponent* Comp = Groups[Element.GroupIndex].Comp;
			if (!Comp) continue;

			Element.LocalInstanceIndex = Comp->AddInstance(Element.Xform, /*bWorldSpace=*/true);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 0, Element.Tint.R, /*bMarkRenderStateDirty=*/false);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 1, Element.Tint.G, false);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 2, Element.Tint.B, false);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 3, Element.Tint.A, false);
			Element.bDirty = false;
		}
		bGroupingDirty = false;
	}
	else
	{
		// Stable grouping (the cursor-drag hot path): update touched instances in place.
		for (FElementState& Element : Elements)
		{
			if (!Element.bDirty) continue;
			Element.bDirty = false;
			if (Element.LocalInstanceIndex == INDEX_NONE
				|| !Groups.IsValidIndex(Element.GroupIndex)) continue;
			UInstancedStaticMeshComponent* Comp = Groups[Element.GroupIndex].Comp;
			if (!Comp) continue;

			Comp->UpdateInstanceTransform(Element.LocalInstanceIndex, Element.Xform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 0, Element.Tint.R, /*bMarkRenderStateDirty=*/false);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 1, Element.Tint.G, false);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 2, Element.Tint.B, false);
			Comp->SetCustomDataValue(Element.LocalInstanceIndex, 3, Element.Tint.A, false);
		}
	}

	// All per-instance updates above used bMarkRenderStateDirty=false; flush each group once.
	for (FSeinISMFormationPreviewGroup& Group : Groups)
	{
		if (Group.Comp) { Group.Comp->MarkRenderStateDirty(); }
	}
}
