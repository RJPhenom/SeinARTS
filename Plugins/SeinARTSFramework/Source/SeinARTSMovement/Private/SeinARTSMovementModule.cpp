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
 *          NavDebug registers a per-view Canvas overlay for managed move
 *          actions. It shows remaining committed geometry, driver segment
 *          progress, targets, and the sim-pose offset in the current rendered
 *          frame; heading and motion stay with the Steering view. The custom
 *          Navigation flag also controls the cell proxy.
 *
 *          Shipping strip: ticker, console command, and helper draw functions
 *          are gated on UE_ENABLE_DEBUG_DRAWING. Shipping still registers the
 *          native simulation-content contributor.
 */

#include "SeinARTSMovementModule.h"
#include "Debug/SeinNavPathDebugDraw.h"
#include "Debug/SeinSteeringDebugDraw.h"
#include "Movement/SeinAvoidance.h"
#include "Movement/SeinAvoidanceDefault.h"
#include "Movement/SeinBasicMovement.h"
#include "Movement/SeinBasicUnitMovement.h"
#include "Movement/SeinMovement.h"
#include "Serialization/SeinMoveToActionCodec.h"
#include "Serialization/SeinMovementCanonicalStateProvider.h"
#include "Serialization/SeinMovementStateCoverageInternal.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UObjectIterator.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Actions/SeinMoveToAction.h"
#include "Debug/SeinDebugDrawCull.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Debug/SeinDebugLegend.h"
#include "Debug/DebugDrawService.h"
#include "SceneInterface.h"
#include "Components/SeinExtentsHelpers.h"  // editor-world cascade: BoundingRadius
#include "Actor/SeinEntityBridgeComponent.h"  // editor-world Extents viz: walks Bridge->ComponentData
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
#include "Types/Entity.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "Editor.h"
#endif
#endif // UE_ENABLE_DEBUG_DRAWING

IMPLEMENT_MODULE(FSeinARTSMovementModule, SeinARTSMovement)

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSMovementModule, Log, All);

namespace
{
	FSeinSimulationContentDiscoveryRoot MakePackageDiscoveryRoot(
		const UClass* RootClass)
	{
		check(RootClass);

		FSeinSimulationContentDiscoveryRoot Root;
		Root.RootClassPath = RootClass->GetPathName();
		Root.StableRecordKindId =
			FSeinSimulationContentManifestCodec::GetCurrentRecordKindId();
		Root.RecordRevision =
			FSeinSimulationContentManifestCodec::CurrentRecordRevision;
		return Root;
	}
}

#if UE_ENABLE_DEBUG_DRAWING
// Custom show flags. UE doesn't ship matching built-ins, so we register them
// via TCustomShowFlag (same pattern as SeinARTSFogOfWar).
//   - SeinSteering: footprint ring, velocity arrow, avoidance arrow,
//     perception circle, look-ahead cone. Module-public so per-controller
//     draw sites can share the gate (via IsSteeringShowFlagOnForWorld).
//   - SeinExtents: each entity's FSeinExtentsPayload shapes in PIE and the
//     level editor — every shape draws as a red wire box / capsule, matching
//     the BP-viewport extents visualizer so the same shape reads identically
//     in both contexts. Sim-side concept (Extents lives in SeinARTSCoreEntity)
//     but hosted here for ticker-infrastructure reuse — SeinARTSCoreEntity has
//     no debug-draw pipeline of its own.
//
// SFG_Hidden keeps these runtime registrations out of Unreal's fixed built-in
// groups. SeinARTSEditor supplies the first-class level/PIE SeinARTS submenu;
// Blueprint authoring visualization remains separately always-on while the
// entity bridge is selected and must not expose runtime toggles.
namespace UE::SeinARTSMovement
{
	static TCustomShowFlag<> ShowSteering(
		TEXT("SeinSteering"),
		/*DefaultEnabled*/ false,
		SFG_Hidden,
		NSLOCTEXT("SeinARTSMovement", "ShowSteering", "Steering"));

