/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinNavigationAStar.cpp
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       13 Aug 2026
 * @brief        Implements the shipped deterministic A* navigation policy.
 *
 *               Non-shipping path reporters live in the adjacent private
 *               diagnostics implementation include.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "SeinNavigationAStar.h"
#include "Core/SeinParallel.h"
#include "Settings/PluginSettings.h"
#include "Data/SeinNavLayerDefinition.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Stamping/SeinStampUtils.h"
#include "SeinARTSNavigationModule.h"
#include "SeinLevelData.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Volumes/SeinLevelVolume.h"
#include "Math/MathLib.h"  // SeinMath fixed-point sqrt / trig used by the search + clearance math

#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Hash/Blake3.h"
#include "Math/Box.h"
#include "Algo/Reverse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "SeinARTSNavigationLog.h"

namespace
{
	/** Canonical 8-neighbor ordering shared by bake-derived connectivity,
	 *  reachability components, A*, smoothing, and diagnostics. */
	static const int32 SeinNeighborDX[8] =
		{ 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 SeinNeighborDY[8] =
		{ 0, 0, 1, -1, 1, -1, 1, -1 };
	static const int32 SeinNeighborCost[8] =
		{ 10, 10, 10, 10, 14, 14, 14, 14 };
	static const uint8 SeinDiagCardinalA[4] =
		{ 0, 0, 1, 1 };
	static const uint8 SeinDiagCardinalB[4] =
		{ 2, 3, 2, 3 };

	uint32 ReadDigestWord(const uint8* Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}

	class FAStarStaticGridDigestBuilder
	{
	public:
		FAStarStaticGridDigestBuilder()
		{
			constexpr uint8 Domain[] = {
				'S', 'E', 'I', 'N', 'N', 'A', 'V', 'G', 'R', 'I', 'D', 1};
			Hasher.Update(Domain, UE_ARRAY_COUNT(Domain));
		}

		void WriteUInt32(uint32 Value)
		{
			const uint8 Bytes[4] = {
				static_cast<uint8>(Value >> 24),
				static_cast<uint8>(Value >> 16),
				static_cast<uint8>(Value >> 8),
				static_cast<uint8>(Value)};
			Hasher.Update(Bytes, UE_ARRAY_COUNT(Bytes));
		}

		void WriteUInt64(uint64 Value)
		{
			WriteUInt32(static_cast<uint32>(Value >> 32));
			WriteUInt32(static_cast<uint32>(Value));
		}

		void WriteInt32(int32 Value)
		{
			WriteUInt32(BitCast<uint32>(Value));
		}

		void WriteInt64(int64 Value)
		{
			WriteUInt64(static_cast<uint64>(Value));
		}

		void WriteBytes(TConstArrayView<uint8> Bytes)
		{
			WriteUInt64(static_cast<uint64>(Bytes.Num()));
			if (!Bytes.IsEmpty())
			{
				Hasher.Update(Bytes.GetData(), Bytes.Num());
			}
		}

		FGuid Finalize()
		{
			const FBlake3Hash Hash = Hasher.Finalize();
			const uint8* Bytes = Hash.GetBytes();
			return FGuid(
				ReadDigestWord(Bytes),
				ReadDigestWord(Bytes + 4),
				ReadDigestWord(Bytes + 8),
				ReadDigestWord(Bytes + 12));
		}

	private:
		FBlake3 Hasher;
	};

	bool BuildAStarStaticGridDigest(
		int32 Width,
		int32 Height,
		FFixedPoint CellSize,
		const FFixedVector& Origin,
		TConstArrayView<uint8> CellCost,
		TConstArrayView<FFixedPoint> CellHeight,
		TConstArrayView<uint8> CellTerrainType,
		TConstArrayView<uint8> CellConnections,
		FGuid& OutDigest,
		FString& OutError)
	{
		OutDigest.Invalidate();
		OutError.Reset();
		const int64 NumCells64 =
			static_cast<int64>(Width) * static_cast<int64>(Height);
		if (Width <= 0
			|| Height <= 0
			|| NumCells64 > MAX_int32
			|| CellSize <= FFixedPoint::Zero
			|| CellCost.Num() != NumCells64
			|| CellHeight.Num() != NumCells64
			|| CellTerrainType.Num() != NumCells64
			|| CellConnections.Num() != NumCells64)
		{
			OutError =
				TEXT("A* static grid is incomplete or has inconsistent dimensions.");
			return false;
		}

		FAStarStaticGridDigestBuilder Writer;
		Writer.WriteInt32(Width);
		Writer.WriteInt32(Height);
		Writer.WriteInt64(CellSize.Value);
		Writer.WriteInt64(Origin.X.Value);
		Writer.WriteInt64(Origin.Y.Value);
		Writer.WriteInt64(Origin.Z.Value);
		Writer.WriteBytes(CellCost);
		Writer.WriteUInt64(static_cast<uint64>(CellHeight.Num()));
		for (const FFixedPoint HeightValue : CellHeight)
		{
			Writer.WriteInt64(HeightValue.Value);
		}
		Writer.WriteBytes(CellTerrainType);
		Writer.WriteBytes(CellConnections);
		OutDigest = Writer.Finalize();
		if (!OutDigest.IsValid())
		{
			OutError = TEXT("A* static grid digest was invalid.");
			return false;
		}
		return true;
	}

	const UClass* FindNearestNativeClass(const UClass* Class)
	{
		while (Class && !Class->HasAnyClassFlags(CLASS_Native))
		{
			Class = Class->GetSuperClass();
		}
		return Class;
	}
}

// ============================================================================
// ISeinLevelLayerProvider (CP1.1 nav port)
// ============================================================================

FName USeinNavigationAStar::GetLayerId() const
{
	return TEXT("Nav");
}

void USeinNavigationAStar::BakeLayer(const USeinLevelData& Substrate, UWorld* World, TArray<uint8>& OutData)
{
	OutData.Reset();
	if (!World) return;

	const FIntPoint Dims = Substrate.GetDimensions();
	const int32 GridW = Dims.X;
	const int32 GridH = Dims.Y;
	if (GridW <= 0 || GridH <= 0) return;
	const int32 NumCells = GridW * GridH;

	const FFixedPoint CellSizeFP = Substrate.GetFinestCellSize();
	const FFixedVector OriginFP = Substrate.GetOrigin();
	const float CellSizeF = CellSizeFP.ToFloat();
	const FVector OriginWorld(OriginFP.X.ToFloat(), OriginFP.Y.ToFloat(), OriginFP.Z.ToFloat());

	// ----------------------------------------------------------------------
	// Stage 1 — Cost + Height from the SHARED substrate (slope gate on the
	// surface normal). Reproduces nav's per-cell trace result without re-tracing:
	// the substrate's height/normal came from the same nav-faithful line trace.
	// `bInBounds` is new (brush mask, D10) — for a box volume it is true
	// everywhere, so Cost matches nav's legacy AABB bake exactly.
	// ----------------------------------------------------------------------
	const FFixedPoint MaxSlopeCosFP = FFixedPoint::FromFloat(FMath::Cos(FMath::DegreesToRadians(MaxWalkableSlopeDegrees)));
	// Settings drive both the terrain-type → cost lookup (Stage 1 below) and the
	// step-height fallback (Stage 2). Fetched once here.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	TArray<uint8> Cost;        Cost.SetNumUninitialized(NumCells);
	TArray<uint8> Connections; Connections.Init(0, NumCells);
	TArray<FFixedPoint> CellH; CellH.SetNumUninitialized(NumCells);
	for (int32 i = 0; i < NumCells; ++i)
	{
		FSeinLevelCellSurface Surf;
		const bool bOk = Substrate.GetCellSurface(i, Surf);
		const bool bPassable = bOk && Surf.bInBounds && Surf.bHasSurface && Surf.NormalZ >= MaxSlopeCosFP;
		// Passable cells carry their terrain type's movement-cost multiplier (Default → 1,
		// so an un-authored level bakes byte-identically to before); A* multiplies it into
		// step cost. Blocked cells stay 0.
		Cost[i]  = bPassable ? (uint8)(Settings ? Settings->GetTerrainNavCost(Surf.TerrainTypeIndex) : 1) : 0;
		CellH[i] = bOk ? Surf.Height : FFixedPoint::Zero;
	}

	// ----------------------------------------------------------------------
	// Stage 2 — connectivity (nav's midpoint-trace pass, reproduced). Needs the
	// level volumes (per-cell max-step + union Z extent) and the same nav skip
	// list for the midpoint traces.
	// ----------------------------------------------------------------------
	TArray<ASeinLevelVolume*> Volumes;
	FBox UnionBounds(ForceInit);
	for (TActorIterator<ASeinLevelVolume> It(World); It; ++It)
	{
		if (ASeinLevelVolume* V = *It) { Volumes.Add(V); UnionBounds += V->GetVolumeWorldBounds(); }
	}
	const float TopZ    = (UnionBounds.IsValid ? UnionBounds.Max.Z : OriginWorld.Z) + BakeTraceHeadroom;
	const float BottomZ = (UnionBounds.IsValid ? UnionBounds.Min.Z : OriginWorld.Z) - 10.0f;

	FCollisionQueryParams QP(SCENE_QUERY_STAT(SeinNavLayerBake), true /*bTraceComplex*/);
	for (ASeinLevelVolume* V : Volumes) { if (V) QP.AddIgnoredActor(V); }
	for (TActorIterator<ASeinActor> It(World); It; ++It)
	{
		ASeinActor* A = *It;
		if (!A) continue;
		bool bSkip = false;
		if (const USeinEntityComponent* Bridge = A->FindComponentByClass<USeinEntityComponent>())
		{
			if (const FSeinExtentsComponent* Ext = Bridge->FindAuthoredData<FSeinExtentsComponent>())
			{
				if (!Ext->bBakesIntoNav) bSkip = true;
			}
			if (!bSkip)
			{
				for (const FInstancedStruct& E : Bridge->ComponentData)
				{
					if (E.GetScriptStruct() == FSeinMovementComponent::StaticStruct()) { bSkip = true; break; }
				}
			}
		}
		if (bSkip) QP.AddIgnoredActor(A);
	}

	const FFixedPoint FallbackStepFP = Settings ? Settings->MaxStepHeight : FFixedPoint::FromInt(50);
	TArray<FFixedPoint> CellMaxStep; CellMaxStep.SetNum(NumCells);
	for (int32 Y = 0; Y < GridH; ++Y)
	{
		for (int32 X = 0; X < GridW; ++X)
		{
			const float CX = OriginWorld.X + (X + 0.5f) * CellSizeF;
			const float CY = OriginWorld.Y + (Y + 0.5f) * CellSizeF;
			FFixedPoint Step = FallbackStepFP;
			for (ASeinLevelVolume* V : Volumes)
			{
				if (!V) continue;
				const FBox VB = V->GetVolumeWorldBounds();
				if (CX >= VB.Min.X && CX <= VB.Max.X && CY >= VB.Min.Y && CY <= VB.Max.Y)
				{ Step = V->GetResolvedMaxStepHeight(); break; }
			}
			CellMaxStep[Y * GridW + X] = Step;
		}
	}

	const float BakeTan = FMath::Tan(FMath::DegreesToRadians(MaxWalkableSlopeDegrees));
	const FFixedPoint BakeMaxSlopeTanSq = FFixedPoint::FromFloat(BakeTan * BakeTan);
	const FFixedPoint HalfCellSq = (CellSizeFP * CellSizeFP) / FFixedPoint::FromInt(4);
	const FFixedPoint HalfDiagSq = HalfCellSq * FFixedPoint::FromInt(2);
	static const int32 DX8[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
	static const int32 DY8[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };

	for (int32 Y = 0; Y < GridH; ++Y)
	{
		for (int32 X = 0; X < GridW; ++X)
		{
			const int32 AIdx = Y * GridW + X;
			if (Cost[AIdx] == 0) { Connections[AIdx] = 0; continue; }
			const FFixedPoint AStep = CellMaxStep[AIdx];

			uint8 Mask = 0;
			for (int32 n = 0; n < 8; ++n)
			{
				const int32 NX = X + DX8[n];
				const int32 NY = Y + DY8[n];
				if (NX < 0 || NX >= GridW || NY < 0 || NY >= GridH) continue;
				const int32 BIdx = NY * GridW + NX;
				if (Cost[BIdx] == 0) continue;

				const float MidX = OriginWorld.X + (X + 0.5f + 0.5f * DX8[n]) * CellSizeF;
				const float MidY = OriginWorld.Y + (Y + 0.5f + 0.5f * DY8[n]) * CellSizeF;
				FHitResult MidHit;
				if (!World->LineTraceSingleByChannel(MidHit, FVector(MidX, MidY, TopZ), FVector(MidX, MidY, BottomZ), ECC_Visibility, QP))
				{
					continue; // no surface at midpoint → no edge
				}

				const FFixedPoint MidZ = FFixedPoint::FromFloat(MidHit.ImpactPoint.Z);
				const FFixedPoint AZ = CellH[AIdx];
				const FFixedPoint BZ = CellH[BIdx];
				const FFixedPoint AMid = (MidZ > AZ) ? (MidZ - AZ) : (AZ - MidZ);
				const FFixedPoint MidB = (BZ > MidZ) ? (BZ - MidZ) : (MidZ - BZ);

				const FFixedPoint BStep = CellMaxStep[BIdx];
				const FFixedPoint EdgeStep = (AStep < BStep) ? AStep : BStep;
				if (AMid > EdgeStep || MidB > EdgeStep) continue;

				const FFixedPoint HalfSq = (n < 4) ? HalfCellSq : HalfDiagSq;
				if ((AMid * AMid) > HalfSq * BakeMaxSlopeTanSq ||
				    (MidB * MidB) > HalfSq * BakeMaxSlopeTanSq) continue;

				Mask |= (1 << n);
			}
			Connections[AIdx] = Mask;
		}
	}

	// ----------------------------------------------------------------------
	// Stage 3 — connected-component prune → SIZE THRESHOLD (Decisions D11; was
	// "keep only the largest"). Removes junk islands (cube tops, floating
	// geometry) while keeping intentional disjoint play regions. For a typical
	// single-region level this matches the legacy largest-wins result.
	// ----------------------------------------------------------------------
	{
		TArray<int32> Labels; Labels.Init(-1, NumCells);
		TArray<int32> SizeByLabel;
		int32 NextLabel = 0;
		TArray<int32> Stack; Stack.Reserve(256);
		for (int32 Seed = 0; Seed < NumCells; ++Seed)
		{
			if (Cost[Seed] == 0 || Labels[Seed] != -1) continue;
			const int32 L = NextLabel++;
			int32 Count = 0;
			Stack.Reset(); Stack.Add(Seed); Labels[Seed] = L;
			while (Stack.Num() > 0)
			{
				const int32 Cur = Stack.Pop(EAllowShrinking::No);
				++Count;
				const int32 CX = Cur % GridW;
				const int32 CY = Cur / GridW;
				const uint8 Conn = Connections[Cur];
				for (int32 n = 0; n < 8; ++n)
				{
					if ((Conn & (1 << n)) == 0) continue;
					const int32 NX = CX + DX8[n];
					const int32 NY = CY + DY8[n];
					if (NX < 0 || NX >= GridW || NY < 0 || NY >= GridH) continue;
					const int32 NIdx = NY * GridW + NX;
					if (Labels[NIdx] != -1) continue;
					Labels[NIdx] = L;
					Stack.Add(NIdx);
				}
			}
			SizeByLabel.Add(Count);
		}

		const int32 NumLabels = NextLabel;

		// Elevated obstacle-top detection (CP1.1 — a deliberate improvement over
		// legacy). A walkable component that PERCHES above lower walkable ground it's
		// disconnected from — every disconnected walkable 8-neighbour is more than a
		// step LOWER — is a wall / cube top: unreachable AND not a valid standing
		// position. Legacy kept these walkable-isolated (pathing was still correct,
		// but IsPassable / placement wrongly accepted them, and a tall play volume
		// makes them appear as floating walkable cells). We block them. A same-level
		// disjoint region (D11) keeps a same-level/higher disconnected neighbour, so
		// PerchesAbove && !HasNonLower is false for it → never flagged. Toggle via
		// bBlockElevatedObstacleTops (default on; off = legacy behaviour).
		int32 ElevatedBlocked = 0;
		if (bBlockElevatedObstacleTops && NumLabels > 0)
		{
			TArray<uint8> PerchesAbove; PerchesAbove.Init(0, NumLabels); // has a >step-lower disconnected nbr
			TArray<uint8> HasNonLower;  HasNonLower.Init(0, NumLabels);  // has a disconnected nbr NOT >step-lower
			for (int32 Idx = 0; Idx < NumCells; ++Idx)
			{
				const int32 L = Labels[Idx];
				if (L < 0) continue;
				const int32 CX = Idx % GridW;
				const int32 CY = Idx / GridW;
				const FFixedPoint HereH = CellH[Idx];
				for (int32 n = 0; n < 8; ++n)
				{
					const int32 NX = CX + DX8[n];
					const int32 NY = CY + DY8[n];
					if (NX < 0 || NX >= GridW || NY < 0 || NY >= GridH) continue;
					const int32 NIdx = NY * GridW + NX;
					const int32 NL = Labels[NIdx];
					if (NL < 0 || NL == L) continue; // only DIFFERENT walkable components
					const FFixedPoint EdgeStep = (CellMaxStep[Idx] < CellMaxStep[NIdx]) ? CellMaxStep[Idx] : CellMaxStep[NIdx];
					if (HereH - CellH[NIdx] > EdgeStep) PerchesAbove[L] = 1; // neighbour is >step LOWER
					else                                HasNonLower[L]  = 1; // neighbour same-level or higher
				}
			}
			for (int32 i = 0; i < NumCells; ++i)
			{
				const int32 L = Labels[i];
				if (L >= 0 && Cost[i] != 0 && PerchesAbove[L] && !HasNonLower[L])
				{ Cost[i] = 0; Connections[i] = 0; ++ElevatedBlocked; }
			}
		}

		// Smallest connected walkable region (in cells) the bake keeps; anything smaller is
		// pruned as junk (cube tops, slivers of floating geometry). Designer-tunable via
		// USeinARTSCoreSettings::NavMinWalkableIslandCells (default 16); a custom nav subclass
		// can override the prune entirely in its own BakeLayer.
		const int32 MinComponentCells = Settings ? FMath::Max(1, Settings->NavMinWalkableIslandCells) : 16;
		int32 Pruned = 0;
		for (int32 i = 0; i < NumCells; ++i)
		{
			if (Labels[i] != -1 && Cost[i] != 0 && SizeByLabel[Labels[i]] < MinComponentCells)
			{ Cost[i] = 0; Connections[i] = 0; ++Pruned; }
		}
		UE_LOG(LogSeinNavigationAStar, Log,
			TEXT("Nav layer bake: %dx%d, %d components, pruned %d (size<%d) + %d (elevated tops)"),
			GridW, GridH, SizeByLabel.Num(), Pruned, MinComponentCells, ElevatedBlocked);
	}

	// ----------------------------------------------------------------------
	// Serialize: Cost[] then Connections[] (W*H each). Height comes from the
	// shared substrate at runtime, so it isn't in the channel. The runtime
	// consumer knows Width/Height from the substrate.
	// ----------------------------------------------------------------------
	OutData.SetNumUninitialized(2 * NumCells);
	FMemory::Memcpy(OutData.GetData(), Cost.GetData(), NumCells);
	FMemory::Memcpy(OutData.GetData() + NumCells, Connections.GetData(), NumCells);
}

// ============================================================================
// Runtime load (unified substrate)
// ============================================================================

FSeinStaticEnvironmentAdoptionResult
USeinNavigationAStar::LoadFromSubstrateImpl(
	const USeinLevelData& Substrate)
{
	// An empty substrate is a valid fallback. Once a participating A* nav is
	// offered prepared runtime Level Data, its required channel must exist and
	// validate. All validation and digest construction complete before the live
	// grid is replaced.
	if (!Substrate.HasRuntimeData())
	{
		return FSeinStaticEnvironmentAdoptionResult::NotApplicable(
			TEXT("The Level Data substrate has no runtime data."));
	}

	TArray<uint8> NavChannel;
	if (!Substrate.GetLayerChannel(TEXT("Nav"), NavChannel))
	{
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			TEXT("The prepared Level Data substrate is missing the required Nav channel; re-bake with the configured navigation provider."));
	}

	const FIntPoint Dims = Substrate.GetDimensions();
	const int64 NumCells64 =
		static_cast<int64>(Dims.X) * static_cast<int64>(Dims.Y);
	if (Dims.X <= 0
		|| Dims.Y <= 0
		|| NumCells64 > MAX_int32 / 2)
	{
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("The Nav channel targets invalid substrate dimensions %dx%d."),
				Dims.X,
				Dims.Y));
	}
	const int32 N = static_cast<int32>(NumCells64);

	if (NavChannel.Num() != 2 * N)
	{
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("The Nav channel is malformed: expected %d bytes for a %dx%d substrate, received %d."),
				2 * N,
				Dims.X,
				Dims.Y,
				NavChannel.Num()));
	}

	const FFixedPoint NewCellSize = Substrate.GetFinestCellSize();
	const FFixedVector NewOrigin = Substrate.GetOrigin();

	// Nav channel layout mirrors BakeLayer exactly: [Cost(N)][Connections(N)].
	TArray<uint8> NewCellCost;
	TArray<uint8> NewCellConnections;
	NewCellCost.SetNumUninitialized(N);
	NewCellConnections.SetNumUninitialized(N);
	FMemory::Memcpy(NewCellCost.GetData(), NavChannel.GetData(), N);
	FMemory::Memcpy(
		NewCellConnections.GetData(), NavChannel.GetData() + N, N);

	// Cell snap-height reads from the shared substrate surface — the dedup win:
	// one trace pass feeds nav instead of nav re-tracing every cell.
	TArray<FFixedPoint> NewCellHeight;
	TArray<uint8> NewCellTerrainType;
	NewCellHeight.SetNumUninitialized(N);
	NewCellTerrainType.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i)
	{
		FSeinLevelCellSurface Surf;
		if (!Substrate.GetCellSurface(i, Surf))
		{
			return FSeinStaticEnvironmentAdoptionResult::Rejected(
				FString::Printf(
					TEXT("The Nav channel references missing shared substrate cell %d of %d."),
					i,
					N));
		}
		NewCellHeight[i] = Surf.Height;
		NewCellTerrainType[i] = Surf.TerrainTypeIndex;
	}

	FGuid NewStaticGridDigest;
	FString DigestError;
	if (!BuildAStarStaticGridDigest(
		Dims.X,
		Dims.Y,
		NewCellSize,
		NewOrigin,
		NewCellCost,
		NewCellHeight,
		NewCellTerrainType,
		NewCellConnections,
		NewStaticGridDigest,
		DigestError))
	{
		UE_LOG(LogSeinNavigationAStar, Error,
			TEXT("Nav: refused inconsistent substrate grid (%s)."),
			*DigestError);
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("The Nav channel failed static-grid validation: %s"),
				*DigestError));
	}

	Width = Dims.X;
	Height = Dims.Y;
	CellSize = NewCellSize;
	Origin = NewOrigin;
	CellCost = MoveTemp(NewCellCost);
	CellConnections = MoveTemp(NewCellConnections);
	CellHeight = MoveTemp(NewCellHeight);
	CellTerrainType = MoveTemp(NewCellTerrainType);
	StaticGridDigest = NewStaticGridDigest;
	++StaticGridGeneration;

	// Derived field — pure function of CellCost; recomputed on every grid load.
	RebuildWallDistanceField();

	// Derived field — pure function of CellCost/CellConnections; backs O(1)
	// IsReachable. Recomputed on every grid load alongside WallDistance.
	RebuildConnectivityComponents();
	ReachabilityProfileCache.Reset();

	// Grid adoption can change width/height while retaining the same total cell
	// count. Drop both overlay bytes and their 2D dirty rectangle so neither is
	// reinterpreted through the new row stride. Reset retains array capacity.
	MainScratch.DynamicBlocked.Reset();
	MainScratch.LastOverlayDirtyRect =
		FIntRect(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
	MainScratch.bOverlayReuseValid = false;
	RebuildDynamicBlockerCellIndex();

	// Broadcast after runtime state is in sync — subscribers (debug scene proxy,
	// cached plan invalidation, etc.) see a consistent snapshot.
	OnNavigationMutated.Broadcast();
	return FSeinStaticEnvironmentAdoptionResult::Adopted();
}

