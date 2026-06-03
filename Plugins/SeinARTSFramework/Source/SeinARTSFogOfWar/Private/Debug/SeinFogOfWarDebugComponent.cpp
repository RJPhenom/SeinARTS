/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDebugComponent.cpp
 *
 * Scene-proxy pattern mirrors USeinNavDebugComponent / UE's FNavMeshSceneProxy:
 *   - View relevance sets bSeparateTranslucency + bNormalTranslucency (the
 *     DebugMeshMaterial is translucent; bOpaque alone produces nothing).
 *   - Custom `ShowFlags.FogOfWar` lookup via `FindCustomShowFlagByName` +
 *     `GetSingleFlag(Index + SF_FirstCustom)` — the same dance `TCustomShowFlag::IsEnabled`
 *     does internally, done here without linking against the module's TU.
 *   - Cell layout: if the active impl has runtime data (bake / PIE stamp grid),
 *     route through `USeinFogOfWar::CollectDebugCellQuads`. Otherwise, rasterize
 *     the owner volume's XY bounds into cells at the volume's resolved cell
 *     size and downward-trace each cell for a terrain-Z snap — gives designers
 *     a "this volume covers these cells" red layer in editor preview without
 *     needing a bake. The per-cell trace is capped at 20k cells to keep proxy
 *     creation under ~100ms on typical hardware; beyond that we fall back to a
 *     flat plane at volume mid-Z.
 *
 * Shipping strip: scene-proxy class, method bodies, and render-specific
 * includes are gated on UE_ENABLE_DEBUG_DRAWING. The component class stays
 * in shipping (ABI + reflection) but every virtual compiles to a no-op stub.
 */

#include "Debug/SeinFogOfWarDebugComponent.h"
#include "SeinFogOfWar.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"
#include "Volumes/SeinFogOfWarVolume.h"
#include "Core/SeinPlayerID.h"
#include "Settings/PluginSettings.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
#include "MeshElementCollector.h"
#include "ShowFlags.h"
#include "Components/BrushComponent.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFogOfWarDebug, Log, All);

namespace
{
	/** Query the custom ShowFlags.FogOfWar state on an FEngineShowFlags.
	 *  `FindIndexByName` handles both engine and custom flags — it falls
	 *  through to the custom-flag lookup internally and returns a raw bit
	 *  index that includes SF_FirstCustom (so we can feed it directly to
	 *  GetSingleFlag). `FindCustomShowFlagByName` would be more direct but
	 *  is private to FEngineShowFlags in 5.7. */
	static bool IsFogOfWarShowFlagOn(const FEngineShowFlags& ShowFlags)
	{
		// The flag is registered by FSeinARTSFogOfWarModule at module load.
		// First-call init here happens well after module init for any code
		// path that reaches a scene proxy (viewport render), so the lookup
		// reliably resolves.
		static const int32 FlagBitIndex = FEngineShowFlags::FindIndexByName(TEXT("FogOfWar"));
		if (FlagBitIndex == INDEX_NONE) return false;
		return ShowFlags.GetSingleFlag(FlagBitIndex);
	}

	/** Rasterize an AABB into cell centers on the XY plane, with per-cell
	 *  downward trace for terrain-Z snap. The loop iteration count itself
	 *  is bounded by MaxTracedCells: when the grid would exceed the cap,
	 *  we stride uniformly across the grid (taking every Nth cell on each
	 *  axis) and emit larger quads that cover the strided footprint. The
	 *  result is a sparse-but-uniform sampling of the volume rather than
	 *  the previous "trace the first 20k cells, fall back to a flat plane
	 *  for the rest" — which on a 1km² volume meant only the top-left
	 *  corner sampled correctly.
	 *
	 *  Cells sit +SEIN_FOW_DEBUG_Z_OFFSET above the hit to overlay cleanly
	 *  on top of nav's debug layer. */
	static constexpr int32 MaxTracedCells = 20000;

