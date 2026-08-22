#include "TestTypes/SeinMoveToContinuationEditorTestTypes.h"

#include "Misc/SecureHash.h"
#include "SeinPathTypes.h"
#include "Types/Entity.h"

FFixedVector USeinMoveToContinuationEditorTestNavigation::EscapeOffset;
int32 USeinMoveToContinuationEditorTestMovement::PlanCount = 0;
int32 USeinMoveToContinuationEditorTestMovement::BeginCount = 0;
int32 USeinMoveToContinuationEditorTestMovement::TickCount = 0;
int32 USeinMoveToContinuationEditorTestMovement::EndCount = 0;
bool USeinMoveToContinuationEditorTestMovement::bAdvanceWaypoint = false;
bool USeinMoveToContinuationEditorTestMovement::
	bAdvanceInitialWaypointOnTick = false;
bool USeinMoveToContinuationEditorTestMovement::bFinishMove = false;

void USeinMoveToContinuationEditorTestNavigation::Reset()
{
	EscapeOffset = FFixedVector(
		FFixedPoint::Zero,
		FFixedPoint::FromInt(1200),
		FFixedPoint::Zero);
}

bool USeinMoveToContinuationEditorTestNavigation::
	ComputeStaticEnvironmentDigest(
		FGuid& OutDigest,
		FString& OutError) const
{
	OutError.Reset();
	FMD5 Hash;
	static constexpr ANSICHAR StableId[] =
		"seinarts.tests.navigation.move_to_continuation_escape/v1";
	Hash.Update(
		reinterpret_cast<const uint8*>(StableId),
		static_cast<uint32>(UE_ARRAY_COUNT(StableId) - 1));
	auto AppendInt64 = [&Hash](int64 Value)
	{
		uint8 Bytes[8];
		const uint64 Bits = static_cast<uint64>(Value);
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Bytes[Index] = static_cast<uint8>(Bits >> (Index * 8));
		}
		Hash.Update(Bytes, UE_ARRAY_COUNT(Bytes));
	};
	AppendInt64(EscapeOffset.X.Value);
	AppendInt64(EscapeOffset.Y.Value);
	AppendInt64(EscapeOffset.Z.Value);

	uint8 Digest[16];
	Hash.Final(Digest);
	auto ReadUInt32 = [&Digest](int32 Offset)
	{
		return static_cast<uint32>(Digest[Offset])
			| (static_cast<uint32>(Digest[Offset + 1]) << 8)
			| (static_cast<uint32>(Digest[Offset + 2]) << 16)
			| (static_cast<uint32>(Digest[Offset + 3]) << 24);
	};
	OutDigest = FGuid(
		ReadUInt32(0),
		ReadUInt32(4),
		ReadUInt32(8),
		ReadUInt32(12));
	return true;
}

bool USeinMoveToContinuationEditorTestNavigation::
	ComputeStateCoverageClaim(
		FSeinNavigationStateCoverageClaim& OutClaim,
		FString& OutError) const
{
	OutClaim = {};
	OutError.Reset();
	OutClaim.StableImplementationId =
		TEXT("seinarts.tests.navigation.move_to_continuation_escape");
	OutClaim.BehaviorRevision = 1;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage = ESeinNavigationStateCoverage::Stateless;
	return true;
}

bool USeinMoveToContinuationEditorTestNavigation::QueryEscapeTarget(
	const FSeinEscapeQuery& Query,
	FFixedVector& OutTarget) const
{
	OutTarget = FFixedVector(
		Query.From.X + EscapeOffset.X,
		Query.From.Y + EscapeOffset.Y,
		Query.From.Z + EscapeOffset.Z);
	return true;
}

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
	bAdvanceInitialWaypointOnTick = false;
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
	if ((bAdvanceWaypoint
			|| (bAdvanceInitialWaypointOnTick
				&& Context.CurrentWaypointIndex == 0))
		&& Context.CurrentWaypointIndex + 1
			< Context.Path.Waypoints.Num())
	{
		++Context.CurrentWaypointIndex;
		if (bAdvanceWaypoint)
		{
			bAdvanceWaypoint = false;
		}
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
