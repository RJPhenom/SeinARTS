/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSMovementModule.cpp
 * @brief   Module startup + movement debug toggles.
 *
 *          `Sein.Show.Steering [0|1|on|off]` toggles a custom show flag
 *          (`ShowFlags.SeinSteering`) that gates movement debug viz. Today it
 *          draws each unit's footprint ring, velocity vector, and avoidance
 *          steer (carrot / look-ahead / turn-radius viz is planned). Movement
 *          code consumes the flag via
 *          `UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld`.
 *
 *          The active-move path overlay ticker iterates running
 *          `USeinMoveToAction` instances and draws yellow path cells +
 *          waypoint lines + the current target marker. Gates per-world on
 *          `UE::SeinARTSNavigation::IsNavigationShowFlagOnForWorld` so a
 *          single `Sein.Nav.Show` toggle drives both the cell-quad scene
 *          proxy (Nav module's `USeinNavDebugComponent`) and this overlay.
 *
 *          Shipping strip: ticker, console command, and helper draw functions
 *          are gated on UE_ENABLE_DEBUG_DRAWING. In UE_BUILD_SHIPPING the
 *          module's StartupModule / ShutdownModule are no-ops.
 */

#include "SeinARTSMovementModule.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Actions/SeinMoveToAction.h"
#include "Debug/SeinDebugDrawCull.h"
#include "Movement/SeinMovement.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"  // editor-world cascade: BoundingRadius
#include "Actor/SeinEntityComponent.h"  // editor-world Extents viz: walks Bridge->ComponentData
#include "SeinARTSNavigationModule.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "SeinPathTypes.h"

#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"                 // TActorIterator — editor actor walk
#include "GameFramework/Actor.h"
#include "ShowFlags.h"
#include "UObject/UObjectIterator.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "Core/SeinEntityPool.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "Editor.h"
#endif
#endif // UE_ENABLE_DEBUG_DRAWING

IMPLEMENT_MODULE(FSeinARTSMovementModule, SeinARTSMovement)

#if UE_ENABLE_DEBUG_DRAWING
// Custom show flag for steering viz (footprint ring, velocity arrow,
// avoidance arrow, perception circle, look-ahead cone). UE doesn't ship a
// matching built-in, so we register one via TCustomShowFlag (same pattern
// as SeinARTSFogOfWar). Module-public so per-controller draw sites can
// share the gate.
namespace UE::SeinARTSMovement
{
	static TCustomShowFlag<> ShowSteering(
		TEXT("SeinSteering"),
		/*DefaultEnabled*/ false,
		SFG_Normal,
		NSLOCTEXT("SeinARTSMovement", "ShowSteering", "Steering"));

	// Extents show flag — draws each entity's FSeinExtentsComponent shapes
	// in PIE and the level editor — every shape draws as a red wire box /
	// capsule, matching the BP-viewport extents visualizer so the same shape
	// reads identically in both contexts. Sim-side concept (Extents lives in
	// SeinARTSCoreEntity) but hosted here for ticker-infrastructure reuse —
	// SeinARTSCoreEntity has no debug-draw pipeline of its own.
	static TCustomShowFlag<> ShowExtents(
		TEXT("SeinExtents"),
		/*DefaultEnabled*/ false,
		SFG_Normal,
		NSLOCTEXT("SeinARTSMovement", "ShowExtents", "Extents"));

	bool IsSteeringShowFlagOnForWorld(UWorld* World)
	{
		if (!World) return false;

#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && Vp->GetWorld() == World
				    && ShowSteering.IsEnabled(Vp->EngineShowFlags))
				{
					return true;
				}
			}
		}
#endif
		// Enumerate every FWorldContext's GameViewport — in multi-client PIE
		// there are multiple GameViewports and only one is GEngine->GameViewport.
		// Match by world so toggling the flag off on the user's visible
		// viewport actually hides the viz for that viewport's units, even
		// if some other viewport (editor / other PIE) still has the flag on.
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport
				    && Ctx.GameViewport->GetWorld() == World
				    && ShowSteering.IsEnabled(Ctx.GameViewport->EngineShowFlags))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool IsExtentsShowFlagOnForWorld(UWorld* World)
	{
		if (!World) return false;

#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && Vp->GetWorld() == World
				    && ShowExtents.IsEnabled(Vp->EngineShowFlags))
				{
					return true;
				}
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport
				    && Ctx.GameViewport->GetWorld() == World
				    && ShowExtents.IsEnabled(Ctx.GameViewport->EngineShowFlags))
				{
					return true;
				}
			}
		}
		return false;
	}
}

