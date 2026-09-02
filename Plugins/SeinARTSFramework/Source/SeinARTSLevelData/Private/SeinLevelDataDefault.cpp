/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataDefault.cpp
 */

#include "SeinLevelDataDefault.h"
#include "SeinLevelDataDefaultAsset.h"
#include "Volumes/SeinLevelVolume.h"
#include "Volumes/SeinTerrainVolume.h"
#include "SeinLevelLayerProvider.h"
#include "Settings/PluginSettings.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinExtentsPayload.h"

#include "StructUtils/InstancedStruct.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Misc/ScopedSlowTask.h"
#include "TextureResource.h"
#include "Hash/Blake3.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

#include "SeinARTSLevelDataLog.h"

namespace
{
	class FLevelDataDigestStream
	{
	public:
		FLevelDataDigestStream()
		{
			WriteString(TEXT("SeinARTS.LevelData.Default.StaticEnvironment"));
			WriteUInt32(1);
		}

		void WriteUInt8(uint8 Value)
		{
			Hasher.Update(&Value, sizeof(Value));
		}

		void WriteUInt32(uint32 Value)
		{
			uint8 Bytes[4];
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Bytes[Index] = static_cast<uint8>(
					Value >> ((3 - Index) * 8));
			}
			Hasher.Update(Bytes, sizeof(Bytes));
		}

		void WriteInt32(int32 Value)
		{
			WriteUInt32(BitCast<uint32>(Value));
		}

		void WriteUInt64(uint64 Value)
		{
			uint8 Bytes[8];
			for (int32 Index = 0; Index < 8; ++Index)
			{
				Bytes[Index] = static_cast<uint8>(
					Value >> ((7 - Index) * 8));
			}
			Hasher.Update(Bytes, sizeof(Bytes));
		}

		void WriteInt64(int64 Value)
		{
			WriteUInt64(BitCast<uint64>(Value));
		}

		void WriteString(const FString& Value)
		{
			const FTCHARToUTF8 Utf8(*Value, Value.Len());
			WriteUInt64(static_cast<uint64>(Utf8.Length()));
			Hasher.Update(Utf8.Get(), Utf8.Length());
		}

		void WriteBytes(TConstArrayView<uint8> Bytes)
		{
			WriteUInt64(static_cast<uint64>(Bytes.Num()));
			if (!Bytes.IsEmpty())
			{
				Hasher.Update(Bytes.GetData(), Bytes.Num());
			}
		}

		FGuid Finalize() const
		{
			const FBlake3Hash Hash = Hasher.Finalize();
			const uint8* Bytes = Hash.GetBytes();
			auto ReadUInt32 = [](const uint8* In)
			{
				return static_cast<uint32>(In[0]) << 24
					| static_cast<uint32>(In[1]) << 16
					| static_cast<uint32>(In[2]) << 8
					| static_cast<uint32>(In[3]);
			};
			return FGuid(
				ReadUInt32(Bytes),
				ReadUInt32(Bytes + 4),
				ReadUInt32(Bytes + 8),
				ReadUInt32(Bytes + 12));
		}

	private:
		FBlake3 Hasher;
	};

	bool BuildDefaultStaticEnvironmentDigest(
		const FString& ClassPath,
		int32 Width,
		int32 Height,
		FFixedPoint CellSize,
		const FFixedVector& Origin,
		TConstArrayView<FFixedPoint> SharedHeight,
		TConstArrayView<FFixedPoint> SharedNormalZ,
		TConstArrayView<uint8> CellFlags,
		TConstArrayView<uint8> CellTerrainType,
		TConstArrayView<FSeinLevelChannelBlock> Channels,
		FGuid& OutDigest,
		FString& OutError)
	{
		OutDigest.Invalidate();
		OutError.Reset();
		const bool bHasRuntimeData = Width > 0 && Height > 0;
		const int64 NumCells64 =
			static_cast<int64>(Width) * static_cast<int64>(Height);
		if ((bHasRuntimeData
				&& (CellSize <= FFixedPoint::Zero
					|| NumCells64 <= 0
					|| NumCells64 > MAX_int32
					|| SharedHeight.Num() != NumCells64
					|| SharedNormalZ.Num() != NumCells64
					|| CellFlags.Num() != NumCells64
					|| CellTerrainType.Num() != NumCells64))
			|| (!bHasRuntimeData
				&& (!SharedHeight.IsEmpty()
					|| !SharedNormalZ.IsEmpty()
					|| !CellFlags.IsEmpty()
					|| !CellTerrainType.IsEmpty()
					|| !Channels.IsEmpty())))
		{
			OutError =
				TEXT("Default Level Data runtime arrays do not match their static-environment dimensions.");
			return false;
		}

		FLevelDataDigestStream Stream;
		Stream.WriteString(ClassPath);
		Stream.WriteUInt8(bHasRuntimeData ? 1 : 0);
		Stream.WriteInt32(Width);
		Stream.WriteInt32(Height);
		Stream.WriteInt64(CellSize.Value);
		Stream.WriteInt64(Origin.X.Value);
		Stream.WriteInt64(Origin.Y.Value);
		Stream.WriteInt64(Origin.Z.Value);
		Stream.WriteUInt64(static_cast<uint64>(SharedHeight.Num()));
		for (const FFixedPoint Value : SharedHeight)
		{
			Stream.WriteInt64(Value.Value);
		}
		Stream.WriteUInt64(static_cast<uint64>(SharedNormalZ.Num()));
		for (const FFixedPoint Value : SharedNormalZ)
		{
			Stream.WriteInt64(Value.Value);
		}
		Stream.WriteBytes(CellFlags);
		Stream.WriteBytes(CellTerrainType);
		Stream.WriteUInt64(static_cast<uint64>(Channels.Num()));
		FString PreviousLayerId;
		for (const FSeinLevelChannelBlock& Channel : Channels)
		{
			const FString LayerId =
				Channel.LayerId.ToString().ToLower();
			if (LayerId.IsEmpty()
				|| (!PreviousLayerId.IsEmpty()
					&& LayerId <= PreviousLayerId)
				|| Channel.CellSizeMultiple <= 0)
			{
				OutError =
					TEXT("Default Level Data channels must have unique IDs in canonical order and positive cell-size multiples.");
				return false;
			}
			Stream.WriteString(LayerId);
			Stream.WriteInt32(Channel.CellSizeMultiple);
			Stream.WriteBytes(Channel.Data);
			PreviousLayerId = LayerId;
		}

		OutDigest = Stream.Finalize();
		if (!OutDigest.IsValid())
		{
			OutError =
				TEXT("Default Level Data produced an invalid static-environment digest.");
			return false;
		}
		return true;
	}
}

