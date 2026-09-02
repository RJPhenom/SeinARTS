/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefault.cpp
 * @brief   MVP stamp engine + unified-bake layer provider. Flat-circle stamp
 *          (shadowcast is step 2); BakeLayer runs the fog-specific occluder
 *          sweep on the shared level-data bake and serializes quantized uint8
 *          heights into the substrate's "FogOfWar" channel. Runtime
 *          dequantizes to FFixedPoint in LoadFromSubstrate.
 */

#include "Default/SeinFogOfWarDefault.h"
#include "Components/SeinVisionPayload.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinFogVisibilityPayload.h"
#include "Stamping/SeinStampShape.h"
#include "Stamping/SeinStampUtils.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"
#include "SeinARTSFogOfWarLog.h"

#include "SeinLevelData.h"
#include "Volumes/SeinLevelVolume.h"

#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Settings/PluginSettings.h"   // terrain-type → vision multiplier

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinMovementPayload.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinParallel.h"
#include "Types/Entity.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Math/Box.h"

// LogSeinFogOfWar is module-declared (SeinARTSFogOfWarLog.h) so it is reliably
// filterable in the Output Log — do not re-introduce a _STATIC define here.

namespace
{
	/** Resize and zero every element, including retained same-size storage.
	 *  TArray::SetNumZeroed only initializes newly added elements. */
	template <typename ElementType>
	void ResetToZeroedSize(TArray<ElementType>& Values, int32 Num)
	{
		Values.Reset(Num);
		Values.AddZeroed(Num);
	}
}

// ============================================================================
// Unified level-data layer provider (CP1.1; Decisions D12/D13/D17)
// ============================================================================

FName USeinFogOfWarDefault::GetLayerId() const
{
	return TEXT("FogOfWar");
}

void USeinFogOfWarDefault::BakeLayer(const USeinLevelData& Substrate, UWorld* World, TArray<uint8>& OutData)
{
	OutData.Reset();
	LastBakedCellSizeMultiple = 1;
	if (!World) return;

	const FIntPoint FineDims = Substrate.GetDimensions();
	const float FinestCellF = Substrate.GetFinestCellSize().ToFloat();
	if (FineDims.X <= 0 || FineDims.Y <= 0 || FinestCellF <= 0.0f) return;

	const FFixedVector OriginFP = Substrate.GetOrigin();
	const float OriginXF = OriginFP.X.ToFloat();
	const float OriginYF = OriginFP.Y.ToFloat();
	const float OriginZF = OriginFP.Z.ToFloat();

	// Fog config comes from the level volumes (first volume wins — the legacy
	// fog-volume convention); their union also gives the sweep ceiling
	// + the quantization range, exactly like the legacy fog bake.
	TArray<ASeinLevelVolume*> Volumes;
	FBox UnionBounds(ForceInit);
	for (TActorIterator<ASeinLevelVolume> It(World); It; ++It)
	{
		if (ASeinLevelVolume* Vol = *It)
		{
			Volumes.Add(Vol);
			UnionBounds += Vol->GetVolumeWorldBounds();
		}
	}
	if (Volumes.Num() == 0 || !UnionBounds.IsValid) return; // can't happen mid-bake; defensive

	// Snap the configured fog cell size to an integer multiple of the finest grid
	// so the channel records a clean resolution (D13/D15).
	const float DesiredCellF = Volumes[0]->GetResolvedVisionCellSize().ToFloat();
	const int32 M = FMath::Max(1, FMath::RoundToInt(DesiredCellF / FinestCellF));
	LastBakedCellSizeMultiple = M;
	const float FoWCellF = FinestCellF * M;
	if (!FMath::IsNearlyEqual(FoWCellF, DesiredCellF, 0.5f))
	{
		UE_LOG(LogSeinFogOfWar, Log,
			TEXT("FoW layer bake: vision cell size %.0f snapped to %.0f (%dx the %.0f shared grid)."),
			DesiredCellF, FoWCellF, M, FinestCellF);
	}
	const bool bBakeBlockers = Volumes[0]->bBakeStaticBlockers;

	const int32 FoWW = FMath::DivideAndRoundUp(FineDims.X, M);
	const int32 FoWH = FMath::DivideAndRoundUp(FineDims.Y, M);
	const int32 NumCells = FoWW * FoWH;

	// Fog-specific skip list — fog KEEPS its own bBakesIntoFogOfWar semantics
	// (a glass wall may bake into nav but not sight, and a sight-blocking hedge
	// may not bake into nav). Mirrors the legacy fog bake's skip block.
	FCollisionQueryParams QP(SCENE_QUERY_STAT(SeinFogOfWarBakeLayer), /*bTraceComplex*/ true);
	for (ASeinLevelVolume* Vol : Volumes)
	{
		if (Vol) QP.AddIgnoredActor(Vol);
	}
	int32 NumIgnoredActors = 0;
	for (TActorIterator<ASeinActor> It(World); It; ++It)
	{
		ASeinActor* SeinActor = *It;
		if (!SeinActor) continue;

		bool bSkip = false;
		if (const USeinEntityBridgeComponent* Bridge = SeinActor->FindComponentByClass<USeinEntityBridgeComponent>())
		{
			if (const FSeinExtentsPayload* Extents = Bridge->FindAuthoredData<FSeinExtentsPayload>())
			{
				if (!Extents->bBakesIntoFogOfWar) bSkip = true;
			}
			if (!bSkip)
			{
				for (const FInstancedStruct& Entry : Bridge->ComponentData)
				{
					if (Entry.GetScriptStruct() == FSeinMovementPayload::StaticStruct())
					{
						bSkip = true;
						break;
					}
				}
			}
		}
		if (bSkip)
		{
			QP.AddIgnoredActor(SeinActor);
			++NumIgnoredActors;
		}
	}

	const float TopZ = UnionBounds.Max.Z + BakeTraceHeadroom;
	const float BottomZ = UnionBounds.Min.Z - 100.0f;

	// Legacy quantization: 255 steps over the volume Z range + headroom.
	const float RangeZ = FMath::Max(1.0f, UnionBounds.Max.Z - UnionBounds.Min.Z + BakeTraceHeadroom);
	const float QuantumF = FMath::Max(1.0f, RangeZ / 255.0f);

	// Cell-footprint box sweep — same thin-wall rationale as the legacy bake: a
	// fence that doesn't cross the cell center must still register for LOS.
	const FCollisionShape CellBox = FCollisionShape::MakeBox(
		FVector(FoWCellF * 0.5f, FoWCellF * 0.5f, 1.0f));

	TArray<uint8> GroundQ;  GroundQ.SetNumUninitialized(NumCells);
	TArray<uint8> BlockerQ; BlockerQ.SetNumUninitialized(NumCells);
	TArray<uint8> MaskQ;    MaskQ.SetNumUninitialized(NumCells);

	int32 NumBlockers = 0;
	for (int32 FY = 0; FY < FoWH; ++FY)
	{
		for (int32 FX = 0; FX < FoWW; ++FX)
		{
			const int32 Idx = FY * FoWW + FX;
			const float CX = OriginXF + (FX + 0.5f) * FoWCellF;
			const float CY = OriginYF + (FY + 0.5f) * FoWCellF;

			// Shared ground at the fog cell's center: the finest cell containing it
			// (D17 — the ground ray is traced ONCE by the substrate, fog adopts it).
			const int32 FineX = FMath::Clamp(FX * M + M / 2, 0, FineDims.X - 1);
			const int32 FineY = FMath::Clamp(FY * M + M / 2, 0, FineDims.Y - 1);
			FSeinLevelCellSurface Surf;
			const bool bSurf = Substrate.GetCellSurface(FineY * FineDims.X + FineX, Surf) && Surf.bHasSurface;
			const float SharedZ = bSurf ? Surf.Height.ToFloat() : BottomZ;

			// Fog's own occluder sweep (the layer-specific ray that stays per-provider).
			FHitResult TopHit;
			const bool bHit = World->SweepSingleByChannel(TopHit,
				FVector(CX, CY, TopZ), FVector(CX, CY, BottomZ),
				FQuat::Identity, BakeTraceChannel, CellBox, QP);

			float GroundZ;
			float BlockerRelZ = 0.0f;
			if (!bHit && !bSurf)
			{
				// Nothing here at all — matches the legacy no-hit path (quantizes to 0).
				GroundZ = OriginZF;
			}
			else
			{
				const float TopHitZ = bHit ? TopHit.ImpactPoint.Z : SharedZ;
				// Ground = the shared height, EXCEPT where fog's own sweep sees LOWER —
				// geometry the fog skip-list ignores (bBakesIntoFogOfWar=false, e.g.
				// glass) is nav-ground but must not become sight-occluding ground.
				// Where an occluder covers the cell center the shared height IS the
				// occluder top, so it occludes as high ground (terrain occludes all
				// layers) — the masked blocker channel is for occluders ABOVE the
				// shared ground (thin walls, hedges, fog-only geometry).
				GroundZ = bSurf ? FMath::Min(SharedZ, TopHitZ) : TopHitZ;
				if (bBakeBlockers && bHit)
				{
					const float Gap = TopHitZ - GroundZ;
					if (Gap >= StaticBlockerMinHeight)
					{
						BlockerRelZ = Gap;
						++NumBlockers;
					}
				}
			}

			GroundQ[Idx]  = (uint8)FMath::Clamp(FMath::RoundToInt((GroundZ - OriginZF) / QuantumF), 0, 255);
			BlockerQ[Idx] = (uint8)FMath::Clamp(FMath::RoundToInt(BlockerRelZ / QuantumF), 0, 255);
			MaskQ[Idx]    = (BlockerRelZ > 0.0f) ? SEIN_FOW_MASK_VISIBLE : 0;
		}
	}

	// Channel blob — self-describing, mirrored byte-for-byte by LoadFromSubstrate:
	// [int32 W][int32 H][int64 CellSize][int64 MinHeight][int64 Quantum]
	// [Ground×N][Blocker×N][Mask×N]. Origin/coordinate space come from the
	// substrate at load.
	const FFixedPoint CellSizeFP = Substrate.GetFinestCellSize() * FFixedPoint::FromInt(M);
	const FFixedPoint MinHeightFP = OriginFP.Z;
	const FFixedPoint QuantumFP = FFixedPoint::FromFloat(QuantumF);

	const int32 HeaderBytes = 2 * sizeof(int32) + 3 * sizeof(int64);
	OutData.SetNumUninitialized(HeaderBytes + 3 * NumCells);
	uint8* Out = OutData.GetData();
	auto WriteI32 = [&Out](int32 V) { FMemory::Memcpy(Out, &V, sizeof(int32)); Out += sizeof(int32); };
	auto WriteI64 = [&Out](int64 V) { FMemory::Memcpy(Out, &V, sizeof(int64)); Out += sizeof(int64); };
	WriteI32(FoWW);
	WriteI32(FoWH);
	WriteI64(CellSizeFP.Value);
	WriteI64(MinHeightFP.Value);
	WriteI64(QuantumFP.Value);
	FMemory::Memcpy(Out, GroundQ.GetData(), NumCells);  Out += NumCells;
	FMemory::Memcpy(Out, BlockerQ.GetData(), NumCells); Out += NumCells;
	FMemory::Memcpy(Out, MaskQ.GetData(), NumCells);

	UE_LOG(LogSeinFogOfWar, Log,
		TEXT("FoW layer bake: %dx%d cells (cell=%.0f, %dx shared res) — blockers=%d (ignored %d actors)"),
		FoWW, FoWH, FoWCellF, M, NumBlockers, NumIgnoredActors);
}

