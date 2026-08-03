/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		GeometryQueries.h
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic geometric query functions operating entirely
 * 				on fixed-point primitives. Provides the SeinGeometry
 * 				namespace with point containment tests (box, sphere,
 * 				capsule), shape-vs-shape intersection tests, ray casting
 * 				against boxes/spheres/planes, and distance/closest-point
 * 				queries for broad-phase and narrow-phase collision detection.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/Box.h"
#include "Types/Sphere.h"
#include "Types/Capsule.h"
#include "Types/Ray.h"
#include "Types/Plane.h"
#include "Math/MathLib.h"

/**
 * Geometric query utilities for collision detection and spatial queries.
 * All functions are deterministic and use fixed-point arithmetic.
 */
namespace SeinGeometry
{
	// Point Containment Tests
	// ====================================================================================================

	/**
	 * Tests if a point is inside a box.
	 */
	FORCEINLINE bool PointInBox(const FFixedVector& Point, const FFixedBox& Box)
	{
		return Box.Contains(Point);
	}

	/**
	 * Tests if a point is inside a sphere.
	 */
	FORCEINLINE bool PointInSphere(const FFixedVector& Point, const FFixedSphere& Sphere)
	{
		return Sphere.Contains(Point);
	}

	/**
	 * Tests if a point is inside a capsule.
	 */
	FORCEINLINE bool PointInCapsule(const FFixedVector& Point, const FFixedCapsule& Capsule)
	{
		return Capsule.Contains(Point);
	}

	// Intersection Tests
	// ====================================================================================================

	/**
	 * Tests if two boxes intersect.
	 */
	FORCEINLINE bool BoxIntersectsBox(const FFixedBox& A, const FFixedBox& B)
	{
		return A.Intersects(B);
	}

	/**
	 * Tests if two spheres intersect.
	 */
	FORCEINLINE bool SphereIntersectsSphere(const FFixedSphere& A, const FFixedSphere& B)
	{
		return A.Intersects(B);
	}

	/**
	 * Tests if a box intersects a sphere.
	 */
	FORCEINLINE bool BoxIntersectsSphere(const FFixedBox& Box, const FFixedSphere& Sphere)
	{
		// Find the closest point on the box to the sphere center
		FFixedVector ClosestPoint = FFixedVector(
			SeinMath::Clamp(Sphere.Center.X, Box.Min.X, Box.Max.X),
			SeinMath::Clamp(Sphere.Center.Y, Box.Min.Y, Box.Max.Y),
			SeinMath::Clamp(Sphere.Center.Z, Box.Min.Z, Box.Max.Z)
		);

		// Check if the closest point is within the sphere
		FFixedPoint DistanceSquared = (ClosestPoint - Sphere.Center).SizeSquared();
		return DistanceSquared <= (Sphere.Radius * Sphere.Radius);
	}

	/**
	 * Tests if two capsules intersect.
	 */
	FORCEINLINE bool CapsuleIntersectsCapsule(const FFixedCapsule& A, const FFixedCapsule& B)
	{
		// Get closest points on the two line segments
		FFixedVector DirA = A.End - A.Start;
		FFixedVector DirB = B.End - B.Start;
		FFixedVector StartDiff = A.Start - B.Start;

		FFixedPoint A_Dot_A = FFixedVector::DotProduct(DirA, DirA);
		FFixedPoint B_Dot_B = FFixedVector::DotProduct(DirB, DirB);
		FFixedPoint A_Dot_B = FFixedVector::DotProduct(DirA, DirB);
		FFixedPoint A_Dot_D = FFixedVector::DotProduct(DirA, StartDiff);
		FFixedPoint B_Dot_D = FFixedVector::DotProduct(DirB, StartDiff);

		FFixedPoint Denom = A_Dot_A * B_Dot_B - A_Dot_B * A_Dot_B;
		
		FFixedPoint T_A, T_B;
		
		if (Denom < FFixedPoint::Epsilon)
		{
			// Lines are parallel
			T_A = FFixedPoint::Zero;
			T_B = -B_Dot_D / B_Dot_B;
		}
		else
		{
			T_A = (A_Dot_B * B_Dot_D - B_Dot_B * A_Dot_D) / Denom;
			T_B = (A_Dot_A * B_Dot_D - A_Dot_B * A_Dot_D) / Denom;
		}

		// Clamp to [0, 1]
		T_A = SeinMath::Clamp(T_A, FFixedPoint::Zero, FFixedPoint::One);
		T_B = SeinMath::Clamp(T_B, FFixedPoint::Zero, FFixedPoint::One);

		FFixedVector ClosestA = A.Start + DirA * T_A;
		FFixedVector ClosestB = B.Start + DirB * T_B;

		FFixedPoint DistanceSquared = (ClosestA - ClosestB).SizeSquared();
		FFixedPoint CombinedRadius = A.Radius + B.Radius;
		return DistanceSquared <= (CombinedRadius * CombinedRadius);
	}

