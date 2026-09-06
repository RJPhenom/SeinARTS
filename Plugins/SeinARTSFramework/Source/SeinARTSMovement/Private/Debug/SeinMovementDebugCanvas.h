/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinMovementDebugCanvas.h
 * @author       RJ Macklem
 * @created      05 Sep 2026
 * @latest       05 Sep 2026
 * @brief        Clipped current-view Canvas primitives shared by navigation and steering.
 * @disclaimer   This code was generated in whole or in part with the assistance of an AI language model.
 */
#pragma once
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Texture.h"
#include "SceneView.h"
namespace UE::SeinARTSMovement::DebugCanvas
{
	inline bool Project(UCanvas& Canvas, const FVector& Point, FVector2D& Out)
		{
			if (Canvas.SceneView->WorldToScreen(Point).W <= 0.01) return false;
			const FVector P = Canvas.Project(Point);
			Out = FVector2D(P.X, P.Y);
			return !Out.ContainsNaN();
		}

	inline void FilledCell(UCanvas& Canvas, const FVector& Center, double HalfExtent, const FLinearColor& Color)
		{
			// Clip the quad before projection, then clip its screen polygon. Cells that
			// cross the camera plane must not explode into full-screen triangles.
			TArray<FVector> WorldPolygon = {
				Center + FVector(-HalfExtent, -HalfExtent, 0), Center + FVector(HalfExtent, -HalfExtent, 0),
				Center + FVector(HalfExtent, HalfExtent, 0), Center + FVector(-HalfExtent, HalfExtent, 0)};
			TArray<FVector> NearClipped;
			FVector Previous = WorldPolygon.Last();
			double PreviousW = Canvas.SceneView->WorldToScreen(Previous).W;
			for (const FVector& Current : WorldPolygon)
			{
				const double W = Canvas.SceneView->WorldToScreen(Current).W;
				if ((W >= 0.02) != (PreviousW >= 0.02))
					NearClipped.Add(FMath::Lerp(Previous, Current, (0.02 - PreviousW) / (W - PreviousW)));
				if (W >= 0.02) NearClipped.Add(Current);
				Previous = Current;
				PreviousW = W;
			}
			TArray<FVector2D> Polygon;
			for (const FVector& Point : NearClipped)
			{
				FVector2D P;
				if (!Project(Canvas, Point, P)) return;
				Polygon.Add(P);
			}
			auto Clip = [&](int32 Axis, double Bound, double Sign)
			{
				if (Polygon.IsEmpty()) return;
				TArray<FVector2D> Result;
				FVector2D A = Polygon.Last();
				double DA = Sign * (A[Axis] - Bound);
				for (const FVector2D& B : Polygon)
				{
					const double DB = Sign * (B[Axis] - Bound);
					if ((DA >= 0) != (DB >= 0)) Result.Add(FMath::Lerp(A, B, DA / (DA - DB)));
					if (DB >= 0) Result.Add(B);
					A = B;
					DA = DB;
				}
				Polygon = MoveTemp(Result);
			};
			Clip(0, 0, 1); Clip(0, Canvas.ClipX, -1);
			Clip(1, 0, 1); Clip(1, Canvas.ClipY, -1);
			for (int32 I = 1; I + 1 < Polygon.Num(); ++I)
			{
				FCanvasTriangleItem Item(Polygon[0], Polygon[I], Polygon[I + 1], GWhiteTexture);
				Item.SetColor(Color);
				Canvas.DrawItem(Item);
			}
		}

	inline void ScreenLine(UCanvas& Canvas, const FVector2D& From, const FVector2D& To,
			const FLinearColor& Color, float Thickness)
		{
			// Liang-Barsky clip prevents huge off-screen coordinates near the camera plane.
			const FVector2D Delta = To - From;
			double Low = 0.0, High = 1.0;
			auto Clip = [&](double P, double Q)
			{
				if (FMath::Abs(P) < UE_SMALL_NUMBER) return Q >= 0.0;
				const double R = Q / P;
				if (P < 0.0) Low = FMath::Max(Low, R); else High = FMath::Min(High, R);
				return Low <= High;
			};
			if (!Clip(-Delta.X, From.X) || !Clip(Delta.X, Canvas.ClipX - From.X)
				|| !Clip(-Delta.Y, From.Y) || !Clip(Delta.Y, Canvas.ClipY - From.Y)) return;
			FCanvasLineItem Item(From + Low * Delta, From + High * Delta);
			Item.SetColor(Color);
			Item.LineThickness = Thickness;
			Canvas.DrawItem(Item);
		}

	inline void WorldLine(UCanvas& Canvas, FVector From, FVector To, const FLinearColor& Color, float Thickness = 2.0f)
		{
			// Clip the line in world space at positive homogeneous W before projection.
			const double A = Canvas.SceneView->WorldToScreen(From).W;
			const double B = Canvas.SceneView->WorldToScreen(To).W;
			constexpr double NearW = 0.02;
			if (A < NearW && B < NearW) return;
			if (A < NearW) From = FMath::Lerp(From, To, (NearW - A) / (B - A));
			else if (B < NearW) To = FMath::Lerp(From, To, (NearW - A) / (B - A));
			FVector2D P, Q;
			if (Project(Canvas, From, P) && Project(Canvas, To, Q)) ScreenLine(Canvas, P, Q, Color, Thickness);
		}

	inline void Marker(UCanvas& Canvas, const FVector& Point, const FLinearColor& Color, double Radius = 5.0)
		{
			FVector2D P;
			if (!Project(Canvas, Point, P)) return;
			ScreenLine(Canvas, P + FVector2D(-Radius, 0), P + FVector2D(0, -Radius), Color, 2);
			ScreenLine(Canvas, P + FVector2D(0, -Radius), P + FVector2D(Radius, 0), Color, 2);
			ScreenLine(Canvas, P + FVector2D(Radius, 0), P + FVector2D(0, Radius), Color, 2);
			ScreenLine(Canvas, P + FVector2D(0, Radius), P + FVector2D(-Radius, 0), Color, 2);
		}

	inline void Arrow(UCanvas& Canvas, const FVector& From, const FVector& To, const FLinearColor& Color)
		{
			WorldLine(Canvas, From, To, Color);
			FVector2D P, Q;
			if (!Project(Canvas, From, P) || !Project(Canvas, To, Q)) return;
			const FVector2D D = (Q - P).GetSafeNormal();
			const FVector2D N(-D.Y, D.X);
			ScreenLine(Canvas, Q, Q - D * 8 + N * 4, Color, 2);
			ScreenLine(Canvas, Q, Q - D * 8 - N * 4, Color, 2);
		}

}
