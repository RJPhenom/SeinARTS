/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinSteeringDebugDraw.cpp
 * @author       RJ Macklem
 * @created      05 Sep 2026
 * @latest       05 Sep 2026
 * @brief        Per-view steering requests, driver velocity, and settled motion diagnostics.
 * @disclaimer   This code was generated in whole or in part with the assistance of an AI language model.
 */
#include "Debug/SeinSteeringDebugDraw.h"
#include "EngineDefines.h"
#if UE_ENABLE_DEBUG_DRAWING
#include "Debug/SeinMovementDebugCanvas.h"
#include "Debug/SeinDebugLegend.h"
#include "Debug/SeinSteeringDebugSelection.h"
#include "Debug/DebugDrawService.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinExtentsHelpers.h"
#include "Components/SeinContainmentMemberData.h"
#include "Movement/SeinMovement.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "SceneInterface.h"
#include "Types/Entity.h"

namespace UE::SeinARTSMovement
{
	FSeinSteeringDebugSelectionQuery& SteeringDebugSelectionQuery()
	{
		static FSeinSteeringDebugSelectionQuery Query;
		return Query;
	}
}
namespace UE::SeinARTSMovement::SteeringDebug
{
	namespace
	{
		using namespace UE::SeinARTSMovement::DebugCanvas;
		FDelegateHandle DrawHandle;
		const FLinearColor Amber(1, 0.65f, 0);
		const FLinearColor Red(1, 0.1f, 0.1f);
		const FLinearColor Cyan(0, 0.8f, 1);
		const FLinearColor Green(0.2f, 1, 0.3f);
		const FLinearColor Magenta(1, 0.2f, 1);
		const FLinearColor Gray(0.6f, 0.6f, 0.6f);

