#pragma once

#include "Abilities/SeinAbility.h"
#include "Movement/SeinTrackedVehicleMovement.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinNavigation.h"
#include "SeinVehicleGymTestTypes.generated.h"

class USeinPlannerHandle;

/** Axis-aligned blocked region used by the code-only Vehicle Gym nav double. */
struct FSeinVehicleGymBlockedRect
{
	FFixedPoint MinX = FFixedPoint::Zero;
	FFixedPoint MaxX = FFixedPoint::Zero;
	FFixedPoint MinY = FFixedPoint::Zero;
	FFixedPoint MaxY = FFixedPoint::Zero;

	bool Contains(const FFixedVector& Position) const
	{
		return Position.X >= MinX && Position.X <= MaxX
			&& Position.Y >= MinY && Position.Y <= MaxY;
	}
};

/** Immutable recipe installed before a transient Vehicle Gym world is made. */
struct FSeinVehicleGymNavigationRecipe
{
	FName ScenarioId = NAME_None;
	TArray<FFixedVector> Route;
	/** Pure start-position-selected route used to exercise interval repaths. */
	TArray<FFixedVector> RepathRoute;
	FFixedPoint RepathRouteStartX = FFixedPoint::Zero;
	bool bTrimRepathRouteByStartX = false;
	TArray<FSeinVehicleGymBlockedRect> BlockedRects;
	bool bUseCorridor = false;
	FFixedPoint CorridorOpenAtOrBelowX = FFixedPoint::Zero;
	FFixedPoint CorridorHalfWidth = FFixedPoint::Zero;
};

/**
 * Stateless, code-only navigation used by the Vehicle Gym.
 *
 * It returns a deterministic authored coarse route and exposes simple bounded
 * obstacle geometry to the real Movement+ maneuver probes and movement floor.
 * The complete immutable recipe participates in its static-environment digest.
 */
UCLASS()
class USeinVehicleGymNavigation : public USeinNavigation
{
	GENERATED_BODY()

public:
	static void InstallRecipe(const FSeinVehicleGymNavigationRecipe& InRecipe);
	static void ResetRecipe();
	static int32 GetOccupancyQueryCount();
	static void ResetOccupancyQueryCount();
	static int32 GetPathQueryCount();

	virtual bool HasRuntimeData() const override { return true; }
	virtual bool ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const override;
	virtual bool ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const override;
	virtual bool FindPath(
		const FSeinPathRequest& Request,
		FSeinPath& OutPath) const override;
	virtual bool FindCellPath(
		const FSeinPathRequest& Request,
		FSeinPath& OutPath) const override;
	virtual bool IsPassable(const FFixedVector& WorldPos) const override;
	virtual bool IsWorldPositionClear(
		const FFixedVector& WorldPos,
		uint8 AgentNavLayerMask) const override;
	virtual bool IsWorldPositionClearForAgent(
		const FFixedVector& WorldPos,
		const FSeinNavAgentProfile& Agent) const override;
	virtual bool GetCellHeightAt(
		const FFixedVector& WorldPos,
		FFixedPoint& OutZ,
		bool bWalkableOnly = true) const override;

private:
	static FSeinVehicleGymNavigationRecipe Recipe;
	static int32 OccupancyQueryCount;
	static int32 PathQueryCount;
};

/** Coarse-route injector for public wheeled PlanPath contract tests. */
UCLASS()
class USeinVehicleGymWheeledPlanner : public USeinWheeledVehicleMovement
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FFixedVector> CoarseRoute;
	virtual ESeinPathResult BP_PlanPath_Implementation(
		USeinPlannerHandle* Planner) const override;
};

/** Coarse-route injector for public tracked PlanPath contract tests. */
UCLASS()
class USeinVehicleGymTrackedPlanner : public USeinTrackedVehicleMovement
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FFixedVector> CoarseRoute;
	virtual ESeinPathResult BP_PlanPath_Implementation(
		USeinPlannerHandle* Planner) const override;
};

/** Minimal owner for real USeinMoveToAction snapshot/continuation tests. */
UCLASS()
class USeinVehicleGymAbility : public USeinAbility
{
	GENERATED_BODY()
};
