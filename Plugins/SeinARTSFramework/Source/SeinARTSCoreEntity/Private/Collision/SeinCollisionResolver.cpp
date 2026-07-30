/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolver.cpp
 *
 * Shared collision-resolution helpers, lifted VERBATIM from the Gauss-Seidel
 * default resolver so a second strategy (parallel Jacobi, …) reuses the exact
 * same narrowphase shape build, channel/collider/mass helpers, hard-barrier
 * gate, and overlap-event diff with NO logic change. The Default's separation
 * math (ResolvePass) and the Jacobi's (JacobiPass) stay in their own files; only
 * this shared floor moved up to the base.
 *
 * CanOccupy is the one extraction (not a pure move): it was the `CanOccupy`
 * lambda + its `BarrierRing` table inside the Default's ResolvePass — same logic,
 * now a protected base method so both resolvers gate pushes identically.
 */

#include "Collision/SeinCollisionResolver.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinExtentsHelpers.h"
#include "Settings/PluginSettings.h"
#include "Events/SeinVisualEvent.h"
#include "Math/MathLib.h"

USeinCollisionResolver::FCollisionShape2D USeinCollisionResolver::BuildShape2D(const FFixedTransform& Xf, const FSeinExtentsShape& Shape)
{
	FCollisionShape2D Out;
	Out.bIsBox = (Shape.Shape == ESeinExtentsShape::Box);

	const FFixedVector Center = Xf.GetLocation() + Xf.TransformVector(Shape.LocalOffset);
	Out.Center = Center;
	Out.ZMin = Center.Z;
	Out.ZMax = Center.Z + Shape.Height;

	if (!Out.bIsBox)
	{
		Out.Radius = Shape.Radius;
		return Out;
	}

	Out.HalfX = Shape.HalfExtentX;
	Out.HalfY = Shape.HalfExtentY;

	// Planar facing from the entity rotation (drop Z, renormalize) so slope
	// pitch/roll don't tilt the footprint axes.
	FFixedVector Forward = Xf.GetQuaternionRotation().GetForwardVector();
	Forward.Z = FFixedPoint::Zero;
	Forward = FFixedVector::GetSafeNormal(Forward);
	if (Forward.IsZero())
	{
		Forward = FFixedVector::ForwardVector;
	}

	// Apply the shape's yaw offset (degrees) about Z, if any.
	if (Shape.YawOffsetDegrees != FFixedPoint::Zero)
	{
		const FFixedPoint Rad = Shape.YawOffsetDegrees * FFixedPoint::Pi / FFixedPoint::FromInt(180);
		const FFixedPoint CosA = SeinMath::Cos(Rad);
		const FFixedPoint SinA = SeinMath::Sin(Rad);
		const FFixedPoint NX = Forward.X * CosA - Forward.Y * SinA;
		const FFixedPoint NY = Forward.X * SinA + Forward.Y * CosA;
		Forward = FFixedVector(NX, NY, FFixedPoint::Zero);
	}

	Out.AxisX = Forward;
	// Planar perpendicular. The box footprint is symmetric (±HalfX, ±HalfY),
	// so the sign of the right axis is irrelevant to the SAT result.
	Out.AxisY = FFixedVector(Forward.Y, -Forward.X, FFixedPoint::Zero);
	return Out;
}

SeinCollision::FSeinContact2D USeinCollisionResolver::NarrowphasePair(const FCollisionShape2D& A, const FCollisionShape2D& B)
{
	SeinCollision::FSeinContact2D Contact;
	if (!SeinCollision::RangesOverlap(A.ZMin, A.ZMax, B.ZMin, B.ZMax))
	{
		return Contact; // vertical spans don't overlap → not colliding
	}

	if (!A.bIsBox && !B.bIsBox)
	{
		return SeinCollision::DiscVsDisc(A.Center, A.Radius, B.Center, B.Radius);
	}
	if (!A.bIsBox && B.bIsBox)
	{
		return SeinCollision::DiscVsOBB(A.Center, A.Radius, B.Center, B.AxisX, B.AxisY, B.HalfX, B.HalfY);
	}
	if (A.bIsBox && !B.bIsBox)
	{
		Contact = SeinCollision::DiscVsOBB(B.Center, B.Radius, A.Center, A.AxisX, A.AxisY, A.HalfX, A.HalfY);
		Contact.Normal = -Contact.Normal; // disc(B)→box(A) is B→A; flip to A→B
		return Contact;
	}
	return SeinCollision::OBBVsOBB(
		A.Center, A.AxisX, A.AxisY, A.HalfX, A.HalfY,
		B.Center, B.AxisX, B.AxisY, B.HalfX, B.HalfY);
}

void USeinCollisionResolver::BuildShapes2D(const FSeinExtentsComponent& Ext, const FFixedTransform& Xf, TArray<FCollisionShape2D>& Out)
{
	Out.Reset(Ext.Shapes.Num());
	for (const FSeinExtentsShape& Shape : Ext.Shapes)
	{
		Out.Add(BuildShape2D(Xf, Shape));
	}
}