	static void RasterizeVolumeCells(
		UWorld* World,
		const ASeinFogOfWarVolume* Volume,
		TArray<FVector>& OutCenters,
		float& OutHalfExtent)
	{
		if (!World || !Volume) return;

		const FBox Bounds = Volume->GetVolumeWorldBounds();
		if (!Bounds.IsValid || Bounds.GetSize().IsNearlyZero()) return;

		// Render-side path: pulled to float here, no determinism concern —
		// debug viz only.
		const float CellSize = Volume->GetResolvedCellSize().ToFloat();
		if (CellSize <= 0.0f) return;

		const int32 Width  = FMath::Max(1, FMath::CeilToInt(Bounds.GetSize().X / CellSize));
		const int32 Height = FMath::Max(1, FMath::CeilToInt(Bounds.GetSize().Y / CellSize));
		const int32 NumCells = Width * Height;
		const float FallbackZ = (Bounds.Min.Z + Bounds.Max.Z) * 0.5f;

		// Stride bounds the loop itself by MaxTracedCells. Conceptually we
		// sample super-cells of size Stride×Stride in cell space, so the
		// emitted count is ≈ NumCells / Stride² ≤ MaxTracedCells. ceil(sqrt)
		// makes the bound conservative; ceil-of-ratio under-strides on
		// non-square grids.
		const int32 Stride = (NumCells <= MaxTracedCells)
			? 1
			: FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(
				static_cast<float>(NumCells) / static_cast<float>(MaxTracedCells))));

		// 0.9 inset — matches the impl's CollectDebugCellQuads so the no-bake
		// fallback path reads as the same "grid of squares". Scale by stride
		// so super-cells visually tile the volume instead of leaving gaps.
		OutHalfExtent = CellSize * static_cast<float>(Stride) * 0.5f * 0.9f;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(SeinFowDebugTrace), /*bTraceComplex*/ true);
		Params.AddIgnoredActor(Volume);

		// Estimate the emitted count tightly so we reserve once.
		const int32 BlocksWide  = (Width  + Stride - 1) / Stride;
		const int32 BlocksTall  = (Height + Stride - 1) / Stride;
		OutCenters.Reserve(BlocksWide * BlocksTall);

		// Center the super-cell on its strided footprint: (X + 0.5*Stride)
		// places the center half a stride above the upper-left of the block,
		// which is the geometric center of a Stride×Stride sub-grid.
		const float HalfStride = 0.5f * static_cast<float>(Stride);
		for (int32 Y = 0; Y < Height; Y += Stride)
		{
			for (int32 X = 0; X < Width; X += Stride)
			{
				const float CX = Bounds.Min.X + (X + HalfStride) * CellSize;
				const float CY = Bounds.Min.Y + (Y + HalfStride) * CellSize;

				FHitResult Hit;
				const FVector Start(CX, CY, Bounds.Max.Z + 100.0f);
				const FVector End  (CX, CY, Bounds.Min.Z - 100.0f);
				if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
				{
					OutCenters.Add(FVector(CX, CY, Hit.Location.Z + SEIN_FOW_DEBUG_Z_OFFSET));
				}
				else
				{
					OutCenters.Add(FVector(CX, CY, FallbackZ + SEIN_FOW_DEBUG_Z_OFFSET));
				}
			}
		}

		UE_LOG(LogSeinFogOfWarDebug, Verbose,
			TEXT("RasterizeVolumeCells: grid=%dx%d (%d cells), stride=%d, emitted=%d"),
			Width, Height, NumCells, Stride, OutCenters.Num());
	}
}

// ============================================================================
// Scene proxy — N-bucket by color. Cells grouped by their FColor sentinel
// (visible/blocker/default can each be any palette), one FDynamicMeshBuilder
// + FColoredMaterialRenderProxy per unique color. Tops out at a small number
// of buckets (≤ 3 in practice: one "visible" layer color + red + black), so
// the extra flexibility costs nothing vs the old hardcoded three-batch.
// ============================================================================

struct FSeinFogOfWarColorBucket
{
	FColor Color;
	TArray<FVector> Centers;
};

