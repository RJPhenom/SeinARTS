/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCursorTrace.cpp
 * @author       RJ Macklem
 * @created      5 Sep 2026
 * @latest       5 Sep 2026
 * @brief        Analytic cursor intersections independent of render mesh collision.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#include "Player/SeinCursorTrace.h"
#include "Actor/SeinActor.h"
#include "Components/SeinExtentsPayload.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	bool IntersectBox(const FVector& O, const FVector& D, const FVector& Half, double& Distance)
	{
		double Near = 0.0;
		double Far = Distance;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(D[Axis]) < UE_DOUBLE_SMALL_NUMBER)
			{
				if (O[Axis] < -Half[Axis] || O[Axis] > Half[Axis]) return false;
				continue;
			}
			double A = (-Half[Axis] - O[Axis]) / D[Axis];
			double B = (Half[Axis] - O[Axis]) / D[Axis];
			if (A > B) Swap(A, B);
			Near = FMath::Max(Near, A);
			Far = FMath::Min(Far, B);
			if (Near > Far) return false;
		}
		Distance = Near;
		return true;
	}

	bool IntersectCapsule(const FVector& O, const FVector& D, double Radius,
		double CylinderHalfHeight, double& Distance)
	{
		const FVector Closest(0, 0, FMath::Clamp(O.Z, -CylinderHalfHeight, CylinderHalfHeight));
		if ((O - Closest).SizeSquared() <= Radius * Radius)
		{
			Distance = 0.0;
			return true;
		}
		bool bHit = false;
		const auto Accept = [&](double T)
		{
			if (T >= 0.0 && T <= Distance) { Distance = T; bHit = true; }
		};
		// Finite cylinder, followed by the two spherical caps. A capsule is the
		// union of these volumes, so the earliest nonnegative entry is the hit.
		const double A = D.X * D.X + D.Y * D.Y;
		const double B = O.X * D.X + O.Y * D.Y;
		const double C = O.X * O.X + O.Y * O.Y - Radius * Radius;
		const double Disc = B * B - A * C;
		if (A > UE_DOUBLE_SMALL_NUMBER && Disc >= 0.0)
		{
			const double Root = FMath::Sqrt(Disc);
			for (double T : {(-B - Root) / A, (-B + Root) / A})
			{
				if (FMath::Abs(O.Z + D.Z * T) <= CylinderHalfHeight) Accept(T);
			}
		}
		for (double Z : {-CylinderHalfHeight, CylinderHalfHeight})
		{
			const FVector Offset = O - FVector(0, 0, Z);
			const double Along = FVector::DotProduct(Offset, D);
			const double SphereDisc = Along * Along - (Offset.SizeSquared() - Radius * Radius);
			if (SphereDisc >= 0.0) Accept(-Along - FMath::Sqrt(SphereDisc));
		}
		return bHit;
	}
}

bool SeinCursorTrace::IntersectExtents(const FSeinExtentsPayload& Extents,
	const FTransform& ActorTransform, const FVector& Origin, const FVector& Direction,
	double MaxDistance, double& Distance)
{
	Distance = MaxDistance;
	if (MaxDistance < 0.0 || !FMath::IsFinite(MaxDistance)
		|| Origin.ContainsNaN() || Direction.ContainsNaN() || !Direction.IsNormalized()) return false;
	bool bHit = false;
	const FQuat ActorRotation = ActorTransform.GetRotation();
	for (const FSeinExtentsShape& Shape : Extents.Shapes)
	{
		// Same scale-free pose and capsule height convention as the extents
		// visualizer and accurate marquee query. Use the interpolated actor pose.
		const double Height = FMath::Max(0.0, static_cast<double>(Shape.Height.ToFloat()));
		const FQuat Rotation = ActorRotation * FQuat(FVector::UpVector,
			FMath::DegreesToRadians(static_cast<double>(Shape.YawOffsetDegrees.ToFloat())));
		const FVector Base = ActorTransform.GetLocation() + ActorRotation.RotateVector(Shape.LocalOffset.ToVector());
		const FVector Center = Base + Rotation.GetUpVector() * (Height * 0.5);
		const FVector O = Rotation.UnrotateVector(Origin - Center);
		const FVector D = Rotation.UnrotateVector(Direction);
		if (Shape.Shape == ESeinExtentsShape::Box)
		{
			const FVector Half(FMath::Max(0.0, static_cast<double>(Shape.HalfExtentX.ToFloat())),
				FMath::Max(0.0, static_cast<double>(Shape.HalfExtentY.ToFloat())), Height * 0.5);
			bHit |= IntersectBox(O, D, Half, Distance);
		}
		else if (Shape.Shape == ESeinExtentsShape::Capsule)
		{
			const double Radius = FMath::Max(0.0, static_cast<double>(Shape.Radius.ToFloat()));
			bHit |= IntersectCapsule(O, D, Radius, FMath::Max(0.0, Height * 0.5 - Radius), Distance);
		}
	}
	return bHit;
}

bool SeinCursorTrace::TraceEntities(UWorld& World, const FVector& Origin,
	const FVector& Direction, double MaxDistance, FHitResult& OutHit)
{
	OutHit = FHitResult();
	const USeinWorldSubsystem* Sim = World.GetSubsystem<USeinWorldSubsystem>();
	const USeinActorBridgeSubsystem* Bridge = World.GetSubsystem<USeinActorBridgeSubsystem>();
	if (!Sim || !Bridge) return false;

	ASeinActor* BestActor = nullptr;
	FSeinEntityHandle BestHandle;
	double BestDistance = MaxDistance;
	Bridge->ForEachRegisteredActor([&](FSeinEntityHandle Handle, ASeinActor& Actor)
	{
		if (Actor.IsHidden() || !Sim->IsEntityAlive(Handle) || Actor.GetEntityHandle() != Handle) return;
		const FSeinExtentsPayload* Extents = Sim->GetComponent<FSeinExtentsPayload>(Handle);
		if (!Extents) return;
		double Distance;
		if (IntersectExtents(*Extents, Actor.GetActorTransform(), Origin, Direction, BestDistance, Distance)
			&& (!BestActor || Distance < BestDistance || Handle < BestHandle))
		{
			BestActor = &Actor;
			BestHandle = Handle;
			BestDistance = Distance;
		}
	});
	if (!BestActor) return false;
	const FVector Point = Origin + Direction * BestDistance;
	OutHit = FHitResult(BestActor, nullptr, Point, -Direction);
	OutHit.bBlockingHit = true;
	OutHit.Distance = BestDistance;
	OutHit.Time = MaxDistance > 0.0 ? BestDistance / MaxDistance : 0.0;
	OutHit.TraceStart = Origin;
	OutHit.TraceEnd = Origin + Direction * MaxDistance;
	return true;
}

bool SeinCursorTrace::TraceEnvironment(UWorld& World, const FVector& Origin,
	const FVector& Direction, double MaxDistance, ECollisionChannel Channel, FHitResult& OutHit)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SeinCursorEnvironment), false);
	// Include unregistered/dead actors too: their mesh collision must never
	// turn a missed extents query back into an entity hit or displace ground.
	for (TActorIterator<ASeinActor> It(&World); It; ++It) Params.AddIgnoredActor(*It);
	return World.LineTraceSingleByChannel(OutHit, Origin, Origin + Direction * MaxDistance, Channel, Params);
}