	static TCustomShowFlag<> ShowExtents(
		TEXT("SeinExtents"),
		/*DefaultEnabled*/ false,
		SFG_Hidden,
		NSLOCTEXT("SeinARTSMovement", "ShowExtents", "Extents"));
}

namespace
{
	/** Generic show-flag plumbing shared by the Steering and Extents flags.
	 *  Wraps one TCustomShowFlag with the viewport-iterating toggle/query/
	 *  console-command boilerplate that was previously copy-pasted per flag.
	 *  The `LogName` is the human-facing console command name (e.g.
	 *  "Sein.Show.Steering") used only for the toggle's UE_LOG line. */
	struct FSeinShowFlagToggle
	{
		TCustomShowFlag<>& Flag;
		const TCHAR* LogName;
		const TCHAR* FlagName;

		/** True iff some viewport rendering `World` currently has the flag on.
		 *  Matches by world so toggling off on the user's visible viewport
		 *  hides the viz for that viewport's units even if some other viewport
		 *  (editor / other PIE) still has the flag on. */
		bool IsOnForWorld(UWorld* World) const
		{
			if (!World) return false;

#if WITH_EDITOR
			if (GEditor)
			{
				for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
				{
					if (Vp && Vp->GetWorld() == World && Flag.IsEnabled(Vp->EngineShowFlags))
					{
						return true;
					}
				}
			}
#endif
			// Enumerate every FWorldContext's GameViewport — in multi-client
			// PIE there are multiple GameViewports and only one is
			// GEngine->GameViewport.
			if (GEngine)
			{
				for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
				{
					if (Ctx.GameViewport && Ctx.GameViewport->GetWorld() == World
					    && Flag.IsEnabled(Ctx.GameViewport->EngineShowFlags))
					{
						return true;
					}
				}
			}
			return false;
		}

		/** Set the flag across all editor + game viewport clients. Iterates
		 *  every FWorldContext's GameViewport so multi-client PIE applies the
		 *  toggle to every PIE window, not just the primary one pointed at by
		 *  GEngine->GameViewport. */
		void SetEnabledAllViewports(bool bEnable) const
		{
#if WITH_EDITOR
			if (GEditor)
			{
				for (FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
				{
					if (Vp)
					{
						Flag.SetEnabled(Vp->EngineShowFlags, bEnable);
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
						Flag.SetEnabled(Ctx.GameViewport->EngineShowFlags, bEnable);
					}
				}
			}
		}

		/** "Any viewport has the flag" check used by the console-command toggle.
		 *  Per-tick draw gates use the per-world IsOnForWorld instead — only the
		 *  toggle command needs a global "current state" query. */
		bool IsOnAnyViewport() const
		{
#if WITH_EDITOR
			if (GEditor)
			{
				for (const FLevelEditorViewportClient* Vp : GEditor->GetLevelViewportClients())
				{
					if (Vp && Flag.IsEnabled(Vp->EngineShowFlags)) return true;
				}
			}
#endif
			if (GEngine)
			{
				for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
				{
					if (Ctx.GameViewport && Flag.IsEnabled(Ctx.GameViewport->EngineShowFlags))
					{
						return true;
					}
				}
			}
			return false;
		}

		/** Console-command handler. Parses on/off the same way as
		 *  ShowNavigation; no-args = toggle. */
		void HandleCommand(const TArray<FString>& Args) const
		{
			bool bEnable = !IsOnAnyViewport();
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
			SetEnabledAllViewports(bEnable);
			UE_LOG(LogTemp, Log, TEXT("%s = %s (ShowFlags.%s)"),
				LogName, bEnable ? TEXT("ON") : TEXT("OFF"), FlagName);
		}
	};

