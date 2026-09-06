// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
#include "Misc/AutomationTest.h"
#include "Debug/SeinNavPathDebug.h"

using namespace UE::SeinARTSMovement::NavDebug;

namespace
{
	FFixedVector Point(int32 X, int32 Y, int32 Z = 0)
	{
		return FFixedVector(FFixedPoint::FromInt(X), FFixedPoint::FromInt(Y), FFixedPoint::FromInt(Z));
	}
	FSeinPath MakePath(std::initializer_list<FFixedVector> Points)
	{
		FSeinPath Path;
		for (const FFixedVector& P : Points) Path.Waypoints.Add(P);
		Path.bIsValid = true;
		Path.DeriveSegmentsFromWaypoints();
		return Path;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinNavDebugRemaining,
	"SeinARTS.Unit.Navigation.Debug.RemainingRoute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinNavDebugRemaining::RunTest(const FString&)
{
	const FSeinPath Path = MakePath({Point(100, 0), Point(100, 100), Point(200, 100)});
	const FRoute Initial = BuildRoute(Path, 0, INDEX_NONE, FVector::ZeroVector, FVector(25, 20, 0));
	TestTrue(TEXT("implicit initial segment stays on committed geometry"), Initial.Lines[0].From.Equals(FVector(25, 0, 0)));
	TestEqual(TEXT("off-route actor does not move exact destination"), Initial.Endpoint, FVector(200, 100, 0));
	const FRoute Remaining = BuildRoute(Path, 1, INDEX_NONE, FVector::ZeroVector, FVector(110, 50, 0));
	TestEqual(TEXT("passed initial leg removed"), Remaining.Lines.Num(), 2);
	TestTrue(TEXT("current leg trimmed"), Remaining.Lines[0].From.Equals(FVector(100, 50, 0)));
	TestEqual(TEXT("current target is not replaced by actor projection"), Remaining.Target, FVector(100, 100, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinNavDebugRestore,
	"SeinARTS.Unit.Navigation.Debug.MissingRawCells", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinNavDebugRestore::RunTest(const FString&)
{
	FSeinPath Path = MakePath({Point(100, 0)});
	Path.DebugCellPath.Add(Point(50, 50));
	const FRoute Before = BuildRoute(Path, 0, INDEX_NONE, FVector::ZeroVector, FVector(40, 0, 0));
	Path.DebugCellPath.Reset(); // Same condition as restored routes and custom planners.
	const FRoute After = BuildRoute(Path, 0, INDEX_NONE, FVector::ZeroVector, FVector(40, 0, 0));
	TestTrue(TEXT("route remains visible without historical search data"), After.bValid);
	TestEqual(TEXT("raw cells do not control endpoint"), After.Endpoint, Before.Endpoint);
	TestEqual(TEXT("raw cells do not control remaining geometry"), After.Lines[0].From, Before.Lines[0].From);
	Path.bIsPartial = true;
	TestTrue(TEXT("partial status reaches presentation"), BuildRoute(Path, 0, INDEX_NONE, FVector::ZeroVector, FVector::ZeroVector).bPartial);
	TestEqual(TEXT("display never mutates committed path"), Path.Waypoints[0], Point(100, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinNavDebugArc,
	"SeinARTS.Unit.Navigation.Debug.TypedArcAndReverse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinNavDebugArc::RunTest(const FString&)
{
	FSeinPath Path = MakePath({Point(100, 0), Point(0, 100), Point(-100, 100)});
	FSeinPathSegment& Arc = Path.Segments[0];
	Arc.Type = ESeinPathSegmentType::Arc;
	Arc.Center = Point(0, 0);
	Arc.Radius = FFixedPoint::FromInt(100);
	Arc.SweepAngle = FFixedPoint::Pi / FFixedPoint::Two;
	Arc.bReverse = true;
	// Waypoint cursor deliberately disagrees with the authoritative segment cursor.
	const FRoute Route = BuildRoute(Path, 2, 0, FVector::ZeroVector, FVector(70.710678, 70.710678, 0));
	TestTrue(TEXT("uses real segment cursor"), Route.bSegmentDriven);
	TestEqual(TEXT("segment endpoint is the target"), Route.Target, FVector(0, 100, 0));
	TestTrue(TEXT("arc is tessellated instead of a chord to its endpoint"), Route.Lines.Num() > 2);
	TestTrue(TEXT("reverse direction preserved"), Route.Lines[0].bReverse);
	TestFalse(TEXT("forward tail remains forward"), Route.Lines.Last().bReverse);
	TestTrue(TEXT("arc projection remains on authored circle"), FMath::IsNearlyEqual(Route.Lines[0].From.Size2D(), 100.0, 0.01));
	TestEqual(TEXT("exact endpoint survives curve sampling"), Route.Lines.Last().To, FVector(-100, 100, 0));
	const FRoute PastArc = BuildRoute(Path, 0, 1, FVector::ZeroVector, FVector(-30, 100, 0));
	TestEqual(TEXT("completed arc not redrawn at cusp"), PastArc.Lines.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinNavDebugClockwise,
	"SeinARTS.Unit.Navigation.Debug.ClockwiseArc", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinNavDebugClockwise::RunTest(const FString&)
{
	FSeinPath Path = MakePath({Point(100, 0), Point(0, -100)});
	FSeinPathSegment& Arc = Path.Segments[0];
	Arc.Type = ESeinPathSegmentType::Arc;
	Arc.Center = Point(0, 0);
	Arc.Radius = FFixedPoint::FromInt(100);
	Arc.SweepAngle = -FFixedPoint::Pi / FFixedPoint::Two;
	const FRoute Route = BuildRoute(Path, 0, 0, FVector::ZeroVector, FVector(70, -70, 0));
	TestTrue(TEXT("signed progress stays clockwise"), Route.Lines[0].From.Y < -60);
	TestEqual(TEXT("exact clockwise endpoint"), Route.Lines.Last().To, FVector(0, -100, 0));
	const FRoute BeforeStart = BuildRoute(Path, 0, 0, FVector::ZeroVector, FVector(100, 1, 0));
	TestEqual(TEXT("outside-arc projection clamps to start"), BeforeStart.Lines[0].From, FVector(100, 0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinNavDebugInvalid,
	"SeinARTS.Unit.Navigation.Debug.InvalidAndDegenerate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinNavDebugInvalid::RunTest(const FString&)
{
	FSeinPath Path;
	TestFalse(TEXT("invalid path omitted"), BuildRoute(Path, 0, INDEX_NONE, FVector::ZeroVector, FVector::ZeroVector).bValid);
	Path = MakePath({Point(0, 0), Point(0, 0)});
	const FRoute Zero = BuildRoute(Path, 1, INDEX_NONE, FVector::ZeroVector, FVector::ZeroVector);
	TestTrue(TEXT("zero-length leg remains finite"), Zero.bValid && !Zero.Lines[0].From.ContainsNaN());
	TestFalse(TEXT("invalid waypoint cursor omitted"), BuildRoute(Path, 8, INDEX_NONE, FVector::ZeroVector, FVector::ZeroVector).bValid);
	TestFalse(TEXT("invalid authoritative segment cursor is not guessed"), BuildRoute(Path, 0, 8, FVector::ZeroVector, FVector::ZeroVector).bValid);
	return true;
}
