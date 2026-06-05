/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityComponentVisualizer.cpp
 * @brief   Implementation — built-in draw layers (extents + production spawn
 *          point) + a fan-out to every per-component-type draw callback
 *          registered via `FSeinARTSEditorModule::RegisterComponentDataDraw`.
 *
 *          Decoupling rule: this file references ONLY components that live in
 *          SeinARTSCoreEntity (a hard dep of SeinARTSEditor) — currently
 *          `FSeinExtentsComponent` + `FSeinProductionComponent`. Every other
 *          per-component viz layer (FoW vision stamps, cover area + slots,
 *          future systems) lives in its OWNING editor module and registers a
 *          delegate at StartupModule. Optional systems can be fully disabled
 *          without affecting this file or SeinARTSEditor's dep graph.
 */

#include "Visualizers/SeinEntityComponentVisualizer.h"

#include "Actor/SeinEntityComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinProductionComponent.h"
#include "SeinARTSEditorModule.h"  // FSeinARTSEditorModule + per-component draw registry

#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "SceneManagement.h"
#include "StructUtils/InstancedStruct.h"

namespace SeinEntityVisualizerLocal
{
	// ----------------------------------------------------------------------
	// Extents drawing (Box / Capsule) — built-in. The extents struct lives in
	// SeinARTSCoreEntity, which SeinARTSEditor already depends on, so this
	// stays in the editor module as the always-on baseline layer.
	// ----------------------------------------------------------------------

	static void DrawExtentsEntries(
		const TArray<FInstancedStruct>& ComponentData,
		const FQuat& ActorQuat, const FVector& ActorPos,
		FPrimitiveDrawInterface* PDI)
	{
		const FColor WireColor = FColor::Red;  // all extents draw red
		const float Thickness = 1.5f;

		for (const FInstancedStruct& Entry : ComponentData)
		{
			if (!Entry.IsValid()) continue;
			if (Entry.GetScriptStruct() != FSeinExtentsComponent::StaticStruct()) continue;

			const FSeinExtentsComponent& Data = Entry.Get<FSeinExtentsComponent>();
			if (Data.Shapes.Num() == 0) continue;

			for (const FSeinExtentsShape& Shape : Data.Shapes)
			{
				const FVector LocalOffset(
					Shape.LocalOffset.X.ToFloat(),
					Shape.LocalOffset.Y.ToFloat(),
					Shape.LocalOffset.Z.ToFloat());
				const FVector WorldOffset = ActorQuat.RotateVector(LocalOffset);
				const FVector ShapeBase = ActorPos + WorldOffset;

				const float YawOffsetDeg = Shape.YawOffsetDegrees.ToFloat();
				const FQuat YawOffsetQuat(FVector::UpVector, FMath::DegreesToRadians(YawOffsetDeg));
				const FQuat ShapeQuat = ActorQuat * YawOffsetQuat;

				const FVector AxisX = ShapeQuat.GetForwardVector();
				const FVector AxisY = ShapeQuat.GetRightVector();
				const FVector AxisZ = ShapeQuat.GetUpVector();

				const float Height = FMath::Max(0.0f, Shape.Height.ToFloat());
				const FVector ShapeCenter = ShapeBase + AxisZ * (Height * 0.5f);


				switch (Shape.Shape)
				{
				case ESeinExtentsShape::Box:
				{
					const FVector HalfExtents(
						FMath::Max(0.0f, Shape.HalfExtentX.ToFloat()),
						FMath::Max(0.0f, Shape.HalfExtentY.ToFloat()),
						Height * 0.5f);
					DrawOrientedWireBox(
						PDI,
						ShapeCenter,
						AxisX, AxisY, AxisZ,
						HalfExtents,
						WireColor,
						SDPG_World,
						Thickness);
					break;
				}

				case ESeinExtentsShape::Capsule:
				{
					const float Radius = FMath::Max(0.0f, Shape.Radius.ToFloat());
					const float HalfHeight = FMath::Max(Height * 0.5f, Radius);
					DrawWireCapsule(
						PDI,
						ShapeCenter,
						AxisX, AxisY, AxisZ,
						WireColor,
						Radius,
						HalfHeight,
						/*NumSides*/ 16,
						SDPG_World,
						Thickness);
					break;
				}
				}
			}
		}
	}