FSeinStaticEnvironmentAdoptionResult
USeinFogOfWarDefault::LoadFromSubstrateImpl(
	const USeinLevelData& Substrate)
{
	if (!Substrate.HasRuntimeData())
	{
		return FSeinStaticEnvironmentAdoptionResult::NotApplicable(
			TEXT("The Level Data substrate has no runtime data."));
	}

	TArray<uint8> Blob;
	if (!Substrate.GetLayerChannel(TEXT("FogOfWar"), Blob))
	{
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			TEXT("The prepared Level Data substrate is missing the required FogOfWar channel; re-bake with the configured fog provider."));
	}

	constexpr int64 HeaderBytes = 2 * sizeof(int32) + 3 * sizeof(int64);
	if (Blob.Num() < HeaderBytes)
	{
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("The FogOfWar channel is malformed: expected at least %lld header bytes, received %d."),
				static_cast<long long>(HeaderBytes),
				Blob.Num()));
	}

	const uint8* In = Blob.GetData();
	auto ReadI32 = [&In]() { int32 V; FMemory::Memcpy(&V, In, sizeof(int32)); In += sizeof(int32); return V; };
	auto ReadI64 = [&In]() { int64 V; FMemory::Memcpy(&V, In, sizeof(int64)); In += sizeof(int64); return V; };
	const int32 W = ReadI32();
	const int32 H = ReadI32();
	const FFixedPoint CellSizeIn = FFixedPoint(ReadI64());
	const FFixedPoint MinH = FFixedPoint(ReadI64());
	const FFixedPoint Quantum = FFixedPoint(ReadI64());

	const int64 NumCells64 = static_cast<int64>(W) * static_cast<int64>(H);
	const bool bDimensionsValid =
		W > 0
		&& H > 0
		&& NumCells64 > 0
		&& NumCells64 <= MAX_int32;
	const int64 ExpectedBytes = bDimensionsValid
		? HeaderBytes + 3 * NumCells64
		: INDEX_NONE;
	if (!bDimensionsValid
		|| CellSizeIn <= FFixedPoint::Zero
		|| Quantum <= FFixedPoint::Zero
		|| static_cast<int64>(Blob.Num()) != ExpectedBytes)
	{
		UE_LOG(LogSeinFogOfWar, Warning,
			TEXT("LoadFromSubstrate: FogOfWar channel malformed (%d bytes for %dx%d) — ignoring (re-bake needed)."),
			Blob.Num(), W, H);
		return FSeinStaticEnvironmentAdoptionResult::Rejected(
			FString::Printf(
				TEXT("The FogOfWar channel is malformed: dimensions=%dx%d, cell size raw=%lld, height quantum raw=%lld, bytes=%d, expected=%lld."),
				W,
				H,
				static_cast<long long>(CellSizeIn.Value),
				static_cast<long long>(Quantum.Value),
				Blob.Num(),
				static_cast<long long>(ExpectedBytes)));
	}
	const int32 NumCells = static_cast<int32>(NumCells64);

	Width = W;
	Height = H;
	CellSize = CellSizeIn;
	Origin = Substrate.GetOrigin();
	StaticGridDigest.Invalidate();

	const uint8* GroundIn  = In;
	const uint8* BlockerIn = In + NumCells;
	const uint8* MaskIn    = In + 2 * NumCells;

	GroundHeight.SetNumUninitialized(NumCells);
	BlockerHeight.SetNumUninitialized(NumCells);
	BlockerLayerMask.SetNumUninitialized(NumCells);
	ResetToZeroedSize(DynamicBlockerHeight, NumCells);
	ResetToZeroedSize(DynamicBlockerLayerMask, NumCells);
	DynamicBlockerHeightExceptions.Reset();
	VisionGroups.Empty(); // per-player state recreates lazily on next stamp
	SourceStates.Empty();
	DynamicBlockerSnapshots.Reset();
	LastDynamicBlockerCells.Reset();
	ResetRoutineRootCache();

	// Dequantize. Runtime stores ABSOLUTE world Z for both (Ground = MinHeight
	// + Q·steps; Blocker = Ground + Q·steps) so shadowcast's lampshade test is
	// a straight world-Z compare.
	for (int32 Idx = 0; Idx < NumCells; ++Idx)
	{
		const FFixedPoint GroundZ = MinH + Quantum * FFixedPoint::FromInt(GroundIn[Idx]);
		GroundHeight[Idx] = GroundZ;
		BlockerHeight[Idx] = (BlockerIn[Idx] > 0)
			? (GroundZ + Quantum * FFixedPoint::FromInt(BlockerIn[Idx]))
			: FFixedPoint::Zero;
		BlockerLayerMask[Idx] = MaskIn[Idx];
	}

	OnFogOfWarMutated.Broadcast();
	return FSeinStaticEnvironmentAdoptionResult::Adopted();
}

// ============================================================================
// Init (no bake fallback)
// ============================================================================

