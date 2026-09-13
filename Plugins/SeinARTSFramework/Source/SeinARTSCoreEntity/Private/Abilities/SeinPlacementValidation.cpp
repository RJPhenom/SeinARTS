/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPlacementValidation.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Validates placement against static navigation and live blocker footprints.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Abilities/SeinPlacementValidation.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Components/SeinExtentsHelpers.h"
#include "Components/SeinNavigationPayload.h"
#include "Math/CollisionQueries.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Stamping/SeinStampUtils.h"

namespace
{
	struct FFootprint
	{
		FSeinExtentsShape Shape;
		FFixedVector Center, AxisX, AxisY;
		FFootprint(const FSeinExtentsShape& InShape, const FFixedTransform& Transform) : Shape(InShape)
		{
			const auto Stamp = Shape.AsStampShape();
			Center = SeinStampUtils::ComputeStampWorldOrigin(Stamp, Transform.GetLocation(), Transform.Rotation);
			const auto Yaw = SeinStampUtils::YawFromRotation(Transform.Rotation)
				+ SeinStampUtils::DegToRad(Shape.YawOffsetDegrees);
			AxisX = FFixedVector(SeinMath::Cos(Yaw), SeinMath::Sin(Yaw), FFixedPoint::Zero);
			AxisY = FFixedVector(-AxisX.Y, AxisX.X, FFixedPoint::Zero);
		}
	};

	bool Overlaps(const FFootprint& A, const FFootprint& B)
	{
		const bool BoxA = A.Shape.Shape == ESeinExtentsShape::Box;
		const bool BoxB = B.Shape.Shape == ESeinExtentsShape::Box;
		if (!BoxA && !BoxB) return SeinCollision::DiscVsDisc(A.Center, A.Shape.Radius, B.Center, B.Shape.Radius).bHit;
		if (!BoxA) return SeinCollision::DiscVsOBB(A.Center, A.Shape.Radius, B.Center, B.AxisX, B.AxisY, B.Shape.HalfExtentX, B.Shape.HalfExtentY).bHit;
		if (!BoxB) return SeinCollision::DiscVsOBB(B.Center, B.Shape.Radius, A.Center, A.AxisX, A.AxisY, A.Shape.HalfExtentX, A.Shape.HalfExtentY).bHit;
		return SeinCollision::OBBVsOBB(A.Center, A.AxisX, A.AxisY, A.Shape.HalfExtentX, A.Shape.HalfExtentY,
			B.Center, B.AxisX, B.AxisY, B.Shape.HalfExtentX, B.Shape.HalfExtentY).bHit;
	}
}

bool SeinPlacementValidation::IsValid(const USeinWorldSubsystem& World,
	const USeinPointFacingTargeterSpec* Spec, const FFixedVector& Location, FFixedPoint Yaw)
{
	return Spec && IsValidClass(World, Spec->BuildingClass, Location, Yaw);
}

TSoftClassPtr<AActor> SeinPlacementValidation::ResolveActorClass(const USeinAbility& Ability)
{
	if (!Ability.Placement.ActorClass.IsNull()) return Ability.Placement.ActorClass;
	const auto* Spec = Cast<USeinPointFacingTargeterSpec>(Ability.TargeterSpec);
	return Spec ? Spec->BuildingClass : TSoftClassPtr<AActor>();
}

bool SeinPlacementValidation::IsValidClass(const USeinWorldSubsystem& World,
	TSoftClassPtr<AActor> ActorClass, const FFixedVector& Location, FFixedPoint Yaw)
{
	if (ActorClass.IsNull()) return false;
	UClass* Class = ActorClass.LoadSynchronous();
	const auto* Extents = SeinExtentsHelpers::GetExtentsFromActorClass(Class);
	if (!Extents || Extents->Shapes.IsEmpty()) return false;
	FFixedTransform Transform(Location);
	Transform.Rotation = FFixedQuaternion::FromAxisAndAngle(
		FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::One), SeinStampUtils::DegToRad(Yaw));
	TArray<FFootprint> Footprints;
	for (const auto& Shape : Extents->Shapes)
	{
		if (World.FootprintPlacementResolver.IsBound()
			&& !World.FootprintPlacementResolver.Execute(Location, Yaw, Shape, 0xff)) return false;
		Footprints.Emplace(Shape, Transform);
	}

	// Read live state: the PreTick navigation overlay can miss a building spawned
	// earlier in this very command batch. No renderer collision or mutable cache.
	bool Blocked = false;
	World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		if (Blocked || !Entity.IsAlive()) return;
		const auto* Other = World.GetComponent<FSeinExtentsPayload>(Handle);
		const auto CheckShape = [&](const FSeinExtentsShape& Shape)
		{
			const FFootprint Candidate(Shape, Entity.Transform);
			for (const auto& Footprint : Footprints)
				if (Overlaps(Footprint, Candidate)) { Blocked = true; break; }
		};
		if (Other)
		{
			if (!Other->bBlocksNav || Other->BlockedNavLayerMask == 0) return;
			for (const auto& Shape : Other->Shapes) { if (Blocked) break; CheckShape(Shape); }
		}
		else if (const auto* Nav = World.GetComponent<FSeinNavigationPayload>(Handle))
		{
			if (Nav->FallbackFootprintRadius > FFixedPoint::Zero)
			{
				FSeinExtentsShape Shape;
				Shape.Radius = Nav->FallbackFootprintRadius;
				CheckShape(Shape);
			}
		}
	});
	return !Blocked;
}

bool SeinPlacementValidation::IsValidForAbility(const USeinWorldSubsystem& World,
	const USeinAbility& Ability, const TArray<FSeinTargeterPoint>& Points)
{
	if (!Ability.bRequiresFreeFootprint) return true;
	if (Points.IsEmpty()) return false;
	const auto ActorClass = ResolveActorClass(Ability);
	for (const auto& Point : Points)
		if (!IsValidClass(World, ActorClass, Point.Location, Point.YawDegrees)) return false;
	return true;
}