bool USeinLevelDataDefault::ComputeStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	if (GetClass() != USeinLevelDataDefault::StaticClass())
	{
		OutDigest.Invalidate();
		OutError = FString::Printf(
			TEXT("Native Level Data subclass '%s' must explicitly override ComputeStaticEnvironmentDigest."),
			*GetClass()->GetPathName());
		return false;
	}
	return ComputeDefaultStaticEnvironmentDigest(OutDigest, OutError);
}

bool USeinLevelDataDefault::ComputeStateCoverageClaim(
	FSeinLevelDataStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	if (GetClass() != USeinLevelDataDefault::StaticClass())
	{
		OutClaim = {};
		OutError = FString::Printf(
			TEXT("Native Level Data subclass '%s' must explicitly override ComputeStateCoverageClaim."),
			*GetClass()->GetPathName());
		return false;
	}
	return ComputeDefaultStateCoverageClaim(OutClaim, OutError);
}

bool USeinLevelDataDefault::ComputeDefaultStateCoverageClaim(
	FSeinLevelDataStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutClaim = {};
	OutError.Reset();
	OutClaim.StableImplementationId =
		TEXT("seinarts.level-data.default-grid");
	OutClaim.BehaviorRevision = 1;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage = ESeinLevelDataStateCoverage::Stateless;
	return true;
}

bool USeinLevelDataDefault::ComputeDefaultStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	if (!HasRuntimeData())
	{
		return BuildDefaultStaticEnvironmentDigest(
			GetClass()->GetPathName(),
			0,
			0,
			FFixedPoint::FromInt(100),
			FFixedVector::ZeroVector,
			{}, {}, {}, {}, {},
			OutDigest,
			OutError);
	}
	const int64 NumCells64 =
		static_cast<int64>(Width) * static_cast<int64>(Height);
	if (!StaticEnvironmentDigest.IsValid()
		|| NumCells64 <= 0
		|| NumCells64 > MAX_int32
		|| SharedHeight.Num() != NumCells64
		|| SharedNormalZ.Num() != NumCells64
		|| CellFlags.Num() != NumCells64
		|| CellTerrainType.Num() != NumCells64)
	{
		OutDigest.Invalidate();
		OutError =
			TEXT("Default Level Data runtime state no longer matches its cached static-environment contract.");
		return false;
	}
	OutDigest = StaticEnvironmentDigest;
	OutError.Reset();
	return true;
}

void USeinLevelDataDefault::MarkStaticEnvironmentMutated()
{
	++StaticEnvironmentGeneration;
	if (StaticEnvironmentGeneration == 0)
	{
		++StaticEnvironmentGeneration;
	}
}

// ============================================================================
// Runtime queries (reentrant — threading contract)
// ============================================================================

int32 USeinLevelDataDefault::WorldToCellIndex(const FFixedVector& WorldPos) const
{
	if (Width <= 0 || Height <= 0 || CellSizeFP <= FFixedPoint::Zero) return INDEX_NONE;
	const FFixedPoint LocalX = WorldPos.X - OriginFP.X;
	const FFixedPoint LocalY = WorldPos.Y - OriginFP.Y;
	const int32 X = (LocalX / CellSizeFP).ToInt();   // deterministic floor (mirrors nav WorldToGrid)
	const int32 Y = (LocalY / CellSizeFP).ToInt();
	if (X < 0 || X >= Width || Y < 0 || Y >= Height) return INDEX_NONE;
	return Y * Width + X;
}

bool USeinLevelDataDefault::IsInBounds(const FFixedVector& WorldPos) const
{
	const int32 Idx = WorldToCellIndex(WorldPos);
	if (Idx == INDEX_NONE || !CellFlags.IsValidIndex(Idx)) return false;
	return (CellFlags[Idx] & SeinLevelCellFlags::InBounds) != 0;
}

bool USeinLevelDataDefault::GetSharedHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const
{
	const int32 Idx = WorldToCellIndex(WorldPos);
	if (Idx == INDEX_NONE || !SharedHeight.IsValidIndex(Idx)) return false;
	OutZ = SharedHeight[Idx];
	return true;
}