namespace
{
	IConsoleCommand* GShowSteeringCmd = nullptr;
	IConsoleCommand* GShowExtentsCmd = nullptr;
	FTSTicker::FDelegateHandle GTickHandle;

	/** Set ShowFlags.SeinSteering across all editor + game viewport clients.
	 *  Iterates every FWorldContext's GameViewport so multi-client PIE applies
	 *  the toggle to every PIE window, not just the primary one pointed at by
	 *  GEngine->GameViewport. */
	static void SetSteeringShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					UE::SeinARTSMovement::ShowSteering.SetEnabled(Vp->EngineShowFlags, bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport)
				{
					UE::SeinARTSMovement::ShowSteering.SetEnabled(Ctx.GameViewport->EngineShowFlags, bEnable);
				}
			}
		}
	}

	/** "Any viewport has the flag" check used by the console-command toggle.
	 *  Public consumers (per-tick draw gate) use the per-world variant
	 *  `IsSteeringShowFlagOnForWorld` instead — only the toggle command needs
	 *  a global "current state" query. */
	static bool IsSteeringShowFlagOn_AnyViewport()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && UE::SeinARTSMovement::ShowSteering.IsEnabled(Vp->EngineShowFlags)) return true;
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport && UE::SeinARTSMovement::ShowSteering.IsEnabled(Ctx.GameViewport->EngineShowFlags))
				{
					return true;
				}
			}
		}
		return false;
	}

	static void OnShowSteeringCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		// Parse on/off the same way as ShowNavigation. No-args = toggle.
		bool bEnable = !IsSteeringShowFlagOn_AnyViewport();
		if (Args.Num() > 0)
		{
			const FString& A = Args[0];
			if (A == TEXT("0") || A.Equals(TEXT("off"),  ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bEnable = false;
			}
			else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase) || A.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bEnable = true;
			}
		}
		SetSteeringShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("Sein.Show.Steering = %s (ShowFlags.SeinSteering)"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
	}

	// --- Extents show flag: same toggle / query / command pattern as Steering ---
	static void SetExtentsShowFlag(bool bEnable)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp)
				{
					UE::SeinARTSMovement::ShowExtents.SetEnabled(Vp->EngineShowFlags, bEnable);
					Vp->Invalidate();
				}
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport)
				{
					UE::SeinARTSMovement::ShowExtents.SetEnabled(Ctx.GameViewport->EngineShowFlags, bEnable);
				}
			}
		}
	}

	static bool IsExtentsShowFlagOn_AnyViewport()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (Vp && UE::SeinARTSMovement::ShowExtents.IsEnabled(Vp->EngineShowFlags)) return true;
			}
		}
