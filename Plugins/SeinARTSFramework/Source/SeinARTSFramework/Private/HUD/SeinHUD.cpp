/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinHUD.cpp
 * @brief   RTS HUD implementation — marquee box, command drag, debug log panel.
 */

#include "HUD/SeinHUD.h"
#include "Player/SeinPlayerController.h"
#include "Debug/SeinCommandLogSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Actor/SeinActor.h"
#include "Components/SeinExtentsComponent.h"
#include "Settings/PluginSettings.h"
#include "Types/Entity.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Blueprint/UserWidget.h"
#include "CanvasItem.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

// Opt-in marquee-selection funnel diagnostics. Off by default; toggle in the
// console with `Sein.Marquee.DebugLog` to print, per drag, where (if anywhere)
// candidate units are lost — iterate → valid → projected → hull/SAT hit — plus
// the marquee rect and a few units' projected screen boxes for coordinate-space
// sanity. Run it again to silence.
// Both marquee debug switches are console COMMANDS, not cvars, so a bare command
// flips them (just hit Enter). An optional arg still forces a state: 1/on/true →
// on, 0/off/false → off, toggle → flip. State lives in these file-static bools.
static bool GSeinMarqueeDebugLog  = false;
static bool GSeinMarqueeDebugShow = false;

// No arg → flip. Else: 0/off/false → off; toggle → flip; anything else → on.
static void SeinApplyToggleArg(const TArray<FString>& Args, bool& bFlag)
{
	if (Args.Num() == 0) { bFlag = !bFlag; return; }
	const FString A = Args[0].TrimStartAndEnd();
	if (A.Equals(TEXT("toggle"), ESearchCase::IgnoreCase)) { bFlag = !bFlag; }
	else if (A.Equals(TEXT("0")) || A.Equals(TEXT("off"), ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase)) { bFlag = false; }
	else { bFlag = true; }
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice CCmdSeinMarqueeDebugLog(
	TEXT("Sein.Marquee.DebugLog"),
	TEXT("Toggle marquee selection funnel logging. Bare command flips; optional 0/1/on/off/toggle forces a state."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
		{
			SeinApplyToggleArg(Args, GSeinMarqueeDebugLog);
			Ar.Logf(TEXT("Sein.Marquee.DebugLog = %d"), GSeinMarqueeDebugLog ? 1 : 0);
		}));

// Opt-in marquee hull overlay. `Sein.Marquee.Debug.Show` draws, every frame,
// the EXACT screen-space convex polygon the marquee SAT-tests for each unit
// (red outline) plus the raw projected extent samples (amber dots). The overlay
// and the selection share one hull builder (SeinComputeActorScreenHull), so the
// red shape you see IS the shape the box is tested against — compare it directly
// to the cyan marquee. Run it again to hide (off by default).
static FAutoConsoleCommandWithWorldArgsAndOutputDevice CCmdSeinMarqueeDebugShow(
	TEXT("Sein.Marquee.Debug.Show"),
	TEXT("Toggle the marquee hull overlay (red = SAT-tested polygon, amber = projected samples). Bare command flips; optional 0/1/on/off/toggle forces a state."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
		{
			SeinApplyToggleArg(Args, GSeinMarqueeDebugShow);
			Ar.Logf(TEXT("Sein.Marquee.Debug.Show = %d"), GSeinMarqueeDebugShow ? 1 : 0);
		}));

void ASeinHUD::BeginPlay()
{
	Super::BeginPlay();

	if (HUDLayoutWidgetClass)
	{
		APlayerController* PC = GetOwningPlayerController();
		if (PC)
		{
			HUDLayoutWidget = CreateWidget<UUserWidget>(PC, HUDLayoutWidgetClass);
			if (HUDLayoutWidget)
			{
				HUDLayoutWidget->AddToViewport();
			}
		}
	}
}

void ASeinHUD::DrawHUD()
{
	Super::DrawHUD();

	ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	if (PC->bIsMarqueeDragging)
	{
		DrawMarqueeBox();
		bMarqueeWasActive = true;
	}

	// Draw command drag formation line
	if (PC->bIsCommandDragging)
	{
		DrawCommandDragLine();
	}

	if (!PC->bIsMarqueeDragging && bMarqueeWasActive)
	{
		// Marquee just ended — resolve selection
		ResolveMarqueeSelection();
		bMarqueeWasActive = false;
	}

	// Debug: draw the exact screen-space hulls the marquee tests (Sein.Marquee.Debug.Show).
	if (GSeinMarqueeDebugShow)
	{
		DrawMarqueeDebugHulls();
	}

	// Debug command log overlay
	DrawCommandLogPanel();
}

void ASeinHUD::DrawMarqueeBox()
{
	const ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	const FVector2D Start = PC->MarqueeStart;
	const FVector2D Current = PC->MarqueeCurrent;

	const float MinX = FMath::Min(Start.X, Current.X);
	const float MinY = FMath::Min(Start.Y, Current.Y);
	const float MaxX = FMath::Max(Start.X, Current.X);
	const float MaxY = FMath::Max(Start.Y, Current.Y);
	const float Width = MaxX - MinX;
	const float Height = MaxY - MinY;

	// Draw filled rectangle
	DrawRect(MarqueeFillColor, MinX, MinY, Width, Height);

	// Draw border (four lines)
	const float T = MarqueeBorderThickness;
	const FLinearColor& C = MarqueeBorderColor;

	DrawRect(C, MinX, MinY, Width, T);         // Top
	DrawRect(C, MinX, MaxY - T, Width, T);     // Bottom
	DrawRect(C, MinX, MinY, T, Height);         // Left
	DrawRect(C, MaxX - T, MinY, T, Height);     // Right
}

// ====================================================================================================
// Marquee geometry — smarter than AHUD::GetActorsInSelectionRectangle.
//
// The engine routine takes each actor's full 3D bounding box, projects its 8
// corners, and unions them into a screen-space AABB that it tests against the
// marquee. Two problems: (1) the source is the actor's render bounds, which for
// a skeletal/animated or large mesh drift well past the unit's real body; and
// (2) the screen-space AABB is far looser than the projected silhouette, so a
// marquee that only clips the AABB's empty corner still selects the unit.
//
// Instead we project the entity's AUTHORED sim extents (FSeinExtentsComponent —
// stable, doesn't breathe with animation) into a screen-space CONVEX POLYGON
// and SAT-test that polygon against the marquee. Tight, and it tracks the body.
// ====================================================================================================

// Read the marquee geometry mode (Project Settings > Plugins > SeinARTS > UI).
// TRUE → project the exact authored extents silhouette (matches the
// red extents viz); FALSE → the legacy top/bottom-ring approximation. Defaults
// to TRUE if settings are unavailable.
static bool SeinUseExtentsForMarquee()
{
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	return Settings ? Settings->bUseSeinExtentsForMarquee : true;
}

// Faithful capsule surface sampling — matches the editor extents visualizer's
// DrawWireCapsule shape exactly: true hemispherical end-caps, HalfHeight clamped
// to >= Radius. Center/axes are world-space; the convex hull of these points is
// the capsule's real projected silhouette (no flat-cap over-cover).
static void SeinAppendCapsulePoints(const FVector& Center,
	const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	float Radius, float HalfHeight, TArray<FVector>& OutWorld)
{
	constexpr int32 NumSides = 16;                       // matches DrawWireCapsule
	const float CylHalf = FMath::Max(0.0f, HalfHeight - Radius);
	// Cap latitudes equator(0) -> pole(90); the mid bands keep the convex hull
	// from cutting each dome down to a cone.
	static constexpr float CapLatDeg[] = { 0.0f, 30.0f, 60.0f, 90.0f };

	for (int32 S = 0; S < NumSides; ++S)
	{
		const float Ang = (static_cast<float>(S) / static_cast<float>(NumSides)) * 2.0f * PI;
		const FVector Radial = AxisX * FMath::Cos(Ang) + AxisY * FMath::Sin(Ang);
		for (const float LatDeg : CapLatDeg)
		{
			const float Lat = FMath::DegreesToRadians(LatDeg);
			const float RingR = Radius * FMath::Cos(Lat);
			const float RingH = CylHalf + Radius * FMath::Sin(Lat);
			OutWorld.Add(Center + AxisZ * RingH + Radial * RingR);   // top cap + body
			OutWorld.Add(Center - AxisZ * RingH + Radial * RingR);   // bottom cap + body
		}
	}
}

// ACCURATE path: mirror the editor extents visualizer's geometry exactly —
// rotation + offset only (sim extents are scale-free, so actor scale is dropped,
// same as the viz), ShapeBase + per-shape YawOffset, capsule HalfHeight =
// max(Height/2, Radius) with hemispherical caps, box = 8 oriented corners. So the
// projected hull equals the red extents viz the player sees.
static void SeinAppendExtentsHullSourcesAccurate(AActor* Actor,
	const FSeinExtentsComponent* Extents, TArray<FVector>& OutWorld)
{
	const FQuat ActorQuat = Actor->GetActorQuat();
	const FVector ActorPos = Actor->GetActorLocation();

	for (const FSeinExtentsShape& Shape : Extents->Shapes)
	{
		const FVector LocalOffset(
			Shape.LocalOffset.X.ToFloat(),
			Shape.LocalOffset.Y.ToFloat(),
			Shape.LocalOffset.Z.ToFloat());
		const FVector ShapeBase = ActorPos + ActorQuat.RotateVector(LocalOffset);

		const FQuat YawQ(FVector::UpVector, FMath::DegreesToRadians(Shape.YawOffsetDegrees.ToFloat()));
		const FQuat ShapeQuat = ActorQuat * YawQ;
		const FVector AxisX = ShapeQuat.GetForwardVector();
		const FVector AxisY = ShapeQuat.GetRightVector();
		const FVector AxisZ = ShapeQuat.GetUpVector();

		const float Height = FMath::Max(0.0f, Shape.Height.ToFloat());
		const FVector Center = ShapeBase + AxisZ * (Height * 0.5f);

		if (Shape.Shape == ESeinExtentsShape::Box)
		{
			const float HX = FMath::Max(0.0f, Shape.HalfExtentX.ToFloat());
			const float HY = FMath::Max(0.0f, Shape.HalfExtentY.ToFloat());
			const float HZ = Height * 0.5f;
			for (int32 Sx = -1; Sx <= 1; Sx += 2)
			for (int32 Sy = -1; Sy <= 1; Sy += 2)
			for (int32 Sz = -1; Sz <= 1; Sz += 2)
			{
				OutWorld.Add(Center + AxisX * (Sx * HX) + AxisY * (Sy * HY) + AxisZ * (Sz * HZ));
			}
		}
		else // Capsule
		{
			const float Radius = FMath::Max(0.0f, Shape.Radius.ToFloat());
			const float HalfHeight = FMath::Max(Height * 0.5f, Radius);
			SeinAppendCapsulePoints(Center, AxisX, AxisY, AxisZ, Radius, HalfHeight, OutWorld);
		}
	}
}

// LEGACY path (bUseSeinExtentsForMarquee = false): the original approximation —
// box = 8 corners; capsule = top + bottom rings only (a flat-capped cylinder),
// via the FULL actor transform (incl. scale). Over-covers tall/large capsules
// under an angled camera; kept for parity / opt-out.
static void SeinAppendExtentsHullSourcesLegacy(AActor* Actor,
	const FSeinExtentsComponent* Extents, TArray<FVector>& OutWorld)
{
	const FTransform Xf = Actor->GetActorTransform();

	for (const FSeinExtentsShape& Shape : Extents->Shapes)
	{
		const float OX = Shape.LocalOffset.X.ToFloat();
		const float OY = Shape.LocalOffset.Y.ToFloat();
		const float OZ = Shape.LocalOffset.Z.ToFloat();
		const float HZ = Shape.Height.ToFloat();

		if (Shape.Shape == ESeinExtentsShape::Box)
		{
			const float HX = Shape.HalfExtentX.ToFloat();
			const float HY = Shape.HalfExtentY.ToFloat();
			const FQuat YawQ(FVector::UpVector, FMath::DegreesToRadians(Shape.YawOffsetDegrees.ToFloat()));
			for (int32 Sx = -1; Sx <= 1; Sx += 2)
			for (int32 Sy = -1; Sy <= 1; Sy += 2)
			{
				const FVector PlanarXY = FVector(OX, OY, 0.0f) + YawQ.RotateVector(FVector(Sx * HX, Sy * HY, 0.0f));
				OutWorld.Add(Xf.TransformPosition(FVector(PlanarXY.X, PlanarXY.Y, OZ)));        // bottom
				OutWorld.Add(Xf.TransformPosition(FVector(PlanarXY.X, PlanarXY.Y, OZ + HZ)));   // top
			}
		}
		else // Capsule — top + bottom rings (8 each).
		{
			const float R = Shape.Radius.ToFloat();
			for (int32 K = 0; K < 8; ++K)
			{
				const float Ang = (static_cast<float>(K) / 8.0f) * 2.0f * PI;
				const float RX = FMath::Cos(Ang) * R;
				const float RY = FMath::Sin(Ang) * R;
				OutWorld.Add(Xf.TransformPosition(FVector(OX + RX, OY + RY, OZ)));        // bottom
				OutWorld.Add(Xf.TransformPosition(FVector(OX + RX, OY + RY, OZ + HZ)));   // top
			}
		}
	}
}

// Append an entity's silhouette source points (world space) for the marquee
// test. With a valid extents component, dispatches to the accurate or legacy
// builder per the Selection setting; with none, falls back to the actor's visual
// component-bounds box.
static void SeinAppendMarqueeHullSources(AActor* Actor, USeinWorldSubsystem* Sim,
	FSeinEntityHandle Handle, TArray<FVector>& OutWorld)
{
	const FSeinExtentsComponent* Extents = Sim ? Sim->GetComponent<FSeinExtentsComponent>(Handle) : nullptr;
	if (Extents && Extents->Shapes.Num() > 0)
	{
		if (SeinUseExtentsForMarquee())
		{
			SeinAppendExtentsHullSourcesAccurate(Actor, Extents, OutWorld);
		}
		else
		{
			SeinAppendExtentsHullSourcesLegacy(Actor, Extents, OutWorld);
		}
		return;
	}

	// Fallback: the actor's visual bounds box (8 corners). Still tested as a
	// projected polygon, so it beats the engine's screen-AABB approach.
	FVector Origin, BoxExtent;
	Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, BoxExtent);
	for (int32 Sx = -1; Sx <= 1; Sx += 2)
	for (int32 Sy = -1; Sy <= 1; Sy += 2)
	for (int32 Sz = -1; Sz <= 1; Sz += 2)
	{
		OutWorld.Add(Origin + FVector(Sx * BoxExtent.X, Sy * BoxExtent.Y, Sz * BoxExtent.Z));
	}
}