bool USeinNavigationAStar::ComputeStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError.Reset();

	const UClass* NativeClass = FindNearestNativeClass(GetClass());
	if (NativeClass != USeinNavigationAStar::StaticClass())
	{
		OutError = FString::Printf(
			TEXT("Native A* subclass '%s' must override ComputeStaticEnvironmentDigest to claim exact static navigation coverage."),
			*GetClass()->GetPathName());
		return false;
	}
	return ComputeAStarStaticEnvironmentDigest(
		OutDigest, OutError);
}

bool USeinNavigationAStar::ComputeStateCoverageClaim(
	FSeinNavigationStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	const UClass* NativeClass = FindNearestNativeClass(GetClass());
	if (NativeClass != USeinNavigationAStar::StaticClass())
	{
		OutClaim = {};
		OutError = FString::Printf(
			TEXT("Native A* subclass '%s' must explicitly claim exact mutable-state coverage."),
			*GetClass()->GetPathName());
		return false;
	}
	return ComputeAStarStateCoverageClaim(OutClaim, OutError);
}

bool USeinNavigationAStar::ComputeAStarStateCoverageClaim(
	FSeinNavigationStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError.Reset();
	OutClaim.StableImplementationId =
		TEXT("seinarts.navigation.astar");
	OutClaim.BehaviorRevision = 2;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage =
		ESeinNavigationStateCoverage::Stateless;
	return true;
}

bool USeinNavigationAStar::ComputeAStarStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	OutDigest.Invalidate();
	OutError.Reset();
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.Navigation.AStar.StaticEnvironment"), 1);
	if (!Writer.WriteString(GetClass()->GetPathName())
		|| !Writer.WriteBool(HasRuntimeData())
		|| !Writer.WriteInt32(AStarHeuristicWeightPercent)
		|| !Writer.WriteInt32(AStarMaxIterations))
	{
		OutError = Writer.GetError();
		return false;
	}

	if (!HasRuntimeData())
	{
		return Writer.Finalize(OutDigest, OutError);
	}

	const int64 NumCells64 =
		static_cast<int64>(Width) * static_cast<int64>(Height);
	if (!StaticGridDigest.IsValid()
		|| Width <= 0
		|| Height <= 0
		|| NumCells64 > MAX_int32
		|| CellCost.Num() != NumCells64
		|| CellHeight.Num() != NumCells64
		|| CellTerrainType.Num() != NumCells64
		|| CellConnections.Num() != NumCells64
		|| WallDistance.Num() != NumCells64
		|| CellComponent.Num() != NumCells64)
	{
		OutError =
			TEXT("A* runtime grid no longer matches its cached static-environment contract.");
		return false;
	}

	return Writer.WriteGuid(StaticGridDigest)
		&& Writer.Finalize(OutDigest, OutError);
}

void USeinNavigationAStar::RebuildWallDistanceField()
{
	const int32 N = Width * Height;
	WallDistance.SetNumUninitialized(N);

	if (N == 0) return;

	// Multi-source BFS seeded at every blocked cell. Frontier ring-expands
	// outward through passable cells, recording Chebyshev (8-neighbor) cell
	// distance. Capped at WallDistanceCap — cells beyond the cap stop
	// expanding, which bounds work to O(WallDistanceCap × grid area) in the
	// worst case (typically much less since the frontier stops growing as
	// soon as every cell has been touched).
	//
	// Seed pass: blocked cells get distance 0; all others get the cap
	// (treated as "unvisited / no nearby wall observed yet").
	TArray<FIntPoint> Frontier;
	Frontier.Reserve(N / 4);  // rough guess — open maps have far fewer blocked cells
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = CellIndex(X, Y);
			const uint8 C = CellCost[Idx];
			const bool bBlocked = (C == 0 || C == 255);
			if (bBlocked)
			{
				WallDistance[Idx] = 0;
				Frontier.Emplace(X, Y);
			}
			else
			{
				WallDistance[Idx] = WallDistanceCap;
			}
		}
	}

	// Soft-wall seeding: cells that are themselves passable but have AT LEAST
	// ONE missing connection bit to a passable neighbor act as walls for
	// clearance purposes. These are the "raised platform / wall top / dock"
	// case — green walkable on top, but the slope/step gate blocks the
	// transition from a lower-elevation neighbor. Without this seed, WD
	// reports "no walls nearby" at cells visually adjacent to such walls
	// (because the wall cells themselves aren't CellCost-blocked), and
	// PushWaypointsAwayFromWalls / C-space gating can't see them. Effect:
	// designer places a raised wall, agent paths around it correctly (A*
	// honors CellConnections), but path waypoints hug the wall edge because
	// WallPadding sees no wall to push from.
	//
	// We seed any passable cell where ANY of its 8 neighbors are passable
	// but the connection bit is clear — that cell is on the edge of a
	// step/slope discontinuity, and ground agents experience it as a wall
	// (can't approach from the unreachable side). Cells with all 8 bits set
	// (interior of a contiguous traversable region) are NOT seeded, so the
	// WD field still ramps outward from real edges only.
	//
	// NOTE: blocked cells were already added to the frontier above; their
	// CellConnections is irrelevant. This pass only inspects passable cells
	// (the early `if (bBlocked) continue` filters them out).
	static const int32 NeighborDX[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
	static const int32 NeighborDY[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = CellIndex(X, Y);
			const uint8 C = CellCost[Idx];
			const bool bBlocked = (C == 0 || C == 255);
			if (bBlocked) continue; // Already seeded
			if (WallDistance[Idx] == 0) continue; // Already seeded by another pass

			const uint8 Conn = CellConnections[Idx];
			if (Conn == 0xFF) continue; // Fully connected → not at an edge

			for (int32 n = 0; n < 8; ++n)
			{
				if (Conn & (1 << n)) continue; // Connection exists
				const int32 NX = X + NeighborDX[n];
				const int32 NY = Y + NeighborDY[n];
				if (!IsValidCoord(NX, NY)) continue;
				const int32 NIdx = CellIndex(NX, NY);
				const uint8 NC = CellCost[NIdx];
				const bool bNBlocked = (NC == 0 || NC == 255);
				if (bNBlocked) continue; // Real wall — already accounted

				// Passable neighbor with no connection bit set → this cell is
				// at a step/slope transition. Seed WD=0 so clearance ramps
				// outward from it.
				WallDistance[Idx] = 0;
				Frontier.Emplace(X, Y);
				break;
			}
		}
	}

	// Expand: each cell already in the frontier has a known WallDistance.
	// Walk its 8 neighbors; if a neighbor's stored distance > current+1,
	// improve it and enqueue. Stops naturally when no cell can be improved.

	int32 Head = 0;
	while (Head < Frontier.Num())
	{
		const FIntPoint Cur = Frontier[Head++];
		const int32 CurIdx = CellIndex(Cur.X, Cur.Y);
		const uint8 CurDist = WallDistance[CurIdx];
		// Cap reached — no further expansion contributes useful info.
		if (CurDist >= WallDistanceCap - 1) continue;
		const uint8 NextDist = CurDist + 1;

		for (int32 n = 0; n < 8; ++n)
		{
			const int32 NX = Cur.X + NeighborDX[n];
			const int32 NY = Cur.Y + NeighborDY[n];
			if (!IsValidCoord(NX, NY)) continue;
			const int32 NIdx = CellIndex(NX, NY);
			if (WallDistance[NIdx] > NextDist)
			{
				WallDistance[NIdx] = NextDist;
				Frontier.Emplace(NX, NY);
			}
		}
	}
}

void USeinNavigationAStar::RebuildConnectivityComponents()
{
	const int32 N = Width * Height;
	CellComponent.Init(-1, N);   // -1 = blocked / unlabeled
	if (N == 0) return;

	static const int32 NeighborDX[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
	static const int32 NeighborDY[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };

	// Iterative flood-fill (explicit stack — recursion would overflow on large
	// grids). Each passable, unlabeled cell seeds a new component; the fill
	// expands along the SAME static edge relation A* traverses at zero required
	// clearance: a SET connection bit to a PASSABLE neighbor. This matches the
	// bake-time island-prune flood-fill (BakeLayer stage 3) one-for-one, with
	// one added runtime guard:
	//
	//   Neighbor passability is re-checked here (IsCellPassable), not inferred
	//   from the connection bit alone. The bake's island/elevated-top prune sets
	//   a pruned cell's Cost=0 but does NOT clear its NEIGHBORS' inbound bits, so
	//   a kept cell can carry a stale connection bit pointing at a now-blocked
	//   cell. A* guards the identical way (IsCellPassableForPath in AStarSearch);
	//   without this guard the fill would bleed a component into pruned cells.
	//
	// Deterministic and float-free; not a hot path (one pass per grid load).
	TArray<int32> Stack;
	Stack.Reserve(256);
	int32 NextLabel = 0;
	for (int32 Seed = 0; Seed < N; ++Seed)
	{
		if (CellComponent[Seed] != -1) continue;
		const int32 SeedX = Seed % Width;
		const int32 SeedY = Seed / Width;
		if (!IsCellPassable(SeedX, SeedY)) continue;   // leave blocked cells at -1

		const int32 L = NextLabel++;
		Stack.Reset();
		Stack.Add(Seed);
		CellComponent[Seed] = L;
		while (Stack.Num() > 0)
		{
			const int32 Cur = Stack.Pop(EAllowShrinking::No);
			const int32 CX = Cur % Width;
			const int32 CY = Cur / Width;
			const uint8 Conn = CellConnections[Cur];
			for (int32 n = 0; n < 8; ++n)
			{
				if ((Conn & (1 << n)) == 0) continue;        // no traversable edge
				const int32 NX = CX + NeighborDX[n];
				const int32 NY = CY + NeighborDY[n];
				if (!IsCellPassable(NX, NY)) continue;        // bounds + static passability (stale-bit guard)
				const int32 NIdx = CellIndex(NX, NY);
				if (CellComponent[NIdx] != -1) continue;      // already labeled
				CellComponent[NIdx] = L;
				Stack.Add(NIdx);
			}
		}
	}
}

// ============================================================================
// Query helpers
// ============================================================================

bool USeinNavigationAStar::WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const
{
	if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero) return false;
	const FFixedPoint LocalX = WorldPos.X - Origin.X;
	const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
	const int32 X = (LocalX / CellSize).ToInt();   // deterministic floor (>>32); off-grid negatives rejected by IsValidCoord
	const int32 Y = (LocalY / CellSize).ToInt();
	if (!IsValidCoord(X, Y)) return false;
	OutX = X;
	OutY = Y;
	return true;
}

FFixedVector USeinNavigationAStar::GridToWorld(int32 X, int32 Y) const
{
	const FFixedPoint Half = CellSize / FFixedPoint::FromInt(2);
	const FFixedPoint WX = Origin.X + CellSize * FFixedPoint::FromInt(X) + Half;
	const FFixedPoint WY = Origin.Y + CellSize * FFixedPoint::FromInt(Y) + Half;
	const FFixedPoint WZ = IsValidCoord(X, Y) ? CellHeight[CellIndex(X, Y)] : Origin.Z;
	return FFixedVector(WX, WY, WZ);
}

bool USeinNavigationAStar::IsPassable(const FFixedVector& WorldPos) const
{
	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y)) return false;
	return IsCellPassable(X, Y);
}

int32 USeinNavigationAStar::GetTerrainTypeAt(const FFixedVector& WorldPos) const
{
	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y)) return 0;            // off-grid → Default
	const int32 Idx = CellIndex(X, Y);
	return CellTerrainType.IsValidIndex(Idx) ? static_cast<int32>(CellTerrainType[Idx]) : 0;
}

bool USeinNavigationAStar::IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& /*AgentTags*/) const
{
	// O(1) reachability via the static connectivity-component field (built at
	// LoadFromSubstrate). Replaces the base's full-A* fallback on the
	// command-validation hot path — bRequiresPathableTarget runs this per order
	// to reject targets a unit genuinely can't reach (across a chasm, on an
	// isolated island) instead of falling back to a partial path.
	//
	// AgentTags is intentionally unused: the reference grid is single-layer and
	// applies no per-tag STATIC gating (the only per-agent gate is the dynamic-
	// blocker layer mask, which is transient and excluded here by design — a
	// vehicle parked in a doorway doesn't make the far side FUNDAMENTALLY
	// unreachable). A subclass with terrain layers would override with per-layer
	// component fields.
	//
	// Verdict semantics differ from the base fallback ON PURPOSE: the base
	// returned true whenever FindPath could produce ANY polyline (including a
	// partial to the nearest-reachable cell when the goal sits in a different
	// region), so it effectively never rejected an in-bounds target. This
	// component check returns the CORRECT verdict — true iff the goal is in the
	// same reachable region as the start — which is the documented intent of the
	// override ("cheaper reachability component / flood-fill"). See CellComponent
	// for the precision boundary (diagonal-squeeze corners / oversized agents may
	// over-report; both degrade to a graceful partial in FindPath).
	if (CellComponent.Num() != Width * Height || Width <= 0 || Height <= 0) return false;

	// Project both endpoints onto walkable cells (ring-scan to nearest passable
	// — mirrors FindCellPath's start projection, and tolerates a blocked/off-grid
	// click by snapping to the nearest reachable ground).
	FFixedVector FromProj, ToProj;
	if (!ProjectPointToNav(From, FromProj)) return false;
	if (!ProjectPointToNav(To,   ToProj))   return false;

	int32 FX, FY, TX, TY;
	if (!WorldToGrid(FromProj, FX, FY)) return false;   // projected center always maps back in-bounds
	if (!WorldToGrid(ToProj,   TX, TY)) return false;

	const int32 FromComp = CellComponent[CellIndex(FX, FY)];
	const int32 ToComp   = CellComponent[CellIndex(TX, TY)];
	return FromComp >= 0 && FromComp == ToComp;
}

bool USeinNavigationAStar::IsReachableForAgent(
	const FFixedVector& From,
	const FFixedVector& To,
	const FSeinNavAgentProfile& Agent) const
{
	if (!HasRuntimeData()) return false;

	// Command validation asks whether the STATIC terrain is fundamentally
	// reachable for this unit class. It deliberately ignores transient blockers:
	// a parked vehicle may change the route A* chooses, but must not reject the
	// order itself. The first query for a distinct (clearance, blocked-terrain)
	// profile builds a component field; subsequent units of that profile compare
	// two integers instead of running a duplicate A* before their real path.
	const FReachabilityProfileKey Key =
		MakeReachabilityProfileKey(Agent);
	const TArray<int32>* Components =
		FindOrBuildReachabilityComponents(Key);
	if (!Components || Components->Num() != Width * Height)
	{
		return false;
	}

	auto ProjectToProfileCell =
		[this, Components](
			const FFixedVector& World,
			int32& OutX,
			int32& OutY) -> bool
	{
		int32 X = 0;
		int32 Y = 0;
		if (!WorldToGrid(World, X, Y))
		{
			if (Width <= 0 || Height <= 0
				|| CellSize <= FFixedPoint::Zero)
			{
				return false;
			}
			const FFixedPoint LocalX = World.X - Origin.X;
			const FFixedPoint LocalY = World.Y - Origin.Y;
			X = FMath::Clamp(
				(LocalX / CellSize).ToInt(), 0, Width - 1);
			Y = FMath::Clamp(
				(LocalY / CellSize).ToInt(), 0, Height - 1);
		}

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		const int32 MaxRing = Settings
			? Settings->NavProjectionMaxRingRadius
			: 30;
		FFixedVector Projected;
		if (!RingScanForCell(
			X,
			Y,
			MaxRing,
			[this, Components](int32 CX, int32 CY)
			{
				return Components->IsValidIndex(CellIndex(CX, CY))
					&& (*Components)[CellIndex(CX, CY)] >= 0;
			},
			Projected))
		{
			return false;
		}
		return WorldToGrid(Projected, OutX, OutY);
	};

	int32 FromX = 0;
	int32 FromY = 0;
	int32 ToX = 0;
	int32 ToY = 0;
	if (!ProjectToProfileCell(From, FromX, FromY)
		|| !ProjectToProfileCell(To, ToX, ToY))
	{
		return false;
	}
	const int32 FromComponent =
		(*Components)[CellIndex(FromX, FromY)];
	const int32 ToComponent =
		(*Components)[CellIndex(ToX, ToY)];
	return FromComponent >= 0
		&& FromComponent == ToComponent;
}

void USeinNavigationAStar::WarmAgentProfile(
	const FSeinNavAgentProfile& Agent) const
{
	if (!HasRuntimeData()) return;
	FindOrBuildReachabilityComponents(
		MakeReachabilityProfileKey(Agent));
}