bool USeinCollisionResolver::ComputeDeepestContact(
	const TArray<FCollisionShape2D>& SelfShapes,
	const FSeinExtentsComponent& OtherExt, const FFixedTransform& OtherXf,
	FFixedVector& OutNormal, FFixedPoint& OutDepth)
{
	bool bAny = false;
	FFixedPoint BestDepth = FFixedPoint::Zero;
	FFixedVector BestNormal = FFixedVector::ZeroVector;

	for (const FCollisionShape2D& A : SelfShapes)
	{
		for (const FSeinExtentsShape& ShapeB : OtherExt.Shapes)
		{
			const FCollisionShape2D B = BuildShape2D(OtherXf, ShapeB);
			const SeinCollision::FSeinContact2D Contact = NarrowphasePair(A, B);
			if (Contact.bHit && Contact.Depth > BestDepth)
			{
				BestDepth = Contact.Depth;
				BestNormal = Contact.Normal;
				bAny = true;
			}
		}
	}

	if (bAny)
	{
		OutNormal = BestNormal;
		OutDepth = BestDepth;
	}
	return bAny;
}

void USeinCollisionResolver::BuildChannelDefaults(TMap<FName, ESeinCollisionResponse>& Out)
{
	Out.Reset();
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return;
	for (const FSeinCollisionChannelDefinition& Channel : Settings->GetAllCollisionChannels())
	{
		if (!Channel.Name.IsNone())
		{
			Out.Add(Channel.Name, Channel.DefaultResponse);
		}
	}
}

bool USeinCollisionResolver::IsCollider(const FSeinExtentsComponent* Ext)
{
	return Ext && Ext->bCollisionEnabled && Ext->Shapes.Num() > 0 && !Ext->ObjectType.Channel.IsNone();
}

FFixedPoint USeinCollisionResolver::ResolveColliderMass(const FSeinExtentsComponent& Ext)
{
	return (Ext.Mass > FFixedPoint::Epsilon) ? Ext.Mass : FFixedPoint::Epsilon;
}