// 2D convex hull (Andrew's monotone chain). Returns a CCW polygon; collinear
// points are dropped. For < 3 input points the points are returned as-is (the
// SAT test below handles point/segment degenerates).
static void SeinConvexHull2D(TArray<FVector2D> Points, TArray<FVector2D>& OutHull)
{
	OutHull.Reset();
	const int32 N = Points.Num();
	if (N < 3)
	{
		OutHull = MoveTemp(Points);
		return;
	}

	Points.Sort([](const FVector2D& A, const FVector2D& B)
	{
		return (A.X < B.X) || (A.X == B.X && A.Y < B.Y);
	});

	auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
	};

	TArray<FVector2D> Hull;
	Hull.SetNumUninitialized(2 * N);
	int32 K = 0;

	// Lower hull.
	for (int32 i = 0; i < N; ++i)
	{
		while (K >= 2 && Cross(Hull[K - 2], Hull[K - 1], Points[i]) <= 0.0) { --K; }
		Hull[K++] = Points[i];
	}
	// Upper hull.
	const int32 Lower = K + 1;
	for (int32 i = N - 2; i >= 0; --i)
	{
		while (K >= Lower && Cross(Hull[K - 2], Hull[K - 1], Points[i]) <= 0.0) { --K; }
		Hull[K++] = Points[i];
	}

	Hull.SetNum(FMath::Max(0, K - 1)); // last point repeats the first
	OutHull = MoveTemp(Hull);
}