bool USeinNavigationAStar::BuildBlockedTerrainTypeLookup(
	const FGameplayTagContainer& BlockedTerrainTags,
	TArray<uint8>& OutBlockedTypes) const
{
	OutBlockedTypes.Init(0, 256);
	if (BlockedTerrainTags.IsEmpty()) return false;

	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return false;

	bool bAny = false;
	const int32 TypeCount = FMath::Min(
		Settings->TerrainTypes.Num(), 255);
	for (int32 TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
	{
		if (BlockedTerrainTags.HasTag(
			Settings->TerrainTypes[TypeIndex].TerrainTag))
		{
			OutBlockedTypes[TypeIndex + 1] = 1;
			bAny = true;
		}
	}
	return bAny;
}

USeinNavigationAStar::FReachabilityProfileKey
USeinNavigationAStar::MakeReachabilityProfileKey(
	const FSeinNavAgentProfile& Agent) const
{
	FReachabilityProfileKey Key;
	Key.RequiredClearance = ComputeRequiredClearance(
		Agent.AgentFootprintRadius,
		Agent.AgentWallPaddingCells);

	TArray<uint8> BlockedTypes;
	if (BuildBlockedTerrainTypeLookup(
		Agent.BlockedTerrainTags, BlockedTypes))
	{
		for (int32 StoredType = 0;
			StoredType < BlockedTypes.Num();
			++StoredType)
		{
			if (BlockedTypes[StoredType] == 0) continue;
			Key.BlockedTerrainTypeWords[StoredType / 64]
				|= uint64(1) << (StoredType % 64);
		}
	}
	return Key;
}

const TArray<int32>*
USeinNavigationAStar::FindOrBuildReachabilityComponents(
	const FReachabilityProfileKey& Key) const
{
	for (const FReachabilityProfileCacheEntry& Entry
		: ReachabilityProfileCache)
	{
		if (Entry.Key == Key)
		{
			return &Entry.Components;
		}
	}

	FReachabilityProfileCacheEntry NewEntry;
	NewEntry.Key = Key;
	BuildReachabilityComponents(Key, NewEntry.Components);
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 CacheCapacity = FMath::Clamp(
		Settings
			? Settings->NavReachabilityProfileCacheCapacity
			: 8,
		1,
		64);
	if (ReachabilityProfileCache.Num() >= CacheCapacity)
	{
		ReachabilityProfileCache.RemoveAt(0, 1,
			EAllowShrinking::No);
	}
	ReachabilityProfileCache.Add(MoveTemp(NewEntry));
	return &ReachabilityProfileCache.Last().Components;
}

void USeinNavigationAStar::BuildReachabilityComponents(
	const FReachabilityProfileKey& Key,
	TArray<int32>& OutComponents) const
{
	const int32 N = Width * Height;
	OutComponents.Init(-1, N);
	if (N == 0 || WallDistance.Num() != N) return;

	auto IsTerrainTypeBlocked = [&Key](uint8 StoredType)
	{
		return (Key.BlockedTerrainTypeWords[StoredType / 64]
			& (uint64(1) << (StoredType % 64))) != 0;
	};

	// Start with the baked static wall-distance field, then add this profile's
	// forbidden terrain cells as extra zero-distance sources. The bounded BFS
	// produces the exact same Chebyshev clearance metric used by A*, once per
	// profile instead of ring-scanning every command.
	TArray<uint8> ProfileWallDistance = WallDistance;
	TArray<int32> Frontier;
	Frontier.Reserve(256);
	for (int32 Idx = 0; Idx < N; ++Idx)
	{
		if (CellTerrainType.IsValidIndex(Idx)
			&& IsTerrainTypeBlocked(CellTerrainType[Idx])
			&& ProfileWallDistance[Idx] != 0)
		{
			ProfileWallDistance[Idx] = 0;
			Frontier.Add(Idx);
		}
	}

	int32 FrontierHead = 0;
	while (FrontierHead < Frontier.Num())
	{
		const int32 Cur = Frontier[FrontierHead++];
		const uint8 CurDistance = ProfileWallDistance[Cur];
		if (CurDistance >= WallDistanceCap - 1) continue;
		const int32 CurX = Cur % Width;
		const int32 CurY = Cur / Width;
		const uint8 NextDistance = CurDistance + 1;
		for (int32 Direction = 0; Direction < 8; ++Direction)
		{
			const int32 NextX = CurX + SeinNeighborDX[Direction];
			const int32 NextY = CurY + SeinNeighborDY[Direction];
			if (!IsValidCoord(NextX, NextY)) continue;
			const int32 Next = CellIndex(NextX, NextY);
			if (ProfileWallDistance[Next] > NextDistance)
			{
				ProfileWallDistance[Next] = NextDistance;
				Frontier.Add(Next);
			}
		}
	}

	auto IsEligible =
		[this, &Key, &ProfileWallDistance,
			&IsTerrainTypeBlocked](int32 X, int32 Y)
	{
		if (!IsCellPassable(X, Y)) return false;
		const int32 Idx = CellIndex(X, Y);
		if (CellTerrainType.IsValidIndex(Idx)
			&& IsTerrainTypeBlocked(CellTerrainType[Idx]))
		{
			return false;
		}
		return Key.RequiredClearance <= 0
			|| ProfileWallDistance[Idx]
				>= Key.RequiredClearance;
	};

	TArray<int32> Stack;
	Stack.Reserve(256);
	int32 NextLabel = 0;
	for (int32 Seed = 0; Seed < N; ++Seed)
	{
		if (OutComponents[Seed] != -1) continue;
		const int32 SeedX = Seed % Width;
		const int32 SeedY = Seed / Width;
		if (!IsEligible(SeedX, SeedY)) continue;

		const int32 Label = NextLabel++;
		Stack.Reset();
		Stack.Add(Seed);
		OutComponents[Seed] = Label;
		while (!Stack.IsEmpty())
		{
			const int32 Cur = Stack.Pop(EAllowShrinking::No);
			const int32 CurX = Cur % Width;
			const int32 CurY = Cur / Width;
			const uint8 CurConnections =
				CellConnections[Cur];
			for (int32 Direction = 0;
				Direction < 8;
				++Direction)
			{
				if ((CurConnections
					& (1 << Direction)) == 0)
				{
					continue;
				}
				const int32 NextX =
					CurX + SeinNeighborDX[Direction];
				const int32 NextY =
					CurY + SeinNeighborDY[Direction];
				if (!IsEligible(NextX, NextY)) continue;

				if (Direction >= 4)
				{
					const uint8 CardinalA =
						SeinDiagCardinalA[Direction - 4];
					const uint8 CardinalB =
						SeinDiagCardinalB[Direction - 4];
					if ((CurConnections & (1 << CardinalA)) == 0
						|| (CurConnections & (1 << CardinalB)) == 0)
					{
						continue;
					}
					if (Key.RequiredClearance > 0)
					{
						const int32 CardAX =
							CurX + SeinNeighborDX[CardinalA];
						const int32 CardAY =
							CurY + SeinNeighborDY[CardinalA];
						const int32 CardBX =
							CurX + SeinNeighborDX[CardinalB];
						const int32 CardBY =
							CurY + SeinNeighborDY[CardinalB];
						if (!IsValidCoord(CardAX, CardAY)
							|| !IsValidCoord(CardBX, CardBY)
							|| ProfileWallDistance[
								CellIndex(CardAX, CardAY)]
								< Key.RequiredClearance
							|| ProfileWallDistance[
								CellIndex(CardBX, CardBY)]
								< Key.RequiredClearance)
						{
							continue;
						}
					}
				}

				const int32 Next = CellIndex(NextX, NextY);
				if (OutComponents[Next] != -1) continue;
				OutComponents[Next] = Label;
				Stack.Add(Next);
			}
		}
	}
}

bool USeinNavigationAStar::GetRandomReachablePoint(const FFixedVector& QueryOrigin, FFixedPoint Radius, FFixedRandom& Rng, FFixedVector& OutPoint) const
{
	if (CellComponent.Num() != Width * Height || Width <= 0 || Height <= 0) return false;
	if (Radius <= FFixedPoint::Zero) return false;

	// Project the origin to a walkable cell; its component id defines the
	// "reachable" set the sample must land in (same notion as IsReachable).
	FFixedVector OriginProj;
	if (!ProjectPointToNav(QueryOrigin, OriginProj)) return false;
	int32 OX, OY;
	if (!WorldToGrid(OriginProj, OX, OY)) return false;
	const int32 OriginComp = CellComponent[CellIndex(OX, OY)];
	if (OriginComp < 0) return false;

	const FFixedPoint RadiusSq = Radius * Radius;
	const FFixedVector2D Centre(OriginProj.X, OriginProj.Y);

	// Disc rejection-sampling: draw a uniform point in the radius (FFixedRandom's
	// own area-uniform sampler — deterministic, advances the caller's stream),
	// snap to its cell, and accept the first that is passable AND in the origin's
	// component AND whose CELL CENTER is genuinely within Radius. Sampling over
	// the disc then filtering by component yields a uniform draw over the
	// reachable cells in range. Bounded attempts: a region too sparse to hit
	// returns false (best-effort — see RandomReachableMaxAttempts).
	for (int32 Attempt = 0; Attempt < RandomReachableMaxAttempts; ++Attempt)
	{
		const FFixedVector2D P = Rng.PointInCircle(Centre, Radius);
		int32 CX, CY;
		if (!WorldToGrid(FFixedVector(P.X, P.Y, OriginProj.Z), CX, CY)) continue;
		if (!IsCellPassable(CX, CY)) continue;
		if (CellComponent[CellIndex(CX, CY)] != OriginComp) continue;

		// The sampled point was inside the disc, but its cell CENTER can sit a
		// fraction outside — enforce the true radius on the value we return.
		const FFixedVector Candidate = GridToWorld(CX, CY);
		const FFixedPoint DX = Candidate.X - OriginProj.X;
		const FFixedPoint DY = Candidate.Y - OriginProj.Y;
		if (DX * DX + DY * DY > RadiusSq) continue;

		OutPoint = Candidate;
		return true;
	}
	return false;
}

bool USeinNavigationAStar::IsTerrainBlockedAtCell(
	int32 X,
	int32 Y,
	const FGameplayTagContainer& BlockedTerrainTags) const
{
	if (BlockedTerrainTags.IsEmpty()
		|| !IsValidCoord(X, Y))
	{
		return false;
	}
	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	if (!Settings)
	{
		return false;
	}
	const int32 StoredType = CellTerrainType.IsValidIndex(
		CellIndex(X, Y))
		? static_cast<int32>(CellTerrainType[CellIndex(X, Y)])
		: 0;
	const FGameplayTag TerrainTag =
		Settings->GetTerrainTag(StoredType);
	return TerrainTag.IsValid()
		&& BlockedTerrainTags.HasTag(TerrainTag);
}

bool USeinNavigationAStar::IsWorldPositionDynamicallyClear(
	const FFixedVector& WorldPos,
	uint8 AgentNavLayerMask,
	FSeinEntityHandle Exclude,
	const TSet<FSeinEntityHandle>* IgnoredDynamicBlockerOwners) const
{
	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y))
	{
		return false;
	}

	// The sparse index is rebuilt from the same shared stamp iterator used by
	// path overlays, so this point query and A* agree exactly without repeatedly
	// rasterizing a nearby wall/vehicle shape for every footprint sample.
	for (auto It = DynamicBlockerIndicesByCell.CreateConstKeyIterator(
		CellIndex(X, Y)); It; ++It)
	{
		const int32 BlockerIndex = It.Value();
		if (!DynamicBlockers.IsValidIndex(BlockerIndex)) continue;
		const FSeinDynamicBlocker& B = DynamicBlockers[BlockerIndex];
		if (B.Owner == Exclude
			|| (IgnoredDynamicBlockerOwners
				&& IgnoredDynamicBlockerOwners->Contains(B.Owner)))
		{
			continue;
		}
		if ((B.BlockedNavLayerMask & AgentNavLayerMask) == 0) continue;
		return false;
	}
	return true;
}

bool USeinNavigationAStar::IsWorldPositionClear(
	const FFixedVector& WorldPos,
	uint8 AgentNavLayerMask) const
{
	int32 X, Y;
	return WorldToGrid(WorldPos, X, Y)
		&& IsCellPassable(X, Y)
		&& IsWorldPositionDynamicallyClear(
			WorldPos,
			AgentNavLayerMask,
			FSeinEntityHandle());
}

bool USeinNavigationAStar::IsWorldPositionClearForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent) const
{
	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y))
	{
		return false;
	}
	if (!IsCellPassable(X, Y))
	{
		return false;
	}
	if (IsTerrainBlockedAtCell(X, Y, Agent.BlockedTerrainTags))
	{
		return false;
	}
	if (!IsWorldPositionDynamicallyClear(
		WorldPos,
		Agent.AgentNavLayerMask,
		Agent.Requester))
	{
		return false;
	}
	return true;
}

bool USeinNavigationAStar::IsWorldPositionClearForAgentIgnoringDynamicBlockers(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	const TSet<FSeinEntityHandle>& IgnoredDynamicBlockerOwners) const
{
	int32 X, Y;
	return WorldToGrid(WorldPos, X, Y)
		&& IsCellPassable(X, Y)
		&& !IsTerrainBlockedAtCell(
			X, Y, Agent.BlockedTerrainTags)
		&& IsWorldPositionDynamicallyClear(
			WorldPos,
			Agent.AgentNavLayerMask,
			Agent.Requester,
			&IgnoredDynamicBlockerOwners);
}

bool USeinNavigationAStar::IsFootprintClearForAgentIgnoringDynamicBlockers(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	const TSet<FSeinEntityHandle>& IgnoredDynamicBlockerOwners) const
{
	if (!IsWorldPositionClearForAgentIgnoringDynamicBlockers(
		WorldPos, Agent, IgnoredDynamicBlockerOwners))
	{
		return false;
	}
	const FFixedPoint Radius = Agent.AgentFootprintRadius;
	if (Radius <= FFixedPoint::Zero)
	{
		return true;
	}
	static const FFixedPoint RingDiag(3036971375LL);
	static const FFixedVector Ring[8] = {
		FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(RingDiag, RingDiag, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero),
		FFixedVector(-RingDiag, RingDiag, FFixedPoint::Zero),
		FFixedVector(-FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(-RingDiag, -RingDiag, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::Zero, -FFixedPoint::One, FFixedPoint::Zero),
		FFixedVector(RingDiag, -RingDiag, FFixedPoint::Zero),
	};
	for (const FFixedVector& Offset : Ring)
	{
		const FFixedVector Sample(
			WorldPos.X + Offset.X * Radius,
			WorldPos.Y + Offset.Y * Radius,
			WorldPos.Z);
		if (!IsWorldPositionClearForAgentIgnoringDynamicBlockers(
			Sample, Agent, IgnoredDynamicBlockerOwners))
		{
			return false;
		}
	}
	return true;
}

bool USeinNavigationAStar::IsAuthoritativeDestinationSafeForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent) const
{
	int32 X, Y;
	return WorldToGrid(WorldPos, X, Y)
		&& !IsTerrainBlockedAtCell(
			X, Y, Agent.BlockedTerrainTags)
		&& IsWorldPositionDynamicallyClear(
			WorldPos,
			Agent.AgentNavLayerMask,
			Agent.Requester);
}

bool USeinNavigationAStar::IsCellClearForAgent(
	int32 X,
	int32 Y,
	const FSeinNavAgentProfile& Agent,
	const TSet<FSeinEntityHandle>* IgnoredDynamicBlockerOwners) const
{
	if (!IsCellPassable(X, Y)
		|| IsTerrainBlockedAtCell(
			X, Y, Agent.BlockedTerrainTags))
	{
		return false;
	}

	const int32 RequiredClearance = ComputeRequiredClearance(
		Agent.AgentFootprintRadius,
		Agent.AgentWallPaddingCells);
	const int32 Idx = CellIndex(X, Y);
	if (RequiredClearance > 0
		&& (!WallDistance.IsValidIndex(Idx)
			|| WallDistance[Idx] < RequiredClearance))
	{
		return false;
	}
	if (RequiredClearance > 1
		&& !IsAgentSpecificClearanceSatisfied(
			X, Y, RequiredClearance, Agent,
			IgnoredDynamicBlockerOwners))
	{
		return false;
	}

	const FFixedVector Center = GridToWorld(X, Y);
	return IgnoredDynamicBlockerOwners
		? IsFootprintClearForAgentIgnoringDynamicBlockers(
			Center, Agent, *IgnoredDynamicBlockerOwners)
		: IsFootprintClearForAgent(Center, Agent);
}

bool USeinNavigationAStar::IsAgentSpecificClearanceSatisfied(
	int32 X,
	int32 Y,
	int32 RequiredClearance,
	const FSeinNavAgentProfile& Agent,
	const TSet<FSeinEntityHandle>* IgnoredDynamicBlockerOwners) const
{
	// Static WallDistance already handled the baked obstacle field. Repeat the
	// same Chebyshev threshold only for request-specific obstacles: forbidden
	// terrain and layer-matching dynamic stamps. Without this, an agent-aware
	// formation projection could choose a cell beside water/a deployable even
	// though the first A* request rejects that cell for the same wall padding.
	const int32 ScanRadius = FMath::Max(RequiredClearance - 1, 0);
	for (int32 DY = -ScanRadius; DY <= ScanRadius; ++DY)
	{
		for (int32 DX = -ScanRadius; DX <= ScanRadius; ++DX)
		{
			const int32 SampleX = X + DX;
			const int32 SampleY = Y + DY;
			if (!IsValidCoord(SampleX, SampleY)) continue;
			if (IsTerrainBlockedAtCell(
				SampleX,
				SampleY,
				Agent.BlockedTerrainTags))
			{
				return false;
			}
			if (!IsWorldPositionDynamicallyClear(
				GridToWorld(SampleX, SampleY),
				Agent.AgentNavLayerMask,
				Agent.Requester,
				IgnoredDynamicBlockerOwners))
			{
				return false;
			}
		}
	}
	return true;
}

bool USeinNavigationAStar::IsPlacementValid(const FFixedVector& CenterWorld, FFixedPoint YawDegrees,
	const FSeinExtentsShape& Shape, uint8 /*AgentLayerMask*/) const
{
	if (!HasRuntimeData()) return false;

	// Rasterize the FULL footprint and reject if ANY covered cell is blocked. The base
	// scaffold sampled only the centre cell, so a multi-cell building could pass validation
	// with a corner hanging over a wall. We reuse SeinStampUtils::ForEachCoveredCell — the
	// SAME deterministic fixed-point cell-cover test the path planner and dynamic-blocker
	// overlay use — so a footprint this accepts is one pathing also considers walkable.
	// Static bake only (IsCellPassable); dynamic-occupancy gating, if ever wanted, would
	// swap in IsWorldPositionClear per cell.
	const FSeinStampShape Stamp = Shape.AsStampShape();

	// Entity rotation from the placement yaw, built with fixed-point LUT trig (never FMath —
	// float sin/cos over rotation is determinism-fragile across platforms, and this runs in
	// ability validation on every client). ForEachCoveredCell composes it with the shape's
	// own YawOffset.
	const FFixedPoint YawRad = SeinStampUtils::DegToRad(YawDegrees);
	const FFixedQuaternion Rot = FFixedQuaternion::FromAxisAndAngle(
		FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::One), YawRad);

	bool bAnyCell = false;
	bool bBlocked = false;
	SeinStampUtils::ForEachCoveredCell(
		Stamp, CenterWorld, Rot, CellSize, Origin, Width, Height,
		[this, &bAnyCell, &bBlocked](int32 X, int32 Y)
		{
			bAnyCell = true;
			if (!bBlocked && !IsCellPassable(X, Y)) bBlocked = true;
		});

	if (bBlocked) return false;
	if (bAnyCell)  return true;

	// Degenerate footprint (zero extent, or centre off-grid so no cell was visited) — fall
	// back to a single centre sample so a point-like placement still gets a verdict.
	const FFixedVector SampleWorld(
		CenterWorld.X + Shape.LocalOffset.X,
		CenterWorld.Y + Shape.LocalOffset.Y,
		CenterWorld.Z);
	return IsPassable(SampleWorld);
}

void USeinNavigationAStar::SetDynamicBlockers(const TArray<FSeinDynamicBlocker>& InBlockers)
{
	// The PreTick producer emits a deterministic handle/shape-ordered list.
	// Exact equality avoids collision-prone fingerprints and catches generation,
	// sub-cell pose, rotation, and same-count geometry changes. Order-only
	// differences conservatively invalidate, which is safe for custom callers.
	if (DynamicBlockers == InBlockers) return;

	DynamicBlockers = InBlockers;
	RebuildDynamicBlockerCellIndex();
	MainScratch.bOverlayReuseValid = false;
	OnNavigationMutated.Broadcast();
}

void USeinNavigationAStar::RebuildDynamicBlockerCellIndex()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_Nav_RebuildDynamicBlockerCellIndex);
	DynamicBlockerIndicesByCell.Reset();
	if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero)
	{
		return;
	}

	for (int32 BlockerIndex = 0;
		BlockerIndex < DynamicBlockers.Num();
		++BlockerIndex)
	{
		const FSeinDynamicBlocker& Blocker =
			DynamicBlockers[BlockerIndex];
		SeinStampUtils::ForEachCoveredCell(
			Blocker.Shape,
			Blocker.EntityCenter,
			Blocker.EntityRotation,
			CellSize,
			Origin,
			Width,
			Height,
			[this, BlockerIndex](int32 X, int32 Y)
			{
				DynamicBlockerIndicesByCell.Add(
					CellIndex(X, Y), BlockerIndex);
			});
	}
}