#endif
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.GameViewport && UE::SeinARTSMovement::ShowExtents.IsEnabled(Ctx.GameViewport->EngineShowFlags))
				{
					return true;
				}
			}
		}
		return false;
	}

	static void OnShowExtentsCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		bool bEnable = !IsExtentsShowFlagOn_AnyViewport();
		if (Args.Num() > 0)
		{
			const FString& A = Args[0];
			if (A == TEXT("0") || A.Equals(TEXT("off"),  ESearchCase::IgnoreCase) || A.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bEnable = false;
			}
			else if (A == TEXT("1") || A.Equals(TEXT("on"), ESearchCase::IgnoreCase) || A.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bEnable = true;
			}
		}
		SetExtentsShowFlag(bEnable);
		UE_LOG(LogTemp, Log, TEXT("Sein.Show.Extents = %s (ShowFlags.SeinExtents)"),
			bEnable ? TEXT("ON") : TEXT("OFF"));
	}

	/** Draw active-move debug for `World`:
	 *   - Yellow solid boxes at each cell the remaining path crosses
	 *   - Blue solid box at the final-destination cell (flag marker)
	 *   - Yellow line overlay from entity → current target → remaining waypoints
	 *   - Sphere at the destination, white dots at each waypoint
	 *
	 *  All lifetime=0 (one frame) so nothing accumulates. Draws only while
	 *  the Navigation show flag is on.
	 *
	 *  Performance: two-phase gather→draw. Phase 1 walks every active
	 *  USeinMoveToAction, applies camera distance + cone-frustum cull
	 *  (settings: DebugDrawMaxDistance / bDebugDrawFrustumCullEnabled), and
	 *  collects survivors with their distance-to-camera. Phase 2 sorts
	 *  closest-first and draws up to the global per-frame cap
	 *  (DebugDrawMaxEntities). On 1km² maps with hundreds of moving units
	 *  this drops the per-frame DrawDebug* count from ~thousands to dozens
	 *  with no perceptible viz change inside the focus area. */
	static void DrawActiveMoveDebug(UWorld* World)
	{
		if (!World) return;
		USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* NavSub = World->GetSubsystem<USeinNavigationSubsystem>();
		if (!Sim || !NavSub) return;
		USeinNavigation* Nav = NavSub->GetNavigation();
		if (!Nav) return;

		// Path = yellow, final destination = blue (RTS "flag on the map" feel).
		// More opaque than the translucent green floor tint so the path pops
		// clearly against baked cells.
		const FColor CellRemaining(255, 220, 0, 220);
		const FColor CellTarget(0, 140, 255, 230);
		const FColor LineEntityToTarget(255, 220, 0);
		const FColor LineChain(255, 220, 0);

		// Resolve the active camera once for the entire gather pass — every
		// candidate test reads the same view, so the (relatively expensive)
		// editor / PIE viewport iterator walk amortizes to one call per draw
		// tick instead of per-action.
		using namespace UE::SeinARTSMovement::DebugDraw;
		const FCameraView View = GetActiveCameraView(World);

		// Gather pass — collect candidates that pass camera cull plus their
		// distance-to-camera (squared, used only for the closest-first sort).
		struct FCandidate
		{
			USeinMoveToAction* Action;
			FFixedVector AgentPosFixed;
			float DistSq;
		};
		TArray<FCandidate> Candidates;
		Candidates.Reserve(64);

		for (TObjectIterator<USeinMoveToAction> It; It; ++It)
		{
			USeinMoveToAction* Action = *It;
			// NOTE: don't filter by Action->GetWorld() — the proxy's outer is the
			// transient package, so the action's world chain is null. Use the
			// entity-lookup below as the implicit world filter (Sim->GetEntity
			// only returns valid for entities registered in THIS sim).
			if (!IsValid(Action) || !Action->IsPathValid()) continue;
			// Skip actions that have finished — they linger as unreferenced
			// UObjects with a still-valid Path until the next GC cycle, and
			// without this filter every move order leaves a trail behind.
			if (Action->bCompleted || Action->bCancelled || Action->bFailed) continue;

			const FSeinPath& Path = Action->Path;
			const int32 CurIdx = Action->GetCurrentWaypointIndex();
			if (!Path.Waypoints.IsValidIndex(CurIdx)) continue;

			const FSeinEntity* E = Sim->GetEntity(Action->OwnerEntity);
			if (!E) continue;

			const FFixedVector AgentPosFixed = E->Transform.GetLocation();
			const FVector AgentPos(AgentPosFixed.X.ToFloat(),
				AgentPosFixed.Y.ToFloat(), AgentPosFixed.Z.ToFloat());

			if (!PassesCameraCull(View, AgentPos)) continue;

			const float DistSq = View.bValid
				? static_cast<float>((AgentPos - View.Location).SizeSquared())
				: 0.0f;
			Candidates.Add({ Action, AgentPosFixed, DistSq });
		}

		// Closest-first ordering so the per-frame cap consumes its budget on
		// the most visible (and most useful for the user) units. Skip the
		// sort when the camera couldn't be resolved — DistSq is meaningless
		// in that case, all candidates carry DistSq=0 anyway.
		if (View.bValid)
		{
			Candidates.Sort([](const FCandidate& A, const FCandidate& B)
			{
				return A.DistSq < B.DistSq;
			});
		}

		// Draw pass — bounded by the shared per-frame budget. Existing
		// per-action draw code (cell highlights + waypoint lines) is unchanged
		// from the pre-LOD revision.
		for (const FCandidate& C : Candidates)
		{
			if (!TryReserveBudget())
			{
				// Global cap exhausted (other sites — e.g. steering vectors —
				// may have already consumed slots). Stop drawing for this
				// tick; remaining candidates are by definition further from
				// the camera so the visual loss is on the periphery.
				break;
			}

			const FSeinPath& Path = C.Action->Path;
			const int32 CurIdx = C.Action->GetCurrentWaypointIndex();
			const FFixedVector AgentPosFixed = C.AgentPosFixed;


			// Path cell highlights: yellow cells along the remaining path, blue cell
			// at the final destination (flag marker).
			TArray<FVector> RemainingCells;
			TArray<FVector> TargetCell;
			float HalfExtent = 0.0f;
			Nav->CollectDebugPathCells(AgentPosFixed, Path.Waypoints, CurIdx,
				RemainingCells, TargetCell, HalfExtent);

			if (HalfExtent > 0.0f)
			{
				// Lift path cells well above the scene-proxy floor tint (which
				// draws at CellHeight + 2cm) so they don't z-fight / alpha-blend
				// into invisibility. 15cm center + 5cm half-height = box spans
				// +10 to +20 above the cell's baked height.
				const FVector Ext(HalfExtent * 0.95f, HalfExtent * 0.95f, 5.0f);
				for (const FVector& Center : RemainingCells)
				{
					DrawDebugSolidBox(World, Center + FVector(0, 0, 15.0f), Ext, CellRemaining, false, 0.0f, SDPG_World);
				}
				for (const FVector& Center : TargetCell)
				{
					DrawDebugSolidBox(World, Center + FVector(0, 0, 16.0f), Ext * 1.05f, CellTarget, false, 0.0f, SDPG_World);
				}
			}

			// Waypoint line overlay.
			auto ToFVector = [](const FFixedVector& V, float ZBias = 8.0f)
			{
				return FVector(V.X.ToFloat(), V.Y.ToFloat(), V.Z.ToFloat() + ZBias);
			};

			const FVector EntityPos = ToFVector(AgentPosFixed);
			const FVector CurTarget = ToFVector(Path.Waypoints[CurIdx]);

			DrawDebugLine(World, EntityPos, CurTarget, LineEntityToTarget, false, 0.0f, 0, 3.0f);
			for (int32 i = CurIdx; i < Path.Waypoints.Num() - 1; ++i)
			{
				DrawDebugLine(World, ToFVector(Path.Waypoints[i]), ToFVector(Path.Waypoints[i + 1]),
					LineChain, false, 0.0f, 0, 2.0f);
			}
			DrawDebugSphere(World, ToFVector(Path.Waypoints.Last()), 20.0f, 12,
				LineChain, false, 0.0f, 0, 2.0f);
			for (int32 i = CurIdx; i < Path.Waypoints.Num(); ++i)
			{
				DrawDebugPoint(World, ToFVector(Path.Waypoints[i]), 6.0f, FColor::White, false, 0.0f);
			}
		}
	}

	/** Per-frame steering-vector viz pass for every entity in `World` that has
	 *  both an `FSeinMovementComponent` (velocity source) AND an
	 *  `FSeinNavigationComponent` (footprint radius source). Decoupled from
	 *  the per-Tick carrot viz inside Movement subclasses so units that are
	 *  NOT currently inside an active `USeinMoveToAction` (idle between
	 *  orders, parked at destination, etc.) still draw the footprint ring +
	 *  velocity arrow.
	 *
	 *  Reads the effective collision radius via the shared cascade (Extents
	 *  → NavComp->FallbackFootprintRadius → 0; see
	 *  `USeinMovement::ResolveCollisionRadius`) for the ring and
	 *  MovementData->Velocity for the arrow — both are sim component fields
	 *  kept up to date by the movement subclasses' Tick. No per-frame
	 *  recompute, no sim mutation.
	 *
	 *  Camera-cull + budget cap via `ShouldDrawAndReserve` so dense maps with
	 *  hundreds of units don't blow the per-frame DrawDebug* count. Closest-
	 *  to-camera units win the budget (the active-move pass uses an explicit
	 *  closest-first sort; here we rely on entity-pool iteration order +
	 *  per-call cull, which is good enough for the always-on viz).
	 *
	 *  Cost: O(entities-with-FSeinMovementComponent) × cheap component
	 *  lookups + few DrawDebug calls. Gated on UE_ENABLE_DEBUG_DRAWING —
	 *  shipping build compiles to nothing. */
	static void DrawSteeringVectorsViz(USeinWorldSubsystem* Sim, UWorld* World)
	{
		if (!World) return;

		if (World->IsGameWorld())
		{
			// PIE / game: sim entity pool, with velocity arrows + camera cull.
			if (!Sim) return;
			FSeinEntityPool& Pool = Sim->GetEntityPool();
			Pool.ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
			{
				const FSeinMovementComponent* MovementData = Sim->GetComponent<FSeinMovementComponent>(Handle);
				if (!MovementData) return;
				const FSeinNavigationComponent* NavData = Sim->GetComponent<FSeinNavigationComponent>(Handle);

				// Footprint cascade matches USeinMovement::ResolveCollisionRadius
				// (single source of truth — same helper used by collision +
				// path planning). Skip the entity entirely when the cascade
				// resolves to zero — intangible units opt out of viz by design.
				const FFixedPoint FootprintFixed = USeinMovement::ResolveCollisionRadius(
					Sim, Handle, NavData);
				const float FootprintRadius = FootprintFixed.ToFloat();
				if (FootprintRadius <= 0.0f) return;

				// Camera cull + budget reserve — fail-open (no camera resolved)
				// renders for every entity, same as the active-move pass.
				const FFixedVector EntityPosFixed = Entity.Transform.GetLocation();
				const FVector EntityPosFloat(
					EntityPosFixed.X.ToFloat(),
					EntityPosFixed.Y.ToFloat(),
					EntityPosFixed.Z.ToFloat());
				if (!UE::SeinARTSMovement::DebugDraw::ShouldDrawAndReserve(World, EntityPosFloat))
				{
					return;
				}

				// Steering arrows only while under an active move order — at rest the stored Velocity
				// / AvoidanceSteer can be stale, so pass zero → the unit shows the ring only. During a
				// move both are live: orange = world velocity (entity → velocity), red = avoidance.
				const bool bActiveMove = MovementData->bHasTarget;
				USeinMovement::DrawSteeringDebugViz(
					World, EntityPosFixed, FootprintRadius,
					bActiveMove ? MovementData->Velocity : FFixedVector::ZeroVector,
					bActiveMove ? MovementData->AvoidanceOutput.SteerDir : FFixedVector::ZeroVector);
			});
		}
		else
		{
			// Editor world: walk actors with USeinEntityComponent and read
			// authored ComponentData. No sim → no live velocity, so draw the
			// footprint ring only (velocity arrow skips when |velocity|=0
			// in DrawSteeringDebugViz). NO cull / budget — editor users want
			// to see every authored unit's footprint, not just nearby ones.
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;
				USeinEntityComponent* Bridge = Actor->FindComponentByClass<USeinEntityComponent>();
				if (!Bridge) continue;

				// Run the same Extents → NavComp cascade against AUTHORED
				// ComponentData (no Sim available to look up runtime components).
				FFixedPoint AuthoredRadius = FFixedPoint::Zero;
				const FSeinNavigationComponent* AuthoredNav = nullptr;
				for (const FInstancedStruct& Entry : Bridge->ComponentData)
				{
					if (!Entry.IsValid()) continue;
					const UScriptStruct* ScriptStruct = Entry.GetScriptStruct();
					if (ScriptStruct == FSeinExtentsComponent::StaticStruct())
					{
						const FSeinExtentsComponent& Extents = Entry.Get<FSeinExtentsComponent>();
						for (const FSeinExtentsShape& Shape : Extents.Shapes)
						{
							const FFixedPoint R = SeinExtentsHelpers::BoundingRadius(Shape);
							if (R > AuthoredRadius) AuthoredRadius = R;
						}
					}
					else if (ScriptStruct == FSeinNavigationComponent::StaticStruct())
					{
						AuthoredNav = &Entry.Get<FSeinNavigationComponent>();
					}
				}
				if (AuthoredRadius <= FFixedPoint::Zero && AuthoredNav)
				{
					AuthoredRadius = AuthoredNav->FallbackFootprintRadius;
				}
				const float FootprintRadius = AuthoredRadius.ToFloat();
				if (FootprintRadius <= 0.0f) continue;

				const FVector ActorPos = Actor->GetActorLocation();
				const FFixedVector EntityPosFixed(
					FFixedPoint::FromFloat(ActorPos.X),
					FFixedPoint::FromFloat(ActorPos.Y),
					FFixedPoint::FromFloat(ActorPos.Z));
				// Editor (no sim): no move target and no avoidance, so pass a zero direction —
				// DrawSteeringDebugViz then draws the footprint ring only.
				USeinMovement::DrawSteeringDebugViz(
					World, EntityPosFixed, FootprintRadius, FFixedVector::ZeroVector);
			}
		}
	}