	// The two toggles — one instance of the shared helper per flag.
	static FSeinShowFlagToggle GSteeringToggle{
		UE::SeinARTSMovement::ShowSteering, TEXT("Sein.Show.Steering"), TEXT("SeinSteering") };
	static FSeinShowFlagToggle GExtentsToggle{
		UE::SeinARTSMovement::ShowExtents, TEXT("Sein.Show.Extents"), TEXT("SeinExtents") };

	IConsoleCommand* GShowSteeringCmd = nullptr;
	IConsoleCommand* GShowExtentsCmd = nullptr;
	FDelegateHandle GExtentsLegendHandle;

	static void DrawExtentsLegend(UCanvas* Canvas, APlayerController*)
	{
		using namespace UE::SeinARTS::DebugLegend;
		if (!Canvas || !Canvas->SceneView || !Canvas->SceneView->Family || !Canvas->SceneView->Family->Scene) return;
		const UWorld* World = Canvas->SceneView->Family->Scene->GetWorld();
		if (!World) return;
		const FPanel Panel(Canvas, EPanel::Extents);
		if (!Panel.IsVisible()) return;
		Panel.Text(8, World->IsGameWorld() ? TEXT("Extents: entity shapes | distance / view / shared-budget limited")
			: TEXT("Extents: authored entity shapes | no distance or budget limit"), FLinearColor(1, 0.15f, 0.15f));
		Panel.Text(24, TEXT("Red wire: boxes / capsules, including shape offsets, yaw and height"), FLinearColor(1, 0.15f, 0.15f));
		Panel.Text(40, World->IsGameWorld() ? TEXT("Runtime data at simulation pose; depth-tested against the scene")
			: TEXT("Editor: authored shapes at actor pose; depth-tested against the scene"), FLinearColor::White);
		Panel.Text(56, TEXT("Steering's amber ring shows the separate movement footprint radius"), FLinearColor(1, 0.65f, 0));
	}
	FTSTicker::FDelegateHandle GTickHandle;

	static void OnShowSteeringCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		GSteeringToggle.HandleCommand(Args);
	}

	static void OnShowExtentsCommand(const TArray<FString>& Args, UWorld* /*WorldContext*/)
	{
		GExtentsToggle.HandleCommand(Args);
	}
}

namespace UE::SeinARTSMovement
{
	bool IsSteeringShowFlagOnForWorld(UWorld* World)
	{
		return GSteeringToggle.IsOnForWorld(World);
	}

	bool IsExtentsShowFlagOnForWorld(UWorld* World)
	{
		return GExtentsToggle.IsOnForWorld(World);
	}
}