class FSeinFogOfWarDebugProxy final : public FPrimitiveSceneProxy
{
public:
	FSeinFogOfWarDebugProxy(
		UPrimitiveComponent* InComponent,
		TArray<FSeinFogOfWarColorBucket>&& InBuckets,
		float InHalfExtent)
		: FPrimitiveSceneProxy(InComponent)
		, Buckets(MoveTemp(InBuckets))
		, HalfExtent(InHalfExtent)
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
		uint32 Total = sizeof(*this) + Buckets.GetAllocatedSize();
		for (const FSeinFogOfWarColorBucket& B : Buckets)
		{
			Total += B.Centers.GetAllocatedSize();
		}
		return Total;
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		const bool bVisible = IsFogOfWarShowFlagOn(View->Family->EngineShowFlags);
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = bVisible && IsShown(View);
		Result.bDynamicRelevance = true;
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
		if (Buckets.Num() == 0) return;

		for (int32 ViewIdx = 0; ViewIdx < Views.Num(); ++ViewIdx)
		{
			if (!(VisibilityMap & (1 << ViewIdx))) continue;
			const FSceneView* View = Views[ViewIdx];
			if (!IsFogOfWarShowFlagOn(View->Family->EngineShowFlags)) continue;

			// Pass the full FSceneView through to EmitQuads — gives it the
			// frustum (per-cell cull), camera location (per-cell distance
			// cull), and feature level. All three plugin-settings knobs
			// (`bDebugDrawFrustumCullEnabled`, `DebugDrawMaxDistance`,
			// `DebugDrawMaxEntities`) are read inside EmitQuads.

			for (const FSeinFogOfWarColorBucket& Bucket : Buckets)
			{
				if (Bucket.Centers.Num() == 0) continue;
				FLinearColor Tint = FLinearColor(Bucket.Color);
				// Alpha tuned per-role: blockers opaque enough to read against
				// underlying mesh at distance, visible/default translucent to
				// show terrain through them.
				Tint.A = (Bucket.Color == FColor(200, 0, 0)) ? 0.55f
					   : (Bucket.Color == FColor(0, 0, 0))   ? 0.40f
					                                         : 0.45f;
				const FColoredMaterialRenderProxy* Mat =
					&Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
						BaseProxy, Tint);

				FDynamicMeshBuilder Builder(View->GetFeatureLevel());
				EmitQuads(Builder, Bucket.Centers, View);
				Builder.GetMesh(FMatrix::Identity, Mat, SDPG_World,
					/*bDisableBackfaceCulling*/ true,
					/*bReceivesDecals*/ false,
					ViewIdx, Collector);
			}
		}
	}

private:

	void EmitQuads(FDynamicMeshBuilder& Builder, const TArray<FVector>& Centers,
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
		const FVector CullExtent(HalfExtent, HalfExtent, 10.0f);

		// Bounded closest-N selection: max-heap of (DistSq, Center) keeps the
		// MaxItems closest-to-camera cells across the whole bucket. Memory is
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
			V0.Position = FVector3f(C + FVector(-HalfExtent, -HalfExtent, 0));
			V1.Position = FVector3f(C + FVector( HalfExtent, -HalfExtent, 0));
			V2.Position = FVector3f(C + FVector( HalfExtent,  HalfExtent, 0));
			V3.Position = FVector3f(C + FVector(-HalfExtent,  HalfExtent, 0));
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

	TArray<FSeinFogOfWarColorBucket> Buckets;
	float HalfExtent;
};

#endif // UE_ENABLE_DEBUG_DRAWING

// ============================================================================
// Component
// ============================================================================

USeinFogOfWarDebugComponent::USeinFogOfWarDebugComponent()
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