void USeinFogOfWarDefault::InitGridFromVolumesImpl(UWorld* World)
{
	if (!World) return;

	// Union all level volume bounds (using editor-baked PlacedBounds — never
	// FromFloat at runtime). Fog cell size resolves from the first volume
	// (GetResolvedVisionCellSize — same first-volume-wins convention BakeLayer
	// uses).
	FFixedVector UnionMin(FFixedPoint::FromInt(INT32_MAX), FFixedPoint::FromInt(INT32_MAX), FFixedPoint::FromInt(INT32_MAX));
	FFixedVector UnionMax(FFixedPoint::FromInt(INT32_MIN), FFixedPoint::FromInt(INT32_MIN), FFixedPoint::FromInt(INT32_MIN));
	FFixedPoint ResolvedCellSize = FFixedPoint::Zero;
	int32 VolumeCount = 0;
	bool bAnyUnbaked = false;
	for (TActorIterator<ASeinLevelVolume> It(World); It; ++It)
	{
		ASeinLevelVolume* Vol = *It;
		if (!Vol) continue;

		// Snapshot guard: legacy actors fall back to runtime FromFloat
		// (NOT cross-arch deterministic). Re-saving the level after this
		// landed bakes the snapshot.
		FFixedVector VMin, VMax;
		if (Vol->bBoundsBaked)
		{
			VMin = Vol->PlacedBoundsMin;
			VMax = Vol->PlacedBoundsMax;
		}
		else
		{
			const FBox VB = Vol->GetVolumeWorldBounds();
			if (!VB.IsValid) continue;
			bAnyUnbaked = true;
			VMin = FFixedVector(FFixedPoint::FromFloat(VB.Min.X), FFixedPoint::FromFloat(VB.Min.Y), FFixedPoint::FromFloat(VB.Min.Z));
			VMax = FFixedVector(FFixedPoint::FromFloat(VB.Max.X), FFixedPoint::FromFloat(VB.Max.Y), FFixedPoint::FromFloat(VB.Max.Z));
		}

		if (VMin.X < UnionMin.X) UnionMin.X = VMin.X;
		if (VMin.Y < UnionMin.Y) UnionMin.Y = VMin.Y;
		if (VMin.Z < UnionMin.Z) UnionMin.Z = VMin.Z;
		if (VMax.X > UnionMax.X) UnionMax.X = VMax.X;
		if (VMax.Y > UnionMax.Y) UnionMax.Y = VMax.Y;
		if (VMax.Z > UnionMax.Z) UnionMax.Z = VMax.Z;

		if (VolumeCount == 0)
		{
			ResolvedCellSize = Vol->GetResolvedVisionCellSize();
		}
		++VolumeCount;
	}
	if (VolumeCount == 0)
	{
		Width = 0;
		Height = 0;
		GroundHeight.Reset();
		BlockerHeight.Reset();
		BlockerLayerMask.Reset();
		DynamicBlockerHeight.Reset();
		DynamicBlockerLayerMask.Reset();
		DynamicBlockerHeightExceptions.Reset();
		VisionGroups.Empty();
		SourceStates.Empty();
		DynamicBlockerSnapshots.Reset();
		LastDynamicBlockerCells.Reset();
		StaticGridDigest.Invalidate();
		ResetRoutineRootCache();
		OnFogOfWarMutated.Broadcast();
		return;
	}
	if (bAnyUnbaked)
	{
		UE_LOG(LogSeinFogOfWar, Warning,
			TEXT("InitGridFromVolumes: one or more level volumes have stale "
				 "PlacedBounds (bBoundsBaked == false). Re-save the level to "
				 "bake snapshots. NOT cross-arch deterministic until then."));
	}

	if (ResolvedCellSize <= FFixedPoint::Zero)
	{
		UE_LOG(LogSeinFogOfWar, Error,
			TEXT("InitGridFromVolumes: resolved vision cell size is not positive."));
		return;
	}
	const float CellSizeF = ResolvedCellSize.ToFloat();
	const float SizeXF = (UnionMax.X - UnionMin.X).ToFloat();
	const float SizeYF = (UnionMax.Y - UnionMin.Y).ToFloat();
	const int64 Width64 = FMath::Max<int64>(
		1, FMath::CeilToInt64(
			static_cast<double>(SizeXF) / CellSizeF));
	const int64 Height64 = FMath::Max<int64>(
		1, FMath::CeilToInt64(
			static_cast<double>(SizeYF) / CellSizeF));
	if (Width64 > MAX_int32
		|| Height64 > MAX_int32)
	{
		UE_LOG(LogSeinFogOfWar, Error,
			TEXT("InitGridFromVolumes: resolved grid is too large (%lldx%lld). Bake or increase the configured vision cell size."),
			static_cast<long long>(Width64),
			static_cast<long long>(Height64));
		return;
	}
	const int64 NumCells64 = Width64 * Height64;
	if (NumCells64 <= 0 || NumCells64 > MAX_int32)
	{
		UE_LOG(LogSeinFogOfWar, Error,
			TEXT("InitGridFromVolumes: resolved grid is too large (%lldx%lld). Bake or increase the configured vision cell size."),
			static_cast<long long>(Width64),
			static_cast<long long>(Height64));
		return;
	}

	CellSize = ResolvedCellSize;
	Origin = UnionMin;
	Width = static_cast<int32>(Width64);
	Height = static_cast<int32>(Height64);
	StaticGridDigest.Invalidate();
	const int32 NumCells = static_cast<int32>(NumCells64);
	GroundHeight.SetNumUninitialized(NumCells);
	ResetToZeroedSize(BlockerHeight, NumCells);
	ResetToZeroedSize(BlockerLayerMask, NumCells);
	ResetToZeroedSize(DynamicBlockerHeight, NumCells);
	ResetToZeroedSize(DynamicBlockerLayerMask, NumCells);
	DynamicBlockerHeightExceptions.Reset();
	VisionGroups.Empty();
	SourceStates.Empty();
	DynamicBlockerSnapshots.Reset();
	LastDynamicBlockerCells.Reset();
	ResetRoutineRootCache();

	// Per-cell downward trace capped at InitTraceCellCap — no-bake fallback.
	// Trace endpoints are runtime-only (the trace itself is non-deterministic
	// already — designers should bake to capture stable Z); just float them.
	const float OriginXF = UnionMin.X.ToFloat();
	const float OriginYF = UnionMin.Y.ToFloat();
	const float MinZF = UnionMin.Z.ToFloat();
	const float MaxZF = UnionMax.Z.ToFloat();

	const bool bTracePerCell = NumCells <= InitTraceCellCap;
	const FFixedPoint FallbackZ = FFixedPoint::FromFloat((MinZF + MaxZF) * 0.5f);
	if (!bTracePerCell)
	{
		// No-bake fallback: grid too large to per-cell trace at load, so every
		// cell takes a flat mid-Z. Units sit at that flat height on sloped
		// terrain until the level data is baked. Loud on purpose: baking is the
		// fix (correct per-cell Z AND no load hitch).
		UE_LOG(LogSeinFogOfWar, Warning,
			TEXT("InitGridFromVolumes: %d cells exceeds InitTraceCellCap (%d); using FLAT fallback Z. BAKE the level data for correct ground height and faster load."),
			NumCells, InitTraceCellCap);
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SeinFowInitTrace), /*bTraceComplex*/ true);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float CX = OriginXF + (X + 0.5f) * CellSizeF;
			const float CY = OriginYF + (Y + 0.5f) * CellSizeF;
			FFixedPoint Z = FallbackZ;
			if (bTracePerCell)
			{
				FHitResult Hit;
				const FVector Start(CX, CY, MaxZF + 100.0f);
				const FVector End  (CX, CY, MinZF - 100.0f);
				if (World->LineTraceSingleByChannel(Hit, Start, End, BakeTraceChannel, Params))
				{
					Z = FFixedPoint::FromFloat(Hit.Location.Z);
				}
			}
			GroundHeight[CellIndex(X, Y)] = Z;
		}
	}

	UE_LOG(LogSeinFogOfWar, Log,
		TEXT("InitGridFromVolumes: %d×%d cells @ %.0f cm (%d volumes, %d traces)"),
		Width, Height, CellSizeF, VolumeCount, bTracePerCell ? NumCells : 0);

	OnFogOfWarMutated.Broadcast();
}

// ============================================================================
// Tick / stamp
// ============================================================================

FSeinFogVisionGroup& USeinFogOfWarDefault::GetOrCreateGroup(FSeinPlayerID PlayerID)
{
	const bool bWasPresent = VisionGroups.Contains(PlayerID);
	FSeinFogVisionGroup& Group = VisionGroups.FindOrAdd(PlayerID);
	const int32 NumCells = Width * Height;
	if (Group.CellBitfield.Num() != NumCells)
	{
		Group.CellBitfield.SetNumZeroed(NumCells);
	}
	if (!bWasPresent)
	{
		MarkRoutineExploredCellDirty(PlayerID, INDEX_NONE);
	}
	return Group;
}

