/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavDebugComponent.cpp
 *
 * Scene-proxy pattern follows UE's FNavMeshSceneProxy (Engine/Private/
 * NavMesh/NavMeshRenderingComponent.cpp):
 *   - View relevance sets bSeparateTranslucency + bNormalTranslucency (the
 *     DebugMeshMaterial is translucent; setting only bOpaque results in zero
 *     visible output).
 *   - EngineShowFlags.Navigation gating happens in both GetViewRelevance
 *     (early frustum cull) AND GetDynamicMeshElements (belt-and-braces).
 *   - One FDynamicMeshBuilder per color bucket per view — submitted via
 *     FColoredMaterialRenderProxy wrapping GEngine->DebugMeshMaterial.
 *     Two draw calls per view regardless of cell count.
 *
 * Shipping strip: the scene-proxy class, all method bodies, and all render-
 * specific includes are gated on UE_ENABLE_DEBUG_DRAWING. The component class
 * declaration stays (for ABI + reflection consistency) but every virtual
 * compiles to a no-op stub in shipping builds.
 */

#include "Debug/SeinNavDebugComponent.h"
#include "SeinARTSNavigationModule.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Volumes/SeinLevelVolume.h"
#include "SeinLevelDataDefaultAsset.h"
#include "Settings/PluginSettings.h"

#include "Engine/World.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
#include "MeshElementCollector.h"
#include "SeinARTSNavigationLog.h"

// ============================================================================
// Scene proxy (debug-only — class doesn't exist in shipping)
// ============================================================================

/** One color group of cells for the proxy. The collectors emit per-cell colors;
 *  CreateSceneProxy buckets them into these groups so GetDynamicMeshElements can
 *  render one batched mesh per color. Used for BOTH the static nav cells (walkable
 *  green / blocked red / terrain-type DebugColor) and the dynamic-blocker overlay —
 *  any color the collector emits renders faithfully, instead of being snapped to a
 *  fixed green/red. */
struct FSeinNavBlockerBucket
{
	FLinearColor Color;
	TArray<FVector> Centers;
};

/** Immutable baked-grid payload. Shared between the game-thread component and
 *  render-thread scene proxies so a moving dynamic blocker never forces an
 *  O(all nav cells) recollect/rebucket/copy on the game thread. */
struct FSeinNavDebugStaticSnapshot
{
	TArray<FSeinNavBlockerBucket> Buckets;
	float HalfExtent = 0.0f;
};

class FSeinNavDebugProxy final : public FPrimitiveSceneProxy
{
public:
	FSeinNavDebugProxy(UPrimitiveComponent* InComponent,
	                   TSharedPtr<const FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe> InStaticSnapshot,
	                   TArray<FSeinNavBlockerBucket>&& InBlockerBuckets,
	                   float InBlockerHalfExtent)
		: FPrimitiveSceneProxy(InComponent)
		, StaticSnapshot(MoveTemp(InStaticSnapshot))
		, BlockerBuckets(MoveTemp(InBlockerBuckets))
		, BlockerHalfExtent(InBlockerHalfExtent)
	{
		bWillEverBeLit = false;
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		uint32 Bytes = sizeof(*this)
		     + BlockerBuckets.GetAllocatedSize();
		for (const FSeinNavBlockerBucket& B : BlockerBuckets) { Bytes += B.Centers.GetAllocatedSize(); }
		return Bytes;
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		const bool bVisible = !!View->Family->EngineShowFlags.Navigation;
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = bVisible && IsShown(View);
		Result.bDynamicRelevance = true;
		// DebugMeshMaterial is translucent — setting these pass flags is what
		// actually gets the mesh submitted. bOpaque does not work.
		Result.bSeparateTranslucency = Result.bNormalTranslucency = bVisible && IsShown(View);
		Result.bShadowRelevance = false;
		Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
		return Result;
	}

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		UMaterialInterface* BaseMat = GEngine ? GEngine->DebugMeshMaterial : nullptr;
		if (!BaseMat) return;
		const FMaterialRenderProxy* BaseProxy = BaseMat->GetRenderProxy();
		if (!BaseProxy) return;