bool USeinLevelDataDefault::GetSharedNormalZAt(const FFixedVector& WorldPos, FFixedPoint& OutNormalZ) const
{
	const int32 Idx = WorldToCellIndex(WorldPos);
	if (Idx == INDEX_NONE || !SharedNormalZ.IsValidIndex(Idx)) return false;
	OutNormalZ = SharedNormalZ[Idx];
	return true;
}

bool USeinLevelDataDefault::GetCellSurface(int32 CellIndex, FSeinLevelCellSurface& OutSurface) const
{
	if (!SharedHeight.IsValidIndex(CellIndex)) return false;
	OutSurface.Height = SharedHeight[CellIndex];
	OutSurface.NormalZ = SharedNormalZ.IsValidIndex(CellIndex) ? SharedNormalZ[CellIndex] : FFixedPoint::Zero;
	const uint8 Flags = CellFlags.IsValidIndex(CellIndex) ? CellFlags[CellIndex] : 0;
	OutSurface.bHasSurface = (Flags & SeinLevelCellFlags::HasSurface) != 0;
	OutSurface.bInBounds   = (Flags & SeinLevelCellFlags::InBounds) != 0;
	OutSurface.TerrainTypeIndex = CellTerrainType.IsValidIndex(CellIndex) ? CellTerrainType[CellIndex] : 0;
	return true;
}

bool USeinLevelDataDefault::GetLayerChannel(FName LayerId, TArray<uint8>& OutData) const
{
	for (const FSeinLevelChannelBlock& Block : RuntimeChannels)
	{
		if (Block.LayerId == LayerId)
		{
			OutData = Block.Data;
			return true;
		}
	}
	return false;
}

// ============================================================================
// Provider registry
// ============================================================================

void USeinLevelDataDefault::OnDeinitialized()
{
	bCancelRequested = true;
	bBaking = false;
	Providers.Reset();
	RuntimeChannels.Reset();
	SharedHeight.Reset();
	SharedNormalZ.Reset();
	CellFlags.Reset();
	CellTerrainType.Reset();
	MinimapTextureRuntime = nullptr;
	Width = 0;
	Height = 0;
	StaticEnvironmentDigest.Invalidate();
	MarkStaticEnvironmentMutated();
}

void USeinLevelDataDefault::RegisterLayerProvider(ISeinLevelLayerProvider* Provider)
{
	if (Provider) Providers.AddUnique(Provider);
}

void USeinLevelDataDefault::UnregisterLayerProvider(ISeinLevelLayerProvider* Provider)
{
	Providers.Remove(Provider);
}

// ============================================================================
// Runtime load
// ============================================================================