void USeinFogOfWarDefault::TickStamps(UWorld* World)
{
	if (Width <= 0 || Height <= 0 || !World) return;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	const ISeinComponentStorage* Storage = Sim->GetComponentStorageRaw(FSeinVisionPayload::StaticStruct());
	if (!Storage) return;

	// Vision tick skip: when the world has no vision sources, no blocker
	// entities, no cached source state from prior ticks, and no stamped
	// dynamic-blocker cells from last tick, TickStamps would be a complete
	// no-op. Bail before paying the entity-iteration scan + RebuildDynamicBlockers
	// overhead. The four conditions together mean "we did nothing last tick AND
	// there's nothing in the world that could need stamping this tick" —
	// safe even with the bDynamicBlockersChanged invalidation path because
	// neither side of the change set has anything to invalidate.
	//
	// Once any vision source or blocker spawns, GetComponentCount() flips
	// non-zero and the skip stops firing — no per-spawn callback needed.
	// Useful primarily for loading screens, fog-disabled games, and pre-
	// observer setup; once ~one observer exists, SourceStates is non-empty
	// for the rest of the match.
	const ISeinComponentStorage* ExtentsStorage =
		Sim->GetComponentStorageRaw(FSeinExtentsPayload::StaticStruct());
	const int32 VisionCount = Storage->GetComponentCount();
	const int32 ExtentsCount = ExtentsStorage ? ExtentsStorage->GetComponentCount() : 0;
	if (VisionCount == 0 && ExtentsCount == 0
		&& SourceStates.IsEmpty()
		&& LastDynamicBlockerCells.Num() == 0)
	{
		UE_LOG(LogSeinFogOfWar, VeryVerbose,
			TEXT("TickStamps: world has no vision sources, blockers, or cached state — skipping"));
		return;
	}

	// Rebuild dynamic blocker overlay first — smoke grenades / destructible
	// mid-animation / any runtime USeinExtentsComponent (bBlocksFogOfWar) entities stamp
	// their shapes into the dense dynamic overlay plus sparse per-layer
	// height exceptions here,
	// so the vision passes below see the freshest occlusion state.
	const bool bDynamicBlockersChanged = RebuildDynamicBlockers(World);
	if (bDynamicBlockersChanged)
	{
		MarkRoutineDynamicBlockersDirty();
	}

	// If dynamic blockers changed, every source's LOS may have shifted —
	// flip bValid=false on every source so the change-detection stable-
	// fast-path below recomputes (each invalidated source gets a work item)
	// and the diff path runs. We DON'T eagerly tear down old footprints here
	// (as we used to) because the per-bit diff in the serial apply computes
	// the minimal old→new delta when it runs — for sources whose LOS results
	// don't actually change
	// despite the blocker mutation, the diff is empty and the per-tick
	// cost collapses to "generate footprint + sort + zero-diff." That's
	// the win: a smoke grenade halfway across the map no longer triggers
	// a full re-stamp on every observer, only on those whose LOS shifted.
	if (bDynamicBlockersChanged)
	{
		for (TPair<FSeinEntityHandle, FSeinFogSourceState>& Pair : SourceStates)
		{
			Pair.Value.bValid = false;
		}
	}

	// ----------------------------------------------------------------------
	// Per-source footprint stamping — parallel-compute, serial-apply
	// (the established pattern; mirrors FSeinAvoidanceSystem / the Jacobi
	// collision resolver). The per-source Bresenham LOS footprint generation
	// is the dominant per-tick FoW cost and is the classic one-worker-per-
	// vision-source candidate: each source's footprint is a PURE READ of the
	// immutable per-tick grid (GroundHeight / Blocker / DynamicBlocker arrays,
	// all finalized by RebuildDynamicBlockers ABOVE this loop) plus the
	// source's own pose, and the outputs (per-bit cell lists) are per-source
	// and disjoint. So we split the old per-source UpdateSourceStamp into:
	//   (1) SERIAL gather + change-detection — reads the SourceStates cache to
	//       decide which sources changed; collects only those into a work-list.
	//   (2) PARALLEL compute — each changed source rasterizes its per-bit
	//       footprint cells into its OWN work-item scratch (no SourceStates /
	//       VisionGroups touch).
	//   (3) SERIAL apply — in work-list (handle) order, commit each source's
	//       SourceStates cache entry and ApplyFootprintDiff its new vs old
	//       cells into the shared refcounted VisionGroups.
	// `Sein.Sim.Parallel 0` forces the compute serial; the result is bit-
	// identical (immutable reads, disjoint per-source scratch, fixed-point /
	// integer Bresenham, handle-ordered serial apply), so it stays lockstep-
	// safe for the ability LOS gate.
	// ----------------------------------------------------------------------

	// Terrain-scaled vision: sample the baked terrain type under each source and scale its
	// stamp radii by that type's VisionMultiplier (a unit in a forest sees less far).
	// Resolved once here; null nav / no terrain types → multiplier 1 → unscaled fast path.
	USeinNavigation* TerrainNav = USeinNavigationSubsystem::GetNavigationForWorld(World);
	const USeinARTSCoreSettings* TerrainSettings = GetDefault<USeinARTSCoreSettings>();

	// (1) SERIAL gather + change-detection. ForEachEntity walks slots in
	//     ascending index order, so the work-list (and thus the serial apply
	//     below) is naturally handle-sorted — a fixed, deterministic order.
	//     The change decision READS the SourceStates cache (FindOrAdd ensures
	//     the entry exists so the apply phase can re-fetch it by handle); a
	//     source whose pose + exact effective stamps match last tick skips with no work
	//     item, exactly as the old stable-fast-path did. We do NOT hold the
	//     FindOrAdd reference past this body — the map may rehash as more
	//     entries are added — the apply phase re-fetches by handle.
	TSet<FSeinEntityHandle> AliveSources;
	AliveSources.Reserve(SourceStates.Num());
	TArray<FSeinFogStampWork> WorkItems;
	WorkItems.Reserve(SourceStates.Num());

	Storage->ForEachLiveComponent(
		[this, Sim, &AliveSources, &WorkItems, TerrainNav, TerrainSettings](FSeinEntityHandle Handle, const void* Raw)
		{
			if (!Sim->GetEntityPool().IsValid(Handle)) return;
			const FSeinEntity* Entity = Sim->GetEntity(Handle);
			if (!Entity || !Raw) return;
			const FSeinVisionPayload* VData = static_cast<const FSeinVisionPayload*>(Raw);
			if (!VData) return;

			AliveSources.Add(Handle);
			const FSeinPlayerID OwnerPlayer = Sim->GetEntityOwner(Handle);
			const FFixedVector SourcePos = Entity->Transform.GetLocation();
			const FFixedQuaternion SourceRot = Entity->Transform.Rotation;

			// Terrain vision scale at the source's cell (1 = unchanged → unscaled fast path).
			FFixedPoint VisMult = FFixedPoint::One;
			if (TerrainNav && TerrainSettings)
			{
				const int32 TType = TerrainNav->GetTerrainTypeAt(SourcePos);
				if (TType != 0) VisMult = TerrainSettings->GetTerrainVisionMultiplier(TType);
			}

			// Build the stamp set this tick rasterizes — the authored set, or a
			// terrain-scaled copy. The cache comparison + footprint compute both run
			// against THIS set, so the cache stays consistent. Source MOVEMENT —
			// the only way the terrain under a source changes (the bake is
			// static) — already invalidates the cache via WorldPos, so a
			// stationary source on constant terrain stays on the fast path.
			const TArray<FSeinVisionStamp>* StampsToUse = &VData->VisionStamps;
			TArray<FSeinVisionStamp> ScaledStamps;
			if (VisMult != FFixedPoint::One)
			{
				ScaledStamps = VData->VisionStamps;
				for (FSeinVisionStamp& S : ScaledStamps)
				{
					ScaleStampRangeForTerrain(S.Shape, VisMult);
				}
				StampsToUse = &ScaledStamps;
			}

			// Stable-source fast path (the old UpdateSourceStamp early-out).
			// Identical pose + owner + eye + stamp set ⇒ no work, no work item.
			// Pose includes Rotation since shaped stamps depend on it (rect
			// axes, cone direction). bValid is flipped false above for ALL
			// sources when dynamic blockers changed, so this correctly falls
			// through to a recompute then.
			const FSeinFogSourceState& State = SourceStates.FindOrAdd(Handle);
			if (State.bValid
				&& State.Owner == OwnerPlayer
				&& State.WorldPos == SourcePos
				&& State.Rotation == SourceRot
				&& State.EyeHeight == VData->EyeHeight
				&& State.Stamps == *StampsToUse)
			{
				return;
			}

			// Changed → queue a work item carrying everything the parallel
			// footprint compute needs. The stamp set is copied into the item so
			// the parallel body never touches transient/component state.
			FSeinFogStampWork& Work = WorkItems.AddDefaulted_GetRef();
			Work.Handle     = Handle;
			Work.Owner      = OwnerPlayer;
			Work.WorldPos   = SourcePos;
			Work.Rotation   = SourceRot;
			Work.EyeHeight  = VData->EyeHeight;
			Work.Stamps     = *StampsToUse;
		});

	// (2) PARALLEL compute. Each body rasterizes its source's per-bit footprint
	//     cells into WorkItems[i].GenScratch — a disjoint per-source slot. Pure
	//     reads of the immutable grid (GroundHeight / Blocker / DynamicBlocker,
	//     finalized before this loop) + the item's own inputs; NO SourceStates
	//     FindOrAdd, NO VisionGroups mutation. Per-bit: union this tick's stamp
	//     contributions across all of the source's stamps emitting on that bit,
	//     then sort (the diff in the apply phase wants ascending multisets).
	//     This path calls NO cross-module delegate (every LOS read is a FoW-
	//     internal immutable-grid read), so no bForceSerial is needed — only the
	//     Sein.Sim.Parallel / min-batch gating inside SeinParallelFor.
	const int32 NumWork = WorkItems.Num();
	SeinParallelFor(NumWork, [this, &WorkItems](int32 i)
	{
		FSeinFogStampWork& Work = WorkItems[i];
		for (uint8 Bit = 1; Bit <= 7; ++Bit)
		{
			const uint8 BitMask = static_cast<uint8>(1u << Bit);
			TArray<int32>& NewCells = Work.GenScratch[Bit];
			NewCells.Reset();
			for (const FSeinVisionStamp& VStamp : Work.Stamps)
			{
				if (!VStamp.Shape.bEnabled) continue;
				if (VStamp.LayerMask == 0) continue;
				if ((VStamp.LayerMask & BitMask) == 0) continue;
				GenerateLayerFootprintCells(VStamp.Shape, Work.WorldPos, Work.Rotation,
					Work.EyeHeight, Bit, NewCells);
			}
			NewCells.Sort();
		}
	});

	// (3) SERIAL apply — handle-ordered (the work-list order). Commit each
	//     source's SourceStates cache entry AND ApplyFootprintDiff its new vs
	//     old cell lists into the shared refcounted VisionGroups. The refcount
	//     add/remove is commutative so the final VisionGroups is order-
	//     independent, but we apply in the fixed work-list order anyway. This is
	//     the back half of the old UpdateSourceStamp, verbatim — owner-transfer
	//     migration, per-bit diff + footprint commit, cache-key commit.
	bool bAnySourceChanged = (NumWork > 0);
	for (int32 i = 0; i < NumWork; ++i)
	{
		FSeinFogStampWork& Work = WorkItems[i];
		FSeinFogSourceState& State = SourceStates.FindChecked(Work.Handle);

		// Owner transfer: footprints live in per-player groups, so we can't
		// diff across groups. Decrement everything from the old group (which
		// resets State.Footprints[Bit] in place), then the per-bit diff below
		// runs against the now-empty old set into the new group. Not gated on
		// bValid — forced-invalidation paths (dynamic blockers changed) leave
		// Footprints populated even with bValid=false, and a same-tick owner
		// change would still need them migrated. Happy path (Owner unchanged)
		// skips the find+iter entirely.
		if (State.Owner != Work.Owner)
		{
			if (FSeinFogVisionGroup* OldGroup = VisionGroups.Find(State.Owner))
			{
				DecrementFootprintsForState(State, *OldGroup);
			}
			else
			{
				// Old owner has no group (typical for fresh entities — default-
				// constructed Owner). Footprints[] should already be empty;
				// reset defensively so the diff has a clean baseline.
				for (int32 BitIdx = 1; BitIdx <= 7; ++BitIdx)
				{
					State.Footprints[BitIdx].Reset();
				}
			}
		}

		FSeinFogVisionGroup& NewGroup = GetOrCreateGroup(Work.Owner);

		// Per-bit diff. The parallel body already unioned + sorted the new cell
		// list per bit; here we apply the minimal refcount/bitfield delta vs
		// last tick's stored footprint and commit the (sorted) new set for next
		// tick's diff. Bit 0 (Explored) is sticky and handled inside Increment.
		for (uint8 Bit = 1; Bit <= 7; ++Bit)
		{
			ApplyFootprintDiff(
				Work.Owner,
				NewGroup,
				Bit,
				State.Footprints[Bit],
				Work.GenScratch[Bit]);
			State.Footprints[Bit] = MoveTemp(Work.GenScratch[Bit]);
		}

		// Commit cache key for next tick's compare.
		State.bValid    = true;
		State.Owner     = Work.Owner;
		State.WorldPos  = Work.WorldPos;
		State.Rotation  = Work.Rotation;
		State.EyeHeight = Work.EyeHeight;
		State.Stamps    = Work.Stamps;
		MarkRoutineSourceDirty(Work.Handle);

		// DIAGNOSTIC (2026-05-02 smoke-not-blocking regression): one line per
		// source per re-stamp. EyeZ here is the same value passed to LOS — if
		// it's much higher than the smoke's `TopZ` (logged in
		// StampDynamicBlockerShape) the lampshade test rejects. Free when
		// LogSeinFogOfWar is below Verbose.
		const FFixedPoint EyeZForLog = Work.WorldPos.Z + Work.EyeHeight;
		UE_LOG(LogSeinFogOfWar, Verbose,
			TEXT("UpdateSourceStamp: Handle=%d WorldPos.Z=%lld EyeHeight=%lld EyeZ=%lld "
				 "Stamps=%d Owner=%u"),
			Work.Handle.Index, Work.WorldPos.Z.Value, Work.EyeHeight.Value, EyeZForLog.Value,
			Work.Stamps.Num(), Work.Owner.Value);
	}

	// Source went away (entity destroyed, vision component stripped, etc.) —
	// tear down its footprint so its bits don't linger. Counts as a viz
	// change since the cleared bits flip to baseline.
	const bool bAnySourceRemoved = (SourceStates.Num() != AliveSources.Num());
	if (bAnySourceRemoved)
	{
		TArray<FSeinEntityHandle> Stale;
		for (const TPair<FSeinEntityHandle, FSeinFogSourceState>& Pair : SourceStates)
		{
			if (!AliveSources.Contains(Pair.Key)) Stale.Add(Pair.Key);
		}
		for (FSeinEntityHandle H : Stale) RemoveSourceStamp(H);
	}

	// Maintain the per-observer VisibleOnceSeen latch off the now-current grid.
	// Runs every tick regardless of bAnyChanged: a thing that spawns inside an
	// existing observer's vision must latch even when no SOURCE changed this
	// tick. Cheap once no VisibleOnceSeen entity remains unseen. A latch flip
	// is not a grid-viz change, so it deliberately does NOT feed the broadcast.
	UpdateSeenLatches(*Sim);

	// Only broadcast if SOMETHING about the viz state actually changed.
	// Skipping idle-tick broadcasts is the difference between paying a full
	// CollectDebugCellQuads grid-iteration on every vision tick (the debug
	// component's MarkRenderStateDirty fires regardless of whether the
	// showflag is on) vs paying it only when the FoW actually moved. On a
	// quiescent map with stationary units this drops the per-tick cost to
	// near-zero.
	const bool bAnyChanged = bDynamicBlockersChanged || bAnySourceChanged || bAnySourceRemoved;
	if (bAnyChanged)
	{
		OnFogOfWarMutated.Broadcast();
	}
	else
	{
		UE_LOG(LogSeinFogOfWar, VeryVerbose,
			TEXT("TickStamps: no viz change this tick, broadcast suppressed"));
	}
}