FPrimitiveSceneProxy* USeinFogOfWarDebugComponent::CreateSceneProxy()
{
#if UE_ENABLE_DEBUG_DRAWING
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	ASeinFogOfWarVolume* Volume = Cast<ASeinFogOfWarVolume>(GetOwner());
	if (!Volume) return nullptr;

	TArray<FVector> AllCenters;
	TArray<FColor> AllColors;
	float HalfExtent = 0.0f;

	// Impl-path: when the fog subsystem has runtime data (bake loaded / PIE
	// stamp grid active), route through the impl's cell collector. Falls back
	// to volume-bounds rasterization in non-PIE / pre-bake / fog-less games —
	// everything red (no vision).
	if (USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>())
	{
		if (USeinFogOfWar* Fog = Sub->GetFogOfWar())
		{
			if (Fog->HasRuntimeData())
			{
				// Observer precedence: console override wins, else local PC.
				FSeinPlayerID Observer;
				if (!UE::SeinARTSFogOfWar::TryGetDebugObserverOverride(Observer))
				{
					Observer = UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID(World);
				}
				// Layer precedence: console override wins, else V (bit 1).
				int32 VisibleBit = 1;
				UE::SeinARTSFogOfWar::TryGetDebugLayerOverride(VisibleBit);
				Fog->CollectDebugCellQuads(Observer, VisibleBit, AllCenters, AllColors, HalfExtent);
			}
		}
	}

	if (AllCenters.Num() == 0)
	{
		// Fallback: rasterize volume bounds — all red (no vision, no bake).
		RasterizeVolumeCells(World, Volume, AllCenters, HalfExtent);
		// No colors array → treat everything as unseen (red).
	}

	if (AllCenters.Num() == 0) return nullptr;

	// Bucket cells by exact FColor. N-way instead of the old hardcoded
	// 3-way — lets per-layer visible colors (E=yellow, V=cyan, N0..N5 from
	// plugin settings) coexist with blocker red + baseline black on the
	// same proxy. Typical rebuild has ≤ 3 buckets (one visible color +
	// red + black).
	TArray<FSeinFogOfWarColorBucket> Buckets;
	if (AllColors.Num() == AllCenters.Num())
	{
		for (int32 i = 0; i < AllCenters.Num(); ++i)
		{
			const FColor& C = AllColors[i];
			FSeinFogOfWarColorBucket* Match = Buckets.FindByPredicate(
				[&C](const FSeinFogOfWarColorBucket& B) { return B.Color == C; });
			if (!Match)
			{
				FSeinFogOfWarColorBucket NewBucket;
				NewBucket.Color = C;
				Match = &Buckets.Add_GetRef(MoveTemp(NewBucket));
			}
			Match->Centers.Add(AllCenters[i]);
		}
	}
	else
	{
		// Fallback (volume-rasterize path — no parallel colors). Treat as
		// baseline red so the grid at least reads as "fog extent" in non-
		// PIE when the impl has no runtime data.
		FSeinFogOfWarColorBucket Fallback;
		Fallback.Color = FColor(200, 0, 0);
		Fallback.Centers = MoveTemp(AllCenters);
		Buckets.Add(MoveTemp(Fallback));
	}

	UE_LOG(LogSeinFogOfWarDebug, Verbose,
		TEXT("CreateSceneProxy: %d color buckets, halfExtent=%.1f"),
		Buckets.Num(), HalfExtent);

	return new FSeinFogOfWarDebugProxy(this, MoveTemp(Buckets), HalfExtent);
#else
	return nullptr;
#endif
}

FBoxSphereBounds USeinFogOfWarDebugComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FBox(FVector(-1048576.0), FVector(1048576.0)));
}

void USeinFogOfWarDebugComponent::OnRegister()
{
	Super::OnRegister();

#if UE_ENABLE_DEBUG_DRAWING
	UWorld* World = GetWorld();
	if (!World) return;
	if (USeinFogOfWarSubsystem* Sub = World->GetSubsystem<USeinFogOfWarSubsystem>())
	{
		if (USeinFogOfWar* Fog = Sub->GetFogOfWar())
		{
			SubscribedFog = Fog;
			FogMutatedHandle = Fog->OnFogOfWarMutated.AddUObject(this, &USeinFogOfWarDebugComponent::HandleFogMutated);
		}
	}
	MarkRenderStateDirty();
#endif
}

void USeinFogOfWarDebugComponent::OnUnregister()
{
#if UE_ENABLE_DEBUG_DRAWING
	if (USeinFogOfWar* Fog = SubscribedFog.Get())
	{
		Fog->OnFogOfWarMutated.Remove(FogMutatedHandle);
	}
	SubscribedFog.Reset();
	FogMutatedHandle.Reset();
#endif
	Super::OnUnregister();
}

void USeinFogOfWarDebugComponent::HandleFogMutated()
{
#if UE_ENABLE_DEBUG_DRAWING
	MarkRenderStateDirty();
#endif
}
