// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
#pragma once

#include "CoreMinimal.h"
#include "SeinPathTypes.h"

class USeinWorldSubsystem;
class USeinMoveToAction;

namespace UE::SeinARTSMovement::NavDebug
{
	struct FRouteLine
	{
		FVector From;
		FVector To;
		bool bReverse = false;
	};

	/** Derived presentation geometry. No path state is changed or reconstructed. */
	struct FRoute
	{
		TArray<FRouteLine> Lines;
		FVector Target = FVector::ZeroVector;
		FVector Endpoint = FVector::ZeroVector;
		bool bValid = false;
		bool bSegmentDriven = false;
		bool bPartial = false;
	};

	/** Enumerate this world's managed live moves, never process-global UObject matches. */
	SEINARTSMOVEMENT_API void CollectActions(
		const USeinWorldSubsystem& Sim, TArray<const USeinMoveToAction*>& Out);

	/** Clip only the current leg using its known cursor. TypedSegmentIndex must
	 *  come from the driver, never from a closest-segment search across the route.
	 *  RenderPosition affects the current leg's start only; endpoints stay exact. */
	SEINARTSMOVEMENT_API FRoute BuildRoute(const FSeinPath& Path,
		int32 WaypointIndex, int32 TypedSegmentIndex,
		const FVector& Origin, const FVector& RenderPosition);
}
