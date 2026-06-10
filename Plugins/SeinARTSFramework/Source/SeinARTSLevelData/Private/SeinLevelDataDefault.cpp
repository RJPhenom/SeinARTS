/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataDefault.cpp
 */

#include "SeinLevelDataDefault.h"
#include "SeinLevelDataDefaultAsset.h"
#include "Volumes/SeinLevelVolume.h"
#include "SeinLevelLayerProvider.h"
#include "Settings/PluginSettings.h"
#include "Actor/SeinActor.h"
#include "Actor/SeinEntityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinExtentsComponent.h"

#include "StructUtils/InstancedStruct.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Misc/ScopedSlowTask.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#endif

#include "SeinARTSLevelDataLog.h"

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

void USeinLevelDataDefault::ApplyAssetData(const USeinLevelDataDefaultAsset* Asset)
{
	Width = Asset->Width;
	Height = Asset->Height;
	CellSizeFP = Asset->CellSize;
	OriginFP = Asset->Origin;
	RuntimeChannels = Asset->Channels;

	const int32 NumCells = Width * Height;

	// Validate the flat arrays. An OLD-format asset (pre-quantization, when this stored
	// TArray<FFixedPoint> SharedHeight/SharedNormalZ) loads with these empty → mismatch
	// → clear + warn so the user re-bakes, instead of reading garbage.
	if (NumCells <= 0 || Asset->SharedHeightQ.Num() != NumCells
		|| Asset->SharedNormalZQ.Num() != NumCells || Asset->CellFlags.Num() != NumCells)
	{
		UE_LOG(LogSeinLevelData, Warning,
			TEXT("ApplyAssetData: cell arrays (%d/%d/%d) don't match %dx%d=%d — clearing (re-bake needed)."),
			Asset->SharedHeightQ.Num(), Asset->SharedNormalZQ.Num(), Asset->CellFlags.Num(), Width, Height, NumCells);
		Width = Height = 0;
		SharedHeight.Reset();
		SharedNormalZ.Reset();
		CellFlags.Reset();
		return;
	}

	CellFlags = Asset->CellFlags; // already raw uint8

	// Dequantize uint16 height + uint8 normal·Up → FFixedPoint (deterministic fixed math).
	const FFixedPoint HMin = Asset->HeightMin;
	const FFixedPoint HQuantum = Asset->HeightQuantum;
	const FFixedPoint NormalStep = FFixedPoint::FromInt(2) / FFixedPoint::FromInt(255); // [-1,1] over 255 steps
	SharedHeight.SetNumUninitialized(NumCells);
	SharedNormalZ.SetNumUninitialized(NumCells);
	for (int32 i = 0; i < NumCells; ++i)
	{
		SharedHeight[i]  = HMin + HQuantum * FFixedPoint::FromInt(Asset->SharedHeightQ[i]);
		SharedNormalZ[i] = NormalStep * FFixedPoint::FromInt(Asset->SharedNormalZQ[i]) - FFixedPoint::One;
	}
}

void USeinLevelDataDefault::LoadFromAsset(USeinLevelDataAsset* Asset)
{
	if (const USeinLevelDataDefaultAsset* DefAsset = Cast<USeinLevelDataDefaultAsset>(Asset))
	{
		ApplyAssetData(DefAsset);
	}
	else
	{
		Width = Height = 0;
		SharedHeight.Reset();
		SharedNormalZ.Reset();
		CellFlags.Reset();
		RuntimeChannels.Reset();
	}
	OnLevelDataMutated.Broadcast();
}

// ============================================================================
// Bake
// ============================================================================

bool USeinLevelDataDefault::BeginBake(UWorld* World)
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
	LoadFromAsset(NewAsset);

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

	// Trace query + skip list — nav-faithful so the shared height feeds nav identically
	// (trace-reconciliation note in MicroPlan_CP1.1.md): ignore the volumes; ignore any
	// ASeinActor whose bridge has FSeinExtentsComponent::bBakesIntoNav=false OR an
	// FSeinMovementComponent (mobile units don't carve the static bake).
	FCollisionQueryParams QP(SCENE_QUERY_STAT(SeinLevelDataBake), true /*bTraceComplex*/);
	for (ASeinLevelVolume* Vol : Volumes) { if (Vol) QP.AddIgnoredActor(Vol); }

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
				if (!Extents->bBakesIntoNav) bSkip = true;
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

	// Per-cell trace. MT-ready (Roadmap_Multithreading.md step 1-2): each cell writes its
	// own slot by index; no shared mutable accumulator in the body (counters are
	// incidental + single-threaded for now).
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

			if (bInBounds)
			{
				Flags |= SeinLevelCellFlags::InBounds;
				++NumInBounds;

				FHitResult TopHit;
				const FVector Start(CenterX, CenterY, TopZ);
				const FVector End(CenterX, CenterY, BottomZ);
				if (World->LineTraceSingleByChannel(TopHit, Start, End, ECC_Visibility, QP))
				{
					Flags |= SeinLevelCellFlags::HasSurface;
					HeightFP = FFixedPoint::FromFloat(TopHit.ImpactPoint.Z);
					NormalZFP = FFixedPoint::FromFloat(FVector::DotProduct(TopHit.Normal, FVector::UpVector));
					++NumSurface;
				}
			}

			SharedHeight[Idx] = HeightFP;
			SharedNormalZ[Idx] = NormalZFP;
			CellFlags[Idx] = Flags;

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
		OutAsset->CellFlags = CellFlags; // already raw uint8
		for (int32 i = 0; i < NumCells; ++i)
		{
			const float HZ = SharedHeight[i].ToFloat();
			OutAsset->SharedHeightQ[i] = (uint16)FMath::Clamp(FMath::RoundToInt((HZ - BottomZ) / HeightQuantumF), 0, 65535);

			const float NZ = SharedNormalZ[i].ToFloat(); // [-1, 1]
			OutAsset->SharedNormalZQ[i] = (uint8)FMath::Clamp(FMath::RoundToInt((NZ + 1.0f) * 0.5f * 255.0f), 0, 255);
		}
	}

	// Re-sync the runtime substrate from the (now quantized) asset so this baking
	// session reads the same dequantized values a reloaded session will.
	ApplyAssetData(OutAsset);

#if WITH_EDITOR
	if (!SaveAssetToDisk(OutAsset))
	{
		UE_LOG(LogSeinLevelData, Warning, TEXT("Level bake: failed to save asset to disk"));
	}
#endif

	return true;
}

#if WITH_EDITOR
USeinLevelDataDefaultAsset* USeinLevelDataDefault::CreateOrLoadAsset(UWorld* World, const FString& AssetName) const
{
	// Save folder from plugin settings (regenerable, gitignored by default).
	FString SaveFolder = TEXT("/Game/LevelData");
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