bool USeinLevelDataDefault::ApplyAssetData(
	const USeinLevelDataDefaultAsset* Asset)
{
	if (!Asset)
	{
		UE_LOG(LogSeinLevelData, Error,
			TEXT("ApplyAssetData: null default level-data asset."));
		return false;
	}
	const int64 NumCells64 =
		static_cast<int64>(Asset->Width)
		* static_cast<int64>(Asset->Height);

	// Validate the flat arrays. An OLD-format asset (pre-quantization, when this stored
	// TArray<FFixedPoint> SharedHeight/SharedNormalZ) loads with these empty → mismatch
	// → reject transactionally so a previously-valid runtime grid remains intact.
	if (Asset->Width <= 0
		|| Asset->Height <= 0
		|| Asset->CellSize <= FFixedPoint::Zero
		|| Asset->HeightQuantum <= FFixedPoint::Zero
		|| NumCells64 <= 0
		|| NumCells64 > MAX_int32
		|| Asset->SharedHeightQ.Num() != NumCells64
		|| Asset->SharedNormalZQ.Num() != NumCells64
		|| Asset->CellFlags.Num() != NumCells64)
	{
		UE_LOG(LogSeinLevelData, Error,
			TEXT("ApplyAssetData: cell arrays (%d/%d/%d) don't match %dx%d=%lld — rejecting (re-bake needed)."),
			Asset->SharedHeightQ.Num(), Asset->SharedNormalZQ.Num(),
			Asset->CellFlags.Num(), Asset->Width, Asset->Height,
			static_cast<long long>(NumCells64));
		return false;
	}
	const int32 NumCells = static_cast<int32>(NumCells64);

	TArray<uint8> NewCellFlags = Asset->CellFlags;
	TArray<uint8> NewCellTerrainType;
	TArray<FSeinLevelChannelBlock> NewRuntimeChannels =
		Asset->Channels;

	// Terrain type — additive/lenient: copy when present + correctly sized; otherwise
	// default every cell to 0 (Default type) so assets baked BEFORE terrain types load
	// unchanged (no re-bake forced merely to open an old level).
	if (Asset->CellTerrainType.Num() == NumCells)
	{
		NewCellTerrainType = Asset->CellTerrainType;
	}
	else
	{
		NewCellTerrainType.Init(0, NumCells);
	}

	NewRuntimeChannels.Sort(
		[](const FSeinLevelChannelBlock& A,
			const FSeinLevelChannelBlock& B)
		{
			return A.LayerId.ToString().ToLower()
				< B.LayerId.ToString().ToLower();
		});
	for (int32 Index = 0; Index < NewRuntimeChannels.Num(); ++Index)
	{
		const FSeinLevelChannelBlock& Channel =
			NewRuntimeChannels[Index];
		const FString LayerId =
			Channel.LayerId.ToString().ToLower();
		if (LayerId.IsEmpty()
			|| Channel.CellSizeMultiple <= 0
			|| (Index > 0
				&& NewRuntimeChannels[Index - 1].LayerId
					.ToString().ToLower() == LayerId))
		{
			UE_LOG(LogSeinLevelData, Error,
				TEXT("ApplyAssetData: layer channels require unique non-empty IDs and positive cell-size multiples."));
			return false;
		}
	}

	// Dequantize uint16 height + uint8 normal·Up → FFixedPoint (deterministic fixed math).
	const FFixedPoint HMin = Asset->HeightMin;
	const FFixedPoint HQuantum = Asset->HeightQuantum;
	const FFixedPoint NormalStep = FFixedPoint::FromInt(2) / FFixedPoint::FromInt(255); // [-1,1] over 255 steps
	TArray<FFixedPoint> NewSharedHeight;
	TArray<FFixedPoint> NewSharedNormalZ;
	NewSharedHeight.SetNumUninitialized(NumCells);
	NewSharedNormalZ.SetNumUninitialized(NumCells);
	for (int32 i = 0; i < NumCells; ++i)
	{
		NewSharedHeight[i] =
			HMin + HQuantum
				* FFixedPoint::FromInt(Asset->SharedHeightQ[i]);
		NewSharedNormalZ[i] =
			NormalStep
				* FFixedPoint::FromInt(Asset->SharedNormalZQ[i])
			- FFixedPoint::One;
	}

	FGuid NewStaticEnvironmentDigest;
	FString DigestError;
	if (!BuildDefaultStaticEnvironmentDigest(
		GetClass()->GetPathName(),
		Asset->Width,
		Asset->Height,
		Asset->CellSize,
		Asset->Origin,
		NewSharedHeight,
		NewSharedNormalZ,
		NewCellFlags,
		NewCellTerrainType,
		NewRuntimeChannels,
		NewStaticEnvironmentDigest,
		DigestError))
	{
		UE_LOG(LogSeinLevelData, Error,
			TEXT("ApplyAssetData: %s"), *DigestError);
		return false;
	}

	Width = Asset->Width;
	Height = Asset->Height;
	CellSizeFP = Asset->CellSize;
	OriginFP = Asset->Origin;
	RuntimeChannels = MoveTemp(NewRuntimeChannels);
	MinimapTextureRuntime = Asset->MinimapTexture;
	CellFlags = MoveTemp(NewCellFlags);
	CellTerrainType = MoveTemp(NewCellTerrainType);
	SharedHeight = MoveTemp(NewSharedHeight);
	SharedNormalZ = MoveTemp(NewSharedNormalZ);
	StaticEnvironmentDigest = NewStaticEnvironmentDigest;
	MarkStaticEnvironmentMutated();
	return true;
}

bool USeinLevelDataDefault::LoadFromAssetImpl(USeinLevelDataAsset* Asset)
{
	const USeinLevelDataDefaultAsset* DefAsset =
		Cast<USeinLevelDataDefaultAsset>(Asset);
	if (!DefAsset || !ApplyAssetData(DefAsset))
	{
		UE_LOG(LogSeinLevelData, Error,
			TEXT("LoadFromAsset: expected a valid Sein Level Data (Default Grid) asset; the current runtime substrate was left unchanged."));
		return false;
	}
	OnLevelDataMutated.Broadcast();
	return true;
}

// ============================================================================
// Bake
// ============================================================================

bool USeinLevelDataDefault::BeginBakeImpl(UWorld* World)
{
	if (!World) { UE_LOG(LogSeinLevelData, Warning, TEXT("BeginBake: null world")); return false; }
	if (bBaking) { UE_LOG(LogSeinLevelData, Warning, TEXT("BeginBake: already baking")); return false; }

	bBaking = true;
	bCancelRequested = false;
	ON_SCOPE_EXIT { bBaking = false; bCancelRequested = false; };

	USeinLevelDataDefaultAsset* NewAsset = nullptr;
	if (!DoSyncBake(World, NewAsset) || !NewAsset)
	{
		UE_LOG(LogSeinLevelData, Warning, TEXT("BeginBake: failed"));
		return false;
	}

	// Point every level volume at the new asset, then load it (final apply + broadcast).
	for (TActorIterator<ASeinLevelVolume> It(World); It; ++It)
	{
		It->BakedAsset = NewAsset;
		It->MarkPackageDirty();
	}
	if (!LoadFromAsset(NewAsset))
	{
		UE_LOG(LogSeinLevelData, Error,
			TEXT("BeginBake: the completed asset could not be adopted."));
		return false;
	}

	UE_LOG(LogSeinLevelData, Log, TEXT("Level bake complete: %dx%d cells"), NewAsset->Width, NewAsset->Height);
	return true;
}