// Separating-axis test between a convex polygon (CCW, >= 1 vertex) and an AABB.
// Correctly handles containment (rect inside polygon and vice versa) plus the
// point/segment degenerates that fall out of a tiny or edge-on silhouette.
static bool SeinConvexHullIntersectsBox2D(const TArray<FVector2D>& Hull, const FBox2D& Box)
{
	const int32 N = Hull.Num();
	if (N == 0)
	{
		return false;
	}
	if (N == 1)
	{
		return Box.IsInside(Hull[0]);
	}

	const FVector2D RectPts[4] =
	{
		FVector2D(Box.Min.X, Box.Min.Y),
		FVector2D(Box.Max.X, Box.Min.Y),
		FVector2D(Box.Max.X, Box.Max.Y),
		FVector2D(Box.Min.X, Box.Max.Y)
	};

	auto SeparatedOn = [&](const FVector2D& Axis) -> bool
	{
		if (Axis.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			return false; // degenerate axis can't separate
		}
		float MinA = TNumericLimits<float>::Max(), MaxA = TNumericLimits<float>::Lowest();
		for (const FVector2D& V : Hull)
		{
			const float D = FVector2D::DotProduct(V, Axis);
			MinA = FMath::Min(MinA, D); MaxA = FMath::Max(MaxA, D);
		}
		float MinB = TNumericLimits<float>::Max(), MaxB = TNumericLimits<float>::Lowest();
		for (const FVector2D& V : RectPts)
		{
			const float D = FVector2D::DotProduct(V, Axis);
			MinB = FMath::Min(MinB, D); MaxB = FMath::Max(MaxB, D);
		}
		return (MaxA < MinB) || (MaxB < MinA);
	};

	// Rect face normals.
	if (SeparatedOn(FVector2D(1.0f, 0.0f))) { return false; }
	if (SeparatedOn(FVector2D(0.0f, 1.0f))) { return false; }

	// Polygon edge normals.
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D Edge = Hull[(i + 1) % N] - Hull[i];
		if (SeparatedOn(FVector2D(-Edge.Y, Edge.X))) { return false; }
	}

	return true; // no separating axis found → overlap
}

