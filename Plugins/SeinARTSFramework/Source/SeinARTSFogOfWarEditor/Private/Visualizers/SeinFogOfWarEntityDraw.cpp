/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarEntityDraw.cpp
 *
 * Vision-stamp draw layer for the entity-bridge visualizer. Registered via
 * FSeinARTSEditorModule's draw callback registry from SeinARTSFogOfWarEditor's
 * StartupModule.
 *
 * Unified cyan for all vision-stamp shapes — radial / conical / rect share
 * the same hue since they all represent "this entity sees here." The shape
 * itself communicates the geometry; color carries only the layer-meaning
 * ("vision"). Disabled stamps grey out so authoring-time toggles read as
 * visually muted without losing the outline.
 */

#include "Visualizers/SeinFogOfWarEntityDraw.h"

#include "Components/SeinVisionPayload.h"
#include "Stamping/SeinStampShape.h"

#include "SceneManagement.h"          // FPrimitiveDrawInterface, DrawCircle, DrawOrientedWireBox
#include "StructUtils/InstancedStruct.h"

namespace SeinFogOfWarEntityDrawLocal
{
	/** Draw a planar wire circle (XY) centred at `Center`. UE's stock
	 *  DrawCircle signature in 5.7 takes (PDI, Base, X, Y, Color, Radius,
	 *  NumSides, DPG, Thickness). */
	static void DrawWireCircleXY(FPrimitiveDrawInterface* PDI,
		const FVector& Center, float Radius, int32 NumSides,
		FColor Color, uint8 DepthPriority, float Thickness)
	{
		DrawCircle(PDI, Center, FVector::ForwardVector, FVector::RightVector,
			Color, Radius, NumSides, DepthPriority, Thickness);
	}