void USeinFogOfWarDefault::RemoveSourceStamp(FSeinEntityHandle Handle)
{
	FSeinFogSourceState* State = SourceStates.Find(Handle);
	if (!State) return;
	// bValid only controls whether the source's cached inputs may take the
	// stable fast path. Dynamic-blocker changes invalidate those inputs while
	// leaving the already-applied footprints (and their refcounts) intact.
	if (FSeinFogVisionGroup* Group = VisionGroups.Find(State->Owner))
	{
		DecrementFootprintsForState(*State, *Group);
	}
	MarkRoutineSourceDirty(Handle);
	SourceStates.Remove(Handle);
}

void USeinFogOfWarDefault::GenerateLayerFootprintCells(
	const FSeinStampShape& Shape,
	const FFixedVector& EntityWorldPos,
	const FFixedQuaternion& EntityRotation,
	FFixedPoint EyeHeight, uint8 StampBit,
	TArray<int32>& OutCells) const
{
	if (StampBit < 1 || StampBit > 7) return;
	if (CellSize <= FFixedPoint::Zero) return;
	if (!Shape.bEnabled) return;

	// Apex world position = entity pos + Quat(EntityYaw)·LocalOffset. For
	// radial stamps this collapses to the entity itself; for window cones
	// or rect stamps it's wherever the designer placed the LocalOffset.
	// LOS originates from this apex (so a window cone casts FROM the
	// window, not the building center).
	const FFixedVector ApexWorld = SeinStampUtils::ComputeStampWorldOrigin(
		Shape, EntityWorldPos, EntityRotation);
	int32 SX, SY;
	if (!WorldToGrid(ApexWorld, SX, SY)) return;

	// Eye Z = entity's actual sim Z + EyeHeight. Uses `EntityWorldPos.Z`
	// rather than GroundHeight[cell] so a unit standing on a climbable
	// platform sees from its actual standing surface, not the terrain
	// beneath. Deterministic (FFixedPoint).
	const FFixedPoint EyeZ = EntityWorldPos.Z + EyeHeight;
	const uint8 StampBitMask = static_cast<uint8>(1u << StampBit);

	// Apex cell — always visible to its own source on this layer (no LOS
	// check; the apex literally IS the eye). Append unconditionally; the
	// caller's diff handles the refcount/bitfield mutation.
	OutCells.Add(CellIndex(SX, SY));

	// Walk every cell inside the shape's coverage. For each one, run LOS
	// from the apex cell to the target cell — the apex eye Z plus the
	// target's terrain Z drive the lampshade interpolation in
	// HasLineOfSightToCell. Same Bresenham walk as before; the only thing
	// the shape primitive changes is which cells are CANDIDATES.
	SeinStampUtils::ForEachCoveredCell(
		Shape, EntityWorldPos, EntityRotation,
		CellSize, Origin, Width, Height,
		[&](int32 TX, int32 TY)
		{
			// Apex cell already added above — skip duplicate work.
			if (TX == SX && TY == SY) return;

			const int32 TargetIdx = CellIndex(TX, TY);
			const FFixedPoint TargetZ = GroundHeight.IsValidIndex(TargetIdx)
				? GroundHeight[TargetIdx] : Origin.Z;

			if (HasLineOfSightToCell(SX, SY, TX, TY, EyeZ, TargetZ, StampBitMask))
			{
				OutCells.Add(TargetIdx);
			}
		});
}

void USeinFogOfWarDefault::ScaleStampRangeForTerrain(
	FSeinStampShape& Shape,
	FFixedPoint Multiplier)
{
	switch (Shape.Shape)
	{
	case ESeinStampShape::Radial:
		Shape.Radius = Shape.Radius * Multiplier;
		break;

	case ESeinStampShape::Rect:
		Shape.HalfExtentX = Shape.HalfExtentX * Multiplier;
		Shape.HalfExtentY = Shape.HalfExtentY * Multiplier;
		break;

	case ESeinStampShape::Conical:
		Shape.ConeLength = Shape.ConeLength * Multiplier;
		break;
	}
}

void USeinFogOfWarDefault::ApplyFootprintDiff(
	FSeinPlayerID Observer,
	FSeinFogVisionGroup& Group,
	uint8 StampBit,
	const TArray<int32>& OldSorted,
	const TArray<int32>& NewSorted)
{
	if (StampBit < 1 || StampBit > 7) return;
	if (OldSorted.IsEmpty() && NewSorted.IsEmpty()) return;

	const int32 NumCells = Width * Height;
	TArray<uint16>& RefCounts = Group.RefCounts[StampBit];
	if (RefCounts.Num() != NumCells) RefCounts.SetNumZeroed(NumCells);
	if (Group.CellBitfield.Num() != NumCells) Group.CellBitfield.SetNumZeroed(NumCells);

	const uint8 BitMask = static_cast<uint8>(1u << StampBit);

	// Decrement = "this cell was in OLD but not NEW" — refcount drops by
	// one, bit clears on 1→0. Explored is sticky (never cleared).
	auto Decrement = [&](int32 CellIdx)
	{
		if (!RefCounts.IsValidIndex(CellIdx)) return;
		uint16& Count = RefCounts[CellIdx];
		if (Count == 0) return; // defensive — shouldn't happen given our invariants
		--Count;
		if (Count == 0 && Group.CellBitfield.IsValidIndex(CellIdx))
		{
			Group.CellBitfield[CellIdx] &= ~BitMask;
		}
	};

	// Increment = "this cell is in NEW but not OLD" — refcount up by one,
	// bit set on 0→1, Explored bit set unconditionally (sticky behavior
	// matches the pre-diff Stamp lambda).
	auto Increment = [&](int32 CellIdx)
	{
		if (!RefCounts.IsValidIndex(CellIdx)) return;
		uint16& Count = RefCounts[CellIdx];
		if (Count == 0 && Group.CellBitfield.IsValidIndex(CellIdx))
		{
			Group.CellBitfield[CellIdx] |= BitMask;
		}
		++Count;
		if (Group.CellBitfield.IsValidIndex(CellIdx))
		{
			const bool bWasExplored =
				(Group.CellBitfield[CellIdx] & SEIN_FOW_BIT_EXPLORED) != 0;
			Group.CellBitfield[CellIdx] |= SEIN_FOW_BIT_EXPLORED;
			if (!bWasExplored)
			{
				MarkRoutineExploredCellDirty(Observer, CellIdx);
			}
		}
	};

	// Merge-walk both sorted multisets. Equal indices pair off (no work);
	// unmatched OLD entries decrement; unmatched NEW entries increment.
	// Multiset semantics: duplicates in OLD pair against duplicates in NEW
	// in iteration order, so a source that stamps the same cell twice
	// (apex + one covered-cell hit) keeps its refcount contribution stable
	// across ticks if both stamps still hit the same cell next tick.
	int32 i = 0;
	int32 j = 0;
	while (i < OldSorted.Num() && j < NewSorted.Num())
	{
		const int32 OldCell = OldSorted[i];
		const int32 NewCell = NewSorted[j];
		if (OldCell < NewCell)      { Decrement(OldCell); ++i; }
		else if (OldCell > NewCell) { Increment(NewCell); ++j; }
		else                        { ++i; ++j; }
	}
	while (i < OldSorted.Num()) { Decrement(OldSorted[i++]); }
	while (j < NewSorted.Num()) { Increment(NewSorted[j++]); }
}

void USeinFogOfWarDefault::DecrementFootprintsForState(FSeinFogSourceState& State, FSeinFogVisionGroup& Group)
{
	for (int32 BitIdx = 1; BitIdx <= 7; ++BitIdx)
	{
		TArray<int32>& Footprint = State.Footprints[BitIdx];
		if (Footprint.Num() == 0) continue;
		TArray<uint16>& RefCounts = Group.RefCounts[BitIdx];
		if (RefCounts.Num() == 0)
		{
			// Refcount array missing — defensive; just clear the
			// footprint so we don't leak it into the next stamp.
			Footprint.Reset();
			continue;
		}
		const uint8 BitMask = static_cast<uint8>(1u << BitIdx);
		for (int32 CellIdx : Footprint)
		{
			if (!RefCounts.IsValidIndex(CellIdx)) continue;
			uint16& Count = RefCounts[CellIdx];
			if (Count > 0)
			{
				--Count;
				if (Count == 0 && Group.CellBitfield.IsValidIndex(CellIdx))
				{
					Group.CellBitfield[CellIdx] &= ~BitMask;
				}
			}
		}
		Footprint.Reset();
	}
}

