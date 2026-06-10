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
#include "SeinNavigation.h"
#include "SeinNavigationAsset.h"
#include "SeinNavigationSubsystem.h"
#include "Volumes/SeinNavVolume.h"
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

/** One color group of blocker cells for the proxy. CollectDebugBlockerCells
 *  emits per-cell colors; CreateSceneProxy buckets them into these groups so
 *  GetDynamicMeshElements can render one batched mesh per color. */
struct FSeinNavBlockerBucket
{
	FLinearColor Color;
	TArray<FVector> Centers;
};

class FSeinNavDebugProxy final : public FPrimitiveSceneProxy
{
public:
	FSeinNavDebugProxy(UPrimitiveComponent* InComponent,
	                   TArray<FVector>&& InWalkable,
	                   TArray<FVector>&& InBlocked,
	                   TArray<FSeinNavBlockerBucket>&& InBlockerBuckets,
	                   float InHalfExtent,
	                   float InBlockerHalfExtent)
		: FPrimitiveSceneProxy(InComponent)
		, WalkableCenters(MoveTemp(InWalkable))
		, BlockedCenters(MoveTemp(InBlocked))
		, BlockerBuckets(MoveTemp(InBlockerBuckets))
		, HalfExtent(InHalfExtent)
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
		     + WalkableCenters.GetAllocatedSize()
		     + BlockedCenters.GetAllocatedSize()
		     + BlockerBuckets.GetAllocatedSize();
		for (const FSeinNavBlockerBucket& B : BlockerBuckets)
		{
			Bytes += B.Centers.GetAllocatedSize();
		}
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

			const FColoredMaterialRenderProxy* GreenMat = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
				BaseProxy, FLinearColor(0.0f, 0.7f, 0.0f, 0.35f));
			const FColoredMaterialRenderProxy* RedMat = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
				BaseProxy, FLinearColor(0.85f, 0.0f, 0.0f, 0.6f));
			if (WalkableCenters.Num() > 0)
			{
				FDynamicMeshBuilder Builder(View->GetFeatureLevel());
				EmitQuads(Builder, WalkableCenters, HalfExtent, View);
				Builder.GetMesh(FMatrix::Identity, GreenMat, SDPG_World,
					true /*bDisableBackfaceCulling*/, false /*bReceivesDecals*/,
					ViewIdx, Collector);
			}
			if (BlockedCenters.Num() > 0)
			{
				FDynamicMeshBuilder Builder(View->GetFeatureLevel());
				EmitQuads(Builder, BlockedCenters, HalfExtent, View);
				Builder.GetMesh(FMatrix::Identity, RedMat, SDPG_World,
					true, false, ViewIdx, Collector);
			}
			// One mesh per blocker color bucket. Typical scene has 1-3 buckets
			// (single layer most blockers) so the per-view cost stays small.
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

	TArray<FVector> WalkableCenters;
	TArray<FVector> BlockedCenters;
	TArray<FSeinNavBlockerBucket> BlockerBuckets;
	float HalfExtent;
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

	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (!Sub) return nullptr;

	USeinNavigation* Nav = Sub->GetNavigation();
	if (!Nav || !Nav->HasRuntimeData())
	{
		UE_LOG(LogSeinNavDebug, Verbose, TEXT("CreateSceneProxy: nav %s"),
			!Nav ? TEXT("null") : TEXT("has no runtime data"));
		return nullptr;
	}

	TArray<FVector> Centers;
	TArray<FColor> Colors;
	float HalfExtent = 0.0f;
	Nav->CollectDebugCellQuads(Centers, Colors, HalfExtent);

	TArray<FVector> Walkable;
	TArray<FVector> Blocked;
	Walkable.Reserve(Centers.Num());
	for (int32 i = 0; i < Centers.Num(); ++i)
	{
		const FColor& C = Colors[i];
		if (C.R > C.G) Blocked.Add(Centers[i]);
		else Walkable.Add(Centers[i]);
	}

	// Dynamic blocker cells (overlay above static cells). Routed through the
	// same scene proxy — folds the previously-per-frame DrawDebugSolidBox
	// path into the batched mesh rebuild that fires on OnNavigationMutated.
	// Per-cell colors come from the nav (resolved against plugin-settings
	// layer colors so a Default-only blocker reads red, an N0 blocker reads
	// N0's color, etc.) — bucketed below for efficient batched rendering.
	TArray<FVector> BlockerCenters;
	TArray<FColor> BlockerColors;
	float BlockerHalfExtent = 0.0f;
	Nav->CollectDebugBlockerCells(BlockerCenters, BlockerColors, BlockerHalfExtent);

	TArray<FSeinNavBlockerBucket> BlockerBuckets;
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

	if (Centers.Num() == 0 && BlockerCenters.Num() == 0)
	{
		UE_LOG(LogSeinNavDebug, Verbose, TEXT("CreateSceneProxy: nav returned 0 static + 0 blocker cells"));
		return nullptr;
	}

	UE_LOG(LogSeinNavDebug, Verbose,
		TEXT("CreateSceneProxy: %d walkable + %d blocked + %d blocker cells in %d color buckets, staticHE=%.1f blockerHE=%.1f"),
		Walkable.Num(), Blocked.Num(), BlockerCenters.Num(), BlockerBuckets.Num(), HalfExtent, BlockerHalfExtent);

	return new FSeinNavDebugProxy(this,
		MoveTemp(Walkable), MoveTemp(Blocked), MoveTemp(BlockerBuckets),
		HalfExtent, BlockerHalfExtent);
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
	EnsureNavLoaded();

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
#endif
	Super::OnUnregister();
}

void USeinNavDebugComponent::HandleNavMutated()
{
#if UE_ENABLE_DEBUG_DRAWING
	MarkRenderStateDirty();
#endif
}

void USeinNavDebugComponent::EnsureNavLoaded()
{
#if UE_ENABLE_DEBUG_DRAWING
	UWorld* World = GetWorld();
	if (!World) return;
	USeinNavigationSubsystem* Sub = World->GetSubsystem<USeinNavigationSubsystem>();
	if (!Sub) return;
	USeinNavigation* Nav = Sub->GetNavigation();
	if (!Nav || Nav->HasRuntimeData()) return;

	if (ASeinNavVolume* Vol = Cast<ASeinNavVolume>(GetOwner()))
	{
		if (USeinNavigationAsset* Asset = Vol->BakedAsset.LoadSynchronous())
		{
			Nav->LoadFromAsset(Asset);
		}
	}
#endif
}