// Build the screen-space convex polygon the marquee tests for one actor. This is
// the single source of truth shared by CollectActorsInMarquee (the SAT test) and
// the Sein.Marquee.Debug.Show overlay (the red outline) — so what's drawn is
// byte-for-byte what's tested. ScratchWorld is caller-owned scratch (reused
// across actors to avoid per-actor allocation). Returns false if the actor
// produced no points in front of the camera. OutScreenPts = the raw projected
// extent samples (pre-hull); OutHull = the CCW silhouette polygon.
static bool SeinComputeActorScreenHull(AHUD* Hud, AActor* Actor, USeinWorldSubsystem* Sim,
	FSeinEntityHandle Handle, TArray<FVector>& ScratchWorld,
	TArray<FVector2D>& OutScreenPts, TArray<FVector2D>& OutHull)
{
	ScratchWorld.Reset();
	SeinAppendMarqueeHullSources(Actor, Sim, Handle, ScratchWorld);
	if (ScratchWorld.Num() == 0)
	{
		return false;
	}

	// Project through the HUD canvas so coordinates share the marquee rect's
	// space. Skip points behind the camera (Z <= 0), mirroring the engine
	// routine's front-side guard.
	OutScreenPts.Reset();
	for (const FVector& WorldPt : ScratchWorld)
	{
		const FVector Projected = Hud->Project(WorldPt, true);
		if (Projected.Z > 0.0f)
		{
			OutScreenPts.Add(FVector2D(Projected.X, Projected.Y));
		}
	}
	if (OutScreenPts.Num() == 0)
	{
		return false;
	}

	SeinConvexHull2D(OutScreenPts, OutHull);
	return true;
}