bool USeinFogOfWarDefault::HasLineOfSightToCell(int32 X0, int32 Y0, int32 X1, int32 Y1,
	FFixedPoint EyeZ, FFixedPoint TargetZ, uint8 StampBitMask) const
{
	if (X0 == X1 && Y0 == Y1) return true;

	// Classic Bresenham — integer-native, symmetric, same walk from either
	// endpoint. Source cell is the starting point (not tested); target
	// cell is whatever we're trying to see (not tested — you can always
	// "see" the thing you're looking at, even if it's a wall). Opacity
	// test applies to every cell in between, filtered by the stamp's
	// layer + checked against the ray's interpolated Z at that cell.
	const int32 DXabs = FMath::Abs(X1 - X0);
	const int32 DYabs = FMath::Abs(Y1 - Y0);
	const int32 TotalSteps = FMath::Max(DXabs, DYabs); // ≥ 1 by the endpoint check above

	// Deterministic lampshade ray. Precompute one StepZ outside the loop
	// and accumulate per iteration: avoids an FFixedPoint mul + div on
	// the inner-loop hot path (replaces them with a single add). Result
	// at step i is EyeZ + StepZ·i, same as EyeZ + ΔZ·i/N to within FFixedPoint
	// quantum. Deterministic across clients because every client does the
	// same sequence of integer adds in the same order.
	const FFixedPoint StepZ = (TargetZ - EyeZ) / FFixedPoint::FromInt(TotalSteps);
	FFixedPoint RayZ = EyeZ;

	const int32 DX =  DXabs;
	const int32 DY = -DYabs;
	const int32 SXStep = (X0 < X1) ? 1 : -1;
	const int32 SYStep = (Y0 < Y1) ? 1 : -1;
	int32 Err = DX + DY;
	int32 X = X0;
	int32 Y = Y0;

	while (true)
	{
		const int32 E2 = 2 * Err;
		if (E2 >= DY) { Err += DY; X += SXStep; }
		if (E2 <= DX) { Err += DX; Y += SYStep; }
		RayZ += StepZ;

		if (X == X1 && Y == Y1) return true;
		if (IsCellOpaqueToEye(X, Y, RayZ, StampBitMask)) return false;
	}
}

bool USeinFogOfWarDefault::IsCellOpaqueToEye(int32 X, int32 Y, FFixedPoint EyeZ, uint8 StampBitMask) const
{
	if (!IsValidCoord(X, Y)) return false;
	const int32 Idx = CellIndex(X, Y);
	const FFixedPoint GroundZ  = GroundHeight.IsValidIndex(Idx)  ? GroundHeight[Idx]  : FFixedPoint::Zero;

	// Terrain (ground) always occludes — hills block line of sight
	// regardless of which layer you're stamping. Height is height.
	if (GroundZ > EyeZ) return true;

	// Static blocker (from bake) — only occludes this layer if the
	// blocker's LayerMask covers the stamping bit.
	const FFixedPoint StaticZ = BlockerHeight.IsValidIndex(Idx) ? BlockerHeight[Idx] : FFixedPoint::Zero;
	if (StaticZ > EyeZ)
	{
		const uint8 Mask = BlockerLayerMask.IsValidIndex(Idx) ? BlockerLayerMask[Idx] : 0;
		if ((Mask & StampBitMask) != 0) return true;
	}

	// Dynamic blocker (runtime — smoke grenades, destructibles mid-anim)
	// tests independently. A short-but-layered dynamic blocker doesn't
	// inherit a tall static's reach, and vice versa.
	const uint8 DynMask = DynamicBlockerLayerMask.IsValidIndex(Idx) ? DynamicBlockerLayerMask[Idx] : 0;
	const FFixedPoint DynZ = GetDynamicBlockerTopForMask(
		Idx, StampBitMask);

	// Per-cell LOS-vs-dynamic-blocker trace. Fires once per cell-walked-by-
	// LOS-into-a-stamped-blocker — extremely high volume on busy frames, so
	// gated at VeryVerbose (UE_LOG short-circuits below the configured
	// verbosity, so the printf work doesn't run when it's off). Enable with
	// `Log LogSeinFogOfWar VeryVerbose` when diagnosing "smoke isn't
	// blocking" or "LOS sees through walls" regressions; the line shows
	// DynZ vs EyeZ (the lampshade ray's Z at this cell), the mask
	// intersection, and the final pass/block outcome. Use the per-stamp +
	// per-source Verbose logs (above + in UpdateSourceStamp) for a quieter
	// picture of what's being written.
	if (DynMask != 0 || DynZ > FFixedPoint::Zero)
	{
		const bool bHeightBlocks = (DynZ > EyeZ);
		const bool bMaskMatches = ((DynMask & StampBitMask) != 0);
		const bool bWouldBlock = bHeightBlocks && bMaskMatches;
		UE_LOG(LogSeinFogOfWar, VeryVerbose,
			TEXT("LOS hit dyn blocker @ (%d,%d) Idx=%d | DynZ=%lld EyeZ=%lld (Hgt>%s) | "
				 "DynMask=0x%02X StampBit=0x%02X (Mask&%s) | GroundZ=%lld | %s"),
			X, Y, Idx,
			DynZ.Value, EyeZ.Value, bHeightBlocks ? TEXT("Eye") : TEXT("Eye? NO"),
			DynMask, StampBitMask, bMaskMatches ? TEXT("OK") : TEXT("MISS"),
			GroundZ.Value,
			bWouldBlock ? TEXT("=> BLOCKED") : TEXT("=> PASSED-THROUGH"));
	}

	if ((DynMask & StampBitMask) != 0 && DynZ > EyeZ)
	{
		return true;
	}

	return false;
}

FFixedPoint USeinFogOfWarDefault::GetDynamicBlockerTopForMask(
	int32 CellIdx,
	uint8 StampBitMask) const
{
	if (!DynamicBlockerLayerMask.IsValidIndex(CellIdx)
		|| !DynamicBlockerHeight.IsValidIndex(CellIdx))
	{
		return FFixedPoint::Zero;
	}

	const uint8 MatchingMask = static_cast<uint8>(
		DynamicBlockerLayerMask[CellIdx]
		& StampBitMask
		& SEIN_FOW_MASK_VISIBLE);
	if (MatchingMask == 0)
	{
		return FFixedPoint::Zero;
	}

	const FSeinFogDynamicBlockerLayerHeights* Exact =
		DynamicBlockerHeightExceptions.Find(CellIdx);
	if (!Exact)
	{
		return DynamicBlockerHeight[CellIdx];
	}

	bool bFound = false;
	FFixedPoint Highest = FFixedPoint::Zero;
	for (uint8 Bit = 1; Bit <= 7; ++Bit)
	{
		const uint8 BitMask = static_cast<uint8>(1u << Bit);
		if ((MatchingMask & BitMask) == 0)
		{
			continue;
		}
		const FFixedPoint TopZ = Exact->LayerTopZ[Bit];
		if (!bFound || TopZ > Highest)
		{
			Highest = TopZ;
			bFound = true;
		}
	}
	return bFound ? Highest : FFixedPoint::Zero;
}

bool USeinFogOfWarDefault::RebuildDynamicBlockers(UWorld* World)
{
	const int32 NumCells = Width * Height;
	const bool bSizeMismatch =
		DynamicBlockerHeight.Num() != NumCells ||
		DynamicBlockerLayerMask.Num() != NumCells;

	// Gather the complete rasterization input before touching the current
	// overlay. Entity-pool and Shapes-array iteration provide canonical order,
	// making exact array equality a collision-free change detector.
	TArray<FSeinFogDynamicBlockerSnapshot> CurrentSnapshots;
	CurrentSnapshots.Reserve(DynamicBlockerSnapshots.Num());
	if (World)
	{
		if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
		{
			const ISeinComponentStorage* Storage =
				Sim->GetComponentStorageRaw(FSeinExtentsPayload::StaticStruct());
			if (Storage)
			{
				Sim->GetEntityPool().ForEachEntity(
					[Storage, &CurrentSnapshots](FSeinEntityHandle Handle, const FSeinEntity& Entity)
					{
						const void* Raw = Storage->GetComponentRaw(Handle);
						if (!Raw) return;
						const FSeinExtentsPayload* Extents =
							static_cast<const FSeinExtentsPayload*>(Raw);
						const uint8 LayerMask = static_cast<uint8>(
							Extents->BlockedFogOfWarLayerMask
							& SEIN_FOW_MASK_VISIBLE);
						if (!Extents->bBlocksFogOfWar
							|| LayerMask == 0
							|| Extents->Shapes.IsEmpty())
						{
							return;
						}

						const FFixedVector Pos = Entity.Transform.GetLocation();
						const FFixedQuaternion Rot = Entity.Transform.Rotation;
						for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
						{
							if (ExtShape.Height <= FFixedPoint::Zero) continue;
							FSeinFogDynamicBlockerSnapshot& Snapshot =
								CurrentSnapshots.AddDefaulted_GetRef();
							Snapshot.WorldPos = Pos;
							Snapshot.WorldPos.Z += ExtShape.LocalOffset.Z;
							Snapshot.Rotation = Rot;
							Snapshot.Shape = ExtShape.AsStampShape();
							Snapshot.Height = ExtShape.Height;
							Snapshot.LayerMask = LayerMask;
						}
					});
			}
			else
			{
				UE_LOG(LogSeinFogOfWar, Verbose,
					TEXT("RebuildDynamicBlockers: no FSeinExtentsPayload storage registered "
						 "— no entity with USeinExtentsComponent has been spawned via SpawnEntity"));
			}
		}
	}

	if (!bSizeMismatch && CurrentSnapshots == DynamicBlockerSnapshots)
	{
		return false;
	}

	// Dirty-clear only when inputs changed. A size mismatch means the old
	// dirty indices belong to another grid and must not be dereferenced.
	const int32 NumDirtyCells = LastDynamicBlockerCells.Num();
	if (bSizeMismatch)
	{
		// Either array may already have NumCells elements; clear both explicitly
		// because SetNumZeroed would retain same-sized contents.
		ResetToZeroedSize(DynamicBlockerHeight, NumCells);
		ResetToZeroedSize(DynamicBlockerLayerMask, NumCells);
	}
	else
	{
		for (const int32 Idx : LastDynamicBlockerCells)
		{
			if (DynamicBlockerHeight.IsValidIndex(Idx))
			{
				DynamicBlockerHeight[Idx] = FFixedPoint::Zero;
				DynamicBlockerLayerMask[Idx] = 0;
			}
		}
	}
	LastDynamicBlockerCells.Reset();
	DynamicBlockerHeightExceptions.Reset();

	for (const FSeinFogDynamicBlockerSnapshot& Snapshot : CurrentSnapshots)
	{
		StampDynamicBlockerShape(Snapshot.Shape, Snapshot.WorldPos,
			Snapshot.Rotation, Snapshot.Height, Snapshot.LayerMask);
	}
	DynamicBlockerSnapshots = MoveTemp(CurrentSnapshots);

	UE_LOG(LogSeinFogOfWar, Verbose,
		TEXT("RebuildDynamicBlockers: exact inputs changed; cleared %d old cell(s), stamped %d shape(s)"),
		NumDirtyCells, DynamicBlockerSnapshots.Num());

	return true;
}

