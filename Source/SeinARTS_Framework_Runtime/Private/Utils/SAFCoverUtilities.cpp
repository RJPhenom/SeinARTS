#include "Utils/SAFCoverUtilities.h"
#include "Utils/SAFLibrary.h"
#include "Utils/SAFMathLibrary.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/SAFCoverCollider.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetMathLibrary.h"
#include "NavigationSystem.h"

#include "Debug/SAFDebugTool.h"

namespace SAFCoverUtilities {

	// Cover Check Helpers
	// =====================================================================================================================================================
	/** Does a sphere collision check at point Point with radius Radius for cover and returns both the nearest cover collider and its parent actor. */
	bool FindNearestCover(UWorld* World, FVector Point, float Radius, AActor*& OutActor, UPrimitiveComponent*& OutComponent, ESAFCoverType& OutCoverType) {
		if (!World) { SAFDEBUG_ERROR("FindNEarestCover aborted: World is nullptr."); return false; }

		OutActor 			= nullptr;
		OutComponent 	= nullptr;
		OutCoverType 	= ESAFCoverType::Neutral;

		// Overlap everything; any Actor could carry a USAFCoverCollider.
		TArray<FOverlapResult> Overlaps;
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
		const FCollisionObjectQueryParams ObjectParams = FCollisionObjectQueryParams::AllObjects;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FindNearestCover), false);

		if (!World->OverlapMultiByObjectType(Overlaps, Point, FQuat::Identity, ObjectParams, Sphere, QueryParams)) return false;

		// Use DistSquared to find the nearest cover collider/actor
		float BestDistSq = TNumericLimits<float>::Max();
		for (const FOverlapResult& Hit : Overlaps) {
			UPrimitiveComponent* Component = Hit.Component.Get();
			USAFCoverCollider* CoverCollider = Component ? Cast<USAFCoverCollider>(Component) : nullptr;
			if (!CoverCollider) continue;

			const float DistSq = FVector::DistSquared(Point, CoverCollider->GetComponentLocation());
			if (DistSq < BestDistSq) {
				BestDistSq 		= DistSq;
				OutComponent 	= CoverCollider;
				OutActor 			= CoverCollider->GetOwner();
				OutCoverType 	= CoverCollider->CoverType;
			}
		}

		// Return true if we found both actor and collider
		return OutComponent != nullptr && OutActor != nullptr;
	}

	/** Checks if the vector is within a USAFCoverCollider collider box. If bProjectToNavmesh (defaults true), 
	 * it projects the point to the nearest navigable point on the NavMesh first. */
	ESAFCoverType GetCoverAtPoint(UWorld* World, FVector Point, bool bProjectToNavmesh, bool bShowDebugMessages) {
		if (!World) return ESAFCoverType::Neutral;

		// Use ProjectedPoint, if bool option is enabled it will be projected first. 
		// Otherwise is just Point.
		FVector ProjectedPoint = Point;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

		if (bProjectToNavmesh && NavSys) {
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(Point, Projected, FVector(50.f, 50.f, 200.f))) ProjectedPoint = Projected.Location;
			else if (bShowDebugMessages) SAFDEBUG_WARNING("GetCoverAtPoint failed to project point onto navigation. Using raw input vector instead.");
		}

		// Overlap at the point then confirm actual containment via box math
		TArray<FOverlapResult> Overlaps;
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(5.f);
		const FCollisionObjectQueryParams ObjectParams = FCollisionObjectQueryParams::AllObjects;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GetCoverAtPoint), false);
		if (!World->OverlapMultiByObjectType(Overlaps, ProjectedPoint, FQuat::Identity, ObjectParams, Sphere, QueryParams)) return ESAFCoverType::Neutral;

		// Compare all cover types found and return the best cover type found 
		// (Negative is higher than neutral in terms of information about cover)
		ESAFCoverType BestCoverType = ESAFCoverType::Neutral;
		for (const FOverlapResult& Result : Overlaps) {
			UPrimitiveComponent* Component = Result.GetComponent();
			USAFCoverCollider* Cover = Component ? Cast<USAFCoverCollider>(Component) : nullptr;
			if (!Cover) continue;
			if (!UKismetMathLibrary::IsPointInBoxWithTransform(ProjectedPoint, Cover->GetComponentTransform(), Cover->GetScaledBoxExtent())) continue;

			ESAFCoverType FoundType = Cover->CoverType;
			if (FoundType == ESAFCoverType::Heavy) return ESAFCoverType::Heavy;
			else if (FoundType == ESAFCoverType::Light) BestCoverType = ESAFCoverType::Light;
			else if (FoundType == ESAFCoverType::Negative && BestCoverType == ESAFCoverType::Neutral) BestCoverType = ESAFCoverType::Negative;
		}

		return BestCoverType;
	}
	
	// Squad Cover Positions Builders (the actual positions to take up)
	// ============================================================================================================================================================
	/** Build Cover Positions Around Cover Box 
	 * Generates an array of world positions aligned to the edges of a rectangular cover object (defined by corners A–D). Squad members are distributed along the 
	 * edges in rows, respecting spacing/offset modifiers and whether the cover should wrap around all sides. 
	 * 
	 * ⚠ Caution:
	 * - Requires a valid navmesh under each generated point; results are only as accurate as the provided navigation system.
	 * - If used outside of a squad context, note that spacing and stagger modifiers assume member bounds are supplied.
	 * - Best suited for walls, sandbags, and other long cover shapes. Avoid using on small/irregular meshes.
	 * 
	 * This is a complex function, if you are encountering undesired or frustrating behaviour please reach out on the SeinARTS Framework support Discord. */
	TArray<FVector> BuildCoverPositionsAroundCoverBox(
		const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Point, 
		const TArray<AActor*>& SquadMembers, const UNavigationSystemV1* NavSys,
		const bool bScattersInCover, const bool bWrapsCover, 
		const float CoverSearchRadius, const float CoverSpacingModifier, const float CoverRowOffsetModifier, const float LateralStaggerModifier
	) {
		TArray<FVector> OutPositions;
		struct FEdge { FVector Start, End; };
		FEdge Edges[4] = { {A,B}, {B,C}, {C,D}, {D,A} };
		const int32 NumEdges = UE_ARRAY_COUNT(Edges);

		int32 BestIndex = 0; 
		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 i = 0; i < NumEdges; ++i) {
			const FVector Closest = FMath::ClosestPointOnSegment(Point, Edges[i].Start, Edges[i].End);
			const float DistSq = FVector::DistSquared(Point, Closest);
			if (DistSq < BestDistSq) { BestDistSq = DistSq; BestIndex = i; }
		}

		const FVector 	EdgeStart 			= Edges[BestIndex].Start;
		const FVector 	EdgeEnd 				= Edges[BestIndex].End;
		const FVector 	CoverDirection 	= (EdgeEnd - EdgeStart).GetSafeNormal();
		const FVector 	CoverCenter 		= (A + B + C + D) * 0.25f;
		const float 		EdgeZeroLength 	= FVector::Distance(EdgeStart, EdgeEnd);

		// Slot 0: pivot on the chosen edge, nudged outward, then nav-projected
		FVector PositionZero = FMath::ClosestPointOnSegment(Point, EdgeStart, EdgeEnd);
		const FVector OutwardN = SAFMathLibrary::EdgeOutwardNormal2D(EdgeStart, EdgeEnd, CoverCenter);
		const FVector QueryExtent(CoverSearchRadius, CoverSearchRadius, 500.f);

		FNavLocation NavProjection;
		FVector SeedZero = PositionZero + OutwardN * FMath::Max(15.f, SAFMathLibrary::ComputeActorStandoff(SquadMembers[0]) * 0.5f);
		bool bProjected = NavSys->ProjectPointToNavigation(SeedZero, NavProjection, QueryExtent);
		if (!bProjected) {
			const FVector BigExtent = QueryExtent * 1.5f;
			bProjected = NavSys->ProjectPointToNavigation(SeedZero, NavProjection, BigExtent);
			if (!bProjected) NavProjection.Location = SeedZero;
		}
		PositionZero = NavProjection.Location;
		OutPositions.Add(PositionZero); // slot 0 placed

		// Precompute available run along this edge and (optionally) one adjacent corner
		const FVector ClosestOnBestEdge = FMath::ClosestPointOnSegment(Point, EdgeStart, EdgeEnd);
		auto WrapIndexFwd  = [NumEdges](int32 i){ return (i + 1) % NumEdges; };
		auto WrapIndexBack = [NumEdges](int32 i){ return (i + NumEdges - 1) % NumEdges; };
		auto EdgeLength    = [](const FEdge& Edge){ return (Edge.End - Edge.Start).Size(); };

		const int32 NextIndex   = WrapIndexFwd(BestIndex);
		const int32 PrevIndex   = WrapIndexBack(BestIndex);
		const float LengthNext  = EdgeLength(Edges[NextIndex]);
		const float LengthPrev  = EdgeLength(Edges[PrevIndex]);

		// Distances from the pivot-on-edge (keeps rows parallel to this edge)
		const FVector PivotOnEdge = FMath::ClosestPointOnSegment(PositionZero, EdgeStart, EdgeEnd);
		const float DistToStart   = (PivotOnEdge - EdgeStart).Size();
		const float DistToEnd     = (EdgeEnd - PivotOnEdge).Size();

		const float MaxBackward   = DistToStart + LengthPrev; // one-corner budget (backward)
		const float MaxForward    = DistToEnd   + LengthNext; // one-corner budget (forward)
		const bool  PreferForwardSideFirst = (MaxForward >= MaxBackward);

		// Walk along edges by distance with single-corner wrap; returns the landing edge index
		auto StepAlongEdges = [&](int32 StartEdgeIndex, const FVector& StartPointOnEdge, float Distance, int32 Dir)->TPair<FVector,int32> {
			int32 EdgeIndex = StartEdgeIndex;
			FVector CurrPoint = StartPointOnEdge;
			float Remaining = FMath::Max(0.f, Distance);
			while (Remaining > KINDA_SMALL_NUMBER) {
				const FVector& Start = Edges[EdgeIndex].Start;
				const FVector& End   = Edges[EdgeIndex].End;
				const FVector SegDir = (End - Start).GetSafeNormal();
				if (Dir > 0) {
					const float ToEnd = (End - CurrPoint).Size();
					if (Remaining <= ToEnd) return { CurrPoint + SegDir * Remaining, EdgeIndex };
					Remaining -= ToEnd; CurrPoint = End; EdgeIndex = WrapIndexFwd(EdgeIndex);
				} else {
					const float ToStart = (CurrPoint - Start).Size();
					if (Remaining <= ToStart) return { CurrPoint - SegDir * Remaining, EdgeIndex };
					Remaining -= ToStart; CurrPoint = Start; EdgeIndex = WrapIndexBack(EdgeIndex);
				}
			}
			return { CurrPoint, EdgeIndex };
		};

		// Per-row/per-side growth from center (so rows fill naturally even near corners)
		TArray<int32> RowCountPos; // +CoverDirection count per row
		TArray<int32> RowCountNeg; // -CoverDirection count per row
		auto EnsureRow = [&](int32 RowIdx){
			if (RowCountPos.Num() <= RowIdx) { RowCountPos.SetNum(RowIdx+1); RowCountNeg.SetNum(RowIdx+1); }
		};
		auto NextLocalIndexForRowSide = [&](int32 RowIdx, int32 DirSign)->int32 {
			EnsureRow(RowIdx);
			return (DirSign > 0 ? RowCountPos[RowIdx] : RowCountNeg[RowIdx]) + 1;
		};
		auto CommitRowSide = [&](int32 RowIdx, int32 DirSign){
			if (DirSign > 0) RowCountPos[RowIdx]++; else RowCountNeg[RowIdx]++;
		};

		// Place remaining members
		for (int32 i = 1; i < SquadMembers.Num(); i++) {
			if (!SAFLibrary::IsActorPtrValidSeinARTSActor(SquadMembers[i])) continue;

			float BaseSpacing = 150.f;
			if (AActor* Member = SquadMembers[i]) {
				if (const UCapsuleComponent* Capsule = Member->FindComponentByClass<UCapsuleComponent>()) 
					BaseSpacing = Capsule->GetScaledCapsuleRadius() * CoverSpacingModifier;
				else if (Member->GetRootComponent()) 
					BaseSpacing = Member->GetRootComponent()->Bounds.SphereRadius * 0.5f * CoverSpacingModifier;
			}

			// Deterministic random scatter, if active
			if (bScattersInCover) {
				const bool 		b2xBase = (EdgeZeroLength >= (BaseSpacing * 2.f));
				const uint32 	Hash 		= GetTypeHash(Point) ^ (i * 2654435761u);
				const float 	Rand01 	= (Hash % 1000) / 1000.f;
				const float 	Coeff 	= (b2xBase) // soften, soften more if edge is small
					? 1.f + Rand01 * .5f : 1.f + Rand01 * .1f;

				BaseSpacing *= Coeff;
			}

			// Symmetric order from center and pick a side with more space first
			const int32 pairIndex = (i + 1) / 2;
			const bool  Odd       = (i % 2 == 1);
			const int32 DirSign   = (Odd ^ (!PreferForwardSideFirst)) ? +1 : -1;

			// Front-row attempt (on the edge), with optional wrap budget
			const float RawTravel = BaseSpacing * pairIndex;

			FVector GeoWrapped;
			int32   LandEdgeIndex 	= BestIndex;
			bool    bCornerClamped 	= false;

			if (bWrapsCover) {
				const float Cap    = (DirSign > 0) ? MaxForward : MaxBackward;
				const float Travel = FMath::Min(RawTravel, FMath::Max(0.f, Cap - 1.f));
				bCornerClamped     = (RawTravel >= (Cap - 1.f));

				const TPair<FVector,int32> StepRes = StepAlongEdges(BestIndex, ClosestOnBestEdge, Travel, DirSign);
				GeoWrapped      = StepRes.Key;
				LandEdgeIndex   = StepRes.Value;
			} else {
				// No wrap: clamp to the visible portion of this edge only
				const float EdgeCap     = (DirSign > 0) ? DistToEnd : DistToStart;
				const float TravelClamp = FMath::Min(RawTravel, FMath::Max(0.f, EdgeCap - 1.f));
				bCornerClamped          = (RawTravel >= (EdgeCap - 1.f));

				GeoWrapped    = ClosestOnBestEdge + CoverDirection * (DirSign * TravelClamp);
				LandEdgeIndex = BestIndex;
			}

			// Push outward slightly to avoid the collider and help nav projection
			const FEdge  LandEdge   = Edges[LandEdgeIndex];
			const FVector LandOutN  = SAFMathLibrary::EdgeOutwardNormal2D(LandEdge.Start, LandEdge.End, CoverCenter);
			GeoWrapped += LandOutN * SAFMathLibrary::ComputeActorStandoff(SquadMembers[i]);

			// Row stacking: place into rows parallel to the edge from PositionZero.
			const float RowBackOffset  = BaseSpacing * CoverRowOffsetModifier;
			const float LateralStagger = BaseSpacing * LateralStaggerModifier;
			const float OverlapRadius  = BaseSpacing;

			FVector Candidate = GeoWrapped;
			if (SAFMathLibrary::CheckPointOverlapsPositions(Candidate, OverlapRadius, OutPositions) || bCornerClamped) {
				const int32 kMaxRows = 8;
				const int32 kMaxLocalTries = 16;
				bool bPlaced = false;
				int32 Row = 1;

				while (!bPlaced && Row <= kMaxRows) {
					// Try preferred side first, then the opposite side
					for (int s = 0; s < 2 && !bPlaced; ++s) {
						const int32 Side = (s == 0) ? DirSign : -DirSign;

						// Walk outward within this row+side until it fits and doesn't overlap
						int32 Tries = 0;
						int32 LocalIndex = NextLocalIndexForRowSide(Row, Side);
						while (Tries++ < kMaxLocalTries) {
							float Along = Side * (BaseSpacing * LocalIndex);
							Along += (Row % 2 == 1 ? -Side : Side) * LateralStagger;

							const bool bFits = (Side > 0) ? (Along <= DistToEnd - 1.f) : (Along >= -(DistToStart - 1.f));
							if (!bFits) break; // this side can't fit any farther slots near this corner

							const FVector Test = PositionZero + CoverDirection * Along + OutwardN * (RowBackOffset * Row);
							if (!SAFMathLibrary::CheckPointOverlapsPositions(Test, OverlapRadius, OutPositions)) {
								Candidate = Test;
								CommitRowSide(Row, Side); // reserve this slot in the row/side
								bPlaced = true;
								break;
							}

							++LocalIndex; // try the next farther slot along this row/side
						}
					}

					if (!bPlaced) ++Row; // try next row back
				}

				if (!bPlaced) {
					// Last resort: use the on-edge candidate (might stack, but never “vanish”)
					Candidate = GeoWrapped;
				}
			}

			// Final nav projection with a retry cushion
			FNavLocation NewNavProjection;
			bool bPass = NavSys->ProjectPointToNavigation(Candidate, NewNavProjection, QueryExtent);
			if (!bPass) {
				const FVector BigExtent = QueryExtent * 2.f;
				bPass = NavSys->ProjectPointToNavigation(Candidate, NewNavProjection, BigExtent);
				if (!bPass) {
					SAFDEBUG_WARNING("GetCoverPositionsAtPoint: nav projection failed after retry. Using raw candidate.");
					NewNavProjection.Location = Candidate;
				}
			}
			OutPositions.Add(NewNavProjection.Location);
		}

		return OutPositions;
	}

	/** Build Cover Positions Around Cover Point 
	 * Generates an array of world positions spiraling outward from a central point of cover. Each successive squad member is staggered around the point, 
	 * offset by the spacing modifier, until all members are assigned positions.
	 * 
	 * ⚠ Caution:
	 * - Designed for foxholes, craters, or circular cover areas where no obvious "front edge" exists.
	 * - Generated positions may cluster tightly if the cover radius is too small relative to squad size.
	 * - Requires a valid navmesh; off-mesh results will fail unless projected back by the navigation system.
	 * 
	 * This is a complex function, if you are encountering undesired or frustrating behaviour please reach out on the SeinARTS Framework support Discord. */
	TArray<FVector> BuildCoverPositionsAroundCoverPoint(
		const FVector& Point, 
		const TArray<AActor*>& SquadMembers, const UNavigationSystemV1* NavSys,
		const bool bScattersInCover,
		const float CoverSearchRadius, const float CoverSpacingModifier
	) {
		TArray<FVector> OutPositions;
		const FVector QueryExtent(CoverSearchRadius, CoverSearchRadius, 500.f);

		FNavLocation CenterNav;
		if (!NavSys->ProjectPointToNavigation(Point, CenterNav, QueryExtent)) {
			const FVector BigExtent = QueryExtent * 1.5f;
			if (!NavSys->ProjectPointToNavigation(Point, CenterNav, BigExtent)) CenterNav.Location = Point;
		}

		const FVector ClusterCenter = CenterNav.Location;

		// Average capsule radius -> target neighbor distance (diameter) we want in the clump
		float SumRadii = 0.f;
		int32 Count = 0;

		for (int32 s = 0; s < SquadMembers.Num(); ++s) {
			if (!SAFLibrary::IsActorPtrValidSeinARTSActor(SquadMembers[s])) continue;
			if (AActor* Member = SquadMembers[s]) {
				if (const UCapsuleComponent* Capsule = Member->FindComponentByClass<UCapsuleComponent>()) {
					SumRadii += Capsule->GetScaledCapsuleRadius();
					++Count;
				} else if (Member->GetRootComponent()) {
					SumRadii += Member->GetRootComponent()->Bounds.SphereRadius * 0.5f;
					++Count;
				}
			}
		}

		const float AvgCapsule = (Count > 0) ? (SumRadii / Count) : 50.f;

		// Desired neighbor distance (approx) and conversion to Vogel-scale:
		// For hex packing the area per point is A = (√3/2) * d^2, so r = sqrt(n*A/π) = d * sqrt((√3/2π) * n).
		const float DesiredD = (AvgCapsule * 2.f) * CoverSpacingModifier;
		const float VogelC   = DesiredD * 0.5253219888f; // sqrt((√3/2)/π) ≈ 0.525322
		const float MinSeparation = DesiredD * 0.95f;    // allow a tiny bit of squeeze but keep clean
		const float Golden = 2.39996322972865332f; // radians (~137.5°)
		FRandomStream Rand(GetTypeHash(Point));    // deterministic per cover point

		OutPositions.Add(ClusterCenter); // slot 0 at the center
		for (int32 i = 1; i < SquadMembers.Num(); ++i) {
			if (!SAFLibrary::IsActorPtrValidSeinARTSActor(SquadMembers[i])) continue;

			// Optional deterministic scatter per-slot (varies with i)
			float jitterR = 0.f, jitterT = 0.f, scale = 1.f;
			if (bScattersInCover) {
				// different but deterministic per slot: advance stream the same way every i
				const float r01a = Rand.GetFraction();
				const float r01b = Rand.GetFraction();
				const float r01c = Rand.GetFraction();
				scale   = 1.f + r01a * 0.5f;                  // 1.0..1.5 spacing scale
				jitterR = (r01b - 0.5f) * (AvgCapsule * 0.35f);
				jitterT = (r01c - 0.5f) * 0.35f;
			}

			float theta  = i * Golden + jitterT;
			float radius = (VogelC * FMath::Sqrt((float)i)) * scale + jitterR;

			FVector Candidate = ClusterCenter + FVector(FMath::Cos(theta) * radius, FMath::Sin(theta) * radius, 0.f);

			// Project to nav and nudge out if we collide with earlier points (keep spiral shape)
			FNavLocation CandNav;
			const int32 MaxTries = 8;
			int32 Tries = 0;
			while (Tries++ < MaxTries) {
				if (!NavSys->ProjectPointToNavigation(Candidate, CandNav, QueryExtent)) {
					const FVector BigExtent = QueryExtent * 1.5f;
					if (NavSys->ProjectPointToNavigation(Candidate, CandNav, BigExtent)) Candidate = CandNav.Location;
				} else Candidate = CandNav.Location;

				if (!SAFMathLibrary::CheckPointOverlapsPositions(Candidate, MinSeparation, OutPositions)) break;

				// If overlapping, increase only the radius a hair and keep the angle to preserve the ring feel
				radius += AvgCapsule * 0.25f;
				Candidate = ClusterCenter + FVector(FMath::Cos(theta) * radius, FMath::Sin(theta) * radius, 0.f);
			}

			OutPositions.Add(Candidate);
		}

		return OutPositions;
	}

}