void ASeinHUD::CollectActorsInMarquee(const FVector2D& P0, const FVector2D& P1, TArray<ASeinActor*>& OutActors)
{
	OutActors.Reset();

	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Marquee] ABORT World=%d Canvas=%d"), World != nullptr, Canvas != nullptr);
		return;
	}

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();

	// Marquee rectangle (any corner order, like the engine routine).
	FBox2D Rect(ForceInit);
	Rect += P0;
	Rect += P1;

	// Opt-in funnel diagnostics (Sein.Marquee.DebugLog 1). Counters are cheap; the
	// per-actor logging + projected-box math below only run when debug is on.
	const bool bDebugLog = GSeinMarqueeDebugLog;
	int32 NumIterated = 0, NumValid = 0, NumNonSelectable = 0, NumProjected = 0, NumHit = 0, NumSampled = 0;

	TArray<FVector>   WorldPts;
	TArray<FVector2D> ScreenPts;
	TArray<FVector2D> Hull;

	for (TActorIterator<ASeinActor> It(World); It; ++It)
	{
		ASeinActor* Actor = *It;
		++NumIterated;
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}
		++NumValid;

		const FSeinEntityHandle Handle = Actor->GetEntityHandle();

		// Selectable filtering removed for baseline parity (the engine path didn't
		// filter here; ownership gating happens downstream in ReceiveMarqueeSelection).
		// Only COUNT non-selectable, and only when diagnostics are on.
		if (bDebugLog && Sim)
		{
			const FSeinEntity* Entity = Sim->GetEntity(Handle);
			if (Entity && !Entity->IsSelectable()) { ++NumNonSelectable; }
		}

		// Shared with the Sein.Marquee.Debug.Show overlay: the drawn polygon is
		// exactly the one SAT-tested here. WorldPts is reused as scratch.
		if (!SeinComputeActorScreenHull(this, Actor, Sim, Handle, WorldPts, ScreenPts, Hull))
		{
			continue;
		}
		++NumProjected;

		const bool bHit = SeinConvexHullIntersectsBox2D(Hull, Rect);

		// Log the first few candidates so we can compare projected screen coords
		// against the marquee rect directly (catches any coordinate-space surprise).
		if (bDebugLog && NumSampled < 4)
		{
			++NumSampled;
			FBox2D HullBox(ForceInit);
			for (const FVector2D& S : ScreenPts) { HullBox += S; }
			UE_LOG(LogTemp, Warning,
				TEXT("[Marquee] '%s' src=%d scr=%d hull=%d hullBox=(%.0f,%.0f)-(%.0f,%.0f) hit=%d"),
				*Actor->GetName(), WorldPts.Num(), ScreenPts.Num(), Hull.Num(),
				HullBox.Min.X, HullBox.Min.Y, HullBox.Max.X, HullBox.Max.Y, bHit ? 1 : 0);
		}

		if (bHit)
		{
			OutActors.Add(Actor);
			++NumHit;
		}
	}

	if (bDebugLog)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Marquee] Rect=(%.0f,%.0f)-(%.0f,%.0f) iter=%d valid=%d nonSel=%d projected=%d HIT=%d"),
			Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y,
			NumIterated, NumValid, NumNonSelectable, NumProjected, NumHit);
	}
}