	// ----------------------------------------------------------------------
	// Production spawn-point drawing — built-in. FSeinProductionComponent
	// lives in SeinARTSCoreEntity (already a dep), so this stays alongside
	// the extents layer. Mirrors the legacy SeinProductionComponentVisualizer
	// that was lost in the Phase-5 AC excise.
	//
	// For each FSeinProductionComponent entry in ComponentData:
	//   - Green wire sphere at the resolved world spawn point
	//   - Forward arrow indicating produced-unit facing
	//   - Faint tether from owner pivot → spawn point (helps spot uninit
	//     identity-transform cases where the spawn point sits inside the mesh)
	// ----------------------------------------------------------------------

	static void DrawProductionSpawnPoints(
		const TArray<FInstancedStruct>& ComponentData,
		const FTransform& ActorXform,
		FPrimitiveDrawInterface* PDI)
	{
		const FLinearColor MarkerColor = FLinearColor(0.2f, 1.0f, 0.4f);   // bright green
		const FColor MarkerColorByte = MarkerColor.ToFColor(true);
		const FLinearColor TetherColor = FLinearColor(0.2f, 0.6f, 0.3f, 0.5f);
		const float MarkerThickness = 1.5f;
		const float MarkerRadius = 30.0f;
		const float ArrowLength = 120.0f;
		const float ArrowHeadSize = 25.0f;
		const ESceneDepthPriorityGroup DPG = SDPG_World;

		for (const FInstancedStruct& Entry : ComponentData)
		{
			if (!Entry.IsValid()) continue;
			if (Entry.GetScriptStruct() != FSeinProductionComponent::StaticStruct()) continue;

			const FSeinProductionComponent& Data = Entry.Get<FSeinProductionComponent>();

			// Compose: WorldSpawn = SpawnPointOffset * ActorXform. Matches the
			// runtime formula in USeinWorldSubsystem::SpawnEntity so designers
			// see exactly where finished units will appear.
			const FTransform LocalOffsetXform = Data.SpawnPointOffset.ToTransform();
			const FTransform WorldXform = LocalOffsetXform * ActorXform;

			const FVector WorldPos = WorldXform.GetLocation();
			const FQuat WorldQuat = WorldXform.GetRotation();
			const FVector Forward = WorldQuat.GetForwardVector();
			const FVector Right = WorldQuat.GetRightVector();

			DrawWireSphere(PDI, WorldPos, MarkerColorByte, MarkerRadius,
				/*NumSides*/ 16, DPG, MarkerThickness);

			const FVector ArrowEnd = WorldPos + Forward * ArrowLength;
			PDI->DrawLine(WorldPos, ArrowEnd, MarkerColor, DPG, MarkerThickness);
			const FVector HeadBase = ArrowEnd - Forward * ArrowHeadSize;
			PDI->DrawLine(ArrowEnd, HeadBase + Right * ArrowHeadSize * 0.5f, MarkerColor, DPG, MarkerThickness);
			PDI->DrawLine(ArrowEnd, HeadBase - Right * ArrowHeadSize * 0.5f, MarkerColor, DPG, MarkerThickness);

			PDI->DrawLine(ActorXform.GetLocation(), WorldPos, TetherColor, DPG, 0.75f);
		}
	}

	// ----------------------------------------------------------------------
	// Navigation footprint drawing — built-in. FSeinNavigationComponent
	// lives in SeinARTSCoreEntity (already a dep). Draws an orange wire
	// circle at the entity's actor position with radius =
	// FallbackFootprintRadius (which is only used at runtime when no
	// Extents component is present — see field docstring), so designers
	// can eyeball the fallback footprint without dropping into PIE.
	// Distinct color from extents (yellow/cyan), cover (green/magenta),
	// and production (green) so a unit with all four components reads
	// cleanly.
	// ----------------------------------------------------------------------