void USeinNavigationAStar::BuildDynamicBlockedOverlay(FSeinEntityHandle Exclude, uint8 AgentNavLayerMask, FAStarScratch& Scratch) const
{
	const int32 N = Width * Height;
	if (Scratch.DynamicBlocked.Num() != N)
	{
		// First call (or grid resized): allocate + zero the entire buffer.
		// Subsequent calls hit the bounded-clear path below.
		Scratch.DynamicBlocked.SetNumZeroed(N);
		Scratch.LastOverlayDirtyRect = FIntRect(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
	}
	else if (Scratch.LastOverlayDirtyRect.Min.X <= Scratch.LastOverlayDirtyRect.Max.X)
	{
		// Bounded clear — wipe only the cells the previous overlay wrote
		// into. On large maps with a handful of blockers in one corner this
		// replaces a full-grid Memzero (e.g. 1MB on a 1km² map at 100cm
		// cells) with a few-row clear. Row-by-row Memzero so each clear is
		// still a contiguous memory operation (faster than per-cell writes
		// even for narrow rects).
		const int32 ClampedMinY = FMath::Max(0, Scratch.LastOverlayDirtyRect.Min.Y);
		const int32 ClampedMaxY = FMath::Min(Height - 1, Scratch.LastOverlayDirtyRect.Max.Y);
		const int32 ClampedMinX = FMath::Max(0, Scratch.LastOverlayDirtyRect.Min.X);
		const int32 ClampedMaxX = FMath::Min(Width - 1, Scratch.LastOverlayDirtyRect.Max.X);
		const int32 RowSpan = ClampedMaxX - ClampedMinX + 1;
		if (RowSpan > 0)
		{
			for (int32 Y = ClampedMinY; Y <= ClampedMaxY; ++Y)
			{
				FMemory::Memzero(&Scratch.DynamicBlocked[CellIndex(ClampedMinX, Y)], RowSpan);
			}
		}
	}

	// Reset dirty rect to empty before this call's stamps populate it.
	Scratch.LastOverlayDirtyRect = FIntRect(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);

	if (DynamicBlockers.Num() == 0 || N == 0) return;

	for (const FSeinDynamicBlocker& B : DynamicBlockers)
	{
		// Self-exclusion: a unit pathing out of its own footprint must not see
		// its own blocker stamped — A* would never find a start cell otherwise.
		if (B.Owner == Exclude) continue;

		// Layer-mask filter: blocker only affects this agent if their bits
		// intersect. Water (mask = Default) skips amphibious agent
		// (mask = N0) because the AND is zero.
		if ((B.BlockedNavLayerMask & AgentNavLayerMask) == 0) continue;

		// Shape iteration handles all three kinds (radial / rect / cone) plus
		// LocalOffset / YawOffset pose composition. Each visit also extends
		// the dirty rect so the next call's bounded clear knows exactly which
		// cells to wipe.
		SeinStampUtils::ForEachCoveredCell(
			B.Shape, B.EntityCenter, B.EntityRotation,
			CellSize, Origin, Width, Height,
			[this, &Scratch](int32 X, int32 Y)
			{
				Scratch.DynamicBlocked[CellIndex(X, Y)] = 1;
				if (X < Scratch.LastOverlayDirtyRect.Min.X) Scratch.LastOverlayDirtyRect.Min.X = X;
				if (Y < Scratch.LastOverlayDirtyRect.Min.Y) Scratch.LastOverlayDirtyRect.Min.Y = Y;
				if (X > Scratch.LastOverlayDirtyRect.Max.X) Scratch.LastOverlayDirtyRect.Max.X = X;
				if (Y > Scratch.LastOverlayDirtyRect.Max.Y) Scratch.LastOverlayDirtyRect.Max.Y = Y;
			});
	}
}

// ============================================================================
// Dynamic-WD lazy query
// ============================================================================

int32 USeinNavigationAStar::GetEffectiveWD(int32 X, int32 Y, int32 MaxR, FAStarScratch& Scratch) const
{
	// Defensive — invalid coord returns 0 (interpreted as "wall-adjacent",
	// so any clearance check fails). Caller normally already validated, but
	// IsValidCoord is cheap.
	if (!IsValidCoord(X, Y)) return 0;

	const int32 Idx = CellIndex(X, Y);
	const int32 StaticWD = WallDistance.IsValidIndex(Idx)
		? static_cast<int32>(WallDistance[Idx]) : 0;

	// Fast paths — if this request has neither a valid dynamic overlay nor
	// barred terrain, the baked static WallDistance is already the answer.
	const bool bHasDynamicOverlay =
		Scratch.DynamicBlocked.Num() == WallDistance.Num();
	if (!bHasDynamicOverlay && !Scratch.bRequestHasBlockedTypes)
	{
		return StaticWD;
	}
	if (MaxR <= 0) return StaticWD;

	// Rect-based early-out: if (X, Y) is outside the dirty rect inflated by
	// MaxR, no dyn-blocked cell can be within MaxR Chebyshev distance.
	// `Min.X > Max.X` is the sentinel for "empty overlay" (set in
	// BuildDynamicBlockedOverlay reset path).
	// A blocked terrain type can occur anywhere in the grid, so the dynamic
	// overlay's dirty rectangle is a valid early-out only for the common
	// no-terrain-filter case.
	if (!Scratch.bRequestHasBlockedTypes)
	{
		if (Scratch.LastOverlayDirtyRect.Min.X
			> Scratch.LastOverlayDirtyRect.Max.X)
		{
			return StaticWD;
		}
		if (X < Scratch.LastOverlayDirtyRect.Min.X - MaxR)
			return StaticWD;
		if (X > Scratch.LastOverlayDirtyRect.Max.X + MaxR)
			return StaticWD;
		if (Y < Scratch.LastOverlayDirtyRect.Min.Y - MaxR)
			return StaticWD;
		if (Y > Scratch.LastOverlayDirtyRect.Max.Y + MaxR)
			return StaticWD;
	}

	// Per-request cache lookup (lazy validation via gen tag, same pattern as
	// the A* SearchCellGen state).
	if (Scratch.DynamicWDCacheGen.IsValidIndex(Idx)
		&& Scratch.DynamicWDCacheGen[Idx] == Scratch.CurrentDynamicWDGen)
	{
		const int32 CachedDyn = static_cast<int32>(Scratch.DynamicWDCache[Idx]);
		return FMath::Min(StaticWD, CachedDyn);
	}

	// Slow path — ring scan from (X, Y) outward.
	const int32 DynWD = ComputeRequestObstacleDistanceRingScan(
		X, Y, MaxR, Scratch);

	// Cache the dynamic component. Capped to 255 (uint8 storage). Subsequent
	// reads (A* diagonal anti-squeeze, Push gradient walk) get O(1).
	if (Scratch.DynamicWDCache.IsValidIndex(Idx))
	{
		Scratch.DynamicWDCache[Idx] = static_cast<uint8>(FMath::Min(DynWD, 255));
		Scratch.DynamicWDCacheGen[Idx] = Scratch.CurrentDynamicWDGen;
	}

	return FMath::Min(StaticWD, DynWD);
}

int32 USeinNavigationAStar::ComputeRequestObstacleDistanceRingScan(
	int32 X,
	int32 Y,
	int32 MaxR,
	FAStarScratch& Scratch) const
{
	// Cell itself blocked → distance 0. (Shouldn't happen if caller filtered
	// via IsCellPassableForPath, but defensive — A* C-space gate reads the
	// CURRENT cell's WD too, not just neighbors.)
	const int32 Idx = CellIndex(X, Y);
	if (IsRequestObstacleCell(Idx, Scratch)) return 0;

	// Walk outward in Chebyshev rings (R=1, 2, 3, ...). At each ring, scan
	// only the FRAME (top row, bottom row, left col, right col minus corners).
	// First dyn-blocked cell found is by construction the nearest, so return
	// the current ring index — early exit is the perf win that justifies
	// the ring-by-ring shape over a naive (2R+1)² window scan.
	const int32 ClampedMaxR = FMath::Min(MaxR, static_cast<int32>(WallDistanceCap));
	for (int32 R = 1; R <= ClampedMaxR; ++R)
	{
		const int32 YTop = Y - R;
		const int32 YBot = Y + R;
		const int32 XLeft = X - R;
		const int32 XRight = X + R;

		// Top + bottom rows (full width including corners).
		if (YTop >= 0 && YTop < Height)
		{
			const int32 DXMin = FMath::Max(-R, -X);
			const int32 DXMax = FMath::Min( R, Width - 1 - X);
			for (int32 DX = DXMin; DX <= DXMax; ++DX)
			{
				if (IsRequestObstacleCell(
					CellIndex(X + DX, YTop), Scratch)) return R;
			}
		}
		if (YBot >= 0 && YBot < Height)
		{
			const int32 DXMin = FMath::Max(-R, -X);
			const int32 DXMax = FMath::Min( R, Width - 1 - X);
			for (int32 DX = DXMin; DX <= DXMax; ++DX)
			{
				if (IsRequestObstacleCell(
					CellIndex(X + DX, YBot), Scratch)) return R;
			}
		}

		// Left + right cols (skip corners already covered by top/bottom rows).
		const int32 DYMin = FMath::Max(-R + 1, -Y);
		const int32 DYMax = FMath::Min( R - 1, Height - 1 - Y);
		for (int32 DY = DYMin; DY <= DYMax; ++DY)
		{
			const int32 NY = Y + DY;
			if (XLeft >= 0 && IsRequestObstacleCell(
				CellIndex(XLeft, NY), Scratch)) return R;
			if (XRight < Width && IsRequestObstacleCell(
				CellIndex(XRight, NY), Scratch)) return R;
		}
	}

	// No dyn blocker within MaxR — return the WD cap so `min(static, cap)`
	// downstream collapses to the static value.
	return WallDistanceCap;
}

bool USeinNavigationAStar::GetCellHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ, bool bWalkableOnly) const
{
	if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero) return false;

	// Continuous grid coordinates — keep the fractional part for bilinear
	// interpolation between cell heights. Without this, Z snaps discretely
	// at each cell boundary, producing a visible staircase on inclines.
	const FFixedPoint LocalX = WorldPos.X - Origin.X;
	const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
	const FFixedPoint ContX = LocalX / CellSize;
	const FFixedPoint ContY = LocalY / CellSize;

	const int32 X0 = ContX.ToInt();   // deterministic floor (>>32) — no non-deterministic ToFloat
	const int32 Y0 = ContY.ToInt();

	if (!IsValidCoord(X0, Y0)) return false;

	// Walkable-only mode: refuse on blocked cells. Pruned cube tops,
	// platform interiors, and wall footprints all have their geometry's
	// top-Z stored on the cell. Without this gate, a ground unit whose XY
	// momentarily crosses a blocked cell (path smoother corner-cut, wheeled
	// vehicle's turn radius arc, footstep on a wall edge) Z-snaps to that
	// blocked cell's surface and visibly pops onto the wall. Returning
	// false makes movement preserve the previous tick's Z (floor level),
	// so the unit slides through the blocked-cell sliver at its current
	// height instead.
	//
	// `bWalkableOnly=false`: skip the gate. Flyers WANT the top-of-stuff
	// (cube top, wall top, slope hit point) so flyer Z = surface + Altitude
	// auto-clears anything in the cell.
	if (bWalkableOnly && !IsCellPassable(X0, Y0)) return false;

	const FFixedPoint PrimaryZ = CellHeight[CellIndex(X0, Y0)];

	// Fractional sub-cell position [0, 1) — the bilinear weight.
	const FFixedPoint FracX = ContX - FFixedPoint::FromInt(X0);
	const FFixedPoint FracY = ContY - FFixedPoint::FromInt(Y0);

	// Neighbor cell coords, clamped to grid bounds.
	const int32 X1 = (X0 + 1 < Width)  ? X0 + 1 : X0;
	const int32 Y1 = (Y0 + 1 < Height) ? Y0 + 1 : Y0;

	// Height at each bilinear corner. Two gates substitute PrimaryZ
	// to prevent the bilinear from creating ramps up vertical surfaces:
	//
	//   1. Blocked cells (walkable-only mode) — existing gate; the
	//      interpolation never pulls toward impassable geometry.
	//
	//   2. Step-height clamp — if the neighbor's height differs from
	//      PrimaryZ by more than CellSize, treat it as a hard wall.
	//      Natural terrain slopes never exceed CellSize vertical per
	//      cell (~45°); walls and elevated platforms far exceed it.
	//      Without this, a passable wall-top cell whose height is 300
	//      units above ground creates a smooth bilinear ramp that lets
	//      entities gradually climb the wall from the side.
	auto SafeHeight = [&](int32 X, int32 Y) -> FFixedPoint
	{
		if (bWalkableOnly && !IsCellPassable(X, Y)) return PrimaryZ;
		const FFixedPoint NeighborZ = CellHeight[CellIndex(X, Y)];
		FFixedPoint Diff = NeighborZ - PrimaryZ;
		if (Diff < FFixedPoint::Zero) Diff = -Diff;
		if (Diff > CellSize) return PrimaryZ;
		return NeighborZ;
	};

	const FFixedPoint Z00 = PrimaryZ;
	const FFixedPoint Z10 = SafeHeight(X1, Y0);
	const FFixedPoint Z01 = SafeHeight(X0, Y1);
	const FFixedPoint Z11 = SafeHeight(X1, Y1);

	// Bilinear interpolation.
	const FFixedPoint InvFracX = FFixedPoint::One - FracX;
	const FFixedPoint InvFracY = FFixedPoint::One - FracY;
	OutZ = Z00 * InvFracX * InvFracY
	     + Z10 * FracX    * InvFracY
	     + Z01 * InvFracX * FracY
	     + Z11 * FracX    * FracY;

	return true;
}

template <typename FAcceptPred>
bool USeinNavigationAStar::RingScanForCell(int32 StartX, int32 StartY, int32 MaxR, FAcceptPred&& Accept, FFixedVector& OutProjected) const
{
	// Quick check: start cell itself accepted?
	if (Accept(StartX, StartY)) { OutProjected = GridToWorld(StartX, StartY); return true; }

	// Bounded radial ring scan. Each ring at radius R has 8R cells (the (2R+1)²
	// square minus the (2R-1)² inner square). Iterating only the ring perimeter —
	// top/bottom rows (including corners) then left/right columns (excluding the
	// already-covered corners) — is O(8R) per ring. Visitation order is identical
	// to the former inline copies so results match exactly.
	for (int32 R = 1; R <= MaxR; ++R)
	{
		for (int32 i = -R; i <= R; ++i)
		{
			if (Accept(StartX + i, StartY + R)) { OutProjected = GridToWorld(StartX + i, StartY + R); return true; }
			if (Accept(StartX + i, StartY - R)) { OutProjected = GridToWorld(StartX + i, StartY - R); return true; }
		}
		for (int32 i = -R + 1; i <= R - 1; ++i)
		{
			if (Accept(StartX - R, StartY + i)) { OutProjected = GridToWorld(StartX - R, StartY + i); return true; }
			if (Accept(StartX + R, StartY + i)) { OutProjected = GridToWorld(StartX + R, StartY + i); return true; }
		}
	}
	return false;
}

bool USeinNavigationAStar::ProjectPointToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
{
	if (!HasRuntimeData()) return false;

	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y))
	{
		// Out of bounds — clamp to the nearest edge cell and project from there
		// rather than refuse. A unit shoved off the grid edge by the nav-pure
		// collision floor is recovered onto the boundary by the nav-containment
		// pass (PostTick), which relies on this; a genuinely far-off point still
		// resolves to the nearest edge cell, and the ring scan below walks inward
		// to the closest passable spot.
		if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero) return false;
		const FFixedPoint LocalX = WorldPos.X - Origin.X;
		const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
		X = FMath::Clamp((LocalX / CellSize).ToInt(), 0, Width - 1);
		Y = FMath::Clamp((LocalY / CellSize).ToInt(), 0, Height - 1);
	}

	// Ring scan for the nearest passable cell. Capped at `NavProjectionMaxRingRadius`
	// cells from plugin settings (default 30 ≈ 30m at the default 100cm grid) so a
	// click in a totally blocked region (open ocean, large impassable mountain) fails
	// fast instead of sweeping the entire grid. Past that distance the user almost
	// certainly meant a different click — failure is the right answer.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const int32 MaxProjectionRingRadius = Settings ? Settings->NavProjectionMaxRingRadius : 30;

	return RingScanForCell(X, Y, MaxProjectionRingRadius,
		[this](int32 CX, int32 CY) { return IsCellPassable(CX, CY); },
		OutProjected);
}

bool USeinNavigationAStar::ProjectPointToNavForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	FFixedVector& OutProjected) const
{
	if (!HasRuntimeData()) return false;

	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y))
	{
		if (Width <= 0 || Height <= 0
			|| CellSize <= FFixedPoint::Zero)
		{
			return false;
		}
		const FFixedPoint LocalX = WorldPos.X - Origin.X;
		const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
		X = FMath::Clamp(
			(LocalX / CellSize).ToInt(), 0, Width - 1);
		Y = FMath::Clamp(
			(LocalY / CellSize).ToInt(), 0, Height - 1);
	}

	const USeinARTSCoreSettings* Settings =
		GetDefault<USeinARTSCoreSettings>();
	const int32 MaxProjectionRingRadius = Settings
		? Settings->NavProjectionMaxRingRadius
		: 30;
	return RingScanForCell(
		X,
		Y,
		MaxProjectionRingRadius,
		[this, &Agent](int32 CX, int32 CY)
		{
			return IsCellClearForAgent(CX, CY, Agent);
		},
		OutProjected);
}

bool USeinNavigationAStar::ProjectPointToNavOnElevation(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
{
	if (!HasRuntimeData()) return false;

	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y))
	{
		return false;
	}

	// Z tolerance: cells within this height delta of WorldPos.Z are
	// considered "same elevation." Read from plugin settings — default
	// 100cm covers most stair / curb / ramp-step deltas without crossing
	// typical platform heights (200cm+ for raised platforms is well
	// outside tolerance, so floor cells under a platform won't match a
	// click on the platform top). Designer-tunable for projects with
	// closely-spaced mezzanine levels (lower) or tall step-ups that
	// should still be considered same elevation (higher).
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FFixedPoint ZTolerance = Settings
		? Settings->NavProjectionElevationTolerance
		: FFixedPoint::FromInt(100);
	const FFixedPoint ZHint = WorldPos.Z;

	auto IsAcceptable = [&](int32 CX, int32 CY) -> bool
	{
		if (!IsCellPassable(CX, CY)) return false;
		const FFixedPoint CellZ = CellHeight[CellIndex(CX, CY)];
		const FFixedPoint ZDiff = (CellZ > ZHint) ? (CellZ - ZHint) : (ZHint - CellZ);
		return ZDiff <= ZTolerance;
	};

	// Ring scan for a cell on matching elevation (same scan structure as
	// ProjectPointToNav, accepting only cells within ZTolerance of the input's Z).
	// If no Z-matching cell is found within the scan radius, fall back to plain XY
	// projection (any walkable cell, regardless of elevation) so we never return
	// failure when a walkable cell exists — better to put the unit on the wrong
	// elevation than to drop the command entirely.
	const int32 MaxProjectionRingRadius = Settings ? Settings->NavProjectionMaxRingRadius : 30;
	if (RingScanForCell(X, Y, MaxProjectionRingRadius, IsAcceptable, OutProjected)) return true;

	// No Z-matching cell within scan radius — fall back to plain XY projection.
	return ProjectPointToNav(WorldPos, OutProjected);
}

bool USeinNavigationAStar::ProjectPointToNavFree(
	const FFixedVector& WorldPos,
	FFixedPoint SelfRadius,
	const TArray<FFixedVector>& AvoidCentres,
	const TArray<FFixedPoint>& AvoidRadii,
	FFixedVector& OutProjected) const
{
	return ProjectPointToNavFreeInternal(
		WorldPos,
		SelfRadius,
		nullptr,
		nullptr,
		AvoidCentres,
		AvoidRadii,
		OutProjected);
}

bool USeinNavigationAStar::ProjectPointToNavFreeForAgent(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	const TArray<FFixedVector>& AvoidCentres,
	const TArray<FFixedPoint>& AvoidRadii,
	FFixedVector& OutProjected) const
{
	return ProjectPointToNavFreeInternal(
		WorldPos,
		Agent.AgentFootprintRadius,
		&Agent,
		nullptr,
		AvoidCentres,
		AvoidRadii,
		OutProjected);
}