// Draw, for every selectable unit, the exact screen-space polygon the marquee
// tests against (red outline) plus the raw projected extent samples (amber
// dots). Always-on while Sein.Marquee.Debug.Show is 1 — no drag required — so
// you can line a unit's tested silhouette up against the cyan marquee box and
// see precisely what it does (and doesn't) cover.
void ASeinHUD::DrawMarqueeDebugHulls()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();

	TArray<FVector>   WorldPts;
	TArray<FVector2D> ScreenPts;
	TArray<FVector2D> Hull;

	const FLinearColor HullColor(1.0f, 0.0f, 0.0f, 0.9f);    // red — the SAT-tested polygon
	const FLinearColor PointColor(1.0f, 0.8f, 0.0f, 0.9f);   // amber — raw projected samples
	const float HullThickness = 1.5f;
	const float PointSize = 3.0f;

	for (TActorIterator<ASeinActor> It(World); It; ++It)
	{
		ASeinActor* Actor = *It;
		if (!Actor || !Actor->HasValidEntity())
		{
			continue;
		}

		const FSeinEntityHandle Handle = Actor->GetEntityHandle();
		if (!SeinComputeActorScreenHull(this, Actor, Sim, Handle, WorldPts, ScreenPts, Hull))
		{
			continue;
		}

		// Hull outline — close the loop (i -> i+1, last -> first).
		const int32 N = Hull.Num();
		for (int32 i = 0; N >= 2 && i < N; ++i)
		{
			const FVector2D& A = Hull[i];
			const FVector2D& B = Hull[(i + 1) % N];
			DrawLine(A.X, A.Y, B.X, B.Y, HullColor, HullThickness);
		}

		// Raw projected samples, so degenerate/edge-on silhouettes are visible too.
		for (const FVector2D& P : ScreenPts)
		{
			DrawRect(PointColor, P.X - PointSize * 0.5f, P.Y - PointSize * 0.5f, PointSize, PointSize);
		}
	}
}

void ASeinHUD::ResolveMarqueeSelection()
{
	ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	TArray<ASeinActor*> ActorsInBox;
	CollectActorsInMarquee(PC->MarqueeStart, PC->MarqueeCurrent, ActorsInBox);

	// Opt-in: marquee corners (mouse space) + how many the collector returned.
	// Compare 'collected' against the final "Selected" count: if collected > 0 but
	// nothing selects, the ownership filter in ReceiveMarqueeSelection is the gate.
	if (GSeinMarqueeDebugLog)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Marquee] corners start=(%.0f,%.0f) cur=(%.0f,%.0f) -> collected=%d"),
			PC->MarqueeStart.X, PC->MarqueeStart.Y, PC->MarqueeCurrent.X, PC->MarqueeCurrent.Y, ActorsInBox.Num());
	}

	PC->ReceiveMarqueeSelection(ActorsInBox);
}