bool USeinCollisionResolver::CanOccupy(USeinWorldSubsystem& World, const FFixedVector& P, FFixedPoint Radius)
{
	// Hard-barrier gate: the push must never move a unit's FOOTPRINT onto a
	// non-walkable cell — a baked nav wall, a runtime DYNAMIC nav blocker
	// (bBlocksNav — e.g. a non-baked cover wall / deployable / blocking vehicle),
	// or off the grid edge, is a never-crossable barrier (the body holds clear of
	// the face instead of being shoved across it). Footprint-aware to MATCH the
	// movement step's ResolveNavCollision, which keeps the whole body off walls; a
	// center-only gate would let the push shove a body half-into a wall while its
	// center cell stayed passable (the "units in the wall" symptom). Queried through
	// the world subsystem's pluggable DYNAMIC passability delegate (static bake AND
	// runtime blockers) so the collision floor stays nav-impl-agnostic — a one-way
	// "walkable?" query, no hard nav dependency. Using the dynamic-aware resolver
	// (NOT the static-only PassableResolver) is what extends the barrier to the
	// non-baked cover walls: static IsPassable can't see them, so a static-only gate
	// shoved units straight into them and they got nav-stuck. Cover slots
	// (authoritative destinations) are exempt: a unit may be delivered onto a
	// bake-blocked slot. Unbound (nav-less / tests) → ungated, identical to prior.
	if (!World.DynamicPassableResolver.IsBound()) return true;
	// Center walkability FIRST. The cover-slot exemption only matters when the cell is bake-BLOCKED,
	// so consult the (potentially expensive — a cover-slot spatial query) AuthoritativeDestination-
	// Resolver ONLY on the blocked path, never on the common push onto walkable ground. (It used to
	// run on EVERY push candidate, thrashing the cover query near walls where pushes are most frequent.)
	if (!World.DynamicPassableResolver.Execute(P))
	{
		// Center blocked (static bake OR a dynamic nav blocker) — allow only if it's an
		// authoritative cover slot that overrules the bake. A cover slot sits ~one footprint
		// outside its wall, so its own cell is normally clear of the wall's stamp; the exemption
		// stays for the low-res-bake-false-negative case, unchanged.
		return World.AuthoritativeDestinationResolver.IsBound() && World.AuthoritativeDestinationResolver.Execute(P);
	}
	// Center walkable — the body footprint must clear walls too.
	if (Radius > FFixedPoint::Zero)
	{
		// 8 unit-ring directions (45° spacing), sampled at the collider radius —
		// the body footprint, not just the center.
		const FFixedPoint RingDiag = FFixedPoint::FromInt(7071) / FFixedPoint::FromInt(10000); // ≈ cos 45°
		const FFixedVector BarrierRing[8] = {
			FFixedVector( FFixedPoint::One,   FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedVector( RingDiag,           RingDiag,          FFixedPoint::Zero),
			FFixedVector( FFixedPoint::Zero,  FFixedPoint::One,  FFixedPoint::Zero),
			FFixedVector(-RingDiag,           RingDiag,          FFixedPoint::Zero),
			FFixedVector(-FFixedPoint::One,   FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedVector(-RingDiag,          -RingDiag,          FFixedPoint::Zero),
			FFixedVector( FFixedPoint::Zero, -FFixedPoint::One,  FFixedPoint::Zero),
			FFixedVector( RingDiag,          -RingDiag,          FFixedPoint::Zero),
		};
		for (int32 i = 0; i < 8; ++i)
		{
			const FFixedVector S(P.X + BarrierRing[i].X * Radius, P.Y + BarrierRing[i].Y * Radius, P.Z);
			if (!World.DynamicPassableResolver.Execute(S)) return false;
		}
	}
	return true;
}

void USeinCollisionResolver::DetectOverlapsAndEmit(USeinWorldSubsystem& World, const TMap<FName, ESeinCollisionResponse>& ChannelDefaults)
{
	const FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();
	const FFixedPoint CellSize = Hash.GetCellSize();
	TArray<FSeinEntityHandle> Neighbors;

	TSet<FOverlapPairKey> Current;

	// Hoist the Extents storage once (see ResolvePass) + reusable self-shape scratch.
	const ISeinComponentStorage* ExtentsStorage =
		World.GetComponentStorageRaw(
			FSeinExtentsComponent::StaticStruct());
	TArray<FCollisionShape2D> SelfShapes;

	World.GetEntityPool().ForEachEntity([&](
		FSeinEntityHandle SelfHandle,
		const FSeinEntity& SelfEntity)
	{
		const FSeinExtentsComponent* SelfExt = ExtentsStorage
			? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(SelfHandle))
			: nullptr;
		if (!IsCollider(SelfExt)) return;
		if (SelfExt->Mobility != ESeinCollisionMobility::Movable) return;

		const FFixedPoint SelfRadius = SeinExtentsHelpers::GetColliderBoundingRadius(*SelfExt);
		if (SelfRadius <= FFixedPoint::Zero) return;

		const FFixedVector SelfPos = SelfEntity.Transform.GetLocation();
		const FFixedPoint QueryRadius = SelfRadius + CellSize;

		Neighbors.Reset();
		Hash.QueryRadius(SelfPos, QueryRadius, Neighbors, SelfHandle);

		// Build self's collision shapes ONCE, reused across all neighbours.
		BuildShapes2D(*SelfExt, SelfEntity.Transform, SelfShapes);

		for (const FSeinEntityHandle& OtherHandle : Neighbors)
		{
			const FSeinExtentsComponent* OtherExt = ExtentsStorage
				? static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(OtherHandle))
				: nullptr;
			if (!IsCollider(OtherExt)) continue;

			const bool bOtherImmovable = (OtherExt->Mobility != ESeinCollisionMobility::Movable);
			if (!bOtherImmovable && OtherHandle.Index <= SelfHandle.Index) continue;

			if (ResolvePairFor(*SelfExt, *OtherExt, ChannelDefaults) != ESeinCollisionResponse::Overlap) continue;

			const FSeinEntity* OtherEntity =
				World.GetEntityPool().Get(OtherHandle);
			if (!OtherEntity) continue;

			FFixedVector Normal;
			FFixedPoint  Depth;
			if (!ComputeDeepestContact(SelfShapes, *OtherExt, OtherEntity->Transform, Normal, Depth)) continue;

			Current.Add(MakePairKey(SelfHandle, OtherHandle));
		}
	});

	// Diff against last tick. Collect pair identities, sort for deterministic
	// event order, then emit all begins before all ends as before.
	TArray<FOverlapPairKey> BeginKeys;
	TArray<FOverlapPairKey> EndKeys;
	for (const FOverlapPairKey& Pair : Current)
	{
		if (!ActiveOverlaps.Contains(Pair)) BeginKeys.Add(Pair);
	}
	for (const FOverlapPairKey& Pair : ActiveOverlaps)
	{
		if (!Current.Contains(Pair)) EndKeys.Add(Pair);
	}
	BeginKeys.Sort();
	EndKeys.Sort();

	for (const FOverlapPairKey& Pair : BeginKeys)
	{
		World.EnqueueVisualEvent(FSeinVisualEvent::MakeCollisionOverlapBeginEvent(Pair.A, Pair.B));
	}
	for (const FOverlapPairKey& Pair : EndKeys)
	{
		World.EnqueueVisualEvent(FSeinVisualEvent::MakeCollisionOverlapEndEvent(Pair.A, Pair.B));
	}

	ActiveOverlaps = MoveTemp(Current);
}