bool USeinLevelDataDefault::DoSyncBake(UWorld* World, USeinLevelDataDefaultAsset*& OutAsset)
{
	OutAsset = nullptr;

	// Gather level volumes + union bounds (D10 — multi-volume union).
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
	if (Volumes.Num() == 0 || !UnionBounds.IsValid)
	{
		UE_LOG(LogSeinLevelData, Warning, TEXT("DoSyncBake: no ASeinLevelVolumes in world"));
		return false;
	}

	const FFixedPoint BakedCellSize = Volumes[0]->GetResolvedCellSize();
	const float CellSizeF = BakedCellSize.ToFloat();
	const int32 GridW = FMath::Max(1, FMath::CeilToInt((UnionBounds.Max.X - UnionBounds.Min.X) / CellSizeF));
	const int32 GridH = FMath::Max(1, FMath::CeilToInt((UnionBounds.Max.Y - UnionBounds.Min.Y) / CellSizeF));
	const FVector OriginWorld(UnionBounds.Min.X, UnionBounds.Min.Y, UnionBounds.Min.Z);
	const float TopZ = UnionBounds.Max.Z + 200.0f; // headroom (mirrors nav BakeTraceHeadroom)
	const float BottomZ = UnionBounds.Min.Z - 10.0f;
	const float UnionMidZ = (UnionBounds.Min.Z + UnionBounds.Max.Z) * 0.5f;

#if WITH_EDITOR
	FScopedSlowTask Task(GridW * GridH, NSLOCTEXT("SeinLevelData", "Baking", "Baking SeinARTS Level Data..."));
	Task.MakeDialog(true /*bShowCancelButton*/);
#endif

	// Asset (editor: on-disk; runtime bake: transient).
#if WITH_EDITOR
	const FString AssetName = FString::Printf(TEXT("LevelData_%s"), *World->GetMapName());
	OutAsset = CreateOrLoadAsset(World, AssetName);
#else
	OutAsset = NewObject<USeinLevelDataDefaultAsset>(GetTransientPackage());
#endif
	if (!OutAsset) return false;

	OutAsset->Width = GridW;
	OutAsset->Height = GridH;
	OutAsset->CellSize = BakedCellSize;
	OutAsset->Origin = FFixedVector(FFixedPoint::FromFloat(OriginWorld.X),
	                                FFixedPoint::FromFloat(OriginWorld.Y),
	                                FFixedPoint::FromFloat(OriginWorld.Z));
	const int32 NumCells = GridW * GridH;
	OutAsset->Channels.Reset();

	// Populate the RUNTIME substrate directly with EXACT fixed-point surface data so the
	// layer providers below read un-quantized values (nav's slope gate stays exact). The
	// on-disk asset is quantized only at the END of the bake; runtime is then re-synced
	// from the (quantized) asset so this bake session matches a reloaded session.
	Width = GridW;
	Height = GridH;
	CellSizeFP = BakedCellSize;
	OriginFP = OutAsset->Origin;
	SharedHeight.SetNumUninitialized(NumCells);
	SharedNormalZ.SetNumUninitialized(NumCells);
	CellFlags.SetNumUninitialized(NumCells);
	CellTerrainType.SetNumZeroed(NumCells); // 0 = Default; per-cell loop overwrites where classified

	// Trace query + skip list — nav-faithful so the shared height feeds nav identically
	// (trace-reconciliation note in MicroPlan_CP1.1.md): ignore the volumes; ignore any
	// ASeinActor whose bridge has FSeinExtentsPayload::bBakesIntoNav=false OR an
	// FSeinMovementPayload (mobile units don't carve the static bake).
	FCollisionQueryParams QP(SCENE_QUERY_STAT(SeinLevelDataBake), true /*bTraceComplex*/);
	QP.bReturnPhysicalMaterial = true; // terrain-type classification reads the hit's phys material
	for (ASeinLevelVolume* Vol : Volumes) { if (Vol) QP.AddIgnoredActor(Vol); }

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
				if (!Extents->bBakesIntoNav) bSkip = true;
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

	// Per-cell trace. MT-ready (Roadmap_Multithreading.md step 1-2): each cell writes its
	// own slot by index; no shared mutable accumulator in the body (counters are
	// incidental + single-threaded for now).
	// Bake trace channel — designer-configurable (USeinARTSCoreSettings, default
	// ECC_Visibility) so projects whose ground geometry isn't on Visibility can
	// point the shared down-trace at their own "ground" channel.
	const ECollisionChannel TraceChannel = GetDefault<USeinARTSCoreSettings>()->BakeTraceChannel;

	// --- Terrain-type resolution setup (Phase 1) -------------------------------------
	// Two authoring sources feed the shared per-cell terrain type, in this precedence:
	//   1. Physical-material mapping — a reverse lookup from a trace hit's phys-material
	//      asset PATH to the type that lists it (paint a landscape layer / assign a mesh
	//      material; no custom tool). Compared by path — no asset load needed.
	//   2. Terrain volumes — an explicit brush OVERRIDE that beats the material-derived
	//      type (highest Priority wins on overlap).
	// Both reference types authored in USeinARTSCoreSettings::TerrainTypes; stored index
	// 0 = reserved Default (array position i → stored index i+1).
	const USeinARTSCoreSettings* TerrainSettings = GetDefault<USeinARTSCoreSettings>();

	TMap<FSoftObjectPath, int32> PhysMatToType;
	if (TerrainSettings)
	{
		for (int32 t = 0; t < TerrainSettings->TerrainTypes.Num(); ++t)
		{
			const int32 StoredIndex = t + 1; // reserved-0 Default
			for (const FSoftObjectPath& MatPath : TerrainSettings->TerrainTypes[t].PhysicalMaterials)
			{
				if (MatPath.IsValid()) PhysMatToType.Add(MatPath, StoredIndex);
			}
		}
	}

	// Terrain volumes: precompute (bounds, stored index, priority) once; per cell we pick
	// the highest-Priority volume whose brush contains the cell. Tag → stored index via
	// settings; an unset/unknown tag contributes nothing.
	struct FTerrainVolumeBake { FBox Bounds; int32 StoredIndex; int32 Priority; ASeinTerrainVolume* Volume; };
	TArray<FTerrainVolumeBake> TerrainVolumes;
	for (TActorIterator<ASeinTerrainVolume> It(World); It; ++It)
	{
		ASeinTerrainVolume* TV = *It;
		if (!TV) continue;
		const int32 StoredIndex = TerrainSettings ? TerrainSettings->GetTerrainTypeIndex(TV->TerrainType) : 0;
		if (StoredIndex <= 0) continue; // unknown/unset tag → no effect
		TerrainVolumes.Add({ TV->GetVolumeWorldBounds(), StoredIndex, TV->Priority, TV });
	}

	int32 NumInBounds = 0, NumSurface = 0, Processed = 0;
	for (int32 Y = 0; Y < GridH; ++Y)
	{
		for (int32 X = 0; X < GridW; ++X)
		{
			if (bCancelRequested)
			{
				UE_LOG(LogSeinLevelData, Warning, TEXT("Level bake cancelled by user"));
				OutAsset = nullptr;
				return false;
			}

			const int32 Idx = Y * GridW + X;
			const float CenterX = OriginWorld.X + (X + 0.5f) * CellSizeF;
			const float CenterY = OriginWorld.Y + (Y + 0.5f) * CellSizeF;

			// In-bounds = the cell center is inside ANY volume's brush (D10). Tested at
			// the union mid-Z so an extruded brush's XY footprint is what matters.
			bool bInBounds = false;
			for (ASeinLevelVolume* Vol : Volumes)
			{
				if (Vol && Vol->EncompassesPoint(FVector(CenterX, CenterY, UnionMidZ)))
				{
					bInBounds = true;
					break;
				}
			}

			uint8 Flags = 0;
			FFixedPoint HeightFP = FFixedPoint::FromFloat(BottomZ);
			FFixedPoint NormalZFP = FFixedPoint::Zero;
			uint8 TypeIndex = 0; // terrain type (0 = Default)

			if (bInBounds)
			{
				Flags |= SeinLevelCellFlags::InBounds;
				++NumInBounds;

				FHitResult TopHit;
				const FVector Start(CenterX, CenterY, TopZ);
				const FVector End(CenterX, CenterY, BottomZ);
				if (World->LineTraceSingleByChannel(TopHit, Start, End, TraceChannel, QP))
				{
					Flags |= SeinLevelCellFlags::HasSurface;
					HeightFP = FFixedPoint::FromFloat(TopHit.ImpactPoint.Z);
					NormalZFP = FFixedPoint::FromFloat(FVector::DotProduct(TopHit.Normal, FVector::UpVector));
					++NumSurface;

					// Source 1 — physical-material mapping: the hit surface's phys material
					// (painted landscape layer / mesh material) → the terrain type listing it.
					if (PhysMatToType.Num() > 0)
					{
						if (const UPhysicalMaterial* PM = TopHit.PhysMaterial.Get())
						{
							if (const int32* Found = PhysMatToType.Find(FSoftObjectPath(PM)))
							{
								TypeIndex = (uint8)*Found;
							}
						}
					}
				}

				// Source 2 — terrain-volume override (explicit; beats the material-derived
				// type). Highest Priority wins. A terrain region is a 2D XY concept for nav,
				// so each volume is tested at ITS OWN mid-height (Bounds center Z) rather than
				// the cell's surface Z — robust regardless of where the ground sits relative to
				// the brush (a flat slab dropped on the ground, a brush floating above it, etc.
				// all classify the cells under their XY footprint). EncompassesPoint still
				// respects non-box brush shapes at that height.
				if (TerrainVolumes.Num() > 0)
				{
					int32 BestPriority = MIN_int32;
					for (const FTerrainVolumeBake& TV : TerrainVolumes)
					{
						if (TV.Priority <= BestPriority) continue;     // can't beat best (ties keep earlier)
						const FVector Probe(CenterX, CenterY, TV.Bounds.GetCenter().Z);
						if (!TV.Bounds.IsInsideXY(Probe)) continue;    // cheap XY AABB reject
						if (TV.Volume && TV.Volume->EncompassesPoint(Probe))
						{
							TypeIndex = (uint8)TV.StoredIndex;
							BestPriority = TV.Priority;
						}
					}
				}
			}

			SharedHeight[Idx] = HeightFP;
			SharedNormalZ[Idx] = NormalZFP;
			CellFlags[Idx] = Flags;
			CellTerrainType[Idx] = TypeIndex;

			++Processed;
#if WITH_EDITOR
			if ((Processed & 255) == 0)
			{
				Task.EnterProgressFrame(256.0f);
				if (Task.ShouldCancel()) bCancelRequested = true;
			}
#endif
		}
	}

	UE_LOG(LogSeinLevelData, Log,
		TEXT("Level bake: %dx%d=%d cells — in-bounds=%d surface=%d (ignored %d actors), CellSize=%s"),
		GridW, GridH, NumCells, NumInBounds, NumSurface, NumIgnoredActors, *BakedCellSize.ToString());

	// Terrain-type bake diagnostic. If "volumes gathered" is 0, no ASeinTerrainVolume was
	// found or none resolved its TerrainType tag to a registered type (check the tag matches
	// a USeinARTSCoreSettings::TerrainTypes entry). If volumes > 0 but "classified" is 0, the
	// brush isn't being detected over any cell (shape/placement) — re-check the volume covers
	// the play area in XY. Classified > 0 ⇒ the cost IS baked (nav routes around it; it does
	// NOT slow traversal — A* cost is a routing weight, not a speed multiplier).
	{
		int32 Classified = 0;
		for (int32 i = 0; i < NumCells; ++i) { if (CellTerrainType[i] != 0) ++Classified; }
		UE_LOG(LogSeinLevelData, Log,
			TEXT("Terrain types: %d volume(s) gathered, %d phys-mat mapping(s) -> %d/%d cells classified non-Default."),
			TerrainVolumes.Num(), PhysMatToType.Num(), Classified, NumCells);
	}

	// Runtime substrate already populated (exact) above — providers read it directly
	// via GetCellSurface, so they see un-quantized height/normal.

	// Run each registered layer provider — independent per provider (MT-ready). Each
	// computes its channel block from the shared substrate (+ its own layer-specific
	// traces). None registered until nav/FoW are ported.
	for (ISeinLevelLayerProvider* Provider : Providers)
	{
		if (!Provider) continue;
		FSeinLevelChannelBlock Block;
		Block.LayerId = Provider->GetLayerId();
		Provider->BakeLayer(*this, World, Block.Data);
		// Resolution metadata is read AFTER BakeLayer — providers that derive their
		// cell size from per-bake config (FoW from the volume's vision cell size)
		// record the snapped multiple during the bake.
		Block.CellSizeMultiple = FMath::Max(1, Provider->GetCellSizeMultiple());
		OutAsset->Channels.Add(MoveTemp(Block));
	}

	// Quantize the EXACT runtime surface data into the asset's compact on-disk form:
	// uint16 height + uint8 normal·Up byte blobs (bulk-serialized) instead of tagged
	// FFixedPoint struct arrays (~5x smaller). Quant is at the float→fixed bake
	// boundary; the dequant in ApplyAssetData is deterministic fixed-point.
	{
		const float HeightQuantumF = FMath::Max(1.0f, TopZ - BottomZ) / 65535.0f;
		OutAsset->HeightMin = FFixedPoint::FromFloat(BottomZ);
		OutAsset->HeightQuantum = FFixedPoint::FromFloat(HeightQuantumF);
		OutAsset->SharedHeightQ.SetNumUninitialized(NumCells);
		OutAsset->SharedNormalZQ.SetNumUninitialized(NumCells);
		OutAsset->CellFlags = CellFlags;             // already raw uint8
		OutAsset->CellTerrainType = CellTerrainType; // already raw uint8 (per-cell type index)
		for (int32 i = 0; i < NumCells; ++i)
		{
			const float HZ = SharedHeight[i].ToFloat();
			OutAsset->SharedHeightQ[i] = (uint16)FMath::Clamp(FMath::RoundToInt((HZ - BottomZ) / HeightQuantumF), 0, 65535);

			const float NZ = SharedNormalZ[i].ToFloat(); // [-1, 1]
			OutAsset->SharedNormalZQ[i] = (uint8)FMath::Clamp(FMath::RoundToInt((NZ + 1.0f) * 0.5f * 255.0f), 0, 255);
		}
	}

	// Synthesize the top-down minimap background texture from the exact (pre-dequant)
	// runtime surface arrays and stash it on the asset so it serializes with the
	// package. Done before the re-sync below so ApplyAssetData caches the freshly
	// built texture into MinimapTextureRuntime.
	BuildOrUpdateMinimapTexture(OutAsset);

	// Re-sync the runtime substrate from the (now quantized) asset so this baking
	// session reads the same dequantized values a reloaded session will.
	if (!ApplyAssetData(OutAsset))
	{
		UE_LOG(LogSeinLevelData, Error,
			TEXT("Level bake produced an asset that failed its own runtime validation."));
		return false;
	}

#if WITH_EDITOR
	if (!SaveAssetToDisk(OutAsset))
	{
		UE_LOG(LogSeinLevelData, Warning, TEXT("Level bake: failed to save asset to disk"));
	}
#endif

	return true;
}