		for (int32 ViewIdx = 0; ViewIdx < Views.Num(); ++ViewIdx)
		{
			if (!(VisibilityMap & (1 << ViewIdx))) continue;

			const FSceneView* View = Views[ViewIdx];
			if (!View->Family->EngineShowFlags.Navigation) continue;

			// Pass the full FSceneView through to EmitQuads — gives it the
			// frustum (per-cell cull), camera location (per-cell distance
			// cull), and feature level. All three plugin-settings knobs
			// (`bDebugDrawFrustumCullEnabled`, `DebugDrawMaxDistance`,
			// `DebugDrawMaxEntities`) are read inside EmitQuads.

			// Static nav cells — one batched mesh per color bucket (walkable green,
			// blocked red, terrain-type DebugColor). Renders each cell's TRUE color
			// from the collector instead of snapping every cell to a fixed green/red.
			if (StaticSnapshot.IsValid())
			{
				for (const FSeinNavBlockerBucket& Bucket : StaticSnapshot->Buckets)
				{
					if (Bucket.Centers.Num() == 0) continue;
					const FColoredMaterialRenderProxy* BucketMat = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
						BaseProxy, Bucket.Color);
					FDynamicMeshBuilder Builder(View->GetFeatureLevel());
					EmitQuads(Builder, Bucket.Centers, StaticSnapshot->HalfExtent, View);
					Builder.GetMesh(FMatrix::Identity, BucketMat, SDPG_World,
						true /*bDisableBackfaceCulling*/, false /*bReceivesDecals*/,
						ViewIdx, Collector);
				}
			}
			// One mesh per dynamic-blocker color bucket (own half-extent). Typical scene
			// has 1-3 buckets (single layer most blockers) so the per-view cost stays small.
			for (const FSeinNavBlockerBucket& Bucket : BlockerBuckets)
			{
				if (Bucket.Centers.Num() == 0) continue;
				const FColoredMaterialRenderProxy* BucketMat = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
					BaseProxy, Bucket.Color);
				FDynamicMeshBuilder Builder(View->GetFeatureLevel());
				EmitQuads(Builder, Bucket.Centers, BlockerHalfExtent, View);
				Builder.GetMesh(FMatrix::Identity, BucketMat, SDPG_World,
					true, false, ViewIdx, Collector);
			}
		}
	}

private:

	void EmitQuads(FDynamicMeshBuilder& Builder, const TArray<FVector>& Centers, float QuadHalfExtent,
		const FSceneView* View) const
	{
		// Read settings — three knobs gate emission per-cell:
		//   bDebugDrawFrustumCullEnabled : full frustum test against the view
		//   DebugDrawMaxDistance         : skip cells beyond this from camera
		//   DebugDrawMaxEntities         : cap on emitted cells per bucket
		//                                  (closest-to-camera selected first)
		const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
		const bool bUseFrustum = !Settings || Settings->bDebugDrawFrustumCullEnabled;
		const float MaxDist = Settings ? Settings->DebugDrawMaxDistance : 10000.0f;
		const int32 MaxItems = Settings ? FMath::Max(Settings->DebugDrawMaxEntities, 1) : 10000;
		const float MaxDistSq = MaxDist * MaxDist;

		const FConvexVolume* ViewFrustum = (bUseFrustum && View) ? &View->ViewFrustum : nullptr;
		const FVector CamLoc = View ? View->ViewLocation : FVector::ZeroVector;

		// Frustum-cull AABB extent: XY matches the quad, Z gets a small safety
		// pad (10cm) so a quad sitting exactly on a frustum plane still gets
		// caught — flat-zero-Z boxes can be spuriously rejected at glancing
		// angles.
		const FVector CullExtent(QuadHalfExtent, QuadHalfExtent, 10.0f);

		// Bounded closest-N selection: max-heap of (DistSq, Center) keeps the
		// MaxItems closest-to-camera cells across the whole grid. Memory is
		// O(MaxItems) regardless of input size; per-cell cost is O(log MaxItems)
		// for the heap ops. Heap predicate inverts `<` so the top is the
		// LARGEST DistSq — when a closer candidate arrives we pop the top
		// (farthest) and push the new one.
		struct FCandidate { float DistSq; FVector Center; };
		auto MaxHeapPred = [](const FCandidate& A, const FCandidate& B) { return A.DistSq > B.DistSq; };

		TArray<FCandidate> Selected;
		Selected.Reserve(MaxItems);

		for (const FVector& C : Centers)
		{
			// Per-cell frustum cull (gated). Early-outs on first rejecting plane.
			if (ViewFrustum && !ViewFrustum->IntersectBox(C, CullExtent)) continue;

			// Distance cull. Squared comparison; no sqrt.
			const float DistSq = static_cast<float>(FVector::DistSquared(C, CamLoc));
			if (DistSq > MaxDistSq) continue;

			// Closest-N selection.
			if (Selected.Num() < MaxItems)
			{
				Selected.HeapPush(FCandidate{DistSq, C}, MaxHeapPred);
			}
			else if (DistSq < Selected.HeapTop().DistSq)
			{
				FCandidate Discard;
				Selected.HeapPop(Discard, MaxHeapPred, EAllowShrinking::No);
				Selected.HeapPush(FCandidate{DistSq, C}, MaxHeapPred);
			}
		}

		Builder.ReserveVertices(Selected.Num() * 4);
		Builder.ReserveTriangles(Selected.Num() * 2);

		const FVector3f TangentX(1.0f, 0.0f, 0.0f);
		const FVector3f TangentY(0.0f, 1.0f, 0.0f);
		const FVector3f TangentZ(0.0f, 0.0f, 1.0f);

		for (const FCandidate& Cand : Selected)
		{
			const FVector& C = Cand.Center;
			FDynamicMeshVertex V0, V1, V2, V3;
			V0.Position = FVector3f(C + FVector(-QuadHalfExtent, -QuadHalfExtent, 0));
			V1.Position = FVector3f(C + FVector( QuadHalfExtent, -QuadHalfExtent, 0));
			V2.Position = FVector3f(C + FVector( QuadHalfExtent,  QuadHalfExtent, 0));
			V3.Position = FVector3f(C + FVector(-QuadHalfExtent,  QuadHalfExtent, 0));
			V0.TextureCoordinate[0] = FVector2f(0, 0);
			V1.TextureCoordinate[0] = FVector2f(1, 0);
			V2.TextureCoordinate[0] = FVector2f(1, 1);
			V3.TextureCoordinate[0] = FVector2f(0, 1);
			V0.SetTangents(TangentX, TangentY, TangentZ);
			V1.SetTangents(TangentX, TangentY, TangentZ);
			V2.SetTangents(TangentX, TangentY, TangentZ);
			V3.SetTangents(TangentX, TangentY, TangentZ);

			const int32 Base = Builder.AddVertex(V0);
			Builder.AddVertex(V1);
			Builder.AddVertex(V2);
			Builder.AddVertex(V3);
			Builder.AddTriangle(Base + 0, Base + 1, Base + 2);
			Builder.AddTriangle(Base + 0, Base + 2, Base + 3);
		}
	}

	TSharedPtr<const FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe> StaticSnapshot;
	TArray<FSeinNavBlockerBucket> BlockerBuckets;
	float BlockerHalfExtent;
};