bool USeinNavigationAStar::ProjectPointToNavFreeForAgentIgnoringDynamicBlockers(
	const FFixedVector& WorldPos,
	const FSeinNavAgentProfile& Agent,
	const TSet<FSeinEntityHandle>& IgnoredDynamicBlockerOwners,
	const TArray<FFixedVector>& AvoidCentres,
	const TArray<FFixedPoint>& AvoidRadii,
	FFixedVector& OutProjected) const
{
	return ProjectPointToNavFreeInternal(
		WorldPos,
		Agent.AgentFootprintRadius,
		&Agent,
		&IgnoredDynamicBlockerOwners,
		AvoidCentres,
		AvoidRadii,
		OutProjected);
}

bool USeinNavigationAStar::ProjectPointToNavFreeInternal(
	const FFixedVector& WorldPos,
	FFixedPoint SelfRadius,
	const FSeinNavAgentProfile* Agent,
	const TSet<FSeinEntityHandle>* IgnoredDynamicBlockerOwners,
	const TArray<FFixedVector>& AvoidCentres,
	const TArray<FFixedPoint>& AvoidRadii,
	FFixedVector& OutProjected) const
{
	if (!HasRuntimeData()) return false;

	int32 X, Y;
	if (!WorldToGrid(WorldPos, X, Y))
	{
		// Off-grid input (the slot overflowed the play area): clamp to the nearest edge cell and scan
		// inward from there, exactly like ProjectPointToNav — the ring walk below finds the closest
		// free cell just inside the boundary.
		if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero) return false;
		const FFixedPoint LocalX = WorldPos.X - Origin.X;
		const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
		X = FMath::Clamp((LocalX / CellSize).ToInt(), 0, Width - 1);
		Y = FMath::Clamp((LocalY / CellSize).ToInt(), 0, Height - 1);
	}

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FFixedPoint ZTolerance = Settings
		? Settings->NavProjectionElevationTolerance
		: FFixedPoint::FromInt(100);
	const int32 MaxProjectionRingRadius = Settings ? Settings->NavProjectionMaxRingRadius : 30;
	const FFixedPoint ZHint = WorldPos.Z;

	// Cell centre clears every avoid footprint (planar): dist(centre, AvoidCentres[j]) >= SelfRadius +
	// AvoidRadii[j]. This is the occupancy gate that makes the result a FREE cell, not just a walkable
	// one — two off-nav slots projecting toward the same edge land on distinct cells.
	auto ClearsAvoid = [&](const FFixedVector& Centre) -> bool
	{
		for (int32 j = 0; j < AvoidCentres.Num(); ++j)
		{
			const FFixedPoint Rj = AvoidRadii.IsValidIndex(j) ? AvoidRadii[j] : FFixedPoint::Zero;
			const FFixedPoint MinD = SelfRadius + Rj;
			const FFixedPoint DX = Centre.X - AvoidCentres[j].X;
			const FFixedPoint DY = Centre.Y - AvoidCentres[j].Y;
			if (DX * DX + DY * DY < MinD * MinD) return false;
		}
		return true;
	};
	// bRequireElevation pass 1 prefers a same-elevation free cell (no cliff stragglers); pass 2 drops
	// the elevation constraint so a free cell on the wrong elevation still beats overflowing the map.
	auto IsAcceptable = [&](int32 CX, int32 CY, bool bRequireElevation) -> bool
	{
		if (Agent)
		{
			if (!IsCellClearForAgent(
				CX, CY, *Agent, IgnoredDynamicBlockerOwners))
			{
				return false;
			}
		}
		else if (!IsCellPassable(CX, CY))
		{
			return false;
		}
		if (bRequireElevation)
		{
			const FFixedPoint CellZ = CellHeight[CellIndex(CX, CY)];
			const FFixedPoint ZDiff = (CellZ > ZHint) ? (CellZ - ZHint) : (ZHint - CellZ);
			if (ZDiff > ZTolerance) return false;
		}
		return ClearsAvoid(GridToWorld(CX, CY));
	};

	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bRequireElevation = (Pass == 0);

		// Bounded radial ring scan (same O(8R)-per-ring structure as ProjectPointToNav).
		// Pass 1 (elevation-required) then pass 2 (elevation-relaxed) — the scan checks
		// the input cell first, then walks outward.
		if (RingScanForCell(X, Y, MaxProjectionRingRadius,
			[&](int32 CX, int32 CY) { return IsAcceptable(CX, CY, bRequireElevation); },
			OutProjected))
		{
			return true;
		}
	}

	// No free cell within scan radius (dense crowd / tiny pocket) — never drop the slot: fall back to
	// the occupancy-blind nearest walkable cell. The resolver's de-overlap pass remains the last word.
	if (!Agent)
	{
		return ProjectPointToNavOnElevation(
			WorldPos, OutProjected);
	}
	if (!IgnoredDynamicBlockerOwners)
	{
		return ProjectPointToNavForAgent(
			WorldPos, *Agent, OutProjected);
	}

	return RingScanForCell(
		X,
		Y,
		MaxProjectionRingRadius,
		[this, Agent, IgnoredDynamicBlockerOwners](int32 CX, int32 CY)
		{
			return IsCellClearForAgent(
				CX, CY, *Agent, IgnoredDynamicBlockerOwners);
		},
		OutProjected);
}

// ============================================================================
// A* pathfinding
// ============================================================================

namespace
{
	/** Octile distance * 10 (cardinal step = 10, diagonal = 14). Integer costs
	 *  for determinism. */
	FORCEINLINE int32 OctileHeuristic(int32 AX, int32 AY, int32 BX, int32 BY)
	{
		const int32 DX = FMath::Abs(AX - BX);
		const int32 DY = FMath::Abs(AY - BY);
		const int32 Mn = FMath::Min(DX, DY);
		const int32 Mx = FMath::Max(DX, DY);
		return 14 * Mn + 10 * (Mx - Mn);
	}
	// FAStarNode moved to a class-private nested type on USeinNavigationAStar
	// so the Open heap can live inside FAStarScratch (per-worker on the parallel
	// path; the serial path reuses the persistent MainScratch) and preserve its
	// allocation across FindPath calls. Definition is in SeinNavigationAStar.h.

	// ------------------------------------------------------------------------
	// Shared 8-neighbor direction tables + the single Bresenham grid-walk
	// primitive. These were previously duplicated ~5× (NeighborDX/DY/Cost,
	// CardinalA/B, the (StepDX,StepDY)→DirIdx mapping, and the supercover
	// Bresenham loop in HasLineOfSight / NavRaycast / the FindPath replay
	// diagnostic / the debug Rasterize). One copy each; identical values.
	// ------------------------------------------------------------------------

	/** 8-neighbor offsets, indexed by direction:
	 *    0:E(+1,0) 1:W(-1,0) 2:N(0,+1) 3:S(0,-1)
	 *    4:NE(+1,+1) 5:SE(+1,-1) 6:NW(-1,+1) 7:SW(-1,-1) */
	/** For a diagonal direction index (4..7), the two cardinal direction indices
	 *  that flank it (used by the diagonal anti-squeeze checks). Index with
	 *  (DirIdx - 4). */
	/** Map a single grid step (dx, dy) ∈ {-1,0,+1}² to its 8-neighbor direction
	 *  index, or -1 for a no-move (0,0). Matches the bake-time bitmask ordering. */
	FORCEINLINE int32 SeinStepToDirIdx(int32 StepDX, int32 StepDY)
	{
		if      (StepDX ==  1 && StepDY ==  0) return 0;
		else if (StepDX == -1 && StepDY ==  0) return 1;
		else if (StepDX ==  0 && StepDY ==  1) return 2;
		else if (StepDX ==  0 && StepDY == -1) return 3;
		else if (StepDX ==  1 && StepDY ==  1) return 4;
		else if (StepDX ==  1 && StepDY == -1) return 5;
		else if (StepDX == -1 && StepDY ==  1) return 6;
		else if (StepDX == -1 && StepDY == -1) return 7;
		return -1;
	}

	/** Outcome of a WalkGridLine traversal. */
	enum class ESeinGridWalk : uint8
	{
		ReachedEnd,     // walk arrived at (X1,Y1) with no callback abort
		AbortedAtCell,  // VisitFn returned false at the current cell
		AbortedAtStep,  // StepFn returned false on the step about to be taken
	};

	/** Single Bresenham grid walk from (X0,Y0) to (X1,Y1), promoted to a TRUE
	 *  SUPERCOVER at diagonal steps (visits the two corner cells the line clips,
	 *  so an LoS check can't tunnel diagonally through a thin wall — see the
	 *  in-loop note).
	 *
	 *  Per cell, calls VisitFn(X, Y) — returning false aborts the walk
	 *  (ReachedEnd is NOT reported; AbortedAtCell is). On reaching the end cell
	 *  the walk stops with ReachedEnd BEFORE computing a further step. Otherwise
	 *  it computes the next cell and its direction index and calls
	 *  StepFn(X, Y, NextX, NextY, DirIdx) — returning false aborts with
	 *  AbortedAtStep (NextX/NextY are passed so the caller can record the
	 *  rejected step's target).
	 *
	 *  Templated + FORCEINLINE so the callbacks inline into the loop with no
	 *  indirect call — the hot smoothing/raycast paths keep their original
	 *  codegen. The core Bresenham arithmetic (DX/DY/SX/SY/Err, the `E2 > -DY` /
	 *  `E2 < DX` order) is unchanged; the only addition is the two corner VisitFn
	 *  calls on a diagonal step (the supercover gate). Deterministic on every
	 *  client, so it's safe on the sim FindPath path. */
	template <typename FVisitFn, typename FStepFn>
	FORCEINLINE ESeinGridWalk WalkGridLine(int32 X0, int32 Y0, int32 X1, int32 Y1,
		FVisitFn&& VisitFn, FStepFn&& StepFn)
	{
		const int32 DX = FMath::Abs(X1 - X0);
		const int32 DY = FMath::Abs(Y1 - Y0);
		const int32 SX = (X0 < X1) ? 1 : -1;
		const int32 SY = (Y0 < Y1) ? 1 : -1;
		int32 Err = DX - DY;

		int32 X = X0, Y = Y0;
		for (;;)
		{
			if (!VisitFn(X, Y)) return ESeinGridWalk::AbortedAtCell;
			if (X == X1 && Y == Y1) return ESeinGridWalk::ReachedEnd;

			int32 NextX = X, NextY = Y;
			const int32 E2 = 2 * Err;
			const bool bStepX = (E2 > -DY);
			const bool bStepY = (E2 <  DX);
			if (bStepX) { Err -= DY; NextX += SX; }
			if (bStepY) { Err += DX; NextY += SY; }

			// TRUE-SUPERCOVER corner gate. On a diagonal step (both axes advance) the
			// geometric line clips the two corner cells (X+SX, Y) and (X, Y+SY). A plain
			// Bresenham jumps corner-to-corner and visits NEITHER — so an LoS walk can
			// TUNNEL straight through a thin wall (or a dynamic nav blocker) wedged at the
			// diagonal junction: the smoother then collapses an A* detour into a straight
			// line through the wall, and the StepFn's static-connectivity anti-squeeze
			// can't catch it because a dynamic blocker isn't in CellConnections. Visit
			// BOTH corners so a blocked / low-clearance corner aborts the line, matching
			// A*'s own diagonal anti-squeeze (both flanking cardinals must clear). Runs
			// only on genuine diagonal steps; cardinal steps keep the plain fast path.
			// Deterministic order (X-corner then Y-corner) — same on every client.
			if (bStepX && bStepY)
			{
				if (!VisitFn(NextX, Y)) return ESeinGridWalk::AbortedAtCell;
				if (!VisitFn(X, NextY)) return ESeinGridWalk::AbortedAtCell;
			}

			const int32 DirIdx = SeinStepToDirIdx(NextX - X, NextY - Y);
			if (!StepFn(X, Y, NextX, NextY, DirIdx)) return ESeinGridWalk::AbortedAtStep;

			X = NextX;
			Y = NextY;
		}
	}
}

void USeinNavigationAStar::AStarSearch(FIntPoint Start, FIntPoint End, bool& bOutPartial,
	int32 HeuristicWeightPercent, int32 MaxIterations, int32 RequiredClearance, FAStarScratch& Scratch) const
{
	bOutPartial = false;
	// The reconstructed chain is written into Scratch.CellPath (pooled). Reset it
	// up front so every early-out leaves the caller an empty path.
	Scratch.CellPath.Reset();
	if (!IsValidCoord(Start.X, Start.Y) || !IsValidCoord(End.X, End.Y)) return;
	// Start gate — STATIC passability only (baked wall / off-grid / terrain cost).
	// We deliberately TOLERATE a start cell blocked ONLY in the dynamic overlay: a
	// unit spawned on — or shoved onto — a runtime nav blocker (e.g. a cover wall
	// dropped over a spawn point) is physically standing there and must be able to
	// path OUT. Seeding it lets the C-space escape below (strict-improving effective
	// WD) walk it to the nearest clear cell; a hard IsCellPassableForPath bail here
	// would strand it forever (empty path → move fails on every repath). A genuinely
	// STATIC-blocked start (inside a baked wall) still bails. We also no longer
	// early-out on a blocked End — the search continues and best-H tracking yields a
	// partial to the closest reachable cell.
	if (!IsCellPassable(Start.X, Start.Y)) return;

	const int32 N = Width * Height;

	// Lazy-validated state arrays. Sized to the current grid on first call
	// after a grid load (or grid resize); never re-initialized between
	// searches. Reads are gated on `SearchCellGen[i] == CurrentSearchGen` —
	// stale entries are interpreted as "untouched" (GCost=INT32_MAX,
	// Parent=-1, Closed=false). Writes set the gen alongside the value.
	//
	// SetNumUninitialized for the value arrays since lazy validation
	// guarantees we never read them without a gen check; the bytes can stay
	// uninitialized and we save a memzero. SearchCellGen MUST be zero-init
	// (fresh state = gen 0, never matches any CurrentSearchGen ≥ 1).
	//
	// Per-call cost now scales with **expanded nodes** (hundreds-to-low-
	// thousands) instead of grid size — the previous Init() trio cost
	// ~9 MB of memzero per FindPath on a 1km² grid (4+4+1 MB).
	if (Scratch.SearchGCosts.Num() != N) Scratch.SearchGCosts.SetNumUninitialized(N);
	if (Scratch.SearchParents.Num() != N) Scratch.SearchParents.SetNumUninitialized(N);
	if (Scratch.SearchClosed.Num() != N) Scratch.SearchClosed.SetNumUninitialized(N);
	if (Scratch.SearchCellGen.Num() != N) Scratch.SearchCellGen.Init(0, N);

	// Bump search generation. uint16 wraparound: when ++ rolls 0xFFFF → 0,
	// every existing gen-marked cell would suddenly look "untouched in this
	// search" but actually carry data from search 0xFFFF. Avoid by doing one
	// full reset on wrap and starting back at 1 (0 reserved for "never
	// touched"). Wrap-reset is amortized over ~65k searches so its per-call
	// cost is negligible.
	if (++Scratch.CurrentSearchGen == 0)
	{
		FMemory::Memzero(Scratch.SearchCellGen.GetData(), N * sizeof(uint16));
		Scratch.CurrentSearchGen = 1;
	}
	const uint16 SearchGen = Scratch.CurrentSearchGen;

	// Reset the scratch-scoped open list — preserves capacity from prior
	// searches so consecutive long paths don't repeatedly grow the heap
	// from the default reserve. Declaration in FAStarScratch (header).
	Scratch.Open.Reset();
	int32 Tiebreak = 0;

	const int32 StartIdx = CellIndex(Start.X, Start.Y);
	const int32 EndIdx = CellIndex(End.X, End.Y);

	const int32 StartH = OctileHeuristic(Start.X, Start.Y, End.X, End.Y);

	// Best (lowest-H) cell observed during the search. Tie-broken by GCost
	// (prefer lower G — closer reachable cell to the goal that's also cheap
	// to reach). Initialized to Start so even a single-step search produces
	// a defined no-op partial when no neighbors expand.
	int32 BestCellIdx = StartIdx;
	int32 BestH = StartH;
	int32 BestG = 0;

	// Clamp weight to [100, ...] so admissibility is the floor. Sub-100
	// values would invert "closer to goal wins" search direction; no upside.
	// Lockstep-deterministic: same Weight on every client.
	const int32 Weight = FMath::Max(HeuristicWeightPercent, 100);

	// Iteration cap. Bounds worst-case work on huge grids / unreachable
	// goals — when hit, the existing best-H partial-path return path
	// activates the same as if the open list had exhausted naturally.
	const int32 IterCap = FMath::Max(MaxIterations, 1);
	int32 Iterations = 0;

	// Touch start cell — initialize all gen-validated fields explicitly so
	// the first read in the loop sees the right values regardless of what
	// the uninitialized SetNumUninitialized memory held.
	Scratch.SearchGCosts[StartIdx] = 0;
	Scratch.SearchParents[StartIdx] = -1;
	Scratch.SearchClosed[StartIdx] = 0;
	Scratch.SearchCellGen[StartIdx] = SearchGen;
	Scratch.Open.HeapPush(FAStarNode{StartIdx, (StartH * Weight) / 100, 0, Tiebreak++});

	// Shared 8-neighbor offset/cost tables (file-local SeinNeighbor* above).
	const int32 (&NeighborDX)[8]   = SeinNeighborDX;
	const int32 (&NeighborDY)[8]   = SeinNeighborDY;
	const int32 (&NeighborCost)[8] = SeinNeighborCost;

	// Reconstruct walks Parents from a destination cell back to Start, filling
	// the pooled Scratch.CellPath in place (Reset, not realloc). Every cell on
	// the chain was touched in this search (we only recurse from cells whose
	// Parents we explicitly wrote), so SearchParents reads are safe without gen
	// checks here. The chain is collected reversed (End→Start) then reversed
	// in place to Start→End — one buffer, no per-call by-value temporaries.
	auto Reconstruct = [&](int32 EndIdxLocal)
	{
		Scratch.CellPath.Reset();
		int32 CurIdx = EndIdxLocal;
		while (CurIdx != -1)
		{
			Scratch.CellPath.Add(FIntPoint(CurIdx % Width, CurIdx / Width));
			CurIdx = Scratch.SearchParents[CurIdx];
		}
		Algo::Reverse(Scratch.CellPath);
	};

	while (Scratch.Open.Num() > 0)
	{
		FAStarNode Cur;
		Scratch.Open.HeapPop(Cur, EAllowShrinking::No);

		// Closed check: only meaningful when the cell was touched in THIS
		// search. SearchCellGen mismatch ⇒ stale data from a previous
		// search; treat as "not closed."
		if (Scratch.SearchCellGen[Cur.CellIdx] == SearchGen && Scratch.SearchClosed[Cur.CellIdx]) continue;
		Scratch.SearchClosed[Cur.CellIdx] = 1;
		Scratch.SearchCellGen[Cur.CellIdx] = SearchGen;

		if (Cur.CellIdx == EndIdx)
		{
			// Exact goal reached.
			Reconstruct(EndIdx);
			return;
		}

		// Iteration cap — bound worst-case search on unreachable goals or
		// huge grids. On hit, fall through to the post-loop best-H partial
		// reconstruction (same code path as natural open-list exhaustion).
		// Counted at expansion-time (post-pop) so cap reflects work done.
		if (++Iterations >= IterCap)
		{
			break;
		}

		// Track best-H closed cell. Closed-time tracking ensures we only
		// consider cells we can actually reconstruct a path to (Parents is
		// already set when the cell was first enqueued). Compare by raw
		// (un-weighted) octile distance so the "closest to goal" semantic
		// is independent of HeuristicWeightPercent. Tie-break by lower G
		// so a fast-and-near beats a slow-and-near.
		const int32 CX = Cur.CellIdx % Width;
		const int32 CY = Cur.CellIdx / Width;
		const int32 CurH = OctileHeuristic(CX, CY, End.X, End.Y);
		if (CurH < BestH || (CurH == BestH && Cur.GCost < BestG))
		{
			BestH = CurH;
			BestG = Cur.GCost;
			BestCellIdx = Cur.CellIdx;
		}

		const uint8 CurConn = CellConnections[Cur.CellIdx];

		// Effective clearance threshold for cells reachable from THIS cell.
		// Configuration-space rule: while inside C-space (Current's
		// WallDistance >= RequiredClearance), neighbors must also be in
		// C-space. While escaping a too-tight starting position, neighbors
		// must be NON-DECREASING (>= current WD) — the unit may traverse level
		// low-clearance cells and climb when it can, until it reaches C-space.
		// Once in C-space, we never go back to a low-WD cell (the rule below
		// requires >= RequiredClearance from then on). (Was CurWD+1 "strict
		// climb"; that stranded units shoved onto a flat WD=1 corner plateau
		// with no strictly-higher orthogonal neighbor — corner-orphan fix
		// 2026-06-14. C-space behavior is bit-identical: min(RC, CurWD)==RC
		// whenever CurWD>=RC, so only sub-clearance STARTS see the change.)
		// Effective WD reads `min(static WallDistance, dynamic-blocker
		// Chebyshev distance)` via GetEffectiveWD, so dyn blockers
		// constrain A* topology the same way static walls do (with the
		// rect-based fast path keeping the cost low when blockers are
		// sparse around the search).
		const int32 CurWD = (RequiredClearance > 0)
			? GetEffectiveWD(CX, CY, RequiredClearance, Scratch)
			: 0;
		const int32 RequiredFromHere = (RequiredClearance > 0)
			? FMath::Min(RequiredClearance, CurWD)
			: 0;

		for (int32 n = 0; n < 8; ++n)
		{
			// Connectivity bit — set at bake time via midpoint trace + slope +
			// max-step gate. Replaces live slope math in the hot loop.
			if ((CurConn & (1 << n)) == 0) continue;

			const int32 NX = CX + NeighborDX[n];
			const int32 NY = CY + NeighborDY[n];
			if (!IsCellPassableForPath(NX, NY, Scratch)) continue; // static + dynamic blocker check

			const int32 NIdx = CellIndex(NX, NY);

			// Configuration-space clearance gate. Neighbor's WallDistance must
			// be at least `RequiredFromHere` — see the comment above for the
			// "normal vs escape" branch. This is what makes the A* topology
			// itself footprint-correct: only cells where the unit's body
			// physically fits are expanded into (with a strict-improvement
			// escape carve-out for stuck starts).
			//
			// GOAL carve-out (mirror of the START escape above): allow stepping
			// ONTO the exact destination cell even when its cell-rounded clearance
			// is below RequiredClearance, provided it's passable (checked at the top
			// of the loop) and reached from a clearance-valid cell. A unit may
			// legitimately STAND a fraction of a cell from a wall — e.g. a cover slot
			// is just a point generated ~one footprint outside the wall; the body
			// physically fits there, but whole-cell clearance rounds it "too close".
			// Without this exemption A* can't reach the goal, returns a partial path,
			// and MoveToAction strands the unit short of cover (degeneratePath →
			// escape). The APPROACH still demands full clearance (the gate stays live
			// for every non-goal cell), so this can't squeeze a unit THROUGH a gap —
			// only let it stop AT a reachable, passable destination next to a wall.
			const bool bNeighborIsGoal = (NX == End.X && NY == End.Y);
			if (RequiredClearance > 0 && !bNeighborIsGoal)
			{
				const int32 NeighborWD = GetEffectiveWD(NX, NY, RequiredClearance, Scratch);
				if (NeighborWD < RequiredFromHere) continue;
			}

			// Disallow diagonal squeezes through blocked-or-disconnected corners.
			// The diagonal bit being set on THIS cell doesn't imply both cardinal
			// transitions are legal — check each cardinal-step bit on the current
			// cell's connectivity mask. ALSO checks clearance on both flanking
			// cardinals so a diagonal step can't "tunnel" through low-WD cells
			// even if the diagonal cell itself has enough clearance.
			if (n >= 4)
			{
				// Cardinal indices making up this diagonal:
				//   4: (+1,+1) → cardinal (+1,0)=idx 0 and (0,+1)=idx 2
				//   5: (+1,-1) → (+1,0)=0, (0,-1)=3
				//   6: (-1,+1) → (-1,0)=1, (0,+1)=2
				//   7: (-1,-1) → (-1,0)=1, (0,-1)=3
				const uint8 AIdx = SeinDiagCardinalA[n - 4];
				const uint8 BIdx = SeinDiagCardinalB[n - 4];
				if ((CurConn & (1 << AIdx)) == 0 || (CurConn & (1 << BIdx)) == 0) continue;

				// Diagonal clearance anti-squeeze: both cardinal cells the
				// diagonal step passes adjacent to must satisfy clearance.
				if (RequiredClearance > 0)
				{
					const int32 CardAX = CX + NeighborDX[AIdx];
					const int32 CardAY = CY + NeighborDY[AIdx];
					const int32 CardBX = CX + NeighborDX[BIdx];
					const int32 CardBY = CY + NeighborDY[BIdx];
					if (IsValidCoord(CardAX, CardAY) && IsValidCoord(CardBX, CardBY))
					{
						const int32 CardAWD = GetEffectiveWD(CardAX, CardAY, RequiredClearance, Scratch);
						const int32 CardBWD = GetEffectiveWD(CardBX, CardBY, RequiredClearance, Scratch);
						if (CardAWD < RequiredFromHere || CardBWD < RequiredFromHere) continue;
					}
				}
			}

			// Closed check on neighbor: same gen-validated semantic as the
			// popped-node check above. Stale SearchClosed values from prior
			// searches are filtered by the gen mismatch.
			if (Scratch.SearchCellGen[NIdx] == SearchGen && Scratch.SearchClosed[NIdx]) continue;

			// Base step cost = neighbor-direction weight × per-cell terrain
			// cost. Topology already accounts for clearance via the C-space
			// gate above — no clearance cost-shaping needed. (Previous design
			// kept A* clearance-blind and pushed waypoints post-hoc; the new
			// configuration-space approach makes A* itself produce footprint-
			// correct paths, so smoothing and any post-process are guaranteed
			// to operate on clear cells.)
			const int32 StepCost = NeighborCost[n] * CellCost[NIdx];
			const int32 NewG = Cur.GCost + StepCost;

			// GCost read: gen-validated. Stale entries treated as INT32_MAX
			// so any new G is an improvement on first visit this search.
			const int32 PrevG = (Scratch.SearchCellGen[NIdx] == SearchGen) ? Scratch.SearchGCosts[NIdx] : INT32_MAX;
			if (NewG >= PrevG) continue;

			Scratch.SearchGCosts[NIdx] = NewG;
			Scratch.SearchParents[NIdx] = Cur.CellIdx;
			// Explicitly mark not-yet-closed when first touching — uninit
			// SearchClosed bytes might contain anything; the gen check above
			// reads them safely as "untouched," but as soon as we set the gen
			// here the byte itself must reflect "open."
			Scratch.SearchClosed[NIdx] = 0;
			Scratch.SearchCellGen[NIdx] = SearchGen;
			// Apply heuristic weight to f-cost. Weighted A* — paths bounded
			// by Weight% of optimal; search prunes aggressively on obstacle-
			// rich maps. Determinism preserved via shared Weight constant.
			const int32 H = OctileHeuristic(NX, NY, End.X, End.Y);
			const int32 WeightedHCost = (H * Weight) / 100;
			Scratch.Open.HeapPush(FAStarNode{NIdx, NewG + WeightedHCost, NewG, Tiebreak++});
		}
	}

	// Open exhausted (or iteration cap hit) without reaching End → return
	// a partial path to the best-H cell visited. Falls back to [Start] when
	// no expansion happened (Start in a single-cell pocket), which lets the
	// move-to action complete in place rather than fail outright. Both the
	// "naturally unreachable" and "cap-hit" cases produce identical results
	// for the caller — partial path with bOutPartial=true.
	bOutPartial = true;

#if !UE_BUILD_SHIPPING
	// Both diagnostics (the Verbose "why did A* give up" line and the Warning
	// "stuck at start" neighbor dump) live in ReportAStarPartial so this host
	// function reads at its real logic size. The reporter gates each block by
	// its own log-channel activity BEFORE doing any grid work (WD ring scans),
	// so a release build with the channel off pays nothing here.
	ReportAStarPartial(Start, End, StartIdx, EndIdx, BestCellIdx, Iterations, IterCap, RequiredClearance, Scratch);
#endif

	Reconstruct(BestCellIdx);
}