#if WITH_EDITOR
	/** Force every level-editor viewport pointed at `World` to redraw next
	 *  frame. Editor viewports normally render only on demand (mouse move,
	 *  selection change, etc.) for perf, so DrawDebug* calls submitted by
	 *  our ticker won't appear until something else triggers a redraw —
	 *  user reports this as "show flag doesn't apply until I click in the
	 *  viewport." Calling Invalidate() per-tick when our flag is on creates
	 *  a continuous redraw loop while the flag is enabled (cheap, since the
	 *  alternative — debug viz that visibly lags by one click — is much
	 *  worse). */
	static void InvalidateEditorViewportsForWorld(UWorld* World)
	{
		if (!GEditor || !World) return;
		for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
		{
			if (Vp && Vp->GetWorld() == World)
			{
				Vp->Invalidate();
			}
		}
	}

	/** Tick-time helper: drives "draw + invalidate if flag is on; final
	 *  invalidate on the ON→OFF transition so the viewport actually clears."
	 *
	 *  Without the transition invalidate, the last rendered frame (which had
	 *  debug viz drawn) keeps displaying after the user toggles the show flag
	 *  off — UE's show-flag UI doesn't automatically invalidate viewports
	 *  on toggle, so without our prod the viewport just sticks on the stale
	 *  render until the user clicks somewhere (which incidentally triggers
	 *  an invalidate). Tracking previous state per world means we invalidate
	 *  exactly once on the transition, then go quiet — no continuous editor
	 *  redraw cost when the flag is off.
	 *
	 *  FObjectKey is used instead of UWorld* so stale entries from unloaded
	 *  worlds don't dereference dangling pointers — FObjectKey survives the
	 *  underlying UObject's destruction. */
	static void TickEditorShowFlagDraw(
		TMap<FObjectKey, bool>& LastStateMap,
		UWorld* World,
		bool bIsOn,
		const TFunction<void()>& DoDraw)
	{
		const FObjectKey Key(World);
		const bool bWasOn = LastStateMap.FindRef(Key);
		LastStateMap.Add(Key, bIsOn);

		if (bIsOn)
		{
			DoDraw();
			InvalidateEditorViewportsForWorld(World);
		}
		else if (bWasOn)
		{
			// ON → OFF transition: invalidate once so the viewport clears.
			InvalidateEditorViewportsForWorld(World);
		}
		// OFF → OFF: do nothing (no draw, no invalidate). Saves perf when
		// the user isn't using the flag.
	}

	// Per-flag previous-state maps. Static so they persist across ticker
	// fires; keyed by FObjectKey so stale entries from unloaded worlds are
	// safe. Trivially leaks one bool per world ever seen, which is fine.
	static TMap<FObjectKey, bool> GLastExtentsOnByWorld;
	static TMap<FObjectKey, bool> GLastSteeringOnByWorld;
