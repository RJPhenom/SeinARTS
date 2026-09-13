/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinNavPathDebugDraw.cpp
 * @author       RJ Macklem
 * @created      04 Sep 2026
 * @latest       07 Sep 2026
 * @brief        Current-view navigation cells and supplementary driven-route diagnostics.
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#include "Debug/SeinNavPathDebugDraw.h"
#include "Debug/SeinDebugLegend.h"
#include "EngineDefines.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinNavPathDebug.h"
#include "Debug/SeinMovementDebugCanvas.h"
#include "Debug/DebugDrawService.h"
#include "Actions/SeinMoveToAction.h"
#include "Actor/SeinActor.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Movement/SeinMovement.h"
#include "SeinMovementSubsystem.h"
#include "SeinNavigationSubsystem.h"
#include "SeinNavigation.h"
#include "Settings/PluginSettings.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "CanvasItem.h"
#include "Engine/Texture.h"
#include "SceneView.h"
#include "SceneInterface.h"
#include "HAL/IConsoleManager.h"
#include "Types/Entity.h"

namespace UE::SeinARTSMovement::NavDebug
{
	namespace
	{
		FDelegateHandle DrawHandle;
		TAutoConsoleVariable<int32> RawCells(TEXT("Sein.Nav.Show.RawCells"), 1,
			TEXT("Show filled yellow A* planned cells and the blue final cell (default on). Full pre-smoothing plan, not remaining motion; may be unavailable after restore or custom planning."));
		const FLinearColor Yellow(1.0f, 0.85f, 0.0f);
		const FLinearColor Orange(1.0f, 0.35f, 0.0f);
		const FLinearColor Cyan(0.0f, 0.8f, 1.0f);
		const FLinearColor Blue(0.15f, 0.45f, 1.0f);
		const FLinearColor Gray(0.6f, 0.6f, 0.6f);

		using namespace UE::SeinARTSMovement::DebugCanvas;