#if !UE_BUILD_SHIPPING
#include "SeinNavigationAStarDiagnostics.inl"
#endif

// ============================================================================
// Path smoothing (line-of-sight string-pull)
// ============================================================================

bool USeinNavigationAStar::HasLineOfSight(int32 X0, int32 Y0, int32 X1, int32 Y1, FAStarScratch& Scratch, int32 RequiredClearance) const
{
	// Bresenham supercover with connectivity gate — the smoother must honor the
	// same rules A* uses, otherwise a path that A* carefully routed around an
	// obstacle gets collapsed into a straight line that walks through
	// walkable-but-disconnected cells (e.g., across a cube footprint). Walks the
	// line via the shared WalkGridLine primitive; the per-cell + per-step
	// predicates below carry the exact gates the hand-rolled loop did.
	bool bIsAnchor = true;

	const ESeinGridWalk Walk = WalkGridLine(X0, Y0, X1, Y1,
		[&](int32 X, int32 Y) -> bool
		{
			if (!IsCellPassableForPath(X, Y, Scratch)) return false;

			// Clearance gate. Reject lines that pass through any cell whose
			// WallDistance is below the requested clearance. The anchor cell
			// (X0, Y0) is exempt because the unit may legitimately START in a
			// low-clearance cell — we still need smoothing to find a line OUT.
			// Without this gate, BuildSmoothedPath collapses A* corner detours
			// into straight segments that hug wall edges; PushWaypointsAwayFromWalls
			// then only fixes endpoint waypoints, leaving segments grazing walls.
			// Effective WD (static + dyn blocker) — same gate semantics as A*.
			// Smoother won't collapse segments past dynamic blockers either.
			if (!bIsAnchor && RequiredClearance > 0)
			{
				const int32 WD = GetEffectiveWD(X, Y, RequiredClearance, Scratch);
				if (WD < RequiredClearance) return false;
			}
			bIsAnchor = false;
			return true;
		},
		[&](int32 X, int32 Y, int32 /*NextX*/, int32 /*NextY*/, int32 DirIdx) -> bool
		{
			// Connectivity gate on the step about to be taken (DirIdx already
			// mapped from the (dx,dy) step by WalkGridLine).
			if (DirIdx < 0) return true;
			const uint8 Conn = CellConnections[CellIndex(X, Y)];
			if ((Conn & (1 << DirIdx)) == 0) return false;
			// Diagonal anti-squeeze — mirrors A*'s rule. The diagonal
			// connectivity bit alone says "midpoint trace passed slope+step
			// gates," but a paired-cardinal blockage (the two cells the
			// diagonal step skims past) means the line physically crosses
			// blocked terrain even if the bake's midpoint sample was happy.
			// Without this, path smoothing can collapse around wall corners
			// A* carefully routed around — visible as paths cutting through
			// red-blocked cells at platform edges.
			if (DirIdx >= 4)
			{
				const uint8 AIdx = SeinDiagCardinalA[DirIdx - 4];
				const uint8 BIdx = SeinDiagCardinalB[DirIdx - 4];
				if ((Conn & (1 << AIdx)) == 0 || (Conn & (1 << BIdx)) == 0) return false;
			}
			return true;
		});

	return Walk == ESeinGridWalk::ReachedEnd;
}

bool USeinNavigationAStar::NavRaycast(const FFixedVector& From, const FFixedVector& To, FFixedVector& OutHitPoint) const
{
	OutHitPoint = To;
	if (!HasRuntimeData()) return false; // no bake → treat as clear

	int32 X0, Y0, X1, Y1;
	if (!WorldToGrid(From, X0, Y0) || !WorldToGrid(To, X1, Y1))
	{
		return false; // an endpoint is off-grid — can't trace; report clear-to-To
	}

	// Bresenham supercover with the SAME static passability + connectivity gates A* uses
	// (no dynamic-blocker overlay — this is a standalone query, not inside a FindPath). On
	// the first blocked cell / non-traversable step, report that cell's center and return true.
	// Walks via the shared WalkGridLine primitive; the predicates carry the gates and stash
	// the hit point on abort. ReachedEnd ⇒ clear line (false).
	const ESeinGridWalk Walk = WalkGridLine(X0, Y0, X1, Y1,
		[&](int32 X, int32 Y) -> bool
		{
			if (!IsCellPassable(X, Y))
			{
				OutHitPoint = GridToWorld(X, Y);
				return false; // blocked cell — stop, hit recorded
			}
			return true;
		},
		[&](int32 X, int32 Y, int32 NextX, int32 NextY, int32 DirIdx) -> bool
		{
			// Connectivity gate on the step (mirrors HasLineOfSight): a non-connected edge,
			// or a diagonal squeeze, means the line crosses a slope/step/wall boundary.
			if (DirIdx < 0) return true;
			const uint8 Conn = CellConnections[CellIndex(X, Y)];
			if ((Conn & (1 << DirIdx)) == 0)
			{
				OutHitPoint = GridToWorld(NextX, NextY);
				return false;
			}
			if (DirIdx >= 4)
			{
				const uint8 AIdx = SeinDiagCardinalA[DirIdx - 4];
				const uint8 BIdx = SeinDiagCardinalB[DirIdx - 4];
				if ((Conn & (1 << AIdx)) == 0 || (Conn & (1 << BIdx)) == 0)
				{
					OutHitPoint = GridToWorld(NextX, NextY);
					return false;
				}
			}
			return true;
		});

	// AbortedAtCell / AbortedAtStep ⇒ hit (OutHitPoint set in the predicate);
	// ReachedEnd ⇒ reached the end with no block.
	return Walk != ESeinGridWalk::ReachedEnd;
}

void USeinNavigationAStar::BuildSmoothedPath(const TArray<FIntPoint>& CellPath, FSeinPath& OutPath, FAStarScratch& Scratch, int32 RequiredClearance) const
{
	OutPath.Clear();
	if (CellPath.Num() == 0) return;

#if !UE_BUILD_SHIPPING
	// Capture the RAW A* cell chain (cell centers) BEFORE the string-pull smoother below collapses it
	// into turn-point waypoints — the nav path debug viz draws these 1:1 (the exact cells A* traversed).
	OutPath.DebugCellPath.Reset(CellPath.Num());
	for (const FIntPoint& Cell : CellPath)
	{
		OutPath.DebugCellPath.Add(GridToWorld(Cell.X, Cell.Y));
	}
#endif

	// String-pull from cellPath[0] onward: advance J as far as LoS(anchor, J+1) holds,
	// commit cellPath[J] as a turn point, anchor = J, repeat. We deliberately DO NOT
	// emit cellPath[0] — the unit already occupies that cell, and emitting its center
	// as the first waypoint creates a visible "hook" where the unit snaps to the cell
	// center before heading to the real destination.
	//
	// RequiredClearance forwarded to HasLineOfSight so segments emitted by the
	// smoother are guaranteed to clear the agent's footprint. In tight corridors
	// (where every cell-pair LoS fails the clearance gate) the smoother degrades
	// gracefully to a per-cell polyline — the resulting path zigzags more but
	// still traverses the corridor.
	OutPath.Waypoints.Reserve(CellPath.Num());

	int32 I = 0;
	while (I < CellPath.Num() - 1)
	{
		int32 J = I + 1;
		while (J + 1 < CellPath.Num() &&
			HasLineOfSight(CellPath[I].X, CellPath[I].Y, CellPath[J + 1].X, CellPath[J + 1].Y, Scratch, RequiredClearance))
		{
			++J;
		}
		OutPath.Waypoints.Add(GridToWorld(CellPath[J].X, CellPath[J].Y));
		I = J;
	}

	// Same-cell path (CellPath.Num() == 1): loop above emits nothing, but we still
	// need one waypoint so FindPath can overwrite it with the exact destination.
	if (OutPath.Waypoints.Num() == 0)
	{
		OutPath.Waypoints.Add(GridToWorld(CellPath.Last().X, CellPath.Last().Y));
	}

	OutPath.bIsValid = true;
}

// ============================================================================
// Path query API
//
//   FindCellPath — 2D grid A* + LoS string-pull smoothing + wall-push. Produces
//                  a straight-segment polyline. The real planner; used directly
//                  by all shipped movement modes and as a building block by any
//                  vehicle-aware nav subclass.
//
//   FindPath     — Public entry point. Calls FindCellPath then
//                  PushWaypointsAwayFromWalls. Every emitted segment is Straight.
// ============================================================================

int32 USeinNavigationAStar::ComputeRequiredClearance(FFixedPoint FootprintRadius, int32 WallPaddingCells) const
{
	// Single source of truth for the configuration-space clearance threshold,
	// shared by the A* C-space gate (FindCellPathInternal) and the wall-push pass
	// (PushWaypointsAwayFromWalls):
	//
	//   Required = ceil(FootprintRadius / CellSize + 0.5) + WallPaddingCells
	//
	// The `+ 0.5` accounts for the half-cell distance between a unit's CENTER
	// (positioned at cell-center) and the nearest wall EDGE (cell border).
	// Without it the formula picks one cell less than the body actually needs —
	// e.g. for FootprintRadius=30 and CellSize=50, `ceil(0.6)=1` says "unit
	// center in cells with WD≥1," but at WD=1 the wall edge is only 25 cm from
	// the unit center while the body extends 30 cm (clips by 5 cm). With +0.5,
	// `ceil(1.1)=2` → cells with WD≥2 → wall edge 75 cm away → 30 cm body fits.
	// Geometrically: (Required − 0.5) × CellSize ≥ FootprintRadius.
	//
	// Capped at the WallDistance BFS saturation cap — beyond it the field can't
	// distinguish, so the cap becomes the effective ceiling.
	if (CellSize <= FFixedPoint::Zero) return 0;

	int32 FootprintCells = 0;
	if (FootprintRadius > FFixedPoint::Zero)
	{
		// Ratio = R/CS + 0.5, then deterministic fixed-point ceil (was a
		// non-deterministic ToFloat()+0.999f).
		const FFixedPoint Ratio = FootprintRadius / CellSize + FFixedPoint::Half;
		FootprintCells = Ratio.CeilToInt();
	}

	int32 Required = FootprintCells
		+ FMath::Max(WallPaddingCells, 0);
	if (Required > static_cast<int32>(WallDistanceCap))
	{
		Required = WallDistanceCap;
	}
	return Required;
}

bool USeinNavigationAStar::FindCellPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const
{
	// Thin wrapper over the reentrant body — the serial path uses the single
	// persistent MainScratch, whose buffers survive across calls exactly as the
	// old mutable members did (byte-identical serial behavior). The Scratch-
	// taking helper is what a future parallel batch calls with per-worker scratch.
	return FindCellPathInternal(Request, OutPath, MainScratch);
}

FFixedVector USeinNavigationAStar::QueryDirection(const FSeinDirectionQuery& Query) const
{
	// Obstacle-aware "which way": route From→Goal and return the heading to the first
	// waypoint past the start. The pull-equivalent of FindPath for this route-shaped nav.
	// (A field-shaped nav would override to sample a precomputed field instead — far
	// cheaper to poll per tick.) Falls back to the base straight-line on no data / no path.
	if (!HasRuntimeData()) return Super::QueryDirection(Query);

	FSeinPathRequest Req;
	Req.Start                = Query.From;
	Req.End                  = Query.Goal;
	Req.Requester            = Query.Requester;
	Req.BlockedTerrainTags   = Query.BlockedTerrainTags;
	Req.AgentNavLayerMask    = Query.AgentNavLayerMask;
	Req.AgentFootprintRadius = Query.AgentFootprintRadius;
	Req.AgentWallPaddingCells = Query.AgentWallPaddingCells;
	Req.GroupId              = Query.GroupId;

	FSeinPath Path;
	if (FindCellPath(Req, Path) && Path.Waypoints.Num() >= 2)
	{
		FFixedVector Delta = Path.Waypoints[1] - Query.From;
		Delta.Z = FFixedPoint::Zero;
		if (Delta.SizeSquared() > FFixedPoint::Epsilon)
		{
			return FFixedVector::GetSafeNormal(Delta);
		}
	}
	return Super::QueryDirection(Query);
}