// ============================================================================
// Minimap background texture synthesis
// ============================================================================

void USeinLevelDataDefault::BuildOrUpdateMinimapTexture(USeinLevelDataDefaultAsset* Asset) const
{
	if (!Asset) return;
	const int32 NumCells = Width * Height;
	if (Width <= 0 || Height <= 0 || SharedHeight.Num() != NumCells || CellFlags.Num() != NumCells)
	{
		return;
	}

	// One texel per finest cell, capped so the asset stays light on large maps.
	constexpr int32 MaxDim = 512;
	const int32 TexW = FMath::Clamp(Width, 1, MaxDim);
	const int32 TexH = FMath::Clamp(Height, 1, MaxDim);

	// Height range over in-bounds surface cells → contrast for the shading ramp.
	float MinH = TNumericLimits<float>::Max();
	float MaxH = TNumericLimits<float>::Lowest();
	for (int32 i = 0; i < NumCells; ++i)
	{
		if ((CellFlags[i] & SeinLevelCellFlags::InBounds) && (CellFlags[i] & SeinLevelCellFlags::HasSurface))
		{
			const float H = SharedHeight[i].ToFloat();
			MinH = FMath::Min(MinH, H);
			MaxH = FMath::Max(MaxH, H);
		}
	}
	if (!(MaxH > MinH)) { MinH = 0.0f; MaxH = 1.0f; } // no surface cells → flat ramp, avoid div0

	// Shade each texel (nearest-sample the grid). Row 0 = grid Y 0 = world min Y, so the
	// texture's V axis matches USeinUIBPFL::SeinWorldToMinimap (V grows with world +Y).
	const FLinearColor LowColor(0.16f, 0.24f, 0.15f);       // low ground (dark green)
	const FLinearColor HighColor(0.62f, 0.58f, 0.44f);      // high ground (tan)
	const FLinearColor NoSurfaceColor(0.09f, 0.12f, 0.20f); // pits / no geometry (dark blue)
	const FColor BorderColor(16, 18, 22, 255);              // out of play area

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TexW * TexH);
	for (int32 ty = 0; ty < TexH; ++ty)
	{
		const int32 gy = (TexH == Height) ? ty : FMath::Clamp(ty * Height / TexH, 0, Height - 1);
		for (int32 tx = 0; tx < TexW; ++tx)
		{
			const int32 gx = (TexW == Width) ? tx : FMath::Clamp(tx * Width / TexW, 0, Width - 1);
			const int32 Cell = gy * Width + gx;
			const uint8 Flags = CellFlags[Cell];
			FColor& Out = Pixels[ty * TexW + tx];

			if ((Flags & SeinLevelCellFlags::InBounds) == 0)
			{
				Out = BorderColor;
				continue;
			}
			if ((Flags & SeinLevelCellFlags::HasSurface) == 0)
			{
				Out = NoSurfaceColor.ToFColor(true);
				continue;
			}
			const float T = FMath::Clamp((SharedHeight[Cell].ToFloat() - MinH) / (MaxH - MinH), 0.0f, 1.0f);
			FLinearColor Base = FMath::Lerp(LowColor, HighColor, T);
			// Slope relief: steeper (lower normal·Up) reads darker.
			const float NZ = FMath::Clamp(SharedNormalZ.IsValidIndex(Cell) ? SharedNormalZ[Cell].ToFloat() : 1.0f, 0.0f, 1.0f);
			Base *= FMath::Lerp(0.55f, 1.0f, NZ);
			Base.A = 1.0f;
			Out = Base.ToFColor(true);
		}
	}

	UTexture2D* Tex = Asset->MinimapTexture;