		void Draw(UCanvas* Canvas, APlayerController*)
		{
			if (!Canvas || !Canvas->SceneView || !Canvas->SceneView->Family || !Canvas->SceneView->Family->Scene) return;
			UWorld* World = Canvas->SceneView->Family->Scene->GetWorld();
			if (!World) return;
			if (!World->IsGameWorld())
			{
				const UE::SeinARTS::DebugLegend::FPanel Panel(Canvas, UE::SeinARTS::DebugLegend::EPanel::Navigation);
				Panel.Text(8, TEXT("Navigation: baked grid preview"), FLinearColor(0.2f, 1, 0.3f));
				Panel.TextRuns(24, { { TEXT("Green: walkable cells"), FLinearColor(0.2f, 1, 0.3f) },
					{ TEXT("   Red: blocked cells"), FLinearColor(1, 0.15f, 0.15f) } });
				Panel.Text(40, TEXT("Terrain types use their configured debug colors"), FLinearColor::White);
				Panel.Text(56, TEXT("Editor: cells come from the level volume's baked navigation data"), FLinearColor::White);
				Panel.Text(72, TEXT("Dynamic blockers and active paths appear during play"), FLinearColor::White);
				return;
			}
			const USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
			if (!Sim) return;
			const USeinActorBridgeSubsystem* Bridge = World->GetSubsystem<USeinActorBridgeSubsystem>();
			const USeinMovementSubsystem* Movement = World->GetSubsystem<USeinMovementSubsystem>();
			const USeinNavigation* Navigation = USeinNavigationSubsystem::GetNavigationForWorld(World);
			const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
			const FSceneView& View = *Canvas->SceneView;
			TArray<const USeinMoveToAction*> Actions;
			CollectActions(*Sim, Actions);
			struct FCandidate { const USeinMoveToAction* Action; FVector Position; FVector SimPosition; double Distance; };
			TArray<FCandidate> Candidates;
			for (const USeinMoveToAction* Action : Actions)
			{
				const FVector SimPosition = Sim->GetEntity(Action->OwnerEntity)->Transform.GetLocation().ToVector();
				const ASeinActor* Actor = Bridge ? Bridge->GetActorForEntity(Action->OwnerEntity) : nullptr;
				const FVector Position = IsValid(Actor) ? Actor->GetActorLocation() : SimPosition;
				const double Distance = FVector::DistSquared(Position, View.ViewLocation);
				if (Distance > FMath::Square(double(Settings->DebugDrawMaxDistance))) continue;
				if (Settings->bDebugDrawFrustumCullEnabled && !View.ViewFrustum.IntersectSphere(Position, 100.0)) continue;
				Candidates.Add({Action, Position, SimPosition, Distance});
			}
			Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Distance < B.Distance; });
			const int32 Limit = FMath::Clamp(Settings->DebugDrawMaxEntities, 0, Candidates.Num());
			int32 Drawn = 0, MissingRaw = 0;
			const bool bRaw = RawCells.GetValueOnGameThread() != 0;
			const int32 SteeringFlag = FEngineShowFlags::FindIndexByName(TEXT("SeinSteering"));
			const bool bSteeringVisible = SteeringFlag != INDEX_NONE && View.Family->EngineShowFlags.GetSingleFlag(SteeringFlag);
			const FVector Lift(0, 0, 8);
			for (int32 I = 0; I < Candidates.Num() && Drawn < Limit; ++I)
			{
				const FCandidate& C = Candidates[I];
				FSeinPath Scratch;
				const FSeinPath& Path = C.Action->GetDrivenPath(Scratch);
				const USeinMovement* Driver = Movement ? Movement->FindMovementInstance(C.Action->OwnerEntity) : nullptr;
				const int32 SegmentIndex = Driver && C.Action->GetStuckPhase() != ESeinMoveStuckPhase::Escaping
					? Driver->GetDebugDrivenSegmentIndex() : INDEX_NONE;
				const FRoute Route = BuildRoute(Path, C.Action->GetCurrentWaypointIndex(), SegmentIndex,
					C.Action->GetDrivenPathOrigin().ToVector(), C.Position);
				if (!Route.bValid) continue;
				++Drawn;
				if (bRaw)
				{
					const double HalfExtent = Navigation ? Navigation->GetCellSize().ToFloat() * 0.5 * 0.9 * 0.95 : 0.0;
					if (Path.DebugCellPath.IsEmpty() || HalfExtent <= 0.0) ++MissingRaw;
					else for (int32 CellIndex = 0; CellIndex < Path.DebugCellPath.Num(); ++CellIndex)
					{
						// Exactly the captured A* cells, never invented by rasterizing a smoothed line.
						const bool bLast = CellIndex == Path.DebugCellPath.Num() - 1;
						FilledCell(*Canvas, Path.DebugCellPath[CellIndex].ToVector() + FVector(0, 0, 15),
							HalfExtent, bLast ? Blue : Yellow);
					}
				}
				for (const FRouteLine& Line : Route.Lines)
					WorldLine(*Canvas, Line.From + Lift, Line.To + Lift, Line.bReverse ? Orange : Yellow, 2.5f);
				// The target is a waypoint/segment end, never advertised as a steering carrot.
				WorldLine(*Canvas, C.Position + Lift, Route.Target + Lift, Cyan, 1.0f);
				Marker(*Canvas, Route.Target + Lift, Cyan, 3);
				Marker(*Canvas, Route.Endpoint + Lift, Route.bPartial ? Orange : Blue, 7);
				const FVector Goal = C.Action->GetOrderDestination().ToVector();
				if (!Goal.Equals(Route.Endpoint, 0.1)) Marker(*Canvas, Goal + Lift, Blue, 10);
				// Heading and motion belong to the Steering view; navigation only
				// shows where the rendered actor sits relative to its sim pose.
				if (!bSteeringVisible)
				{
					WorldLine(*Canvas, C.Position + Lift, C.SimPosition + Lift, Gray, 1);
					Marker(*Canvas, C.SimPosition + Lift, Gray, 2);
				}
			}
			// Keep the movement-stack key legible over the grid and clear of the
			// upper-left selection diagnostics. It is local to this viewport.
			const UE::SeinARTS::DebugLegend::FPanel Panel(Canvas, UE::SeinARTS::DebugLegend::EPanel::Navigation);
			auto Legend = [&](float Y, const FString& Text, const FLinearColor& Color)
			{
				Panel.Text(Y - 16, Text, Color);
			};
			Legend(24, FString::Printf(TEXT("Navigation: %d / %d moves | view-limited, drawn through terrain"), Drawn, Actions.Num()), FLinearColor(0.2f, 1, 0.3f));
			Legend(40, TEXT("Yellow cells: full A* plan   Yellow line / curve: remaining smoothed route"), Yellow);
			Panel.TextRuns(40, { { TEXT("Blue: final A* cell / route end / goal"), Blue },
				{ TEXT("   Cyan: next target (not steering)"), Cyan } });
			Legend(72, bSteeringVisible ? TEXT("Gray sim offset: shown once by Steering")
				: TEXT("Gray: sim pose / interpolation offset"), Gray);
			Legend(88, TEXT("Orange: reverse segment / partial route endpoint"), Orange);
			Legend(104, FString::Printf(TEXT("A* cells %s; unavailable for %d routes (restore / custom planner)"),
				bRaw ? TEXT("on") : TEXT("off"), MissingRaw), Orange);
		}
	}

	void RegisterDraw()
	{
		if (!DrawHandle.IsValid()) DrawHandle = UDebugDrawService::Register(TEXT("SeinNavigation"), FDebugDrawDelegate::CreateStatic(&Draw));
	}
	void UnregisterDraw()
	{
		if (DrawHandle.IsValid()) UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}
}
#else
namespace UE::SeinARTSMovement::NavDebug
{
	void RegisterDraw() {}
	void UnregisterDraw() {}
}
#endif