namespace
{
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
		const FSeinExtentsPayload& Extents)
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
	 *      `FSeinExtentsPayload` data, draws at the entity's sim transform.
	 *    - **Editor world** — walks `TActorIterator<AActor>`, finds actors
	 *      with `USeinEntityBridgeComponent`, reads AUTHORED `ComponentData`
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
			const FSeinEntityPool& Pool = Sim->GetEntityPool();
			Pool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
			{
				const FSeinExtentsPayload* Extents = Sim->GetComponent<FSeinExtentsPayload>(Handle);
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
			// Editor world: walk actors with USeinEntityBridgeComponent and read
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
				USeinEntityBridgeComponent* Bridge = Actor->FindComponentByClass<USeinEntityBridgeComponent>();
				if (!Bridge) continue;

				const FTransform Xform = Actor->GetActorTransform();
				const FVector ActorPos = Xform.GetLocation();
				const FQuat ActorQuat = Xform.GetRotation();

				for (const FInstancedStruct& Entry : Bridge->ComponentData)
				{
					if (!Entry.IsValid()) continue;
					if (Entry.GetScriptStruct() != FSeinExtentsPayload::StaticStruct()) continue;
					const FSeinExtentsPayload& Extents = Entry.Get<FSeinExtentsPayload>();
					if (Extents.Shapes.Num() == 0) continue;
					DrawExtentsShapesAt(World, ActorPos, ActorQuat, Extents);
				}
			}
		}
	}

	static bool DebugDrawTick(float /*DeltaTime*/)
	{
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
					// Canvas callback draws per view; only request editor redraw here.
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

namespace
{
#if UE_ENABLE_DEBUG_DRAWING
	void ReleaseMovementDebugBindings()
	{
		if (GExtentsLegendHandle.IsValid()) UDebugDrawService::Unregister(GExtentsLegendHandle);
		GExtentsLegendHandle.Reset();
		UE::SeinARTSMovement::NavDebug::UnregisterDraw();
		UE::SeinARTSMovement::SteeringDebug::UnregisterDraw();
		if (GShowSteeringCmd)
		{
			IConsoleManager::Get().UnregisterConsoleObject(
				GShowSteeringCmd);
			GShowSteeringCmd = nullptr;
		}
		if (GShowExtentsCmd)
		{
			IConsoleManager::Get().UnregisterConsoleObject(
				GShowExtentsCmd);
			GShowExtentsCmd = nullptr;
		}
		if (GTickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GTickHandle);
			GTickHandle.Reset();
		}
	}
#else
	void ReleaseMovementDebugBindings() {}
#endif

	FSeinMovementStateCoverageRegistrationHandle RegisterBuiltInCoverage(
		const UClass* NativeClass,
		ESeinMovementStateCoverage Coverage,
		FString& OutError)
	{
		FSeinMovementStateCoverageDescriptor Descriptor;
		Descriptor.NativeClass = NativeClass;
		Descriptor.Coverage = Coverage;
		return FSeinMovementStateCoverageRegistry::Register(
			TEXT("SeinARTSMovement"), Descriptor, &OutError);
	}
}

void FSeinARTSMovementModule::StartupModule()
{
	SeinSetMovementCoverageProviderRefreshEnabled(false);
	CanonicalStateRegistrationHandle.Reset();
	MoveToActionCodecRegistrationHandle.Reset();
	BuiltInCoverageHandles.Reset();
	SimulationContentRegistrationHandle.Reset();

	FSeinSimulationContentContributorDescriptor ContentDescriptor;
	ContentDescriptor.OwnerModule = TEXT("SeinARTSMovement");
	ContentDescriptor.StableContributorId = TEXT("seinarts.movement");
	ContentDescriptor.ContributorRevision = 1;
	ContentDescriptor.DiscoveryRoots = {
		MakePackageDiscoveryRoot(USeinMovement::StaticClass()),
		MakePackageDiscoveryRoot(USeinAvoidance::StaticClass()),
	};

	FString ContentRegistrationError;
	SimulationContentRegistrationHandle =
		FSeinSimulationContentRegistry::RegisterContributor(
			ContentDescriptor,
			&ContentRegistrationError);
	if (!SimulationContentRegistrationHandle.IsValid())
	{
		UE_LOG(
			LogSeinARTSMovementModule,
			Error,
			TEXT("Simulation-content contributor '%s' failed to register: %s"),
			*ContentDescriptor.StableContributorId,
			*ContentRegistrationError);
	}

	auto AddCoverage = [this](
		const UClass* Class,
		ESeinMovementStateCoverage Coverage)
	{
		FString Error;
		FSeinMovementStateCoverageRegistrationHandle Handle =
			RegisterBuiltInCoverage(Class, Coverage, Error);
		if (!Handle.IsValid())
		{
			UE_LOG(LogSeinARTSMovementModule, Error,
				TEXT("State coverage registration failed for '%s': %s"),
				Class ? *Class->GetPathName() : TEXT("<null>"),
				*Error);
			return;
		}
		BuiltInCoverageHandles.Add(MoveTemp(Handle));
	};
	AddCoverage(
		USeinMovement::StaticClass(),
		ESeinMovementStateCoverage::ReflectedComplete);
	AddCoverage(
		USeinBasicMovement::StaticClass(),
		ESeinMovementStateCoverage::Stateless);
	AddCoverage(
		USeinBasicUnitMovement::StaticClass(),
		ESeinMovementStateCoverage::Stateless);
	AddCoverage(
		USeinAvoidance::StaticClass(),
		ESeinMovementStateCoverage::Stateless);
	AddCoverage(
		USeinAvoidanceDefault::StaticClass(),
		ESeinMovementStateCoverage::ReflectedComplete);

	FString CanonicalError;
	if (!RefreshCanonicalStateProvider(CanonicalError))
	{
		UE_LOG(LogSeinARTSMovementModule, Error,
			TEXT("Movement canonical-state provider failed to register: %s"),
			*CanonicalError);
	}
	FString MoveToCodecError;
	MoveToActionCodecRegistrationHandle =
		SeinRegisterMoveToActionCodec(MoveToCodecError);
	if (!MoveToActionCodecRegistrationHandle.IsValid())
	{
		UE_LOG(LogSeinARTSMovementModule, Error,
			TEXT("Move To continuation codec failed to register: %s"),
			*MoveToCodecError);
	}
	SeinSetMovementCoverageProviderRefreshEnabled(true);

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
			TEXT("Toggle ShowFlags.SeinExtents across all viewports. When on, each entity's FSeinExtentsPayload shapes draw at runtime — every shape draws as a red wire box / capsule (matching the BP viewport). Usage: Sein.Show.Extents [0|1|on|off]."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&OnShowExtentsCommand),
			ECVF_Default);
	}

	GTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&DebugDrawTick), 0.0f);
	UE::SeinARTSMovement::NavDebug::RegisterDraw();
	UE::SeinARTSMovement::SteeringDebug::RegisterDraw();
	if (!GExtentsLegendHandle.IsValid())
		GExtentsLegendHandle = UDebugDrawService::Register(TEXT("SeinExtents"), FDebugDrawDelegate::CreateStatic(&DrawExtentsLegend));