bool USeinNavigationAStar::FindCellPathInternal(const FSeinPathRequest& Request, FSeinPath& OutPath, FAStarScratch& Scratch) const
{
	OutPath.Clear();
	if (!HasRuntimeData()) return false;

	FSeinNavAgentProfile RequestAgent;
	RequestAgent.Requester = Request.Requester;
	RequestAgent.BlockedTerrainTags =
		Request.BlockedTerrainTags;
	RequestAgent.AgentNavLayerMask =
		Request.AgentNavLayerMask;
	RequestAgent.AgentFootprintRadius =
		Request.AgentFootprintRadius;
	RequestAgent.AgentWallPaddingCells =
		Request.AgentWallPaddingCells;

	// Diagnostic: dump per-request clearance inputs. Lets us confirm at log
	// time that the caller-provided WallPaddingCells + FootprintRadius reach
	// the planner intact (catches authoring-vs-storage drift where a BP
	// NavComp value didn't propagate to FSeinGenericComponentStorage at
	// spawn, or a stale NavComp pointer dropped the field).
	UE_LOG(LogSeinNavigationAStar, Verbose,
		TEXT("FindCellPath: request FootprintRadius=%s WallPaddingCells=%d "
		     "NavLayerMask=0x%08X Start=(%.1f,%.1f) End=(%.1f,%.1f)"),
		*Request.AgentFootprintRadius.ToString(),
		Request.AgentWallPaddingCells,
		Request.AgentNavLayerMask,
		Request.Start.X.ToFloat(), Request.Start.Y.ToFloat(),
		Request.End.X.ToFloat(),   Request.End.Y.ToFloat());

	// OVERLAY REUSE (perf; bit-identical). A fresh per-worker scratch rebuilds the dynamic-blocker
	// overlay for its first request each async batch, then RE-STAMPS every blocker per subsequent
	// request even though the overlay is identical — the cover-wall batch cost. Skip the rebuild when
	// this scratch already holds the SAME overlay: same agent mask, no intervening exact blocker/grid
	// mutation, and the held overlay carried no relevant self-exclusion. A path requester that OWNS a blocker
	// (rare — units don't block nav) can't share the no-exclusion overlay and forces a rebuild.
	// Bit-identical because exclusion only removes the requester's own cells, which a non-blocker
	// requester has none of, so the shared overlay equals the per-request overlay.
	bool bRequesterOwnsBlocker = false;
	for (const FSeinDynamicBlocker& B : DynamicBlockers)
	{
		if (B.Owner == Request.Requester) { bRequesterOwnsBlocker = true; break; }
	}
	const bool bReuseOverlay = !bRequesterOwnsBlocker
		&& Scratch.bOverlayReuseValid
		&& Scratch.OverlayReuseMask        == Request.AgentNavLayerMask
		&& Scratch.DynamicBlocked.Num()    == Width * Height;

	if (!bReuseOverlay)
	{
		// Rebuild the dynamic-blocker overlay for this request. Excludes the requesting entity's own
		// blocker so a unit can path out of its own footprint, AND filters by agent layer mask so
		// terrain-class blockers (water blocks Default, ignore amphibious) don't apply universally.
		// Done first so every passability check below (source projection, A*, smoothing) sees the same
		// overlay.
		BuildDynamicBlockedOverlay(Request.Requester, Request.AgentNavLayerMask, Scratch);
		// Reusable ONLY when this build carried no relevant exclusion (requester owns no blocker); a
		// self-blocker build is request-specific and forces the next request to rebuild.
		Scratch.bOverlayReuseValid      = !bRequesterOwnsBlocker;
		Scratch.OverlayReuseMask        = Request.AgentNavLayerMask;
	}

	// Per-agent terrain filter: bar cells whose terrain type's tag is listed in
	// Request.BlockedTerrainTags (e.g. an amphibious-only unit that blocks "Water").
	// Resolved ONCE here into a 256-entry lookup that IsCellPassableForPath reads on the
	// hot path — so A* topology AND the LoS smoother both honor it. Skipped entirely when
	// the agent blocks no terrain or the grid carries no per-cell type, so the common case
	// pays nothing. (HasTag is hierarchical: blocking a parent tag bars its child types.)
	Scratch.bRequestHasBlockedTypes = false;
	if (!Request.BlockedTerrainTags.IsEmpty() && CellTerrainType.Num() == Width * Height)
	{
		Scratch.bRequestHasBlockedTypes =
			BuildBlockedTerrainTypeLookup(
				Request.BlockedTerrainTags,
				Scratch.RequestBlockedType);
	}

	// Invalidate the per-request dynamic-WD cache by bumping the gen — ALWAYS, even when the overlay
	// BYTES were reused above. The cache is NOT overlay-pure: GetEffectiveWD caps its ring scan at the
	// per-request footprint clearance (RequiredClearance / MaxR), so a small-footprint request's cached
	// "nothing within MaxR" is WRONG for a larger-footprint request sharing the same overlay. Only the
	// overlay stamp is reusable; these MaxR-capped distances must be re-derived per request. On
	// wraparound through 0, do one full reset. Allocate / resize the cache buffers if the grid dims
	// changed since the last call.
	{
		const int32 NCells = Width * Height;
		if (Scratch.DynamicWDCache.Num()    != NCells) Scratch.DynamicWDCache.SetNumUninitialized(NCells);
		if (Scratch.DynamicWDCacheGen.Num() != NCells) Scratch.DynamicWDCacheGen.Init(0, NCells);

		if (++Scratch.CurrentDynamicWDGen == 0)
		{
			// uint16 wraparound — clear the gen array and restart at 1.
			// (gen 0 is the "never touched" sentinel; we never want a live
			// gen to collide with it.)
			FMemory::Memzero(Scratch.DynamicWDCacheGen.GetData(), NCells * sizeof(uint16));
			Scratch.CurrentDynamicWDGen = 1;
		}
	}

	int32 SX, SY;
	if (!WorldToGrid(Request.Start, SX, SY))
	{
		UE_LOG(LogSeinNavigationAStar, Verbose, TEXT("FindPath: start out of bounds"));
		return false;
	}
	int32 EX, EY;
	if (!WorldToGrid(Request.End, EX, EY))
	{
		UE_LOG(LogSeinNavigationAStar, Verbose, TEXT("FindPath: end out of bounds"));
		return false;
	}

	// Source projection: if the unit's physical cell is blocked (e.g. stuck on
	// a pruned island from a stale bake, or boxed in by other units' blocker
	// stamps), ring-scan for a nearby walkable cell. Uses static-only
	// passability — ProjectPointToNav doesn't see the dynamic overlay; if it
	// did, a tank surrounded by other blockers couldn't even start a path.
	if (!IsCellPassable(SX, SY))
	{
		FFixedVector Snapped;
		if (!ProjectPointToNav(Request.Start, Snapped))
		{
			UE_LOG(LogSeinNavigationAStar, Warning,
				TEXT("FindCellPath: start cell (%d,%d) blocked AND ProjectPointToNav "
				     "failed (no walkable cell in ring-scan radius) — path fails "
				     "silently at this layer. Caller will get NotFound. "
				     "Request.Start=(%.1f,%.1f)"),
				SX, SY, Request.Start.X.ToFloat(), Request.Start.Y.ToFloat());
			return false;
		}
		UE_LOG(LogSeinNavigationAStar, Verbose,
			TEXT("FindCellPath: start cell (%d,%d) blocked, projected to (%.1f,%.1f)"),
			SX, SY, Snapped.X.ToFloat(), Snapped.Y.ToFloat());
		WorldToGrid(Snapped, SX, SY);
	}

	// A* search tunables (heuristic weight + iteration cap) live on this nav class's CDO — edit them
	// via a Blueprint subclass slotted in Project Settings > NavigationClass. Read as members.
	const int32 HeuristicWeight = AStarHeuristicWeightPercent;
	const int32 DefaultMaxIters = AStarMaxIterations;
	// Per-request override (0 = project default). Lets a caller bound an expensive /
	// long-range pathfind; A* returns a best-effort partial if the cap is hit.
	const int32 MaxIters = (Request.AgentMaxSearchNodes > 0) ? Request.AgentMaxSearchNodes : DefaultMaxIters;

	// Per-request clearance threshold for configuration-space A* — see
	// ComputeRequiredClearance for the formula + geometric derivation (single
	// source of truth, shared with PushWaypointsAwayFromWalls). This value drives
	// BOTH A* expansion (only cells with enough WallDistance are reachable, with a
	// strict-improvement escape from too-tight starts) AND HasLineOfSight clearance
	// (smoothed segments stay clear), so A* topology itself is footprint-correct —
	// the unit's body fits at every cell on the path, not just at the waypoints.
	const int32 RequiredClearance = ComputeRequiredClearance(Request.AgentFootprintRadius, Request.AgentWallPaddingCells);

	// A* explores the full reachable region from Start (within configuration
	// space) and tracks the lowest-H cell visited. Goal reachable through
	// C-space → exact path. Goal NOT in C-space (click landed in a tight spot
	// the unit can't physically fit) OR goal unreachable OR iteration cap hit
	// → partial path to the best-H cell visited (bPartial=true). The "best-H
	// reachable" cell with clearance enforcement is naturally the closest
	// VALID stop position to the click — units stop "near" walls instead of
	// inside them. Single A* call.
	//
	// Designers who want "reject invalid destination instead of falling back"
	// flip the ability's bRequiresPathableTarget flag — that routes through
	// IsReachable at command-validation time, before FindPath is ever called.
	// So FindPath itself stays permissive: MoveToAction's contract is to get
	// somewhere sensible along the way to the click.
	bool bPartial = false;
	AStarSearch(FIntPoint(SX, SY), FIntPoint(EX, EY), bPartial,
		HeuristicWeight, MaxIters, RequiredClearance, Scratch);
	// AStarSearch fills the pooled Scratch.CellPath in place (no by-value return).
	// Alias it for the smoothing + diagnostics below; the buffer's lifetime is the
	// scratch's, so the const-ref stays valid for the rest of this call.
	const TArray<FIntPoint>& CellPath = Scratch.CellPath;

	if (CellPath.Num() == 0)
	{
		UE_LOG(LogSeinNavigationAStar, Warning,
			TEXT("FindCellPath: AStarSearch returned empty for start cell (%d,%d) "
			     "→ end cell (%d,%d). Causes: start cell not passable in dynamic "
			     "overlay (own blocker not excluded? other unit's blocker covers "
			     "start?), search hit iteration cap, start sealed in a low-clearance "
			     "pocket (RequiredClearance=%d, WallDistance[start]=%d), or start "
			     "has no walkable 8-neighbors. Request.Start=(%.1f,%.1f) End=(%.1f,%.1f)"),
			SX, SY, EX, EY,
			RequiredClearance,
			(WallDistance.IsValidIndex(CellIndex(SX, SY)) ? static_cast<int32>(WallDistance[CellIndex(SX, SY)]) : -1),
			Request.Start.X.ToFloat(), Request.Start.Y.ToFloat(),
			Request.End.X.ToFloat(),   Request.End.Y.ToFloat());
		return false;
	}

	BuildSmoothedPath(CellPath, OutPath, Scratch, RequiredClearance);
	// Apply partial flag AFTER smoothing — BuildSmoothedPath's OutPath.Clear()
	// resets it, so setting it earlier gets clobbered.
	OutPath.bIsPartial = bPartial;

#if !UE_BUILD_SHIPPING
	// Per-waypoint Verbose clearance dump, extracted + gated (ReportCellPathClearance)
	// so this host function reads at its real logic size and a release build with the
	// channel off does no grid work. Pure observation. Runs on the smoothed OutPath
	// BEFORE the final-waypoint snap below, matching the original placement.
	ReportCellPathClearance(CellPath, OutPath, RequiredClearance, bPartial);
#endif

	// Replace last waypoint with the requested end position (within the cell)
	// so unit actually arrives at the ordered point, not the cell center.
	// Partial paths normally keep the cell-center final waypoint — the destination
	// is unreachable, so snapping to it would put the unit inside a wall.
	if (OutPath.Waypoints.Num() > 0)
	{
		if (!OutPath.bIsPartial)
		{
			OutPath.Waypoints.Last() = Request.End;
		}
		else if (Request.bAuthoritativeDestination)
		{
			// AUTHORITATIVE destination (cover slot): the goal cell is bake-blocked,
			// but the slot is a valid standing spot that overrules the coarse bake
			// (root CLAUDE.md #6). If the best-H cell we reached is ADJACENT to the
			// goal cell (the slot sits at the edge of a blocked cell with a reachable
			// neighbour), honor the exact destination and mark the path complete so
			// the mover walks the last step onto the slot. If best-H is far (goal
			// genuinely sealed deep in a wall), keep the cell-center stop so we never
			// path the unit through the wall.
			int32 GoalX, GoalY, LastX, LastY;
			const bool bGoalInGrid =
				WorldToGrid(Request.End, GoalX, GoalY);
			if (bGoalInGrid
				&& IsAuthoritativeFootprintSafeForAgent(
					Request.End,
					RequestAgent)
				&& WorldToGrid(OutPath.Waypoints.Last(), LastX, LastY)
				&& FMath::Max(FMath::Abs(GoalX - LastX), FMath::Abs(GoalY - LastY)) <= 1)
			{
				OutPath.Waypoints.Last() = Request.End;
				OutPath.bIsPartial = false;
			}
		}
	}

	// Derive the typed segment list from the cell polyline — one Straight
	// segment per consecutive waypoint pair. Done here so FindCellPath's
	// contract holds: every successful return carries a segment-derived path,
	// so direct callers (infantry / basic / basic-unit PlanPath) can read
	// Segments without an extra DeriveSegmentsFromWaypoints call. A FindPath
	// wrapper that further mutates the polyline (e.g. wall-push) re-derives
	// afterward.
	OutPath.DeriveSegmentsFromWaypoints();

	return OutPath.bIsValid;
}

bool USeinNavigationAStar::FindPath(const FSeinPathRequest& Request, FSeinPath& OutPath) const
{
	// Serial entry: the whole pipeline runs through the persistent MainScratch
	// (its buffers survive across calls exactly as the pre-refactor mutable members).
	return FindPathInternal(Request, OutPath, MainScratch);
}

bool USeinNavigationAStar::FindPathInternal(const FSeinPathRequest& Request, FSeinPath& OutPath, FAStarScratch& Scratch) const
{
	FSeinNavAgentProfile RequestAgent;
	RequestAgent.Requester = Request.Requester;
	RequestAgent.BlockedTerrainTags =
		Request.BlockedTerrainTags;
	RequestAgent.AgentNavLayerMask =
		Request.AgentNavLayerMask;
	RequestAgent.AgentFootprintRadius =
		Request.AgentFootprintRadius;
	RequestAgent.AgentWallPaddingCells =
		Request.AgentWallPaddingCells;

	// 2D configuration-space cell A* + LoS smoothing. A* runs in the unit's
	// configuration space (cells where `WallDistance >= Required` for this
	// footprint), so emitted waypoints and the segments between them are
	// already footprint-clear by construction — no post-process push needed
	// for interior waypoints.
	//
	// `PushWaypointsAwayFromWalls` still runs as a safety net: it's a no-op
	// for cells already in C-space, but nudges the final waypoint inward
	// when the user clicked at a position inside a clearance-valid cell that
	// happens to be near its boundary (the literal click position, before
	// any cell-center snap, can still be close to a wall edge). Cheap.
	OutPath.Clear();
	// Route the whole pipeline through the serial MainScratch: FindCellPathInternal
	// populates the dynamic overlay / blocked-types / dyn-WD cache in it, and the
	// wall-push + post-validation diagnostics below read that SAME live scratch
	// state (the overlay must still reflect this request's blockers). Byte-identical
	// to the pre-refactor flow where all three stages shared the mutable members.
	if (!FindCellPathInternal(Request, OutPath, Scratch)) return false;

	if (Request.AgentFootprintRadius > FFixedPoint::Zero || Request.AgentWallPaddingCells > 0)
	{
		PushWaypointsAwayFromWalls(OutPath, Request.AgentFootprintRadius, Request.AgentWallPaddingCells, Scratch);

		// The wall-push must NOT relocate an authoritative destination (cover slot)
		// off its slot — the slot belongs AT the wall. Restore the exact End that
		// FindCellPath committed as the final waypoint (root CLAUDE.md #6).
		if (Request.bAuthoritativeDestination
			&& !OutPath.bIsPartial
			&& IsAuthoritativeFootprintSafeForAgent(
				Request.End,
				RequestAgent)
			&& OutPath.Waypoints.Num() > 0)
		{
			OutPath.Waypoints.Last() = Request.End;
		}
		OutPath.DeriveSegmentsFromWaypoints();
	}

#if !UE_BUILD_SHIPPING
	// Path-validation diagnostic moved into ReportUnreachableSegments so this host
	// function reads at its real logic size. The reporter gates on Warning activity
	// BEFORE doing any segment grid work (per-segment HasLineOfSight + Bresenham
	// replay), so a release build with the channel off pays nothing.
	ReportUnreachableSegments(Request, OutPath, Scratch);
#endif

	return OutPath.bIsValid;
}

void USeinNavigationAStar::RunPathBatch(const TArray<FSeinPathRequest>& Requests, TArray<FSeinPath>& OutResults) const
{
	const int32 N = Requests.Num();
	OutResults.SetNum(N);
	if (N == 0) return;

	// Serial when parallelism is off or the batch is a single search (dispatch
	// overhead beats the win). Serial runs through the persistent MainScratch, so
	// the result is byte-identical to the inline FindPath path.
	if (N < 2 || !SeinSimParallelEnabled())
	{
		for (int32 i = 0; i < N; ++i)
		{
			FindPathInternal(Requests[i], OutResults[i], MainScratch);
		}
		return;
	}

	// Parallel: ParallelForWithTaskContext gives each worker its OWN FAStarScratch
	// (never shared concurrently), so the searches run race-free. Each FindPathInternal
	// is a pure function of (request, immutable grid) — the scratch is only workspace,
	// reset per search by the generation pattern — so every OutResults[i] is identical
	// to the serial path regardless of thread count or scheduling. This is the
	// standard per-thread search-workspace model; determinism is by construction.
#if !UE_BUILD_SHIPPING
	SeinSetInParallelSection(true);
#endif
	TArray<FAStarScratch> Contexts;
	ParallelForWithTaskContext(Contexts, N, [this, &Requests, &OutResults](FAStarScratch& Scratch, int32 Index)
	{
		FindPathInternal(Requests[Index], OutResults[Index], Scratch);
	});
#if !UE_BUILD_SHIPPING
	SeinSetInParallelSection(false);
#endif
}