#if WITH_EDITOR
	if (!Tex)
	{
		Tex = NewObject<UTexture2D>(Asset, TEXT("MinimapTexture"), RF_Public);
		Asset->MinimapTexture = Tex;
	}
	// Source data is what serializes into the package; UpdateResource builds platform data.
	Tex->Source.Init(TexW, TexH, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
	Tex->SRGB = true;
	Tex->CompressionSettings = TC_EditorIcon; // uncompressed RGBA — crisp for UI, no block artifacts
	Tex->MipGenSettings = TMGS_NoMipmaps;
	Tex->LODGroup = TEXTUREGROUP_UI;
	Tex->Filter = TF_Bilinear;
	Tex->UpdateResource();
	Tex->PostEditChange();
#else
	// Cooked/runtime bake (rare) — build a transient texture for in-memory use.
	Tex = UTexture2D::CreateTransient(TexW, TexH, PF_B8G8R8A8);
	if (Tex)
	{
		Tex->SRGB = true;
		Tex->Filter = TF_Bilinear;
		if (FTexturePlatformData* PD = Tex->GetPlatformData())
		{
			void* Data = PD->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, Pixels.GetData(), TexW * TexH * sizeof(FColor));
			PD->Mips[0].BulkData.Unlock();
		}
		Tex->UpdateResource();
	}
	Asset->MinimapTexture = Tex;