	/** Draw a cone wedge in the XY plane: two edges from `Apex` along
	 *  ±HalfAngle of `Forward`, plus the far arc (round) or chord (flat).
	 *  Drawn as line segments — no fill. */
	static void DrawWireCone2D(FPrimitiveDrawInterface* PDI,
		const FVector& Apex, const FVector& Forward, const FVector& Right,
		float HalfAngleRad, float Length, bool bRoundFarEdge,
		int32 ArcSegments, FColor Color, uint8 DepthPriority, float Thickness)
	{
		const float SinH = FMath::Sin(HalfAngleRad);
		const float CosH = FMath::Cos(HalfAngleRad);

		// Edge endpoints. Right-side at +HalfAngle, left-side at -HalfAngle.
		// For round far edge, edges terminate at radius=Length on the arc;
		// for flat far edge, they terminate at forward projection=Length
		// (i.e., t = Length / CosH along the unit edge direction).
		const float EdgeLen = bRoundFarEdge ? Length : (Length / FMath::Max(CosH, KINDA_SMALL_NUMBER));
		const FVector EdgeR = Apex + (Forward * CosH + Right * SinH) * EdgeLen;
		const FVector EdgeL = Apex + (Forward * CosH - Right * SinH) * EdgeLen;

		PDI->DrawLine(Apex, EdgeR, Color, DepthPriority, Thickness);
		PDI->DrawLine(Apex, EdgeL, Color, DepthPriority, Thickness);

		if (bRoundFarEdge)
		{
			// Arc from EdgeL (−HalfAngle) sweeping to EdgeR (+HalfAngle).
			const int32 Segs = FMath::Max(2, ArcSegments);
			FVector Prev = EdgeL;
			for (int32 i = 1; i <= Segs; ++i)
			{
				const float T = static_cast<float>(i) / static_cast<float>(Segs);
				const float Angle = -HalfAngleRad + 2.0f * HalfAngleRad * T;
				const FVector P = Apex + (Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * Length;
				PDI->DrawLine(Prev, P, Color, DepthPriority, Thickness);
				Prev = P;
			}
		}
		else
		{
			// Straight chord — flat "pie slice" far edge.
			PDI->DrawLine(EdgeL, EdgeR, Color, DepthPriority, Thickness);
		}
	}

	/** Pull a stamp's world-space pose off the actor + LocalOffset/YawOffset. */
	static void ComputeStampWorldPose(
		const FSeinStampShape& Stamp,
		const FQuat& ActorQuat, const FVector& ActorPos,
		FVector& OutPos, FQuat& OutQuat,
		FVector& OutForward, FVector& OutRight)
	{
		const FVector LocalOffset(
			Stamp.LocalOffset.X.ToFloat(),
			Stamp.LocalOffset.Y.ToFloat(),
			Stamp.LocalOffset.Z.ToFloat());
		OutPos = ActorPos + ActorQuat.RotateVector(LocalOffset);

		const float YawOffsetDeg = Stamp.YawOffsetDegrees.ToFloat();
		const FQuat YawOffsetQuat(FVector::UpVector, FMath::DegreesToRadians(YawOffsetDeg));
		OutQuat = ActorQuat * YawOffsetQuat;
		OutForward = OutQuat.GetForwardVector();
		OutRight = OutQuat.GetRightVector();
	}
} // namespace SeinFogOfWarEntityDrawLocal

void SeinFogOfWarEntityDraw::DrawVisionStamps(
	const TArray<FInstancedStruct>& ComponentData,
	const FQuat& ActorQuat, const FVector& ActorPos,
	FPrimitiveDrawInterface* PDI)
{
	// One-shot diagnostic: fires the first time this function executes after
	// a fresh editor load. If you don't see this log line at all, the callback
	// was never registered (check the editor module startup log for the
	// "[SeinARTSFogOfWarEditor] Registered vision-stamp draw callback" line).
	// If you DO see this but no viz, the issue is downstream (color, no
	// stamps in ComponentData, etc.).
	static bool bLoggedOnce = false;
	if (!bLoggedOnce)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[VisionStampViz] first call — ComponentData has %d entries, ActorPos=(%.1f,%.1f,%.1f)"),
			ComponentData.Num(), ActorPos.X, ActorPos.Y, ActorPos.Z);
		bLoggedOnce = true;
	}

	const FColor StampColor    = FColor(  0, 220, 220);
	const FColor DisabledColor = FColor(120, 120, 120);
	const float Thickness   = 1.5f;
	const int32 CircleSides = 32;
	const int32 ArcSegments = 16;

	for (const FInstancedStruct& Entry : ComponentData)
	{
		if (!Entry.IsValid()) continue;
		if (Entry.GetScriptStruct() != FSeinVisionPayload::StaticStruct()) continue;

		const FSeinVisionPayload& Data = Entry.Get<FSeinVisionPayload>();
		if (Data.VisionStamps.Num() == 0) continue;

		for (const FSeinVisionStamp& VStamp : Data.VisionStamps)
		{
			const FSeinStampShape& Shape = VStamp.Shape;

			FVector StampPos;
			FQuat   StampQuat;
			FVector Forward;
			FVector Right;
			SeinFogOfWarEntityDrawLocal::ComputeStampWorldPose(
				Shape, ActorQuat, ActorPos, StampPos, StampQuat, Forward, Right);

			const FColor ActiveColor = Shape.bEnabled ? StampColor : DisabledColor;

			switch (Shape.Shape)
			{
			case ESeinStampShape::Radial:
			{
				const float Radius = FMath::Max(0.0f, Shape.Radius.ToFloat());
				if (Radius <= 0.0f) break;
				SeinFogOfWarEntityDrawLocal::DrawWireCircleXY(
					PDI, StampPos, Radius, CircleSides,
					ActiveColor, SDPG_World, Thickness);
				break;
			}

			case ESeinStampShape::Rect:
			{
				const FVector AxisZ = StampQuat.GetUpVector();
				const FVector HalfExtents(
					FMath::Max(0.0f, Shape.HalfExtentX.ToFloat()),
					FMath::Max(0.0f, Shape.HalfExtentY.ToFloat()),
					1.0f);  // 1cm height — viz only, planar stamp
				if (HalfExtents.X <= 0.0f && HalfExtents.Y <= 0.0f) break;
				DrawOrientedWireBox(
					PDI,
					StampPos,
					Forward, Right, AxisZ,
					HalfExtents,
					ActiveColor,
					SDPG_World,
					Thickness);
				break;
			}

			case ESeinStampShape::Conical:
			{
				const float TotalAngleDeg = FMath::Clamp(Shape.ConeAngleDegrees.ToFloat(), 0.0f, 180.0f);
				const float HalfAngleRad = FMath::DegreesToRadians(TotalAngleDeg * 0.5f);
				const float Length = FMath::Max(0.0f, Shape.ConeLength.ToFloat());
				if (HalfAngleRad <= 0.0f || Length <= 0.0f) break;
				SeinFogOfWarEntityDrawLocal::DrawWireCone2D(
					PDI, StampPos, Forward, Right,
					HalfAngleRad, Length, Shape.bConeRoundEdge,
					ArcSegments, ActiveColor, SDPG_World, Thickness);
				break;
			}
			}
		}
	}
}
