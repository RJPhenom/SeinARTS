#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SAFCoverUtilities.generated.h"

class UNavigationSystemV1;
class AActor;

/**
 * SAFCoverUtilities
 *
 * Cover layout / position-building helpers (moved out of SAFLibrary).
 */
namespace SAFCoverUtilities {

// Cover Check Helpers
// ===================================================================================================================================================================
bool FindNearestCover(UWorld* World, FVector Point, float Radius, AActor*& OutActor, UPrimitiveComponent*& OutComponent, ESAFCoverType& OutCoverType);
inline bool FindNearestCover(UObject* WorldContext, FVector Point, float Radius, AActor*& OutActor, UPrimitiveComponent*& OutComponent, ESAFCoverType& OutCoverType) 
{ return FindNearestCover(WorldContext ? WorldContext->GetWorld() : nullptr, Point, Radius, OutActor, OutComponent, OutCoverType); }

ESAFCoverType GetCoverAtPoint(UWorld* World, FVector Point, bool bProjectToNavmesh = true, bool bShowDebugMessages = false);
inline ESAFCoverType GetCoverAtPoint(UObject* WorldContext, FVector Point, bool bProjectToNavmesh = true, bool bShowDebugMessages = false) 
{ return GetCoverAtPoint(WorldContext ? WorldContext->GetWorld() : nullptr, Point, bProjectToNavmesh, bShowDebugMessages); }

// Squad Cover Positions Builders (the actual positions to take up)
// ============================================================================================
TArray<FVector> BuildCoverPositionsAroundCoverBox(
    const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Point,
    const TArray<AActor*>& SquadMembers, const UNavigationSystemV1* NavSys,
    const bool bWrapsCover = true, const bool bScattersInCover = true,
    const float CoverSearchRadius = 50.f, const float CoverSpacingModifier = 1.f,
    const float CoverRowOffsetModifier = 1.f, const float LateralStaggerModifier = 2.f
);

TArray<FVector> BuildCoverPositionsAroundCoverPoint(
    const FVector& Point,
    const TArray<AActor*>& SquadMembers, const UNavigationSystemV1* NavSys,
    const bool bScattersInCover = true,
    const float CoverSearchRadius = 50.f, const float CoverSpacingModifier = 1.f
);

}


/**
 * USAFCoverUtilities (BPFL)
 *
 * Blueprint exposure for SAFCoverUtilities helpers.
 */
UCLASS()
class USAFCoverUtilities : public UBlueprintFunctionLibrary {
    GENERATED_BODY()

public:

    // Cover Check Helpers
    // ===================================================================================================================================================================
    /** Sphere cover-scan around Point; returns nearest collider & owning actor, plus cover type. */
    UFUNCTION(BlueprintCallable, Category="SeinARTS|Cover")
    static bool FindNearestCover(
        UObject* WorldContextObject, 
        FVector Point, float Radius, 
        UPARAM(ref) AActor*& OutActor, UPARAM(ref) UPrimitiveComponent*& OutCollider, UPARAM(ref) ESAFCoverType OutCoverType
    ) { return SAFCoverUtilities::FindNearestCover(WorldContextObject, Point, Radius, OutActor, OutCollider, OutCoverType); }

    /** Checks cover type at a Point; optionally projects to navmesh first. */
    UFUNCTION(BlueprintCallable, Category="SeinARTS|Cover")
    static ESAFCoverType GetCoverAtPoint(
        UObject* WorldContextObject, 
        FVector Point, bool bProjectToNavmesh = true, bool bShowDebugMessages = false
    ) { return SAFCoverUtilities::GetCoverAtPoint(WorldContextObject, Point, bProjectToNavmesh, bShowDebugMessages); }
    
    // Squad Cover Positions Builders (the actual positions to take up)
    // =============================================================================================
    /**
     * Build Cover Positions Around Cover Box
     *
     * Generates an array of world positions aligned to the edges of a rectangular cover object (defined by corners A–D).
     * Squad members are distributed along the edges in rows, respecting spacing/offset modifiers and whether the cover
     * should wrap around all sides.
     *
     * ⚠ Caution:
     * - Requires a valid navmesh under each generated point.
     * - Spacing and stagger modifiers assume member bounds are supplied by the caller context.
     * - Best for long/linear cover (walls, sandbags).
     */
    UFUNCTION(BlueprintCallable, Category="SeinARTS|Cover")
    static TArray<FVector> BuildCoverPositionsAroundCoverBox(
        UPARAM(ref) FVector& A, UPARAM(ref) FVector& B, UPARAM(ref) FVector& C, UPARAM(ref) FVector& D, const FVector& Point,
        const TArray<AActor*>& Actors, const UNavigationSystemV1* NavSys,
        const bool bScattersInCover = true, const bool bWrapsCover = true,
        const float CoverSearchRadius = 50.f, const float CoverSpacingModifier = 1.f,
        const float CoverRowOffsetModifier = 1.f, const float LateralStaggerModifier = 2.f
    ) {
        return SAFCoverUtilities::BuildCoverPositionsAroundCoverBox(
            A, B, C, D, Point, 
            Actors, NavSys, 
            bWrapsCover, bScattersInCover, 
            CoverSearchRadius, CoverSpacingModifier, 
            CoverRowOffsetModifier, LateralStaggerModifier
        );
    }

    /**
     * Build Cover Positions Around Cover Point
     *
     * Generates an array of world positions spiraling/outward from a central point of cover.
     *
     * ⚠ Caution:
     * - Designed for circular/point-based cover (craters, foxholes).
     * - Ensure radius/spacing are appropriate for squad size to avoid clustering.
     * - Requires a valid navmesh.
     */
    UFUNCTION(BlueprintCallable, Category="SeinARTS|Cover")
    static TArray<FVector> BuildCoverPositionsAroundCoverPoint(
        const FVector& Point,
        const TArray<AActor*>& Actors, const UNavigationSystemV1* NavSys,
        const bool bScattersInCover = true,
        const float CoverSearchRadius = 50.f, const float CoverSpacingModifier = 1.f
    ) {
        return SAFCoverUtilities::BuildCoverPositionsAroundCoverPoint(
            Point, 
            Actors, NavSys, 
            bScattersInCover, 
            CoverSearchRadius, CoverSpacingModifier
        );
    }

};