		void Ring(UCanvas& Canvas, const FVector& Position, double Radius)
		{
			for (int32 I = 0; I < 48; ++I)
			{
				const double A = I * UE_TWO_PI / 48, B = (I + 1) * UE_TWO_PI / 48;
				WorldLine(Canvas, Position + FVector(FMath::Cos(A), FMath::Sin(A), 0) * Radius,
					Position + FVector(FMath::Cos(B), FMath::Sin(B), 0) * Radius, Amber);
			}
		}
		void Vector(UCanvas& Canvas, const FVector& Position, double Radius, const FVector& Value, const FLinearColor& Color)
		{
			if (Value.IsNearlyZero()) return;
			const FVector Origin = Position + Value.GetSafeNormal() * Radius;
			Arrow(Canvas, Origin, Origin + Value, Color);
		}
		void Draw(UCanvas* Canvas, APlayerController* Player)
		{
			if (!Canvas || !Canvas->SceneView || !Canvas->SceneView->Family || !Canvas->SceneView->Family->Scene) return;
			UWorld* World = Canvas->SceneView->Family->Scene->GetWorld();
			if (!World) return;
			const FSceneView& View = *Canvas->SceneView;
			const auto* Settings = GetDefault<USeinARTSCoreSettings>();
			const FVector Lift(0, 0, 12);
			const UE::SeinARTS::DebugLegend::FPanel Panel(Canvas, UE::SeinARTS::DebugLegend::EPanel::Steering);
			auto Text = [&](float Row, const FString& Value, const FLinearColor& Color)
			{
				Panel.Text(Row, Value, Color);
			};
			auto Visible = [&](const FVector& Position, double Radius)
			{
				return FVector::DistSquared(Position, View.ViewLocation) <= FMath::Square(double(Settings->DebugDrawMaxDistance))
					&& (!Settings->bDebugDrawFrustumCullEnabled || View.ViewFrustum.IntersectSphere(Position, FMath::Max(100.0, Radius)));
			};
			if (!World->IsGameWorld())
			{
				int32 Count = 0;
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					const auto* Bridge = It->FindComponentByClass<USeinEntityBridgeComponent>();
					if (!Bridge) continue;
					const FSeinExtentsPayload* Extents = nullptr;
					const FSeinNavigationPayload* Nav = nullptr;
					for (const FInstancedStruct& Entry : Bridge->ComponentData)
					{
						if (Entry.GetScriptStruct() == FSeinExtentsPayload::StaticStruct())
							Extents = &Entry.Get<FSeinExtentsPayload>();
						else if (Entry.GetScriptStruct() == FSeinNavigationPayload::StaticStruct())
							Nav = &Entry.Get<FSeinNavigationPayload>();
					}
					const FFixedPoint Radius = USeinMovement::ResolveCollisionRadius(Extents, Nav);
					if (Radius <= FFixedPoint::Zero) continue;
					Ring(*Canvas, It->GetActorLocation() + Lift, Radius.ToFloat());
					++Count;
				}
				Text(8, FString::Printf(TEXT("Steering: %d authored footprint rings | editor preview"), Count), Amber);
				Text(24, TEXT("Live steering and motion are available in PIE / game."), FLinearColor::White);
				return;
			}
			auto* Sim = World->GetSubsystem<USeinWorldSubsystem>();
			const auto* Sub = World->GetSubsystem<USeinMovementSubsystem>();
			const auto* Bridges = World->GetSubsystem<USeinActorBridgeSubsystem>();
			if (!Sim || !Sub) return;
			if (Player && Player->GetWorld() != World) Player = nullptr;
			if (!Player && World->GetGameInstance())
			{
				const auto& Players = World->GetGameInstance()->GetLocalPlayers();
				if (Players.IsValidIndex(View.PlayerIndex)) Player = Players[View.PlayerIndex]->GetPlayerController(World);
			}
			TArray<FSeinEntityHandle> Selection;
			SteeringDebugSelectionQuery().Broadcast(Player, Selection);
			struct FCandidate { FSeinEntityHandle Handle; const FSeinMovementPayload* Move; const USeinMovement* Driver; FVector Position; FVector SimPosition; double Radius; double Distance; bool bSelected; };
			TArray<FCandidate> Candidates;
			int32 Total = 0;
			Sim->GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
			{
				const auto* Move = Sim->GetComponent<FSeinMovementPayload>(Handle);
				if (!Move) return;
				const auto* Contained = Sim->GetComponent<FSeinContainmentMemberData>(Handle);
				if (Contained && Contained->CurrentContainer.IsValid()) return;
				++Total;
				const auto* Nav = Sim->GetComponent<FSeinNavigationPayload>(Handle);
				const double Radius = USeinMovement::ResolveCollisionRadius(Sim, Handle, Nav).ToFloat();
				const FVector SimPosition = Entity.Transform.GetLocation().ToVector();
				const ASeinActor* Actor = Bridges ? Bridges->GetActorForEntity(Handle) : nullptr;
				const FVector Position = IsValid(Actor) ? Actor->GetActorLocation() : SimPosition;
				if (!Visible(Position, Radius)) return;
				Candidates.Add({Handle, Move, Sub->FindMovementInstance(Handle), Position, SimPosition, Radius,
					FVector::DistSquared(Position, View.ViewLocation), Selection.Contains(Handle)});
			});
			Candidates.Sort([](const FCandidate& A, const FCandidate& B)
			{
				return A.bSelected != B.bSelected ? A.bSelected : A.Distance != B.Distance ? A.Distance < B.Distance : A.Handle.Index < B.Handle.Index;
			});
			const int32 Count = FMath::Clamp(Settings->DebugDrawMaxEntities, 0, Candidates.Num());
			int32 Missing = 0;
			for (int32 I = 0; I < Count; ++I)
			{
				const FCandidate& C = Candidates[I];
				const FVector Position = C.Position + Lift;
				if (C.Radius > 0) Ring(*Canvas, Position, C.Radius);
				const double Scale = C.Move->AvoidanceOutput.SpeedScale.ToFloat();
				const FLinearColor VelocityColor = Scale < 1 ? FMath::Lerp(Amber, Red, FMath::Clamp((1-Scale)*2, 0.0, 1.0))
					: FMath::Lerp(Amber, Green, FMath::Clamp((Scale-1)*2, 0.0, 1.0));
				Vector(*Canvas, Position, C.Radius, C.Move->Velocity.ToVector(), VelocityColor);
				Vector(*Canvas, Position, C.Radius, C.Move->AvoidanceOutput.SteerDir.ToVector() * 100, Red);
				const auto* Sample = C.Driver ? &C.Driver->GetSteeringDebugSample() : nullptr;
				const bool bDecision = Sample && Sample->DecisionTick == Sim->GetCurrentTick();
				const bool bSettled = Sample && Sample->MotionTick == Sim->GetCurrentTick() && Sample->bHasSettledVelocity;
				if (bDecision && Sample->bHasHeadings)
				{
					Vector(*Canvas, Position, C.Radius, Sample->DesiredHeading.ToVector() * 100, FLinearColor::White);
					Vector(*Canvas, Position, C.Radius, Sample->AppliedHeading.ToVector() * 100, Cyan);
				}
				if (bDecision && Sample->bHasTarget)
				{
					WorldLine(*Canvas, Position, Sample->Target.ToVector() + Lift, Magenta, 1);
					Marker(*Canvas, Sample->Target.ToVector() + Lift, Magenta, 5);
				}
				if (bSettled) Vector(*Canvas, Position, C.Radius, Sample->SettledVelocity.ToVector(), Green);
				else ++Missing;
				WorldLine(*Canvas, Position, C.SimPosition + Lift, Gray, 1);
				Marker(*Canvas, C.SimPosition + Lift, Gray, 2);
				if (C.bSelected)
				{
					FVector2D Screen;
					if (Project(*Canvas, Position, Screen))
					{
						const FString Settled = bSettled ? FString::Printf(TEXT("%.0f"), Sample->SettledVelocity.Size().ToFloat()) : TEXT("n/a");
						FCanvasTextItem Label(Screen + FVector2D(8, 8), FText::FromString(FString::Printf(
							TEXT("%s\nDriver %.0f / settled %s cm/s | avoidance x%.2f%s"),
							C.Driver ? *C.Driver->GetClass()->GetDisplayNameText().ToString() : TEXT("No driver"), C.Move->Velocity.Size().ToFloat(), *Settled, Scale,
							bDecision && Sample->bHasHeadings ? TEXT("") : TEXT(" | headings n/a"))), GEngine->GetSmallFont(), FLinearColor::White);
						Label.EnableShadow(FLinearColor::Black);
						Canvas->DrawItem(Label);
					}
				}
			}
			Text(8, FString::Printf(TEXT("Steering: %d / %d movers | selected first | through-terrain overlay"), Count, Total), FLinearColor::White);
			Text(24, TEXT("White: desired heading   Cyan: after avoidance   Magenta: vehicle target"), Cyan);
			Text(40, TEXT("Red: avoidance request (100 cm/unit)   Heading arrows: 100 cm"), Red);
			Text(56, TEXT("Velocity arrows: driver (amber) / settled (green), 1 second of motion"), Green);
			Text(72, TEXT("Driver tint: requested slow/boost   Ring: footprint   Gray: sim offset"), Amber);
			Text(88, FString::Printf(TEXT("%d settled samples unavailable | n/a headings: idle / mode not sampled"), Missing), Gray);
		}
	}
	void RegisterDraw()
	{
		if (!DrawHandle.IsValid()) DrawHandle = UDebugDrawService::Register(TEXT("SeinSteering"), FDebugDrawDelegate::CreateStatic(&Draw));
	}
	void UnregisterDraw()
	{
		if (DrawHandle.IsValid()) UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}
}
#else
namespace UE::SeinARTSMovement::SteeringDebug
{
	void RegisterDraw() {}
	void UnregisterDraw() {}
}
#endif