	static void DrawNavigationFootprints(
		const TArray<FInstancedStruct>& ComponentData,
		const FVector& ActorPos,
		FPrimitiveDrawInterface* PDI)
	{
		const FColor FootprintColor(255, 140, 0);   // orange
		const float Thickness = 3.0f;                // bumped from 1.5 for visibility
		const int32 NumSides = 32;
		const float ZLift = 25.0f;                   // matches cover slot lift — clears nav floor tint + slight terrain

		// One-shot per-load diagnostic: when this function first hits an
		// entry that IS a nav component, log what we found. Helps confirm
		// the layer is firing on the right entities.
		static bool bLoggedOnce = false;

		for (const FInstancedStruct& Entry : ComponentData)
		{
			if (!Entry.IsValid()) continue;
			if (Entry.GetScriptStruct() != FSeinNavigationComponent::StaticStruct()) continue;

			const FSeinNavigationComponent& Data = Entry.Get<FSeinNavigationComponent>();
			const float Radius = Data.FallbackFootprintRadius.ToFloat();

			if (!bLoggedOnce)
			{
				UE_LOG(LogTemp, Log,
					TEXT("[NavFootprintViz] first call — ActorPos=(%.1f,%.1f,%.1f) FallbackFootprintRadius=%.1f"),
					ActorPos.X, ActorPos.Y, ActorPos.Z, Radius);
				bLoggedOnce = true;
			}

			if (Radius <= 0.0f) continue;     // intangible entities (zero footprint) opt out

			// Horizontal circle on the actor's XY plane. X = world +X,
			// Y = world +Y keeps the circle flat regardless of actor
			// rotation (footprint is a top-down concept).
			const FVector Center(ActorPos.X, ActorPos.Y, ActorPos.Z + ZLift);
			DrawCircle(PDI, Center, FVector::ForwardVector, FVector::RightVector,
				FootprintColor, Radius, NumSides, SDPG_World, Thickness);
		}
	}
} // namespace SeinEntityVisualizerLocal

void FSeinEntityComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* /*View*/,
	FPrimitiveDrawInterface* PDI)
{
	const USeinEntityComponent* Bridge = Cast<USeinEntityComponent>(Component);
	if (!Bridge || !PDI) return;

	const AActor* Owner = Bridge->GetOwner();
	const FTransform ActorXform = Owner ? Owner->GetActorTransform() : FTransform::Identity;
	const FQuat ActorQuat = ActorXform.GetRotation();
	const FVector ActorPos = ActorXform.GetLocation();

	// Built-in extents layer — always drawn (struct lives in CoreEntity, no
	// optional dep).
	SeinEntityVisualizerLocal::DrawExtentsEntries(Bridge->ComponentData, ActorQuat, ActorPos, PDI);

	// Built-in production spawn-point layer — same module dep story as
	// extents. Drawn for every FSeinProductionComponent in ComponentData.
	SeinEntityVisualizerLocal::DrawProductionSpawnPoints(Bridge->ComponentData, ActorXform, PDI);

	// Built-in nav footprint layer — orange circle at FallbackFootprintRadius.
	// Always-on; entities with FallbackFootprintRadius=0 (intangible) self-skip.
	SeinEntityVisualizerLocal::DrawNavigationFootprints(Bridge->ComponentData, ActorPos, PDI);

	// Per-component draw fan-out. Each optional system editor module
	// registers a callback at StartupModule and unregisters at Shutdown.
	// We `GetModulePtr` (not `GetModuleChecked`) so a viewport draw during
	// editor teardown — where SeinARTSEditor itself may already be in
	// shutdown — doesn't assert; we just skip the fan-out.
	if (FSeinARTSEditorModule* EditorModule = FModuleManager::GetModulePtr<FSeinARTSEditorModule>("SeinARTSEditor"))
	{
		// One-shot diagnostic: log the registry size the first time we paint
		// after the module's StartupModule. Catches "callback never
		// registered" without spamming the log every frame.
		static bool bLoggedOnce = false;
		if (!bLoggedOnce)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[SeinEntityComponentVisualizer] first DrawVisualization. Registry has %d callback(s)."),
				EditorModule->GetComponentDataDraws().Num());
			for (const TPair<FName, FSeinComponentDataDrawDelegate>& Pair : EditorModule->GetComponentDataDraws())
			{
				UE_LOG(LogTemp, Log, TEXT("  - %s (bound: %s)"),
					*Pair.Key.ToString(),
					Pair.Value.IsBound() ? TEXT("yes") : TEXT("no"));
			}
			bLoggedOnce = true;
		}

		for (const TPair<FName, FSeinComponentDataDrawDelegate>& Pair : EditorModule->GetComponentDataDraws())
		{
			Pair.Value.ExecuteIfBound(Bridge->ComponentData, ActorQuat, ActorPos, PDI);
		}
	}
}