void USeinFogOfWarDefault::StampDynamicBlockerShape(const FSeinStampShape& Shape,
	const FFixedVector& EntityWorldPos, const FFixedQuaternion& EntityRotation,
	FFixedPoint VerticalExtent, uint8 LayerMask)
{
	if (VerticalExtent <= FFixedPoint::Zero) return;
	LayerMask = static_cast<uint8>(LayerMask & SEIN_FOW_MASK_VISIBLE);
	if (LayerMask == 0) return;

	// Per-stamp trace. Height + Mask + shape geometry one line per call,
	// useful when reasoning about what the dynamic-blocker grid actually
	// holds vs what designers intended in the spawning ability/asset.
	// Free when LogSeinFogOfWar is below Verbose.
	UE_LOG(LogSeinFogOfWar, Verbose,
		TEXT("StampDynamicBlockerShape: pos=(%lld,%lld,%lld) Height=%lld Mask=0x%02X "
			 "Shape=%d Radius=%lld HalfX=%lld HalfY=%lld"),
		EntityWorldPos.X.Value, EntityWorldPos.Y.Value, EntityWorldPos.Z.Value,
		VerticalExtent.Value, LayerMask,
		static_cast<int32>(Shape.Shape),
		Shape.Radius.Value, Shape.HalfExtentX.Value, Shape.HalfExtentY.Value);

	// Loud guard against the "Height = 1cm so smoke doesn't block anything"
	// class of bug (the 2026-05-02 regression that motivated this log).
	// The lampshade test rejects any blocker whose top is below the source's
	// eye Z at the cell — so a 1cm-tall smoke disc against an infantry-height
	// (180cm) observer fails to occlude even adjacent cells. 50cm is below
	// the threshold a designer would plausibly intend (prone infantry ~80,
	// standing ~180, tank ~150, building ~300 per FSeinExtentsShape::Height
	// docs). Warning fires per-stamp until the asset is fixed; verbose-spam
	// is the point — it's pointing at a real bug.
	UE_CLOG(VerticalExtent < FFixedPoint::FromInt(50),
		LogSeinFogOfWar, Warning,
		TEXT("StampDynamicBlockerShape: Height=%.1fcm is suspiciously short — "
			 "the lampshade LOS test will let typical (180cm eye) sources see "
			 "right over this blocker. Check FSeinExtentsShape::Height on the "
			 "spawning ability or actor (typical: smoke ~300-400, low cover "
			 "~150, prone-infantry ~80)."),
		VerticalExtent.ToFloat());

	// Lazily resize the dynamic-blocker overlay to grid extent if the
	// caller forgot to (defensive — RebuildDynamicBlockers handles the
	// clear, but a fresh load with no clear yet would otherwise crash on
	// the IsValidIndex below).
	const int32 NumCells = Width * Height;
	if (DynamicBlockerHeight.Num() != NumCells
		|| DynamicBlockerLayerMask.Num() != NumCells)
	{
		ResetToZeroedSize(DynamicBlockerHeight, NumCells);
		ResetToZeroedSize(DynamicBlockerLayerMask, NumCells);
		DynamicBlockerHeightExceptions.Reset();
		LastDynamicBlockerCells.Reset();
	}

	// A dynamic extents shape is a world-space vertical volume. WorldPos.Z
	// already includes the authored LocalOffset.Z; every covered planar cell
	// therefore receives the same absolute top instead of incorrectly growing
	// upward from whatever baked ground happens to be under that cell.
	const FFixedPoint TopZ = EntityWorldPos.Z + VerticalExtent;

	// Walk the shape's covered cells. Dense arrays retain the union mask and
	// overall maximum height. When overlaps give different layers different
	// tops, a sparse exception stores the seven exact per-layer values. The
	// common all-layer/same-height path never allocates an exception.
	//
	// Dirty-rect bookkeeping: append each cell to LastDynamicBlockerCells
	// the first time it gets written this tick (detected via pre-write
	// LayerMask == 0, which is the cleared-state sentinel). Subsequent
	// overlapping writes to the same cell skip the append, so the list
	// holds unique indices per tick — read back next tick by
	// RebuildDynamicBlockers's dirty-rect clear.
	SeinStampUtils::ForEachCoveredCell(
		Shape, EntityWorldPos, EntityRotation,
		CellSize, Origin, Width, Height,
		[&](int32 X, int32 Y)
		{
			const int32 Idx = CellIndex(X, Y);
			const uint8 ExistingMask = DynamicBlockerLayerMask[Idx];
			const bool bFirstWriteThisTick = ExistingMask == 0;

			if (bFirstWriteThisTick)
			{
				DynamicBlockerHeight[Idx] = TopZ;
				DynamicBlockerLayerMask[Idx] = LayerMask;
			}
			else
			{
				const FFixedPoint ExistingMax =
					DynamicBlockerHeight[Idx];
				const uint8 CombinedMask = static_cast<uint8>(
					ExistingMask | LayerMask);
				FSeinFogDynamicBlockerLayerHeights* ExistingExact =
					DynamicBlockerHeightExceptions.Find(Idx);

				// Equal-height contributions collapse into the dense fast path
				// unless this cell was already ambiguous for another layer.
				if (!ExistingExact && TopZ == ExistingMax)
				{
					DynamicBlockerLayerMask[Idx] = CombinedMask;
				}
				else
				{
					FSeinFogDynamicBlockerLayerHeights Exact;
					if (ExistingExact)
					{
						Exact = *ExistingExact;
					}
					else
					{
						for (uint8 Bit = 1; Bit <= 7; ++Bit)
						{
							const uint8 BitMask =
								static_cast<uint8>(1u << Bit);
							if ((ExistingMask & BitMask) != 0)
							{
								Exact.LayerTopZ[Bit] = ExistingMax;
							}
						}
					}

					for (uint8 Bit = 1; Bit <= 7; ++Bit)
					{
						const uint8 BitMask =
							static_cast<uint8>(1u << Bit);
						if ((LayerMask & BitMask) == 0)
						{
							continue;
						}
						if ((ExistingMask & BitMask) == 0
							|| TopZ > Exact.LayerTopZ[Bit])
						{
							Exact.LayerTopZ[Bit] = TopZ;
						}
					}

					const FFixedPoint CombinedMax =
						TopZ > ExistingMax ? TopZ : ExistingMax;
					DynamicBlockerHeight[Idx] = CombinedMax;
					DynamicBlockerLayerMask[Idx] = CombinedMask;

					bool bAllLayersShareMax = true;
					for (uint8 Bit = 1; Bit <= 7; ++Bit)
					{
						const uint8 BitMask =
							static_cast<uint8>(1u << Bit);
						if ((CombinedMask & BitMask) != 0
							&& Exact.LayerTopZ[Bit] != CombinedMax)
						{
							bAllLayersShareMax = false;
							break;
						}
					}
					if (bAllLayersShareMax)
					{
						DynamicBlockerHeightExceptions.Remove(Idx);
					}
					else
					{
						DynamicBlockerHeightExceptions.Add(
							Idx, MoveTemp(Exact));
					}
				}
			}

			if (bFirstWriteThisTick)
			{
				LastDynamicBlockerCells.Add(Idx);
			}
		});
}

uint8 USeinFogOfWarDefault::GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const
{
	int32 CX, CY;
	if (!WorldToGrid(WorldPos, CX, CY)) return 0;
	const FSeinFogVisionGroup* Group = VisionGroups.Find(Observer);
	if (!Group) return 0;
	const int32 Idx = CellIndex(CX, CY);
	return Group->CellBitfield.IsValidIndex(Idx) ? Group->CellBitfield[Idx] : 0;
}

bool USeinFogOfWarDefault::GetObserverGrid(FSeinPlayerID Observer, TArray<uint8>& OutCells,
	FFixedVector& OutOrigin, FFixedPoint& OutCellSize, int32& OutWidth, int32& OutHeight) const
{
	if (Width <= 0 || Height <= 0) return false;

	OutWidth    = Width;
	OutHeight   = Height;
	OutOrigin   = Origin;
	OutCellSize = CellSize;

	const int32 Num = Width * Height;
	if (const FSeinFogVisionGroup* Group = VisionGroups.Find(Observer);
		Group && Group->CellBitfield.Num() == Num)
	{
		OutCells = Group->CellBitfield;          // copy the observer's field
	}
	else
	{
		// Observer has seen nothing yet (no group) — present an all-unexplored
		// field at the correct dims so the renderer paints full fog rather than
		// erroring out.
		OutCells.Reset(Num);
		OutCells.AddZeroed(Num);
	}
	return true;
}