#endif // UE_ENABLE_DEBUG_DRAWING

// ============================================================================
// Component — class stays in shipping for ABI, method bodies become no-ops
// ============================================================================

USeinNavDebugComponent::USeinNavDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	bSelectable = false;
	bUseAsOccluder = false;
	bCastDynamicShadow = false;
	bCastStaticShadow = false;
	bHiddenInGame = false;
	SetMobility(EComponentMobility::Static);
}

FPrimitiveSceneProxy* USeinNavDebugComponent::CreateSceneProxy()
{
#if UE_ENABLE_DEBUG_DRAWING
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	TSharedPtr<const FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe> StaticSnapshot;
	TArray<FSeinNavBlockerBucket> BlockerBuckets;
	float BlockerHalfExtent = 0.0f;

	// Bucket (centers, colors) by EXACT color into per-color groups — shared by the live
	// and asset-preview paths. Each distinct color becomes one batched mesh, so a cell's
	// authored color (walkable green, blocked red, OR a terrain type's DebugColor) renders
	// faithfully. Replaces the old binary "R>G ⇒ blocked/red, else walkable/green" snap,
	// which painted every terrain-tinted (R>G) cell as blocked-red.
	auto BucketByColor = [](const TArray<FVector>& Centers, const TArray<FColor>& Colors,
		TArray<FSeinNavBlockerBucket>& OutBuckets)
	{
		const int32 Num = FMath::Min(Centers.Num(), Colors.Num());
		for (int32 i = 0; i < Num; ++i)
		{
			const FColor& Col = Colors[i];
			FSeinNavBlockerBucket* Match = OutBuckets.FindByPredicate(
				[&Col](const FSeinNavBlockerBucket& B) { return B.Color.ToFColor(true) == Col; });
			if (!Match)
			{
				FSeinNavBlockerBucket NewBucket;
				NewBucket.Color = FLinearColor(Col);
				Match = &OutBuckets.Add_GetRef(MoveTemp(NewBucket));
			}
			Match->Centers.Add(Centers[i]);
		}
	};

	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	USeinNavigation* Nav = Sub ? Sub->GetNavigation() : nullptr;
	if (Nav && Nav->HasRuntimeData())
	{
		const uint64 StaticGeneration = Nav->GetStaticEnvironmentGeneration();
		if (!CachedStaticSnapshot.IsValid()
			|| CachedStaticNav.Get() != Nav
			|| CachedStaticGeneration != StaticGeneration)
		{
			TArray<FVector> Centers;
			TArray<FColor> Colors;
			float HalfExtent = 0.0f;
			Nav->CollectDebugCellQuads(Centers, Colors, HalfExtent);

			TSharedRef<FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe> NewSnapshot =
				MakeShared<FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe>();
			NewSnapshot->HalfExtent = HalfExtent;
			BucketByColor(Centers, Colors, NewSnapshot->Buckets);
			CachedStaticSnapshot = NewSnapshot;
			CachedStaticNav = Nav;
			CachedStaticGeneration = StaticGeneration;
		}
		StaticSnapshot = CachedStaticSnapshot;

		// Dynamic blocker cells (overlay above static cells). Routed through the
		// same scene proxy — folds the previously-per-frame DrawDebugSolidBox
		// path into the batched mesh rebuild that fires on OnNavigationMutated.
		// Per-cell colors come from the nav (resolved against plugin-settings
		// layer colors so a Default-only blocker reads red, an N0 blocker reads
		// N0's color, etc.) — bucketed below for efficient batched rendering.
		TArray<FVector> BlockerCenters;
		TArray<FColor> BlockerColors;
		Nav->CollectDebugBlockerCells(BlockerCenters, BlockerColors, BlockerHalfExtent);

		if (BlockerCenters.Num() == BlockerColors.Num())
		{
			for (int32 i = 0; i < BlockerCenters.Num(); ++i)
			{
				const FColor& Col = BlockerColors[i];
				FSeinNavBlockerBucket* Match = BlockerBuckets.FindByPredicate(
					[&Col](const FSeinNavBlockerBucket& B) { return B.Color.ToFColor(true) == Col; });
				if (!Match)
				{
					FSeinNavBlockerBucket NewBucket;
					NewBucket.Color = FLinearColor(Col);
					Match = &BlockerBuckets.Add_GetRef(MoveTemp(NewBucket));
				}
				Match->Centers.Add(BlockerCenters[i]);
			}
		}
	}
	else
	{
		// Editor-idle preview — no live nav grid (pre-PIE the subsystems don't
		// auto-load baked level data), so read the owner volume's baked asset
		// directly. No blocker cells: dynamic blockers only exist in a live sim.
		TArray<FVector> Centers;
		TArray<FColor> Colors;
		float HalfExtent = 0.0f;
		CollectAssetPreviewQuads(Centers, Colors, HalfExtent);
		TSharedRef<FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe> NewSnapshot =
			MakeShared<FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe>();
		NewSnapshot->HalfExtent = HalfExtent;
		BucketByColor(Centers, Colors, NewSnapshot->Buckets);
		StaticSnapshot = NewSnapshot;
	}

	const int32 StaticBucketCount = StaticSnapshot.IsValid() ? StaticSnapshot->Buckets.Num() : 0;
	if (StaticBucketCount == 0 && BlockerBuckets.Num() == 0)
	{
		UE_LOG(LogSeinNavDebug, Verbose, TEXT("CreateSceneProxy: 0 static + 0 blocker cells (no live nav grid + no baked asset preview)"));
		return nullptr;
	}

	UE_LOG(LogSeinNavDebug, Verbose,
		TEXT("CreateSceneProxy: %d static color buckets + %d blocker color buckets, staticHE=%.1f blockerHE=%.1f"),
		StaticBucketCount, BlockerBuckets.Num(),
		StaticSnapshot.IsValid() ? StaticSnapshot->HalfExtent : 0.0f,
		BlockerHalfExtent);

	return new FSeinNavDebugProxy(this,
		MoveTemp(StaticSnapshot), MoveTemp(BlockerBuckets), BlockerHalfExtent);
#else
	return nullptr;
#endif
}

