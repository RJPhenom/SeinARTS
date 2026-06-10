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
#include "Components/SeinVisionComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Stamping/SeinStampShape.h"
#include "Stamping/SeinStampUtils.h"
#include "SeinFogOfWarTypes.h"
#include "SeinARTSFogOfWarModule.h"
#include "SeinARTSFogOfWarLog.h"

#include "SeinLevelData.h"
#include "Volumes/SeinLevelVolume.h"

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Core/SeinEntityPool.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Entity.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Math/Box.h"

// LogSeinFogOfWar is module-declared (SeinARTSFogOfWarLog.h) so it is reliably
// filterable in the Output Log — do not re-introduce a _STATIC define here.

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
		if (const USeinEntityComponent* Bridge = SeinActor->FindComponentByClass<USeinEntityComponent>())
		{
			if (const FSeinExtentsComponent* Extents = Bridge->FindAuthoredData<FSeinExtentsComponent>())
			{
				if (!Extents->bBakesIntoFogOfWar) bSkip = true;
			}
			if (!bSkip)
			{
				for (const FInstancedStruct& Entry : Bridge->ComponentData)
				{
					if (Entry.GetScriptStruct() == FSeinMovementComponent::StaticStruct())
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

bool USeinFogOfWarDefault::LoadFromSubstrate(const USeinLevelData& Substrate)
{
	if (!Substrate.HasRuntimeData()) return false;

	TArray<uint8> Blob;
	if (!Substrate.GetLayerChannel(TEXT("FogOfWar"), Blob)) return false;

	const int32 HeaderBytes = 2 * sizeof(int32) + 3 * sizeof(int64);
	if (Blob.Num() < HeaderBytes) return false;

	const uint8* In = Blob.GetData();
	auto ReadI32 = [&In]() { int32 V; FMemory::Memcpy(&V, In, sizeof(int32)); In += sizeof(int32); return V; };
	auto ReadI64 = [&In]() { int64 V; FMemory::Memcpy(&V, In, sizeof(int64)); In += sizeof(int64); return V; };
	const int32 W = ReadI32();
	const int32 H = ReadI32();
	const FFixedPoint CellSizeIn = FFixedPoint(ReadI64());
	const FFixedPoint MinH = FFixedPoint(ReadI64());
	const FFixedPoint Quantum = FFixedPoint(ReadI64());

	const int32 NumCells = W * H;
	if (NumCells <= 0 || Blob.Num() != HeaderBytes + 3 * NumCells)
	{
		UE_LOG(LogSeinFogOfWar, Warning,
			TEXT("LoadFromSubstrate: FogOfWar channel malformed (%d bytes for %dx%d) — ignoring (re-bake needed)."),
			Blob.Num(), W, H);
		return false;
	}

	Width = W;
	Height = H;
	CellSize = CellSizeIn;
	Origin = Substrate.GetOrigin();

	const uint8* GroundIn  = In;
	const uint8* BlockerIn = In + NumCells;
	const uint8* MaskIn    = In + 2 * NumCells;

	GroundHeight.SetNumUninitialized(NumCells);
	BlockerHeight.SetNumUninitialized(NumCells);
	BlockerLayerMask.SetNumUninitialized(NumCells);
	DynamicBlockerHeight.SetNumZeroed(NumCells);
	DynamicBlockerLayerMask.SetNumZeroed(NumCells);
	VisionGroups.Empty(); // per-player state recreates lazily on next stamp
	SourceStates.Empty();
	LastDynamicBlockerHash = 0;

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
	return true;
}

// ============================================================================
// Init (no bake fallback)
// ============================================================================

void USeinFogOfWarDefault::InitGridFromVolumes(UWorld* World)
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
		return;
	}
	if (bAnyUnbaked)
	{
		UE_LOG(LogSeinFogOfWar, Warning,
			TEXT("InitGridFromVolumes: one or more level volumes have stale "
				 "PlacedBounds (bBoundsBaked == false). Re-save the level to "
				 "bake snapshots. NOT cross-arch deterministic until then."));
	}

	CellSize = ResolvedCellSize;
	Origin = UnionMin;
	const float CellSizeF = ResolvedCellSize.ToFloat();
	const float SizeXF = (UnionMax.X - UnionMin.X).ToFloat();
	const float SizeYF = (UnionMax.Y - UnionMin.Y).ToFloat();
	Width = FMath::Max(1, FMath::CeilToInt(SizeXF / CellSizeF));
	Height = FMath::Max(1, FMath::CeilToInt(SizeYF / CellSizeF));

	const int32 NumCells = Width * Height;
	GroundHeight.SetNumUninitialized(NumCells);
	BlockerHeight.SetNumZeroed(NumCells);
	BlockerLayerMask.SetNumZeroed(NumCells);
	DynamicBlockerHeight.SetNumZeroed(NumCells);
	DynamicBlockerLayerMask.SetNumZeroed(NumCells);
	VisionGroups.Empty();
	SourceStates.Empty();
	LastDynamicBlockerHash = 0;

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
	FSeinFogVisionGroup& Group = VisionGroups.FindOrAdd(PlayerID);
	const int32 NumCells = Width * Height;
	if (Group.CellBitfield.Num() != NumCells)
	{
		Group.CellBitfield.SetNumZeroed(NumCells);
	}
	return Group;
}

void USeinFogOfWarDefault::TickStamps(UWorld* World)
{
	if (Width <= 0 || Height <= 0 || !World) return;

	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	const ISeinComponentStorage* Storage = Sim->GetComponentStorageRaw(FSeinVisionComponent::StaticStruct());
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
		Sim->GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
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
	// their discs into DynamicBlockerHeight + DynamicBlockerLayerMask here,
	// so the vision passes below see the freshest occlusion state.
	const bool bDynamicBlockersChanged = RebuildDynamicBlockers(World);

	// If dynamic blockers changed, every source's LOS may have shifted —
	// flip bValid=false on every source so UpdateSourceStamp's stable-
	// fast-path returns early and the diff path runs. We DON'T eagerly
	// tear down old footprints here (as we used to) because the per-bit
	// diff in UpdateSourceStamp computes the minimal old→new delta when
	// it runs — for sources whose LOS results don't actually change
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

	// Walk live entities. Each call is O(few-compares) on the stable path
	// — only sources whose inputs differ from last tick pay the full
	// remove-and-restamp cost. Accumulate the change flag from each call so
	// we can suppress the OnFogOfWarMutated broadcast on idle ticks.
	TSet<FSeinEntityHandle> AliveSources;
	AliveSources.Reserve(SourceStates.Num());
	bool bAnySourceChanged = false;

	Sim->GetEntityPool().ForEachEntity(
		[this, Sim, Storage, &AliveSources, &bAnySourceChanged](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const void* Raw = Storage->GetComponentRaw(Handle);
			if (!Raw) return;
			const FSeinVisionComponent* VData = static_cast<const FSeinVisionComponent*>(Raw);
			if (!VData) return;

			AliveSources.Add(Handle);
			const FSeinPlayerID OwnerPlayer = Sim->GetEntityOwner(Handle);
			if (UpdateSourceStamp(Handle, *VData,
				Entity.Transform.GetLocation(), Entity.Transform.Rotation, OwnerPlayer))
			{
				bAnySourceChanged = true;
			}
		});

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

namespace
{
	/** Hash a vision source's stamp set. Folds shape geometry + layer mask
	 *  per stamp; XOR-combine across stamps so iteration order is irrelevant
	 *  (matches the existing dynamic-blocker fingerprint pattern). Used by
	 *  UpdateSourceStamp's stable-fast-path compare. */
	uint32 HashVisionStamps(const TArray<FSeinVisionStamp>& Stamps)
	{
		uint32 H = 0;
		for (const FSeinVisionStamp& S : Stamps)
		{
			H ^= GetTypeHash(S.Shape);
			H ^= static_cast<uint32>(S.LayerMask);
		}
		return H;
	}
}

bool USeinFogOfWarDefault::UpdateSourceStamp(FSeinEntityHandle Handle,
	const FSeinVisionComponent& VData, const FFixedVector& WorldPos,
	const FFixedQuaternion& Rotation, FSeinPlayerID Owner)
{
	FSeinFogSourceState& State = SourceStates.FindOrAdd(Handle);

	const uint32 NewStampsHash = HashVisionStamps(VData.VisionStamps);

	// Stable-source fast path. Identical pose + stamp set ⇒ no work. Pose
	// includes Rotation since shaped stamps depend on it (rect axes, cone
	// direction). Stamps hash folds shape geometry + per-stamp layer mask
	// + bEnabled flags, so toggling a garrison-cone on or off invalidates
	// the cache as expected. Returns false: nothing changed, caller
	// doesn't need to broadcast OnFogOfWarMutated for this source.
	if (State.bValid
		&& State.Owner == Owner
		&& State.WorldPos == WorldPos
		&& State.Rotation == Rotation
		&& State.EyeHeight == VData.EyeHeight
		&& State.StampsHash == NewStampsHash)
	{
		return false;
	}

	// Owner transfer: footprints live in per-player groups, so we can't
	// diff across groups. Decrement everything from the old group (which
	// resets State.Footprints[Bit] arrays in place), then the per-bit diff
	// below runs against the now-empty old set into the new group. Not
	// gated on bValid — forced-invalidation paths (e.g. dynamic blockers
	// changed) leave Footprints populated even with bValid=false, and a
	// same-tick owner change would still need them migrated. The happy
	// path (Owner unchanged) skips the find+iter entirely.
	if (State.Owner != Owner)
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

	FSeinFogVisionGroup& NewGroup = GetOrCreateGroup(Owner);

	// Per-bit diff. For each layer-bit (1..7), union the current tick's
	// stamp contributions across all stamps of this source emitting on
	// that bit, sort, then apply the minimal refcount/bitfield delta vs
	// last tick's stored footprint via ApplyFootprintDiff. The pre-diff
	// path was "decrement all old + increment all new" per cell; the diff
	// path skips the matched-cells case entirely, which is the dominant
	// case for typical movement (unit shifts one cell, footprint mostly
	// overlaps).
	//
	// Bit 0 (Explored) is sticky and never participates in the per-bit
	// mask machinery — Increment in ApplyFootprintDiff sets it on every
	// newly-visible cell, and we never clear it.
	//
	// Per-bit scratch buckets reused across iterations via TArray::Reset()
	// (preserves capacity). A unit emitting on only bit 1 (typical) does
	// 1 allocation across all 7 bit passes.
	TArray<int32> NewCells;
	for (uint8 Bit = 1; Bit <= 7; ++Bit)
	{
		const uint8 BitMask = static_cast<uint8>(1u << Bit);
		NewCells.Reset();
		for (const FSeinVisionStamp& VStamp : VData.VisionStamps)
		{
			if (!VStamp.Shape.bEnabled) continue;
			if (VStamp.LayerMask == 0) continue;
			if ((VStamp.LayerMask & BitMask) == 0) continue;
			GenerateLayerFootprintCells(VStamp.Shape, WorldPos, Rotation,
				VData.EyeHeight, Bit, NewCells);
		}
		NewCells.Sort();
		ApplyFootprintDiff(NewGroup, Bit, State.Footprints[Bit], NewCells);
		State.Footprints[Bit] = NewCells;  // commit (sorted) for next tick's diff
	}

	// Commit cache key for next tick's compare.
	State.bValid = true;
	State.Owner = Owner;
	State.WorldPos = WorldPos;
	State.Rotation = Rotation;
	State.EyeHeight = VData.EyeHeight;
	State.StampsHash = NewStampsHash;

	// DIAGNOSTIC (2026-05-02 smoke-not-blocking regression): one line per
	// source per re-stamp. EyeZ here is the same value passed to LOS — if
	// it's much higher than the smoke's `TopZ` (logged in StampDynamicBlockerShape)
	// the lampshade test rejects. Free when LogSeinFogOfWar is
	// below Verbose.
	const FFixedPoint EyeZForLog = WorldPos.Z + VData.EyeHeight;
	UE_LOG(LogSeinFogOfWar, Verbose,
		TEXT("UpdateSourceStamp: Handle=%d WorldPos.Z=%lld EyeHeight=%lld EyeZ=%lld "
			 "Stamps=%d Owner=%u"),
		Handle.Index, WorldPos.Z.Value, VData.EyeHeight.Value, EyeZForLog.Value,
		VData.VisionStamps.Num(), Owner.Value);

	return true;
}

void USeinFogOfWarDefault::RemoveSourceStamp(FSeinEntityHandle Handle)
{
	FSeinFogSourceState* State = SourceStates.Find(Handle);
	if (!State) return;
	if (State->bValid)
	{
		if (FSeinFogVisionGroup* Group = VisionGroups.Find(State->Owner))
		{
			DecrementFootprintsForState(*State, *Group);
		}
	}
	SourceStates.Remove(Handle);
}

void USeinFogOfWarDefault::GenerateLayerFootprintCells(
	const FSeinStampShape& Shape,
	const FFixedVector& EntityWorldPos,
	const FFixedQuaternion& EntityRotation,
	FFixedPoint EyeHeight, uint8 StampBit,
	TArray<int32>& OutCells)
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

void USeinFogOfWarDefault::ApplyFootprintDiff(
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
			Group.CellBitfield[CellIdx] |= SEIN_FOW_BIT_EXPLORED;
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
	const FFixedPoint DynZ = DynamicBlockerHeight.IsValidIndex(Idx) ? DynamicBlockerHeight[Idx] : FFixedPoint::Zero;
	const uint8 DynMask = DynamicBlockerLayerMask.IsValidIndex(Idx) ? DynamicBlockerLayerMask[Idx] : 0;

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

	if (DynZ > EyeZ)
	{
		if ((DynMask & StampBitMask) != 0) return true;
	}

	return false;
}

bool USeinFogOfWarDefault::RebuildDynamicBlockers(UWorld* World)
{
	// Dirty-rect clear: zero only cells that were stamped last tick instead
	// of memzeroing the entire W*H arrays. At 1km² @ 400cm cells the full
	// memzero costs ~50MB per FoW tick; in practice dynamic blockers (smoke
	// grenades, destructibles) cover orders-of-magnitude fewer cells, so
	// the dirty list is tiny by comparison.
	//
	// Grid-resize fallback: if either array's size disagrees with W*H the
	// dirty list's indices are stale (or arrays haven't been sized yet on
	// first call). SetNumZeroed handles both — it grows + zero-fills, or
	// truncates + zero-fills the kept range. Either way the dirty list is
	// reset since its indices no longer describe the new grid.
	const int32 NumCells = Width * Height;
	const bool bSizeMismatch =
		DynamicBlockerHeight.Num() != NumCells ||
		DynamicBlockerLayerMask.Num() != NumCells;
	if (bSizeMismatch)
	{
		DynamicBlockerHeight.SetNumZeroed(NumCells);
		DynamicBlockerLayerMask.SetNumZeroed(NumCells);
		LastDynamicBlockerCells.Reset();
	}
	else
	{
		// FFixedPoint::Zero is the "no blocker" sentinel — value 0 on its
		// int64 backing. Per-element write is fine; the list typically holds
		// hundreds-to-thousands of indices, not millions.
		for (const int32 Idx : LastDynamicBlockerCells)
		{
			DynamicBlockerHeight[Idx] = FFixedPoint::Zero;
			DynamicBlockerLayerMask[Idx] = 0;
		}
		// Verbose: confirm dirty-rect path is firing and compare against
		// full-grid clear cost. Free when log level is off.
		UE_LOG(LogSeinFogOfWar, Verbose,
			TEXT("RebuildDynamicBlockers: dirty-rect cleared %d cell(s) "
				 "(full-grid clear would have been %d)"),
			LastDynamicBlockerCells.Num(), NumCells);
		LastDynamicBlockerCells.Reset();
	}

	// Whether to force every source's stable-fast-path cache to invalidate
	// this tick. Computed at end via hash compare.
	auto FinalizeReturn = [&](uint32 NewHash) -> bool
	{
		const bool bChanged = (NewHash != LastDynamicBlockerHash);
		LastDynamicBlockerHash = NewHash;
		return bChanged;
	};

	if (!World) return FinalizeReturn(0);
	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return FinalizeReturn(0);
	const ISeinComponentStorage* Storage = Sim->GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
	if (!Storage)
	{
		// Verbose so users can spot this without VeryVerbose. The most
		// common cause of "smoke isn't blocking": no entity in the world
		// has FSeinExtentsComponent in component storage (level-placed actor not
		// registered, or component not on the BP).
		UE_LOG(LogSeinFogOfWar, Verbose,
			TEXT("RebuildDynamicBlockers: no FSeinExtentsComponent storage registered "
				 "— no entity with USeinExtentsComponent has been spawned via SpawnEntity"));
		return FinalizeReturn(0);
	}

	// Walk every alive entity; stamp every shape on every extents that has
	// bBlocksFogOfWar set. Per-shape Height drives occluder height (max-per-
	// cell across overlapping shapes). Smoke-grenade pattern: spawn an
	// entity with FSeinExtentsComponent via an ability, set bBlocksFogOfWar=true
	// + a Capsule shape with the smoke's radius/height; the subsystem picks
	// it up automatically.
	int32 NumStamps = 0;
	uint32 NewHash = 0;
	Sim->GetEntityPool().ForEachEntity(
		[this, Storage, &NumStamps, &NewHash](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const void* Raw = Storage->GetComponentRaw(Handle);
			if (!Raw) return;
			const FSeinExtentsComponent* Extents = static_cast<const FSeinExtentsComponent*>(Raw);
			if (!Extents) return;
			if (!Extents->bBlocksFogOfWar) return;
			if (Extents->BlockedFogOfWarLayerMask == 0) return;
			if (Extents->Shapes.Num() == 0) return;

			const FFixedVector Pos = Entity.Transform.GetLocation();
			const FFixedQuaternion Rot = Entity.Transform.Rotation;

			// Fold entity-level fields into the change-detection fingerprint
			// once. XOR is order-independent so swapping iteration order
			// produces the same fingerprint — only adds / removes / edits
			// flip the hash.
			NewHash ^= GetTypeHash(Handle.Index);
			NewHash ^= GetTypeHash(Pos.X.Value);
			NewHash ^= GetTypeHash(Pos.Y.Value);
			NewHash ^= GetTypeHash(Pos.Z.Value);
			NewHash ^= static_cast<uint32>(Extents->BlockedFogOfWarLayerMask);

			for (const FSeinExtentsShape& ExtShape : Extents->Shapes)
			{
				if (ExtShape.Height <= FFixedPoint::Zero) continue;

				const FSeinStampShape PlanarStamp = ExtShape.AsStampShape();
				StampDynamicBlockerShape(PlanarStamp, Pos, Rot,
					ExtShape.Height, Extents->BlockedFogOfWarLayerMask);
				++NumStamps;
				NewHash ^= GetTypeHash(ExtShape);
			}
		});
	// Verbose every tick — single line confirms the function is firing
	// AND tells you how many stamps it picked up. Diff against the number
	// of enabled stamps across all live FSeinExtentsComponent (bBlocksFogOfWar)
	// entities (one entity with 4 enabled stamps = 4 here).
	UE_LOG(LogSeinFogOfWar, Verbose,
		TEXT("RebuildDynamicBlockers: stamped %d shape(s) across blocker entities"),
		NumStamps);

	return FinalizeReturn(NewHash);
}

void USeinFogOfWarDefault::StampDynamicBlockerShape(const FSeinStampShape& Shape,
	const FFixedVector& EntityWorldPos, const FFixedQuaternion& EntityRotation,
	FFixedPoint HeightAboveGround, uint8 LayerMask)
{
	if (HeightAboveGround <= FFixedPoint::Zero) return;
	if (LayerMask == 0) return;

	// Per-stamp trace. Height + Mask + shape geometry one line per call,
	// useful when reasoning about what the dynamic-blocker grid actually
	// holds vs what designers intended in the spawning ability/asset.
	// Free when LogSeinFogOfWar is below Verbose.
	UE_LOG(LogSeinFogOfWar, Verbose,
		TEXT("StampDynamicBlockerShape: pos=(%lld,%lld,%lld) Height=%lld Mask=0x%02X "
			 "Shape=%d Radius=%lld HalfX=%lld HalfY=%lld"),
		EntityWorldPos.X.Value, EntityWorldPos.Y.Value, EntityWorldPos.Z.Value,
		HeightAboveGround.Value, LayerMask,
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
	UE_CLOG(HeightAboveGround < FFixedPoint::FromInt(50),
		LogSeinFogOfWar, Warning,
		TEXT("StampDynamicBlockerShape: Height=%.1fcm is suspiciously short — "
			 "the lampshade LOS test will let typical (180cm eye) sources see "
			 "right over this blocker. Check FSeinExtentsShape::Height on the "
			 "spawning ability or actor (typical: smoke ~300-400, low cover "
			 "~150, prone-infantry ~80)."),
		HeightAboveGround.ToFloat());

	// Lazily resize the dynamic-blocker overlay to grid extent if the
	// caller forgot to (defensive — RebuildDynamicBlockers handles the
	// clear, but a fresh load with no clear yet would otherwise crash on
	// the IsValidIndex below).
	const int32 NumCells = Width * Height;
	if (DynamicBlockerHeight.Num() != NumCells) DynamicBlockerHeight.SetNumZeroed(NumCells);
	if (DynamicBlockerLayerMask.Num() != NumCells) DynamicBlockerLayerMask.SetNumZeroed(NumCells);

	// Walk the shape's covered cells; OR the layer mask + take the taller
	// of (existing height, ground + this stamp's height) per cell. Multiple
	// overlapping stamps on the same blocker (or multiple blockers stamping
	// the same cell) correctly produce the union of layers + the highest
	// occluder height.
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
			const bool bFirstWriteThisTick = (DynamicBlockerLayerMask[Idx] == 0);

			const FFixedPoint GroundZ = GroundHeight.IsValidIndex(Idx)
				? GroundHeight[Idx] : FFixedPoint::Zero;
			const FFixedPoint TopZ = GroundZ + HeightAboveGround;
			if (TopZ > DynamicBlockerHeight[Idx]) DynamicBlockerHeight[Idx] = TopZ;
			DynamicBlockerLayerMask[Idx] |= LayerMask;

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

	const FSeinExtentsComponent* Extents = Sim.GetComponent<FSeinExtentsComponent>(Target);
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
			const FFixedPoint DynZ     = (DynMask != 0 && DynamicBlockerHeight.IsValidIndex(Idx)) ? DynamicBlockerHeight[Idx] : GroundZ;
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
				// Render at ground (not DynZ = ground + HeightAboveGround).
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