uint8 USeinFogOfWarDefault::GetEntityVisibleBits(FSeinPlayerID Observer,
	USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const
{
	const FSeinEntity* Entity = Sim.GetEntity(Target);
	if (!Entity) return 0;

	const FFixedVector EntityPos = Entity->Transform.GetLocation();
	const FFixedQuaternion EntityRot = Entity->Transform.Rotation;

	// Single-point query at center, used both as the fallback for
	// extents-less entities and as the seed for volumetric OR (covers the
	// entity's exact center cell even if no stamp's footprint hits it
	// exactly — degenerate cases like zero-sized stamps).
	uint8 Bits = GetCellBitfield(Observer, EntityPos);

	const FSeinExtentsPayload* Extents = Sim.GetComponent<FSeinExtentsPayload>(Target);
	if (!Extents || Extents->Shapes.Num() == 0)
	{
		return Bits;
	}

	const FSeinFogVisionGroup* Group = VisionGroups.Find(Observer);
	if (!Group || Group->CellBitfield.Num() == 0) return Bits;

	// OR every cell each shape covers. AsStampShape() converts the volumetric
	// primitive to its planar equivalent (Box→Rect, Capsule→Radial) so we
	// reuse SeinStampUtils for the actual cell rasterization. Once Bits hits
	// every layer (0xFF), early-out — further iteration can't add anything.
	for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
	{
		if (Bits == 0xFF) break;

		const FSeinStampShape PlanarStamp = ExtShape.AsStampShape();
		SeinStampUtils::ForEachCoveredCell(
			PlanarStamp, EntityPos, EntityRot,
			CellSize, Origin, Width, Height,
			[&](int32 X, int32 Y)
			{
				const int32 Idx = CellIndex(X, Y);
				if (Group->CellBitfield.IsValidIndex(Idx))
				{
					Bits |= Group->CellBitfield[Idx];
				}
			});
	}

	return Bits;
}

bool USeinFogOfWarDefault::HasObserverSeenEntity(FSeinPlayerID Observer,
	FSeinEntityHandle Target) const
{
	const FSeinFogVisionGroup* Group = VisionGroups.Find(Observer);
	return Group && Group->SeenEntities.Contains(Target);
}

void USeinFogOfWarDefault::UpdateSeenLatches(USeinWorldSubsystem& Sim)
{
	// Nothing to latch into until at least one observer has a vision group.
	if (VisionGroups.Num() == 0) return;

	// Only entities authored VisibleOnceSeen participate. Walk that component's
	// storage directly so the common case (a world of VisionLayersOnly units /
	// VisibleOnceExplored buildings) pays a single count check and bails.
	const ISeinComponentStorage* Storage =
		Sim.GetComponentStorageRaw(FSeinFogVisibilityPayload::StaticStruct());
	if (!Storage || Storage->GetComponentCount() == 0) return;

	Sim.GetEntityPool().ForEachEntity(
		[this, &Sim, Storage](FSeinEntityHandle Handle, const FSeinEntity& /*Entity*/)
		{
			const void* Raw = Storage->GetComponentRaw(Handle);
			if (!Raw) return;
			const FSeinFogVisibilityPayload* FogVis =
				static_cast<const FSeinFogVisibilityPayload*>(Raw);
			if (FogVis->FogVisibilityPolicy != ESeinFogVisibilityPolicy::VisibleOnceSeen) return;

			// "Live spotting" mask = the entity's emission bits restricted to
			// actual visibility layers (Explored excluded — the whole point of
			// VisibleOnceSeen is that terrain-scouting must NOT count). A zero
			// mask can never be spotted live, so it can never latch.
			const uint8 LiveMask = static_cast<uint8>(FogVis->FogVisibilityLayerMask & SEIN_FOW_MASK_VISIBLE);
			if (LiveMask == 0) return;

			// Latch into every group whose live bits currently cover the
			// entity's footprint. Already-latched groups skip the (volumetric)
			// bit query. The per-group result is independent of TMap iteration
			// order, so determinism holds; we only mutate an existing group's
			// set here (no structural change to VisionGroups), so iterating it
			// is safe.
			for (TPair<FSeinPlayerID, FSeinFogVisionGroup>& Pair : VisionGroups)
			{
				FSeinFogVisionGroup& Group = Pair.Value;
				if (Group.SeenEntities.Contains(Handle)) continue;
				const uint8 Bits = GetEntityVisibleBits(Pair.Key, Sim, Handle);
				if ((Bits & LiveMask) != 0)
				{
					Group.SeenEntities.Add(Handle);
					MarkRoutineSeenEntityDirty(Pair.Key, Handle);
				}
			}
		});
}

void USeinFogOfWarDefault::CollectDebugCellQuads(FSeinPlayerID Observer,
	int32 VisibleBitIndex,
	TArray<FVector>& OutCenters,
	TArray<FColor>& OutColors,
	float& OutHalfExtent) const
{
	if (Width <= 0 || Height <= 0) return;

	// 0.9 inset → ~10% gap between neighboring quads. Reads as a grid (matches
	// USeinNavigationAStar::CollectDebugCellQuads). Also dodges any z-fight at
	// exact cell boundaries between same-Z cells.
	OutHalfExtent = CellSize.ToFloat() * 0.5f * 0.9f;
	const int32 NumCells = Width * Height;
	OutCenters.Reserve(NumCells);
	OutColors.Reserve(NumCells);

	const float OriginXF = Origin.X.ToFloat();
	const float OriginYF = Origin.Y.ToFloat();
	const float CellSizeF = CellSize.ToFloat();
	const float HalfCellF = CellSizeF * 0.5f;

	// Per-observer bitfield lookup. If the observer has never stamped,
	// there's no group — every cell renders as baseline (non-PIE / editor
	// preview / unknown observer).
	const FSeinFogVisionGroup* ObserverGroup = VisionGroups.Find(Observer);
	const TArray<uint8>* ObserverBits = ObserverGroup ? &ObserverGroup->CellBitfield : nullptr;

	// Clamp bit index to valid EVNNNNNN range; anything out-of-range falls
	// back to the V bit (1).
	if (VisibleBitIndex < 0 || VisibleBitIndex > 7) VisibleBitIndex = 1;
	const uint8 VisibleMask = static_cast<uint8>(1u << VisibleBitIndex);

	// Paint colors. "Visible" color comes from the module helper (yellow for
	// E, cyan for V, plugin-settings DebugColor for N0..N5). Blocker red and
	// baseline black are framework-fixed — keep them readable across any
	// layer perspective.
	const FColor VisibleColor = UE::SeinARTSFogOfWar::GetDebugLayerColor(VisibleBitIndex);
	const FColor Red  (200, 0, 0);
	const FColor Black(0, 0, 0);

	int32 NumDynamicBlockerCells = 0;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = CellIndex(X, Y);
			const uint8 Bits = (ObserverBits && ObserverBits->IsValidIndex(Idx)) ? (*ObserverBits)[Idx] : 0;
			const bool bVisible = (Bits & VisibleMask) != 0;

			// Static + dynamic blocker masks per the viewed layer. Treated
			// asymmetrically below: dynamic blockers always show red (smoke
			// grenades / destructibles need unambiguous viz so designers
			// can see the stamp footprint regardless of LOS state), while
			// static blockers keep the legacy "visible wins" behavior so a
			// walkable rooftop renders blue when a unit stands on it.
			const uint8 StaticMask = BlockerLayerMask.IsValidIndex(Idx) ? BlockerLayerMask[Idx] : 0;
			const uint8 DynMask    = DynamicBlockerLayerMask.IsValidIndex(Idx) ? DynamicBlockerLayerMask[Idx] : 0;
			const bool bHasDynamicBlocker = (DynMask    & VisibleMask) != 0;
			const bool bHasStaticBlocker  = (StaticMask & VisibleMask) != 0;

			const FFixedPoint GroundZ  = GroundHeight.IsValidIndex(Idx) ? GroundHeight[Idx] : Origin.Z;
			const FFixedPoint StaticZ  = (StaticMask != 0 && BlockerHeight.IsValidIndex(Idx)) ? BlockerHeight[Idx] : GroundZ;
			const FFixedPoint DynZ = bHasDynamicBlocker
				? GetDynamicBlockerTopForMask(Idx, VisibleMask)
				: GroundZ;
			// Render at the tallest occluder so smoke + wall at the same
			// cell draws at the smoke top if smoke is taller (which is
			// visually correct — it's what the unit sees).
			const FFixedPoint BlockZ   = (DynZ > StaticZ) ? DynZ : StaticZ;
			const FFixedPoint TopZ     = (BlockZ > GroundZ) ? BlockZ : GroundZ;

			// Priority:
			//   1. Dynamic blocker (smoke grenades, destructibles) — always
			//      red. LOS rays reaching a smoke cell as their target would
			//      otherwise mark it "visible" and hide the stamp; designers
			//      need the footprint visible at all times.
			//   2. Visible to observer — cyan/layer-color. A unit standing
			//      on a roof cell still reads as visible here (eye Z over
			//      blocker top is the elevated-LOS case).
			//   3. Static blocker — red, only when not visible (legacy
			//      behavior, preserves rooftop-walkable viz).
			//   4. Baseline black.
			FColor Color;
			FFixedPoint WZFP;
			if (bHasDynamicBlocker)
			{
				Color = Red;
				// Render at ground rather than at the blocker top.
				// Tall smoke (~400cm) at DynZ would float a quad 400cm above
				// the surrounding grid; flush-with-grid reads cleaner and the
				// blocker height still drives shadowcast correctly.
				WZFP = GroundZ;
				++NumDynamicBlockerCells;
			}
			else if (bVisible)
			{
				Color = VisibleColor;
				WZFP = TopZ;
			}
			else if (bHasStaticBlocker)
			{
				Color = Red;
				WZFP = StaticZ;
			}
			else
			{
				Color = Black;
				WZFP = GroundZ;
			}

			const float WX = OriginXF + CellSizeF * X + HalfCellF;
			const float WY = OriginYF + CellSizeF * Y + HalfCellF;

			OutCenters.Emplace(WX, WY, WZFP.ToFloat() + SEIN_FOW_DEBUG_Z_OFFSET);
			OutColors.Add(Color);
		}
	}

	UE_LOG(LogSeinFogOfWar, Verbose,
		TEXT("CollectDebugCellQuads (ViewBit=%d): %d total cells, %d dynamic-blocker cells (red)"),
		VisibleBitIndex, Width * Height, NumDynamicBlockerCells);
}

bool USeinFogOfWarDefault::WorldToGrid(const FFixedVector& WorldPos, int32& OutX, int32& OutY) const
{
	if (Width <= 0 || Height <= 0 || CellSize <= FFixedPoint::Zero) return false;
	const FFixedPoint LocalX = WorldPos.X - Origin.X;
	const FFixedPoint LocalY = WorldPos.Y - Origin.Y;
	const FFixedPoint FX = LocalX / CellSize;
	const FFixedPoint FY = LocalY / CellSize;
	OutX = FX.ToInt();
	OutY = FY.ToInt();
	return IsValidCoord(OutX, OutY);
}