#endif
}

#if WITH_EDITOR
USeinLevelDataDefaultAsset* USeinLevelDataDefault::CreateOrLoadAsset(UWorld* World, const FString& AssetName) const
{
	// Save folder from plugin settings (regenerable, gitignored by default).
	FString SaveFolder = TEXT("/Game/SeinARTS/LevelData");
	if (const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>())
	{
		if (!Settings->LevelDataSaveFolder.Path.IsEmpty())
		{
			SaveFolder = Settings->LevelDataSaveFolder.Path;
		}
	}
	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *SaveFolder, *AssetName);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package) return nullptr;
	Package->FullyLoad();

	if (USeinLevelDataDefaultAsset* Existing = FindObject<USeinLevelDataDefaultAsset>(Package, *AssetName))
	{
		return Existing;
	}

	USeinLevelDataDefaultAsset* Asset = NewObject<USeinLevelDataDefaultAsset>(
		Package, USeinLevelDataDefaultAsset::StaticClass(), FName(*AssetName),
		RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Asset);
	return Asset;
}

bool USeinLevelDataDefault::SaveAssetToDisk(USeinLevelDataDefaultAsset* Asset) const
{
	if (!Asset) return false;
	UPackage* Pkg = Asset->GetOutermost();
	if (!Pkg) return false;

	Pkg->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		Pkg->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_None;
	Args.Error = GError;
	return UPackage::SavePackage(Pkg, Asset, *Filename, Args);
}
#endif