FBoxSphereBounds USeinNavDebugComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Cells live in world space. Bound large enough to never get frustum-culled
	// at reasonable camera distances, but within UE's HALF_WORLD_MAX envelope.
	return FBoxSphereBounds(FBox(FVector(-1048576.0), FVector(1048576.0)));
}

void USeinNavDebugComponent::OnRegister()
{
	Super::OnRegister();

#if UE_ENABLE_DEBUG_DRAWING
	UWorld* World = GetWorld();
	if (!World) return;
	if (USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>())
	{
		if (USeinNavigation* Nav = Sub->GetNavigation())
		{
			SubscribedNav = Nav;
			NavMutatedHandle = Nav->OnNavigationMutated.AddUObject(this, &USeinNavDebugComponent::HandleNavMutated);
		}
	}
	MarkRenderStateDirty();
#endif
}

void USeinNavDebugComponent::OnUnregister()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (USeinNavigation* Nav = SubscribedNav.Get())
	{
		Nav->OnNavigationMutated.Remove(NavMutatedHandle);
	}
	SubscribedNav.Reset();
	NavMutatedHandle.Reset();
	CachedStaticSnapshot.Reset();
	CachedStaticNav.Reset();
	CachedStaticGeneration = MAX_uint64;
#endif
	Super::OnUnregister();
}