#endif
}

bool FSeinARTSMovementModule::RefreshCanonicalStateProvider(
	FString& OutError)
{
	check(IsInGameThread());
	FSeinMovementStateCoverageSnapshot Coverage;
	if (!SeinBuildMovementStateCoverageSnapshot(
			Coverage, OutError,
			/*bRequireCompleteLoadedClasses*/ false))
	{
		return false;
	}

	// Different coverage manifests intentionally conflict under one key, so
	// withdraw this generation before publishing the replacement.
	CanonicalStateRegistrationHandle.Reset();
	CanonicalStateRegistrationHandle =
		SeinRegisterMovementCanonicalStateProvider(
			Coverage, OutError);
	return CanonicalStateRegistrationHandle.IsValid();
}

void FSeinARTSMovementModule::PreUnloadCallback()
{
	check(IsInGameThread());
	SeinSetMovementCoverageProviderRefreshEnabled(false);
	ReleaseMovementDebugBindings();

	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSMovement"),
				TEXT("movement systems and persistent policy instances are unloading"));
		}
	}

	for (TObjectIterator<USeinMovementSubsystem> It; It; ++It)
	{
		if (It->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
		It->ReleaseNativeClassStateForModuleUnload(
			TEXT("SeinARTSMovement"));
		It->ReleaseModuleOwnedStateForModuleUnload();
	}

	MoveToActionCodecRegistrationHandle.Reset();
	CanonicalStateRegistrationHandle.Reset();
	BuiltInCoverageHandles.Reset();
	SimulationContentRegistrationHandle.Reset();
}

void FSeinARTSMovementModule::ShutdownModule()
{
	SeinSetMovementCoverageProviderRefreshEnabled(false);
	ReleaseMovementDebugBindings();
	MoveToActionCodecRegistrationHandle.Reset();
	CanonicalStateRegistrationHandle.Reset();
	BuiltInCoverageHandles.Reset();
	SimulationContentRegistrationHandle.Reset();
}
