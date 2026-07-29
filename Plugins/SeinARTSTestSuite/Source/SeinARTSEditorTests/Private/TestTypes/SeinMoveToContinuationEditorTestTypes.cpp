#include "TestTypes/SeinMoveToContinuationEditorTestTypes.h"

#include "SeinPathTypes.h"
#include "Types/Entity.h"

int32 USeinMoveToContinuationEditorTestMovement::PlanCount = 0;
int32 USeinMoveToContinuationEditorTestMovement::BeginCount = 0;
int32 USeinMoveToContinuationEditorTestMovement::TickCount = 0;
int32 USeinMoveToContinuationEditorTestMovement::EndCount = 0;
bool USeinMoveToContinuationEditorTestMovement::bAdvanceWaypoint = false;
bool USeinMoveToContinuationEditorTestMovement::bFinishMove = false;

void USeinMoveToContinuationEditorTestAbility::RecordCompleted(
	FSeinMoveToResult Result)
{
	++CompletedCount;
	LastResult = Result;
}

void USeinMoveToContinuationEditorTestAbility::RecordFailed(
	FSeinMoveToResult Result)
{
	++FailedCount;
	LastResult = Result;
}

void USeinMoveToContinuationEditorTestAbility::RecordWaypoint(
	FSeinMoveToResult Result)
{
	++WaypointCount;
	LastResult = Result;
}

void USeinMoveToContinuationEditorTestAbility::RecordCancelled(
	FSeinMoveToResult Result)
{
	++CancelledCount;
	LastResult = Result;
}

void USeinMoveToContinuationEditorTestAbility::RecordPartialPath(
	FSeinMoveToResult Result)
{
	++PartialPathCount;
	LastResult = Result;
}

void USeinMoveToContinuationEditorTestAbility::
	RecordPathRecomputed(FSeinMoveToResult Result)
{
	++PathRecomputedCount;
	LastResult = Result;
}

void USeinMoveToContinuationEditorTestMovement::Reset()
{
	PlanCount = 0;
	BeginCount = 0;
	TickCount = 0;
	EndCount = 0;
	bAdvanceWaypoint = false;
	bFinishMove = false;
}

ESeinPathResult
USeinMoveToContinuationEditorTestMovement::PlanPath(
	const FSeinPlanPathContext& Context,
	FSeinPath& OutPath) const
{
	++PlanCount;
	OutPath = FSeinPath();
	const FFixedVector Start =
		Context.Entity.Transform.GetLocation();
	const FFixedVector Mid(
		(Start.X + Context.Destination.X)
			/ FFixedPoint::FromInt(2),
		(Start.Y + Context.Destination.Y)
			/ FFixedPoint::FromInt(2),
		(Start.Z + Context.Destination.Z)
			/ FFixedPoint::FromInt(2));
	OutPath.Waypoints = {
		Start, Mid, Context.Destination
	};
	OutPath.bIsValid = true;
	OutPath.bIsPartial = true;
	OutPath.DeriveSegmentsFromWaypoints();
	return ESeinPathResult::Found;
}

void USeinMoveToContinuationEditorTestMovement::OnMoveBegin(
	const FSeinMovementContext&)
{
	++BeginCount;
}

bool USeinMoveToContinuationEditorTestMovement::Tick(
	const FSeinMovementContext& Context)
{
	++TickCount;
	if (bAdvanceWaypoint
		&& Context.CurrentWaypointIndex + 1
			< Context.Path.Waypoints.Num())
	{
		++Context.CurrentWaypointIndex;
		bAdvanceWaypoint = false;
	}
	return bFinishMove;
}

void USeinMoveToContinuationEditorTestMovement::OnMoveEnd(
	FSeinEntity&)
{
	++EndCount;
}

void USeinMoveToContinuationEditorTestObserver::RecordForeign(
	FSeinMoveToResult)
{
}