void USeinNavDebugComponent::HandleNavMutated()
{
#if UE_ENABLE_DEBUG_DRAWING
	// Navigation can mutate every fixed tick while units carrying blocker
	// stamps move. A hidden debug viewer must have zero rebuild cost. Enabling
	// the showflag explicitly dirties all proxies, so the first visible frame
	// still receives the latest blocker state.
	if (!UE::SeinARTSNavigation::IsNavigationShowFlagOnForWorld(GetWorld()))
	{
		return;
	}
	MarkRenderStateDirty();
#endif
}

void USeinNavDebugComponent::CollectAssetPreviewQuads(
	TArray<FVector>& OutCenters, TArray<FColor>& OutColors, float& OutHalfExtent) const
{
#if UE_ENABLE_DEBUG_DRAWING
	const ASeinLevelVolume* Vol = Cast<ASeinLevelVolume>(GetOwner());
	if (!Vol) return;

	const USeinLevelDataDefaultAsset* Asset = Cast<USeinLevelDataDefaultAsset>(Vol->BakedAsset.LoadSynchronous());
	if (!Asset) return;

	const int32 N = Asset->Width * Asset->Height;
	if (N <= 0) return;

	// Baked "Nav" channel layout: [CellCost: uint8 × N][CellConnections: uint8 × N].
	// Only the cost half is read here (walkable/blocked split).
	const FSeinLevelChannelBlock* NavBlock = Asset->Channels.FindByPredicate(
		[](const FSeinLevelChannelBlock& B) { return B.LayerId == TEXT("Nav"); });
	if (!NavBlock || NavBlock->Data.Num() != 2 * N) return;
	const uint8* CellCost = NavBlock->Data.GetData();

	// Per-cell terrain type (baked into the asset with the terrain feature) tints walkable
	// cells by the type's DebugColor — matches the live nav viz. Empty on assets baked
	// before terrain types existed → all-Default → plain green.
	const bool bHasTerrain = Asset->CellTerrainType.Num() == N;
	const USeinARTSCoreSettings* TerrainSettings = GetDefault<USeinARTSCoreSettings>();

	const float CS = Asset->CellSize.ToFloat();
	OutHalfExtent = CS * 0.5f * 0.9f; // same z-fight inset as the live nav viz
	const float OriginX = Asset->Origin.X.ToFloat();
	const float OriginY = Asset->Origin.Y.ToFloat();

	// Shared height dequantization: world_z = HeightMin + q * HeightQuantum.
	const bool bHasHeight = Asset->SharedHeightQ.Num() == N;
	const float HeightMin = Asset->HeightMin.ToFloat();
	const float HeightQuantum = Asset->HeightQuantum.ToFloat();
	const float FallbackZ = Asset->Origin.Z.ToFloat();

	OutCenters.Reserve(N);
	OutColors.Reserve(N);
	for (int32 Y = 0; Y < Asset->Height; ++Y)
	{
		for (int32 X = 0; X < Asset->Width; ++X)
		{
			const int32 I = Y * Asset->Width + X;
			const uint8 C = CellCost[I];
			const bool bWalkable = C > 0 && C < 255; // 0 / 255 = blocked
			FColor Color = bWalkable ? FColor(0, 200, 0, 160) : FColor(200, 0, 0, 200);
			if (bWalkable && bHasTerrain && TerrainSettings)
			{
				const int32 Type = Asset->CellTerrainType[I];
				if (Type > 0 && Type <= TerrainSettings->TerrainTypes.Num())
				{
					Color = TerrainSettings->TerrainTypes[Type - 1].DebugColor.ToFColor(true);
					Color.A = 160;
				}
			}
			const float CellZ = bHasHeight ? HeightMin + Asset->SharedHeightQ[I] * HeightQuantum : FallbackZ;
			OutCenters.Emplace(OriginX + (X + 0.5f) * CS, OriginY + (Y + 0.5f) * CS, CellZ + 2.0f);
			OutColors.Add(Color);
		}
	}
#endif
}