void ASeinHUD::DrawCommandDragLine()
{
	const ASeinPlayerController* PC = GetSeinPlayerController();
	if (!PC)
	{
		return;
	}

	const FVector& WorldStart = PC->CommandDragStart;
	const FVector& WorldEnd = PC->CommandDragCurrent;

	// Screen-space formation line
	FVector2D ScreenStart, ScreenEnd;
	if (!PC->ProjectWorldLocationToScreen(WorldStart, ScreenStart))
	{
		return;
	}
	if (!PC->ProjectWorldLocationToScreen(WorldEnd, ScreenEnd))
	{
		return;
	}

	if (Canvas)
	{
		FCanvasLineItem LineItem(FVector2D(ScreenStart.X, ScreenStart.Y), FVector2D(ScreenEnd.X, ScreenEnd.Y));
		LineItem.SetColor(CommandDragLineColor);
		LineItem.LineThickness = CommandDragLineThickness;
		Canvas->DrawItem(LineItem);
	}

	// World-space formation facing arrow at the midpoint.
	// The formation line defines the line units spread along.
	// The facing direction is perpendicular to this (cross with world up).
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector FormationDir = WorldEnd - WorldStart;
	const float FormationLength = FormationDir.Size();
	if (FormationLength < 10.0f)
	{
		return; // Too short to determine facing
	}

	const FVector FormationDirNorm = FormationDir / FormationLength;
	// Perpendicular facing direction: cross formation line with world up.
	// This gives the "forward" direction the army should face.
	const FVector FacingDir = FVector::CrossProduct(FormationDirNorm, FVector::UpVector).GetSafeNormal();

	const FVector Midpoint = (WorldStart + WorldEnd) * 0.5f;
	const float ArrowLength = FMath::Clamp(FormationLength * 0.15f, 30.0f, 120.0f);

	DrawDebugDirectionalArrow(World, Midpoint, Midpoint + FacingDir * ArrowLength,
		40.0f, FColor(0, 255, 80), false, 0.0f, SDPG_World, 8.0f);
}

// ==================== Debug Command Log Panel ====================

// Truncate a string with an ellipsis so its rendered width fits within
// `MaxPixelWidth` at the given font scale. Binary-searches the longest prefix
// that still fits when "..." is appended. Used by the command log overlay so
// long descriptions (e.g. broker orders with positions, camera updates) don't
// run past the panel's right edge.
static FString TruncateTextToWidth(const FString& Source, UFont* Font, float Scale, float MaxPixelWidth)
{
	if (!Font || MaxPixelWidth <= 0.f) return Source;

	const float SourceWidth = static_cast<float>(Font->GetStringSize(*Source)) * Scale;
	if (SourceWidth <= MaxPixelWidth) return Source;

	const FString Ellipsis(TEXT("..."));
	const float EllipsisWidth = static_cast<float>(Font->GetStringSize(*Ellipsis)) * Scale;
	if (EllipsisWidth >= MaxPixelWidth) return FString();

	// Binary search the longest prefix length such that prefix+"..." fits.
	int32 Lo = 0;
	int32 Hi = Source.Len();
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi + 1) / 2;
		const FString Candidate = Source.Left(Mid) + Ellipsis;
		const float W = static_cast<float>(Font->GetStringSize(*Candidate)) * Scale;
		if (W <= MaxPixelWidth) Lo = Mid;
		else                    Hi = Mid - 1;
	}
	return Source.Left(Lo) + Ellipsis;
}