#endif

	/** Helper — draws each shape in `Shapes` at the world-space pose
	 *  (Origin, Rotation). Used by both the PIE entity-pool path and the
	 *  editor actor-iterator path so they emit identical visuals.
	 *
	 *  Respects per-shape LocalOffset, YawOffsetDegrees, and Height — same
	 *  math as the BP-editor visualizer and nav-blocker stamping. */
	static void DrawExtentsShapesAt(
		UWorld* World,
		const FVector& Origin,
		const FQuat& Rotation,
		const FSeinExtentsComponent& Extents)
	{
		const FColor WireColor = FColor::Red;     // all extents draw red
		const float Thickness = 3.0f;
		const float DrawLifetime = 0.0f;

		for (const FSeinExtentsShape& Shape : Extents.Shapes)
		{
			// Per-shape local offset rotated into world space by entity yaw.
			const FVector LocalOffset(
				Shape.LocalOffset.X.ToFloat(),
				Shape.LocalOffset.Y.ToFloat(),
				Shape.LocalOffset.Z.ToFloat());
			const FVector WorldOffset = Rotation.RotateVector(LocalOffset);
			const FVector ShapeBase = Origin + WorldOffset;

			// Per-shape YawOffset stacked on entity rotation. Same math as
			// the BP visualizer + nav-blocker stamping — keeps viz
			// orientation-locked with the actual collision shape.
			const float YawOffsetRad = FMath::DegreesToRadians(Shape.YawOffsetDegrees.ToFloat());
			const FQuat YawOffsetQuat(FVector::UpVector, YawOffsetRad);
			const FQuat ShapeQuat = Rotation * YawOffsetQuat;

			const float Height = FMath::Max(0.0f, Shape.Height.ToFloat());
			const FVector ShapeCenter = ShapeBase + ShapeQuat.GetUpVector() * (Height * 0.5f);

			switch (Shape.Shape)
			{
			case ESeinExtentsShape::Box:
			{
				const FVector HalfExtents(
					FMath::Max(0.0f, Shape.HalfExtentX.ToFloat()),
					FMath::Max(0.0f, Shape.HalfExtentY.ToFloat()),
					Height * 0.5f);
				DrawDebugBox(World, ShapeCenter, HalfExtents, ShapeQuat,
					WireColor, /*bPersistent*/ false, DrawLifetime, /*DepthPriority*/ 0, Thickness);
				break;
			}
			case ESeinExtentsShape::Capsule:
			{
				const float Radius = FMath::Max(0.0f, Shape.Radius.ToFloat());
				if (Radius <= 0.0f) break;
				// Half-height includes the radius (UE capsule convention).
				// Min-clamp to Radius so a zero-Height shape still draws
				// as a sphere — matches the BP visualizer.
				const float HalfHeight = FMath::Max(Height * 0.5f, Radius);
				DrawDebugCapsule(World, ShapeCenter, HalfHeight, Radius, ShapeQuat,
					WireColor, /*bPersistent*/ false, DrawLifetime, /*DepthPriority*/ 0, Thickness);
				break;
			}
			}
		}
	}

	/** Per-frame Extents-shape viz. Dispatches to the right source based on
	 *  world type:
	 *    - **PIE / game world** — walks the sim entity pool, reads runtime
	 *      `FSeinExtentsComponent` data, draws at the entity's sim transform.
	 *    - **Editor world** — walks `TActorIterator<AActor>`, finds actors
	 *      with `USeinEntityComponent`, reads AUTHORED `ComponentData`
	 *      (FInstancedStruct entries), draws at the actor's editor transform.
	 *      This is what lets designers see extents in the level editor
	 *      without entering PIE.
	 *
	 *  Both paths emit identical visuals via `DrawExtentsShapesAt`. Gated by
	 *  the SeinExtents show flag (checked once per world before this is
	 *  called). Camera-cull + budget cap via `ShouldDrawAndReserve`. */
	static void DrawExtentsShapesViz(USeinWorldSubsystem* Sim, UWorld* World)
	{
		if (!World) return;

		if (World->IsGameWorld())
		{
			// PIE / game: sim entity pool path.
			if (!Sim) return;
			FSeinEntityPool& Pool = Sim->GetEntityPool();
			Pool.ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
			{
				const FSeinExtentsComponent* Extents = Sim->GetComponent<FSeinExtentsComponent>(Handle);
				if (!Extents || Extents->Shapes.Num() == 0) return;

				// Camera cull + budget reserve at entity granularity.
				const FFixedVector EntityPosFixed = Entity.Transform.GetLocation();
				const FVector EntityPos(EntityPosFixed.X.ToFloat(),
					EntityPosFixed.Y.ToFloat(), EntityPosFixed.Z.ToFloat());
				if (!UE::SeinARTSMovement::DebugDraw::ShouldDrawAndReserve(World, EntityPos))
				{
					return;
				}

				const FFixedQuaternion EntityRotFixed = Entity.Transform.Rotation;
				const FQuat EntityQuat(
					EntityRotFixed.X.ToFloat(), EntityRotFixed.Y.ToFloat(),
					EntityRotFixed.Z.ToFloat(), EntityRotFixed.W.ToFloat());

				DrawExtentsShapesAt(World, EntityPos, EntityQuat, *Extents);
			});
		}
		else
		{
			// Editor world: walk actors with USeinEntityComponent and read
			// AUTHORED ComponentData. The sim entity pool is empty pre-PIE,
			// so this path is the only way to see extents in the level
			// editor.
			//
			// NO camera cull / budget cap here — editor users want to see
			// every authored unit's extents for level inspection, not just
			// those within 100m of the viewport camera (the runtime cull
			// threshold). Performance is a non-concern in editor; the user
			// can toggle the show flag off if it ever feels slow.
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;
				USeinEntityComponent* Bridge = Actor->FindComponentByClass<USeinEntityComponent>();
				if (!Bridge) continue;

				const FTransform Xform = Actor->GetActorTransform();
				const FVector ActorPos = Xform.GetLocation();
				const FQuat ActorQuat = Xform.GetRotation();

				for (const FInstancedStruct& Entry : Bridge->ComponentData)
				{
					if (!Entry.IsValid()) continue;
					if (Entry.GetScriptStruct() != FSeinExtentsComponent::StaticStruct()) continue;
					const FSeinExtentsComponent& Extents = Entry.Get<FSeinExtentsComponent>();
					if (Extents.Shapes.Num() == 0) continue;
					DrawExtentsShapesAt(World, ActorPos, ActorQuat, Extents);
				}
			}
		}
	}

	static bool DebugDrawTick(float /*DeltaTime*/)
	{
		// Static cells AND dynamic blocker stamps are scene-proxy-driven from the
		// SeinARTSNavigation module (USeinNavDebugComponent rebuilds on
		// OnNavigationMutated). Ticker only handles per-active-move path viz —
		// that's per-unit ephemeral data that doesn't fold cleanly into the
		// global cell mesh. Showflag gate stays per-world to match the proxy's
		// GetViewRelevance behavior (no editor→PIE bleed).
		for (TObjectIterator<USeinNavigationSubsystem> It; It; ++It)
		{
			USeinNavigationSubsystem* Sub = *It;
			if (!IsValid(Sub)) continue;
			UWorld* World = Sub->GetWorld();
			if (!World) continue;
			if (!UE::SeinARTSNavigation::IsNavigationShowFlagOnForWorld(World)) continue;
			DrawActiveMoveDebug(World);
		}

		// Steering vectors — dispatches per world type same as Extents:
		//   - PIE/Game → entity pool walk (footprint ring + velocity arrow)
		//   - Editor   → actor walk reading authored ComponentData (ring only)
		// so designers see authored footprints in the level editor too.
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UWorld* World = Ctx.World();
				if (!World || !World->IsGameWorld()) continue;
				if (!UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld(World)) continue;
				USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
				DrawSteeringVectorsViz(Sim, World);
			}
		}