	/**
	 * Tests if a capsule intersects a sphere.
	 */
	FORCEINLINE bool CapsuleIntersectsSphere(const FFixedCapsule& Capsule, const FFixedSphere& Sphere)
	{
		FFixedVector ClosestPoint = Capsule.GetClosestPointOnAxis(Sphere.Center);
		FFixedPoint DistanceSquared = (ClosestPoint - Sphere.Center).SizeSquared();
		FFixedPoint CombinedRadius = Capsule.Radius + Sphere.Radius;
		return DistanceSquared <= (CombinedRadius * CombinedRadius);
	}

	/**
	 * Tests if a capsule intersects a box.
	 */
	FORCEINLINE bool CapsuleIntersectsBox(const FFixedCapsule& Capsule, const FFixedBox& Box)
	{
		// Get closest point on capsule axis to box
		FFixedVector ClosestOnAxis = Capsule.GetClosestPointOnAxis(Box.GetCenter());
		
		// Get closest point on box to that point
		FFixedVector ClosestOnBox = FFixedVector(
			SeinMath::Clamp(ClosestOnAxis.X, Box.Min.X, Box.Max.X),
			SeinMath::Clamp(ClosestOnAxis.Y, Box.Min.Y, Box.Max.Y),
			SeinMath::Clamp(ClosestOnAxis.Z, Box.Min.Z, Box.Max.Z)
		);

		// Check if closest point on box is within capsule radius of axis
		FFixedVector ClosestOnCapsuleAxis = Capsule.GetClosestPointOnAxis(ClosestOnBox);
		FFixedPoint DistanceSquared = (ClosestOnBox - ClosestOnCapsuleAxis).SizeSquared();
		return DistanceSquared <= (Capsule.Radius * Capsule.Radius);
	}

	// Ray Casting
	// ====================================================================================================

	/**
	 * Ray-box intersection test.
	 * @param Ray - Ray to test.
	 * @param Box - Box to test against.
	 * @param OutHitDistance - [out] Distance along ray to hit point (if hit).
	 * @return True if ray intersects box.
	 */
	FORCEINLINE bool RayIntersectsBox(const FFixedRay& Ray, const FFixedBox& Box, FFixedPoint& OutHitDistance)
	{
		FFixedPoint TMin = FFixedPoint::Zero;
		FFixedPoint TMax = FFixedPoint::MaxValue;

		// Check each axis
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			FFixedPoint Origin = (Axis == 0) ? Ray.Origin.X : (Axis == 1) ? Ray.Origin.Y : Ray.Origin.Z;
			FFixedPoint Dir = (Axis == 0) ? Ray.Direction.X : (Axis == 1) ? Ray.Direction.Y : Ray.Direction.Z;
			FFixedPoint Min = (Axis == 0) ? Box.Min.X : (Axis == 1) ? Box.Min.Y : Box.Min.Z;
			FFixedPoint Max = (Axis == 0) ? Box.Max.X : (Axis == 1) ? Box.Max.Y : Box.Max.Z;

			if (SeinMath::Abs(Dir) < FFixedPoint::Epsilon)
			{
				// Ray is parallel to slab
				if (Origin < Min || Origin > Max)
					return false;
			}
			else
			{
				FFixedPoint T1 = (Min - Origin) / Dir;
				FFixedPoint T2 = (Max - Origin) / Dir;

				if (T1 > T2)
				{
					// Swap
					FFixedPoint Temp = T1;
					T1 = T2;
					T2 = Temp;
				}

				TMin = SeinMath::Max(TMin, T1);
				TMax = SeinMath::Min(TMax, T2);

				if (TMin > TMax)
					return false;
			}
		}