void ASeinHUD::DrawCommandLogPanel()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}

	USeinCommandLogSubsystem* LogSub = World->GetSubsystem<USeinCommandLogSubsystem>();
	if (!LogSub || !LogSub->bShowOverlay)
	{
		return;
	}

	const TArray<FSeinCommandLogEntry>& Entries = LogSub->GetLogEntries();
	const int32 MaxDisplay = LogSub->MaxDisplayEntries;

	// Get the default engine font
	UFont* Font = GEngine->GetSmallFont();
	if (!Font)
	{
		return;
	}

	const float FontScale = LogFontScale;
	const float LineHeight = Font->GetMaxCharHeight() * FontScale + 2.0f;

	// Panel dimensions
	const float PanelWidth = Canvas->ClipX * 0.45f;  // 45% of screen width
	const float PanelPadding = 8.0f;
	const float HeaderHeight = LineHeight + 4.0f;

	// Available pixel width for text inside the panel after padding on both sides.
	// Subtract a small safety margin so descenders / italic strokes don't kiss
	// the right border.
	const float TextMaxWidth = FMath::Max(0.f, PanelWidth - (PanelPadding * 2.0f) - 4.0f);

	// How many entries to show
	const int32 StartIndex = FMath::Max(0, Entries.Num() - MaxDisplay);
	const int32 EntryCount = Entries.Num() - StartIndex;
	const float PanelHeight = HeaderHeight + (EntryCount * LineHeight) + PanelPadding * 2.0f;

	// Position: top-left with margin
	const float PanelX = 10.0f;
	const float PanelY = 10.0f;

	// Draw background
	DrawRect(LogPanelBgColor, PanelX, PanelY, PanelWidth, PanelHeight);

	// Draw border
	const FLinearColor BorderColor(0.3f, 0.6f, 1.0f, 0.6f);
	const float BT = 1.0f;
	DrawRect(BorderColor, PanelX, PanelY, PanelWidth, BT);                         // Top
	DrawRect(BorderColor, PanelX, PanelY + PanelHeight - BT, PanelWidth, BT);      // Bottom
	DrawRect(BorderColor, PanelX, PanelY, BT, PanelHeight);                         // Left
	DrawRect(BorderColor, PanelX + PanelWidth - BT, PanelY, BT, PanelHeight);       // Right

	// Header
	const USeinWorldSubsystem* SimSub = World->GetSubsystem<USeinWorldSubsystem>();
	const int32 CurrentTick = SimSub ? SimSub->GetCurrentTick() : -1;

	const FString HeaderRaw = FString::Printf(TEXT(" COMMAND LOG  |  Tick %d  |  %d entries"),
		CurrentTick, Entries.Num());
	const FString Header = TruncateTextToWidth(HeaderRaw, Font, FontScale, TextMaxWidth);

	float TextX = PanelX + PanelPadding;
	float TextY = PanelY + PanelPadding;

	// Draw header text
	{
		FCanvasTextItem HeaderItem(FVector2D(TextX, TextY), FText::FromString(Header), Font, FLinearColor(0.4f, 0.8f, 1.0f, 1.0f));
		HeaderItem.Scale = FVector2D(FontScale, FontScale);
		HeaderItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(HeaderItem);
	}

	// Separator line
	TextY += HeaderHeight;
	DrawRect(BorderColor, PanelX + PanelPadding, TextY - 2.0f, PanelWidth - PanelPadding * 2.0f, 1.0f);

	// Draw entries
	for (int32 i = StartIndex; i < Entries.Num(); ++i)
	{
		const FSeinCommandLogEntry& Entry = Entries[i];

		// Leader column: single-entity commands show "E<index>" (e.g. "E012");
		// list commands (BrokerOrder, SelectionChanged) show "E×N" (e.g. "E×3").
		// Falls back to "E---" when neither applies — rare, but distinguishable
		// from a real index of zero.
		FString EntityCol;
		if (Entry.EntityIndex >= 0)
		{
			EntityCol = FString::Printf(TEXT("E%03d"), Entry.EntityIndex);
		}
		else if (Entry.MemberCount > 0)
		{
			EntityCol = FString::Printf(TEXT("E×%-2d"), Entry.MemberCount);
		}
		else
		{
			EntityCol = TEXT("E---");
		}

		// Substitute the BrokerOrder description's `{R}` placeholder with the
		// resolved abilities (tag + display name) once the dispatch has fired.
		// Pre-resolution it renders empty; post-resolution it expands to
		// "Resolved to <Tag> (<Name>), <Tag> (<Name>) ". DisplayName is
		// pulled from USeinAbility::AbilityName at dispatch time; falls back
		// to the tag's leaf when the BP didn't author one.
		// Non-broker entries don't carry the placeholder — ReplaceInline is a no-op.
		FString Description = Entry.Description;
		if (Description.Contains(TEXT("{R}")))
		{
			FString ResolvedSegment;
			if (Entry.ResolvedAbilities.Num() > 0)
			{
				TArray<FString> Pieces;
				Pieces.Reserve(Entry.ResolvedAbilities.Num());
				for (const FSeinBrokerResolvedAbility& R : Entry.ResolvedAbilities)
				{
					if (R.DisplayName.IsEmpty())
					{
						Pieces.Add(R.Tag.ToString());
					}
					else
					{
						Pieces.Add(FString::Printf(TEXT("%s (%s)"), *R.Tag.ToString(), *R.DisplayName));
					}
				}
				ResolvedSegment = FString::Printf(TEXT("Resolved to %s "), *FString::Join(Pieces, TEXT(", ")));
			}
			Description.ReplaceInline(TEXT("{R}"), *ResolvedSegment);
		}

		const FString LineRaw = FString::Printf(TEXT("[T%04d] P%d %s  %s"),
			Entry.Tick, Entry.PlayerIndex, *EntityCol, *Description);
		const FString Line = TruncateTextToWidth(LineRaw, Font, FontScale, TextMaxWidth);

		FLinearColor LineColor = FLinearColor(
			Entry.DisplayColor.R / 255.0f,
			Entry.DisplayColor.G / 255.0f,
			Entry.DisplayColor.B / 255.0f,
			1.0f);

		FCanvasTextItem TextItem(FVector2D(TextX, TextY), FText::FromString(Line), Font, LineColor);
		TextItem.Scale = FVector2D(FontScale, FontScale);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);

		TextY += LineHeight;
	}
}

ASeinPlayerController* ASeinHUD::GetSeinPlayerController() const
{
	return Cast<ASeinPlayerController>(GetOwningPlayerController());
}