#if WITH_EDITOR
		if (GEditor)
		{
			// Process every editor world once, even when the flag is OFF,
			// so the ON→OFF transition can invalidate to clear the stale
			// render. Dedup across viewports that share a world.
			TSet<UWorld*> SeenEditorWorlds;
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (!Vp) continue;
				UWorld* World = Vp->GetWorld();
				if (!World || World->IsGameWorld()) continue;
				if (SeenEditorWorlds.Contains(World)) continue;
				SeenEditorWorlds.Add(World);
				const bool bIsOn = UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld(World);
				TickEditorShowFlagDraw(GLastSteeringOnByWorld, World, bIsOn, [World]()
				{
					DrawSteeringVectorsViz(/*Sim*/ nullptr, World);
				});
			}
		}
#endif

		// Extents shapes — dispatches per world type:
		//   - PIE/Game → entity-pool walk (uses Sim)
		//   - Editor   → actor walk reading authored ComponentData (Sim null)
		// so designers see authored extents in the level editor *and* runtime
		// extents in PIE through the same show flag.
		//
		// Two passes deliberately scoped to disjoint world sets so each world
		// is drawn at most once: first pass handles GAME worlds via the world
		// contexts list (where PIE/standalone live); second pass handles
		// EDITOR worlds via the level-editor viewport clients (the editor
		// world doesn't show up consistently in GetWorldContexts and doesn't
		// auto-create UWorldSubsystems).
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UWorld* World = Ctx.World();
				if (!World || !World->IsGameWorld()) continue;
				if (!UE::SeinARTSMovement::IsExtentsShowFlagOnForWorld(World)) continue;
				USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
				DrawExtentsShapesViz(Sim, World);
			}
		}