		OutHitDistance = TMin;
		return TMin >= FFixedPoint::Zero;
	}

	/**
	 * Ray-sphere intersection test.
	 * @param Ray - Ray to test.
	 * @param Sphere - Sphere to test against.
	 * @param OutHitDistance - [out] Distance along ray to hit point (if hit).
	 * @return True if ray intersects sphere.
	 */
	FORCEINLINE bool RayIntersectsSphere(const FFixedRay& Ray, const FFixedSphere& Sphere, FFixedPoint& OutHitDistance)
	{
		FFixedVector ToCenter = Sphere.Center - Ray.Origin;
		FFixedPoint ProjectionLength = FFixedVector::DotProduct(ToCenter, Ray.Direction);
		
		FFixedVector ClosestPoint = Ray.Origin + Ray.Direction * ProjectionLength;
		FFixedPoint DistanceToCenterSquared = (ClosestPoint - Sphere.Center).SizeSquared();
		FFixedPoint RadiusSquared = Sphere.Radius * Sphere.Radius;

		if (DistanceToCenterSquared > RadiusSquared)
			return false;

		FFixedPoint HalfChordDistance = SeinMath::Sqrt(RadiusSquared - DistanceToCenterSquared);
		FFixedPoint T = ProjectionLength - HalfChordDistance;

		if (T < FFixedPoint::Zero)
			T = ProjectionLength + HalfChordDistance;

		if (T < FFixedPoint::Zero)
			return false;

		OutHitDistance = T;
		return true;
	}

	/**
	 * Ray-plane intersection test.
	 * @param Ray - Ray to test.
	 * @param Plane - Plane to test against.
	 * @param OutHitDistance - [out] Distance along ray to hit point (if hit).
	 * @return True if ray intersects plane.
	 */
	FORCEINLINE bool RayIntersectsPlane(const FFixedRay& Ray, const FFixedPlane& Plane, FFixedPoint& OutHitDistance)
	{
		FFixedPoint Denom = FFixedVector::DotProduct(Plane.Normal, Ray.Direction);
		
		if (SeinMath::Abs(Denom) < FFixedPoint::Epsilon)
			return false; // Ray is parallel to plane

		FFixedPoint T = (Plane.D - FFixedVector::DotProduct(Plane.Normal, Ray.Origin)) / Denom;

		if (T < FFixedPoint::Zero)
			return false; // Intersection is behind ray

		OutHitDistance = T;
		return true;
	}

	// Distance Queries
	// ====================================================================================================

	/**
	 * Returns the distance from a point to a box.
	 */
	FORCEINLINE FFixedPoint DistancePointToBox(const FFixedVector& Point, const FFixedBox& Box)
	{
		// Clamp point to box bounds
		FFixedVector ClosestPoint = FFixedVector(
			SeinMath::Clamp(Point.X, Box.Min.X, Box.Max.X),
			SeinMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y),
			SeinMath::Clamp(Point.Z, Box.Min.Z, Box.Max.Z)
		);

		return (Point - ClosestPoint).Size();
	}

	/**
	 * Returns the distance from a point to a sphere.
	 */
	FORCEINLINE FFixedPoint DistancePointToSphere(const FFixedVector& Point, const FFixedSphere& Sphere)
	{
		return Sphere.GetDistanceToPoint(Point);
	}

	/**
	 * Returns the distance from a point to a capsule.
	 */
	FORCEINLINE FFixedPoint DistancePointToCapsule(const FFixedVector& Point, const FFixedCapsule& Capsule)
	{
		return Capsule.GetDistanceToPoint(Point);
	}

	/**
	 * Returns the closest point on a box to a given point.
	 */
	FORCEINLINE FFixedVector ClosestPointOnBox(const FFixedVector& Point, const FFixedBox& Box)
	{
		return FFixedVector(
			SeinMath::Clamp(Point.X, Box.Min.X, Box.Max.X),
			SeinMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y),
			SeinMath::Clamp(Point.Z, Box.Min.Z, Box.Max.Z)
		);
	}

	/**
	 * Returns the closest point on a sphere to a given point.
	 */
	FORCEINLINE FFixedVector ClosestPointOnSphere(const FFixedVector& Point, const FFixedSphere& Sphere)
	{
		return Sphere.GetClosestPoint(Point);
	}

	/**
	 * Returns the closest point on a capsule to a given point.
	 */
	FORCEINLINE FFixedVector ClosestPointOnCapsule(const FFixedVector& Point, const FFixedCapsule& Capsule)
	{
		return Capsule.GetClosestPoint(Point);
	}
}