void USeinNavigationAStar::PushWaypointsAwayFromWalls(
	FSeinPath& Path,
	FFixedPoint AgentFootprintRadius,
	int32 WallPaddingCells,
	FAStarScratch& Scratch) const
{
	if (Path.Waypoints.Num() == 0) return;
	if (WallDistance.Num() != Width * Height) return; // asset not loaded
	if (CellSize <= FFixedPoint::Zero) return;

	// Required clearance in whole cells — single source of truth shared with
	// FindCellPathInternal's A* C-space gate (ComputeRequiredClearance):
	//   Required = ceil(R / CS + 0.5) + WallPadding, capped at WallDistanceCap.
	// The `+0.5` accounts for the half-cell distance from a cell-center waypoint
	// to the wall edge; the cap matches the WallDistance BFS saturation so the
	// gradient walk's "cap == cap" stall lands on the same answer. See the
	// comment in FindCellPath for the full geometric derivation.
	const int32 Required = ComputeRequiredClearance(AgentFootprintRadius, WallPaddingCells);
	if (Required <= 0) return; // infantry-class agent with no padding — nothing to push

	// Per-waypoint step budget. The gradient walk takes at most ~Required
	// strictly-improving steps before WallDistance reaches Required, plus a
	// little slack for diagonal walks where the value increment per step is
	// less reliable. 2× Required is generous; the inner "no neighbor strictly
	// improves" check terminates earlier in practice.
	const int32 MaxStepsPerWaypoint = Required * 2;

	// Shared 8-neighbor offset tables (file-local SeinNeighbor* above).
	const int32 (&NeighborDX)[8] = SeinNeighborDX;
	const int32 (&NeighborDY)[8] = SeinNeighborDY;

	// The destination waypoint is pushed alongside the interior ones. Orders
	// issued adjacent to walls produce stop positions in open space rather
	// than at the wall — at the cost of the unit stopping a few cells short
	// of the literal click. Abilities that want pinpoint stop precision
	// (Garrison, Attack, building-interact) should set WallPaddingCells=0
	// on their move-related data so the push is a no-op for them.
	const int32 EndIndex = Path.Waypoints.Num();

	int32 PushedCount = 0;

	for (int32 W = 0; W < EndIndex; ++W)
	{
		int32 X = 0, Y = 0;
		if (!WorldToGrid(Path.Waypoints[W], X, Y)) continue;
		const int32 OrigIdx = CellIndex(X, Y);
		if (!WallDistance.IsValidIndex(OrigIdx)) continue;

		int32 CurX = X;
		int32 CurY = Y;
		// Effective WD = min(static, dyn-blocker Chebyshev distance). Same
		// source-of-truth as A* / smoother clearance gates — Push acts on
		// dynamic blockers the same way it acts on static walls.
		int32 CurDist = GetEffectiveWD(CurX, CurY, Required, Scratch);

		// Already clear — no push needed for this waypoint.
		if (CurDist >= Required) continue;

		for (int32 Step = 0; Step < MaxStepsPerWaypoint && CurDist < Required; ++Step)
		{
			// Per-step connection-bit gate — must match the gate A* and the
			// smoother use, otherwise Push can walk into cells the agent can't
			// physically reach (typically wall tops via step-up restrictions).
			// Look up the CURRENT cell's mask each step since CurX/CurY changes
			// as the walk progresses; bit n is whether the current cell connects
			// to the neighbor in direction n.
			const int32 CurCellIdx = CellIndex(CurX, CurY);
			const uint8 CurConn = CellConnections.IsValidIndex(CurCellIdx)
				? CellConnections[CurCellIdx] : 0;

			// Find the 8-neighbor with strictly greater effective WD. Strict-
			// greater (rather than ≥) prevents oscillation between two cells
			// with the same WD — once we're on a plateau, we stop.
			int32 BestN = -1;
			int32 BestNDist = CurDist;
			for (int32 n = 0; n < 8; ++n)
			{
				if ((CurConn & (1 << n)) == 0) continue; // bake's step/slope gate
				const int32 NX = CurX + NeighborDX[n];
				const int32 NY = CurY + NeighborDY[n];
				if (!IsValidCoord(NX, NY)) continue;
				if (!IsCellPassableForPath(NX, NY, Scratch)) continue;
				const int32 ND = GetEffectiveWD(NX, NY, Required, Scratch);
				if (ND > BestNDist)
				{
					BestN = n;
					BestNDist = ND;
				}
			}

			if (BestN < 0) break; // gradient saturated — local maximum reached
			CurX += NeighborDX[BestN];
			CurY += NeighborDY[BestN];
			CurDist = BestNDist;
		}

		// Only commit the new position if we actually moved AND the new cell
		// is passable AND moving the waypoint there doesn't break adjacency
		// with the previous / next waypoint.
		//
		// Adjacency check: Push moves each waypoint independently along its
		// own WD gradient. The gradient might lead a waypoint to a position
		// where the SEGMENT to its neighbor crosses a wall (e.g., near a
		// pathable wall the gradient points to the opposite side of the wall
		// because that side has higher WD locally). Without re-validating
		// the segments, the path can flash a line straight through the wall
		// for a tick or two until the next repath corrects it. Validate via
		// HasLineOfSight(clearance=0) — passability + connection bits, no
		// clearance gate (the push itself satisfies clearance per cell, and
		// we want this check to focus on physical reachability of the line).
		// If either adjacent segment fails the check, REVERT the push (keep
		// original position). Path stays slightly tighter to the wall for
		// this waypoint, but doesn't cross.
		if ((CurX != X || CurY != Y) && IsCellPassableForPath(CurX, CurY, Scratch))
		{
			bool bPrevSegOk = true;
			bool bNextSegOk = true;

			if (W > 0)
			{
				int32 PrevX, PrevY;
				if (WorldToGrid(Path.Waypoints[W - 1], PrevX, PrevY))
				{
					bPrevSegOk = HasLineOfSight(PrevX, PrevY, CurX, CurY, Scratch, 0);
				}
			}

			if (bPrevSegOk && (W + 1) < EndIndex)
			{
				int32 NextX, NextY;
				if (WorldToGrid(Path.Waypoints[W + 1], NextX, NextY))
				{
					bNextSegOk = HasLineOfSight(CurX, CurY, NextX, NextY, Scratch, 0);
				}
			}

			if (bPrevSegOk && bNextSegOk)
			{
				Path.Waypoints[W] = GridToWorld(CurX, CurY);
				++PushedCount;
			}
			// else: silently revert — Path.Waypoints[W] keeps its original
			// (pre-push) world position. No log line by default; the path
			// is still functionally valid, just less padded for this point.
		}
	}

	if (PushedCount > 0)
	{
		UE_LOG(LogSeinNavigationAStar, Verbose,
			TEXT("PushWaypointsAwayFromWalls: Footprint=%.1f + Padding=%d → Required=%d cells, pushed %d/%d waypoints"),
			AgentFootprintRadius.ToFloat(),
			WallPaddingCells,
			Required,
			PushedCount,
			EndIndex);
	}
}

// ============================================================================
// Debug geometry collectors. Bodies compiled out of shipping via
// UE_ENABLE_DEBUG_DRAWING; declarations stay in the header so the vtable /
// override signature is ABI-stable across build configs.
// ============================================================================

void USeinNavigationAStar::CollectDebugCellQuads(TArray<FVector>& OutCenters, TArray<FColor>& OutColors, float& OutHalfExtent) const
{
#if UE_ENABLE_DEBUG_DRAWING
	OutCenters.Reset();
	OutColors.Reset();
	OutHalfExtent = 0.0f;
	if (!bDrawCellsInDebug || !HasRuntimeData()) return;

	const float CS = CellSize.ToFloat();
	OutHalfExtent = CS * 0.5f * 0.9f; // small inset so neighboring quads don't z-fight

	// One emitted quad per actual nav cell — viz reads the configured
	// `Settings->CellSize` (or per-volume override) authoritatively.
	// At very large grids (1km²+) this can be expensive on proxy rebuild,
	// but the broadcast is exact-change-gated in `SetDynamicBlockers` so rebuilds
	// only fire on actual mutations.
	const int32 N = Width * Height;
	OutCenters.Reserve(N);
	OutColors.Reserve(N);

	// Per-terrain-type debug tint (each type's authored DebugColor).
	const USeinARTSCoreSettings* TerrainSettings = GetDefault<USeinARTSCoreSettings>();

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 I = CellIndex(X, Y);
			const uint8 C = CellCost[I];
			const bool bWalkable = C > 0 && C < 255;
			FColor CellColor = bWalkable ? FColor(0, 200, 0, 160) : FColor(200, 0, 0, 200);
			// Tint walkable cells by their terrain type's DebugColor (non-Default only), so
			// authored terrain (road / mud / forest) is visible in the nav debug overlay.
			if (bWalkable && TerrainSettings && CellTerrainType.IsValidIndex(I))
			{
				const int32 Type = CellTerrainType[I];
				if (Type > 0 && Type <= TerrainSettings->TerrainTypes.Num())
				{
					CellColor = TerrainSettings->TerrainTypes[Type - 1].DebugColor.ToFColor(true);
					CellColor.A = 160;
				}
			}
			OutColors.Add(CellColor);
			OutCenters.Emplace(
				Origin.X.ToFloat() + (X + 0.5f) * CS,
				Origin.Y.ToFloat() + (Y + 0.5f) * CS,
				CellHeight[I].ToFloat() + 2.0f);
		}
	}
#endif
}

bool USeinNavigationAStar::QueryEscapeTarget(
	const FSeinEscapeQuery& Query,
	FFixedVector& OutTarget) const
{
	if (!HasRuntimeData()) return false;

	int32 AgentX, AgentY;
	if (!WorldToGrid(Query.From, AgentX, AgentY)) return false;
	if (!IsValidCoord(AgentX, AgentY)) return false;

	// Walk the WallDistance gradient outward up to `MaxEscapeSteps` cells,
	// picking the highest-WD passable + connected neighbor at each step.
	// The single-step variant (just the immediately-best neighbor) placed
	// the target ~1 cell from the chassis, which is inside typical
	// AcceptanceRadius (50-150cm) — the chassis "arrived" instantly and the
	// move completed before any nudging happened. Walking N cells gives the
	// chassis a target far enough to actually drive toward.
	//
	// The visited array prevents the walk from oscillating between two
	// equal-WD cells (e.g., agent ↔ neighbor when both happen to share the
	// local maximum). Bounded by MaxEscapeSteps so the walk always
	// terminates even on degenerate maps.
	const int32 MaxEscapeSteps = 6;
	static const int32 NeighborDX[8] = { 1, -1,  0,  0,  1,  1, -1, -1 };
	static const int32 NeighborDY[8] = { 0,  0,  1, -1,  1, -1,  1, -1 };

	TArray<int32, TInlineAllocator<MaxEscapeSteps + 1>> Visited;
	Visited.Reserve(MaxEscapeSteps + 1);
	// The stepped chain (excludes the start cell) — kept for the contract
	// validation walk-back below.
	TArray<TPair<int32, int32>, TInlineAllocator<MaxEscapeSteps>> Chain;

	int32 CurX = AgentX;
	int32 CurY = AgentY;
	Visited.Add(CellIndex(CurX, CurY));

	for (int32 step = 0; step < MaxEscapeSteps; ++step)
	{
		const int32 CurIdx = CellIndex(CurX, CurY);
		const uint8 CurConn = CellConnections.IsValidIndex(CurIdx)
			? CellConnections[CurIdx] : 0;

		int32 BestN = -1;
		int32 BestWD = -1;
		for (int32 n = 0; n < 8; ++n)
		{
			// Connection-bit gate: bake's slope/step rule decided this
			// transition is physically legal. Without this check the escape
			// could pick a cell that's passable (CellCost>0) but unreachable
			// from the current elevation (e.g., a wall top above).
			if ((CurConn & (1 << n)) == 0) continue;

			const int32 NX = CurX + NeighborDX[n];
			const int32 NY = CurY + NeighborDY[n];
			if (!IsValidCoord(NX, NY)) continue;
			if (!IsCellPassable(NX, NY)) continue;

			const int32 NIdx = CellIndex(NX, NY);
			if (Visited.Contains(NIdx)) continue;

			const int32 NWD = WallDistance.IsValidIndex(NIdx)
				? static_cast<int32>(WallDistance[NIdx]) : 0;

			if (NWD > BestWD)
			{
				BestWD = NWD;
				BestN = n;
			}
		}

		if (BestN < 0)
		{
			// Dead end — no passable+connected unvisited neighbor at this
			// step. Whatever chain we walked so far goes to validation;
			// an empty chain is the sealed-pocket false below.
			break;
		}

		CurX += NeighborDX[BestN];
		CurY += NeighborDY[BestN];
		Visited.Add(CellIndex(CurX, CurY));
		Chain.Emplace(CurX, CurY);
	}

	// CONTRACT VALIDATION (see the base docstring): the gradient walk above is
	// STATIC-only by design (a transient dynamic obstacle shouldn't reroute the
	// walk), but the RETURNED target must be one the movement floor — which IS
	// dynamic-aware and footprint-aware, and which drives the escape leg as a
	// STRAIGHT line from Query.From — will actually let the agent walk to and
	// stand on, or a curable hold converts to a Stranded fail. Per candidate
	// (walking BACK from the furthest chain cell):
	//   1. the candidate center is clear (static + dynamic, agent layer mask)
	//      and not on a terrain class the agent blocks;
	//   2. the agent FOOTPRINT fits there — 8 ring samples at the carried
	//      radius, mirroring the floor's own footprint gate;
	//   3. the STRAIGHT segment From→candidate is clear at half-cell samples —
	//      the greedy chain can bend around a wall tip the straight leg would
	//      then cross.
	FSeinNavAgentProfile Agent;
	Agent.Requester = Query.Requester;
	Agent.BlockedTerrainTags = Query.BlockedTerrainTags;
	Agent.AgentNavLayerMask = Query.AgentNavLayerMask;
	Agent.AgentFootprintRadius =
		Query.AgentFootprintRadius;

	auto IsFootprintClearAt = [&](const FFixedVector& Center) -> bool
	{
		if (!IsWorldPositionClearForAgent(Center, Agent)) return false;
		const FFixedPoint R = Query.AgentFootprintRadius;
		if (R <= FFixedPoint::Zero) return true;
		// 4 axis + 4 diagonal ring samples (diagonal offset = R·sin45°),
		// the same coverage shape as the movement floor's cached ring.
		const FFixedPoint D = R * FFixedPoint::FromInt(7071) / FFixedPoint::FromInt(10000);
		const FFixedVector Ring[8] = {
			FFixedVector(Center.X + R, Center.Y,     Center.Z),
			FFixedVector(Center.X - R, Center.Y,     Center.Z),
			FFixedVector(Center.X,     Center.Y + R, Center.Z),
			FFixedVector(Center.X,     Center.Y - R, Center.Z),
			FFixedVector(Center.X + D, Center.Y + D, Center.Z),
			FFixedVector(Center.X + D, Center.Y - D, Center.Z),
			FFixedVector(Center.X - D, Center.Y + D, Center.Z),
			FFixedVector(Center.X - D, Center.Y - D, Center.Z) };
		for (const FFixedVector& S : Ring)
		{
			if (!IsWorldPositionClearForAgent(S, Agent)) return false;
		}
		return true;
	};

	auto IsSegmentClear = [&](const FFixedVector& From, const FFixedVector& To) -> bool
	{
		FFixedVector Delta = To - From;
		Delta.Z = FFixedPoint::Zero;
		const FFixedPoint Len = Delta.Size();
		const FFixedPoint StepLen = CellSize / FFixedPoint::FromInt(2);
		if (Len <= StepLen || StepLen <= FFixedPoint::Zero) return true;
		// Sample every half cell along the segment (endpoints validated by the
		// caller). ≤ ~24 samples for the 6-cell walk — trivial for a rare query.
		const int32 Steps = 1 + (Len / StepLen).ToInt();   // deterministic floor (>>32)
		for (int32 s = 1; s < Steps; ++s)
		{
			const FFixedPoint T = FFixedPoint::FromInt(s) / FFixedPoint::FromInt(Steps);
			const FFixedVector Sample(
				From.X + Delta.X * T, From.Y + Delta.Y * T, From.Z);
			if (!IsWorldPositionClearForAgent(Sample, Agent)) return false;
		}
		return true;
	};

	for (int32 i = Chain.Num() - 1; i >= 0; --i)
	{
		const int32 CX = Chain[i].Key;
		const int32 CY = Chain[i].Value;
		const FFixedVector Candidate = GridToWorld(CX, CY);
		if (!IsFootprintClearAt(Candidate)) continue;
		if (!IsSegmentClear(Query.From, Candidate)) continue;
		OutTarget = Candidate;
		return true;
	}
	return false;
}

void USeinNavigationAStar::CollectDebugBlockerCells(
	TArray<FVector>& OutCenters,
	TArray<FColor>& OutColors,
	float& OutHalfExtent) const
{
#if UE_ENABLE_DEBUG_DRAWING
	// VeryVerbose: this fires once per debug-draw frame so it spams the log
	// when LogSeinNavigationAStar is Verbose. The summary entry
	// further down (count of blockers + cells) is the actionable line;
	// keep this one as a "did we get called at all" sanity check that's
	// only visible when you specifically want it.
	UE_LOG(LogSeinNavigationAStar, VeryVerbose,
		TEXT("CollectDebugBlockerCells: ENTERED (HasRuntimeData=%d, DynamicBlockers=%d)"),
		HasRuntimeData() ? 1 : 0, DynamicBlockers.Num());

	OutCenters.Reset();
	OutColors.Reset();
	OutHalfExtent = 0.0f;
	if (!HasRuntimeData() || DynamicBlockers.Num() == 0) return;

	const float CS = CellSize.ToFloat();
	OutHalfExtent = CS * 0.5f * 0.9f;

	// Dedup cells when multiple blockers overlap — without this an overlapping
	// pair stamps shared cells twice and the alpha stacks, which reads as
	// "this cell is double-blocked" when really it's just the viz folding
	// two stamps onto one cell. First-blocker-wins for the color (rare
	// overlap case).
	//
	// TSet of cell-index — sized to the actual stamped-cell count rather than
	// the full grid. A flat W×H bool array is 1MB per call on a 1km² 100cm
	// grid (and grows quadratically with map area), even though the typical
	// blocker stamps cover <100 cells total. The TSet trades O(1) lookup for
	// O(stamped) memory, which is the right tradeoff for sparse stamps on
	// large grids. Reserved at the typical post-stamp size to skip the early
	// rehashes; SeinStampUtils stamps are usually circles/rects covering a
	// few dozen cells each.
	TSet<int32> Stamped;
	Stamped.Reserve(DynamicBlockers.Num() * 64);

	const float OriginXF = Origin.X.ToFloat();
	const float OriginYF = Origin.Y.ToFloat();

	// Layer-perspective filter from the `Sein.Nav.Show.Layer` console command.
	// When set, blockers whose BlockedNavLayerMask doesn't intersect the
	// filter bit are skipped entirely AND the visible blockers all render
	// in the filter layer's color (uniform "this is what blocks layer X" viz).
	// When no filter, each blocker renders in its OWN dominant-bit color
	// (lowest set bit) — so a Default-only blocker shows red, an N0 blocker
	// shows N0's settings color, etc. This matches the user mental model
	// where Sein.Nav.Show and Sein.Nav.Show.Layer 0 produce identical output
	// when every blocker happens to be on the Default layer.
	int32 FilterBit = -1;
	const bool bLayerFilter = UE::SeinARTSNavigation::TryGetDebugNavLayerOverride(FilterBit);
	const uint8 FilterBitMask = bLayerFilter ? static_cast<uint8>(1u << FilterBit) : uint8(0xFF);

	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();

	auto ColorForBit = [&](int32 Bit) -> FColor
	{
		// Bit 0 (Default) → red, matching the static blocker red so dynamic
		// and static "Default-blocking" cells visually merge.
		if (Bit == 0) return FColor(217, 0, 0, 128);
		if (Bit >= 1 && Bit <= 7 && Settings && Settings->NavLayers.IsValidIndex(Bit - 1))
		{
			FLinearColor C = Settings->NavLayers[Bit - 1].DebugColor;
			C.A = 0.5f;
			return C.ToFColor(true);
		}
		// Fallback (no settings entry) — catch-all orange.
		return FColor(255, 140, 0, 128);
	};

	const FColor FilterColor = bLayerFilter ? ColorForBit(FilterBit) : FColor::Magenta;

	for (const FSeinDynamicBlocker& B : DynamicBlockers)
	{
		if (bLayerFilter && (B.BlockedNavLayerMask & FilterBitMask) == 0) continue;

		// Resolve cell color for this blocker:
		//   filter active → uniform filter color (everything visible affects
		//                    that layer, render in its color).
		//   no filter     → blocker's own dominant-bit color (lowest set bit).
		FColor BlockerColor;
		if (bLayerFilter)
		{
			BlockerColor = FilterColor;
		}
		else
		{
			int32 LowestBit = 0;
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				if (B.BlockedNavLayerMask & (1u << Bit)) { LowestBit = Bit; break; }
			}
			BlockerColor = ColorForBit(LowestBit);
		}

		// One pass per stamp. Same cell-iteration helper used by the FindPath
		// overlay rebuild — viz exactly matches what pathing sees.
		SeinStampUtils::ForEachCoveredCell(
			B.Shape, B.EntityCenter, B.EntityRotation,
			CellSize, Origin, Width, Height,
			[&](int32 X, int32 Y)
			{
				const int32 Idx = CellIndex(X, Y);
				bool bAlreadyStamped = false;
				Stamped.Add(Idx, &bAlreadyStamped);
				if (bAlreadyStamped) return;
				const float CellCX = OriginXF + (X + 0.5f) * CS;
				const float CellCY = OriginYF + (Y + 0.5f) * CS;
				// +15cm Z bias so blocker quads layer cleanly above the
				// static cell quads (which render at +2cm).
				OutCenters.Emplace(CellCX, CellCY, CellHeight[Idx].ToFloat() + 15.0f);
				OutColors.Add(BlockerColor);
			});
	}

	// VeryVerbose: per-frame visualizer summary; useful for "did we collect
	// what we expected" diagnostics, but spams the log when Verbose-level
	// movement / planner logs are on. Verbose-tier consumers should turn
	// this on explicitly via LogSeinNavigationAStar VeryVerbose.
	UE_LOG(LogSeinNavigationAStar, VeryVerbose,
		TEXT("CollectDebugBlockerCells: %d blocker(s) → %d unique cell(s) (filter=%d)"),
		DynamicBlockers.Num(), OutCenters.Num(), bLayerFilter ? FilterBit : -1);
#endif
}

void USeinNavigationAStar::CollectDebugPathCells(
	const TArray<FFixedVector>& CellPathWorld,
	TArray<FVector>& OutRouteCells,
	TArray<FVector>& OutDestCell,
	float& OutHalfExtent) const
{
#if UE_ENABLE_DEBUG_DRAWING
	OutRouteCells.Reset();
	OutDestCell.Reset();
	OutHalfExtent = 0.0f;
	if (!HasRuntimeData() || CellPathWorld.Num() == 0) return;

	OutHalfExtent = CellSize.ToFloat() * 0.5f * 0.9f;

	// 1:1 with pathfinding. `CellPathWorld` is the EXACT A* cell chain (cell centers) captured BEFORE
	// smoothing — see FSeinPath::DebugCellPath. Each entry is one cell drawn as one box: no
	// rasterization, no supercover width, so the yellow cells match the logical cell path A* chose,
	// cell-for-cell. The last cell is the destination, split out so the ticker can mark it distinctly.
	OutRouteCells.Reserve(FMath::Max(0, CellPathWorld.Num() - 1));
	const int32 Last = CellPathWorld.Num() - 1;
	for (int32 i = 0; i <= Last; ++i)
	{
		const FFixedVector& V = CellPathWorld[i];
		const FVector Center(V.X.ToFloat(), V.Y.ToFloat(), V.Z.ToFloat());
		if (i == Last) OutDestCell.Add(Center);
		else           OutRouteCells.Add(Center);
	}
#endif
}
