#include "Movement/SeinVehicleGymTestTypes.h"

#include "Movement/SeinPlannerHandle.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"

FSeinVehicleGymNavigationRecipe USeinVehicleGymNavigation::Recipe;
int32 USeinVehicleGymNavigation::OccupancyQueryCount = 0;
int32 USeinVehicleGymNavigation::PathQueryCount = 0;

void USeinVehicleGymNavigation::InstallRecipe(
	const FSeinVehicleGymNavigationRecipe& InRecipe)
{
	Recipe = InRecipe;
	OccupancyQueryCount = 0;
	PathQueryCount = 0;
}

void USeinVehicleGymNavigation::ResetRecipe()
{
	Recipe = FSeinVehicleGymNavigationRecipe();
	OccupancyQueryCount = 0;
	PathQueryCount = 0;
}

int32 USeinVehicleGymNavigation::GetOccupancyQueryCount()
{
	return OccupancyQueryCount;
}

void USeinVehicleGymNavigation::ResetOccupancyQueryCount()
{
	OccupancyQueryCount = 0;
}

int32 USeinVehicleGymNavigation::GetPathQueryCount()
{
	return PathQueryCount;
}

bool USeinVehicleGymNavigation::ComputeStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTSTests.VehicleGym.Navigation"), 1);
	Writer.WriteName(Recipe.ScenarioId);
	Writer.WriteInt32(Recipe.Route.Num());
	for (const FFixedVector& Point : Recipe.Route)
	{
		Writer.WriteInt64(Point.X.Value);
		Writer.WriteInt64(Point.Y.Value);
		Writer.WriteInt64(Point.Z.Value);
	}
	Writer.WriteInt32(Recipe.RepathRoute.Num());
	for (const FFixedVector& Point : Recipe.RepathRoute)
	{
		Writer.WriteInt64(Point.X.Value);
		Writer.WriteInt64(Point.Y.Value);
		Writer.WriteInt64(Point.Z.Value);
	}
	Writer.WriteInt64(Recipe.RepathRouteStartX.Value);
	Writer.WriteBool(Recipe.bTrimRepathRouteByStartX);
	Writer.WriteInt32(Recipe.BlockedRects.Num());
	for (const FSeinVehicleGymBlockedRect& Rect : Recipe.BlockedRects)
	{
		Writer.WriteInt64(Rect.MinX.Value);
		Writer.WriteInt64(Rect.MaxX.Value);
		Writer.WriteInt64(Rect.MinY.Value);
		Writer.WriteInt64(Rect.MaxY.Value);
	}
	Writer.WriteBool(Recipe.bUseCorridor);
	Writer.WriteInt64(Recipe.CorridorOpenAtOrBelowX.Value);
	Writer.WriteInt64(Recipe.CorridorHalfWidth.Value);
	return Writer.Finalize(OutDigest, OutError);
}

bool USeinVehicleGymNavigation::ComputeStateCoverageClaim(
	FSeinNavigationStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutError.Reset();
	OutClaim = FSeinNavigationStateCoverageClaim();
	OutClaim.StableImplementationId =
		TEXT("SeinARTSTests.VehicleGymNavigation");
	OutClaim.BehaviorRevision = 1;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage = ESeinNavigationStateCoverage::Stateless;
	return true;
}

bool USeinVehicleGymNavigation::FindPath(
	const FSeinPathRequest& Request,
	FSeinPath& OutPath) const
{
	OutPath.Clear();
	++PathQueryCount;
	const bool bUseRepathRoute = !Recipe.RepathRoute.IsEmpty()
		&& Request.Start.X > Recipe.RepathRouteStartX;
	if (bUseRepathRoute && Recipe.bTrimRepathRouteByStartX)
	{
		for (const FFixedVector& Point : Recipe.RepathRoute)
		{
			if (Point.X > Request.Start.X)
			{
				OutPath.Waypoints.Add(Point);
			}
		}
	}
	else
	{
		OutPath.Waypoints = bUseRepathRoute
			? Recipe.RepathRoute
			: Recipe.Route;
	}
	if (OutPath.Waypoints.IsEmpty()
		|| OutPath.Waypoints.Last() != Request.End)
	{
		OutPath.Waypoints.Add(Request.End);
	}
	OutPath.bIsValid = !OutPath.Waypoints.IsEmpty();
	OutPath.bIsPartial = false;
	OutPath.DeriveSegmentsFromWaypoints();
	return OutPath.bIsValid;
}

bool USeinVehicleGymNavigation::FindCellPath(
	const FSeinPathRequest& Request,
	FSeinPath& OutPath) const
{
	return FindPath(Request, OutPath);
}

bool USeinVehicleGymNavigation::IsPassable(
	const FFixedVector& WorldPos) const
{
	if (Recipe.bUseCorridor
		&& WorldPos.X > Recipe.CorridorOpenAtOrBelowX)
	{
		const FFixedPoint AbsY = WorldPos.Y < FFixedPoint::Zero
			? -WorldPos.Y
			: WorldPos.Y;
		if (AbsY > Recipe.CorridorHalfWidth)
		{
			return false;
		}
	}
	for (const FSeinVehicleGymBlockedRect& Rect : Recipe.BlockedRects)
	{
		if (Rect.Contains(WorldPos))
		{
			return false;
		}
	}
	return true;
}

bool USeinVehicleGymNavigation::IsWorldPositionClear(
	const FFixedVector& WorldPos,
	uint8 AgentNavLayerMask) const
{
	(void)AgentNavLayerMask;
	++OccupancyQueryCount;
	return IsPassable(WorldPos);
}

bool USeinVehicleGymNavigation::IsWorldPositionClearForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent) const
{
	return IsWorldPositionClear(WorldPos, Agent.AgentNavLayerMask);
}

bool USeinVehicleGymNavigation::GetCellHeightAt(
	const FFixedVector& WorldPos,
	FFixedPoint& OutZ,
	bool bWalkableOnly) const
{
	if (bWalkableOnly && !IsPassable(WorldPos))
	{
		return false;
	}
	OutZ = FFixedPoint::Zero;
	return true;
}

namespace
{
	ESeinPathResult BuildCoarseRoute(
		USeinPlannerHandle* Planner,
		const TArray<FFixedVector>& Route)
	{
		if (!Planner)
		{
			return ESeinPathResult::NoNavigation;
		}
		Planner->ClearPath();
		for (const FFixedVector& Point : Route)
		{
			Planner->AddWaypoint(Point);
		}
		Planner->FinalizePath(false);
		return Route.IsEmpty()
			? ESeinPathResult::NotFound
			: ESeinPathResult::Found;
	}
}

ESeinPathResult USeinVehicleGymWheeledPlanner::
	BP_PlanPath_Implementation(USeinPlannerHandle* Planner) const
{
	return BuildCoarseRoute(Planner, CoarseRoute);
}

ESeinPathResult USeinVehicleGymTrackedPlanner::
	BP_PlanPath_Implementation(USeinPlannerHandle* Planner) const
{
	return BuildCoarseRoute(Planner, CoarseRoute);
}