#if WITH_EDITOR
		if (GEditor)
		{
			// Process every editor world once, even when the flag is OFF,
			// so the ON→OFF transition can invalidate to clear the stale
			// render. Dedup across viewports that share a world.
			TSet<UWorld*> SeenEditorWorlds;
			for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
			{
				if (!Vp) continue;
				UWorld* World = Vp->GetWorld();
				if (!World || World->IsGameWorld()) continue;
				if (SeenEditorWorlds.Contains(World)) continue;
				SeenEditorWorlds.Add(World);
				const bool bIsOn = UE::SeinARTSMovement::IsExtentsShowFlagOnForWorld(World);
				TickEditorShowFlagDraw(GLastExtentsOnByWorld, World, bIsOn, [World]()
				{
					DrawExtentsShapesViz(/*Sim*/ nullptr, World);
				});
			}
		}
#endif

		return true;
	}
}
#endif // UE_ENABLE_DEBUG_DRAWING

void FSeinARTSMovementModule::StartupModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (!GShowSteeringCmd)
	{
		GShowSteeringCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Show.Steering"),
			TEXT("Toggle ShowFlags.SeinSteering across all viewports (custom show flag). When on, movement draws per-unit debug viz: footprint ring, velocity, and avoidance steer. Usage: Sein.Show.Steering [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowSteeringCommand),
			ECVF_Default);
	}
	if (!GShowExtentsCmd)
	{
		GShowExtentsCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Sein.Show.Extents"),
			TEXT("Toggle ShowFlags.SeinExtents across all viewports. When on, each entity's FSeinExtentsComponent shapes draw at runtime — every shape draws as a red wire box / capsule (matching the BP viewport). Usage: Sein.Show.Extents [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowExtentsCommand),
			ECVF_Default);
	}

	GTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&DebugDrawTick), 0.0f);
#endif
}

void FSeinARTSMovementModule::ShutdownModule()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (GShowSteeringCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowSteeringCmd);
		GShowSteeringCmd = nullptr;
	}
	if (GShowExtentsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GShowExtentsCmd);
		GShowExtentsCmd = nullptr;
	}
	FTSTicker::GetCoreTicker().RemoveTicker(GTickHandle);
#endif
}
