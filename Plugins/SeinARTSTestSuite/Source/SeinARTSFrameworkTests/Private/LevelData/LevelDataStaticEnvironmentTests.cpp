#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Engine/Texture2D.h"
#include "Containers/Ticker.h"

#include "SeinLevelDataDefault.h"
#include "SeinLevelDataDefaultAsset.h"
#include "SeinLevelDataSubsystem.h"
#include "Default/SeinFogOfWarDefault.h"
#include "SeinFogOfWarSubsystem.h"
#include "SeinNavigationAStar.h"
#include "SeinNavigationSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinLevelDataTestTypes.h"
#include "Volumes/SeinLevelVolume.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		const FName TransactionFixtureLayerId(
			TEXT("TransactionalFixture"));

		struct FDefaultLevelDataRuntimeSnapshot
		{
			bool bHasRuntimeData = false;
			FIntPoint Dimensions = FIntPoint::ZeroValue;
			FFixedPoint CellSize = FFixedPoint::Zero;
			FFixedVector Origin = FFixedVector::ZeroVector;
			TArray<FSeinLevelCellSurface> Surfaces;
			TArray<uint8> LayerData;
			UTexture2D* MinimapTexture = nullptr;
		};

		struct FScopedDefaultStaticEnvironmentClasses
		{
			FScopedDefaultStaticEnvironmentClasses()
				: Settings(
					GetMutableDefault<USeinARTSCoreSettings>())
				, SavedLevelDataClass(
					Settings->LevelDataClass)
				, SavedNavigationClass(
					Settings->NavigationClass)
				, SavedFogOfWarClass(
					Settings->FogOfWarClass)
			{
				Settings->LevelDataClass =
					FSoftClassPath(
						USeinLevelDataDefault::StaticClass());
				Settings->NavigationClass =
					FSoftClassPath(
						USeinNavigationAStar::StaticClass());
				Settings->FogOfWarClass =
					FSoftClassPath(
						USeinFogOfWarDefault::StaticClass());
			}

			~FScopedDefaultStaticEnvironmentClasses()
			{
				Settings->LevelDataClass =
					SavedLevelDataClass;
				Settings->NavigationClass =
					SavedNavigationClass;
				Settings->FogOfWarClass =
					SavedFogOfWarClass;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			FSoftClassPath SavedLevelDataClass;
			FSoftClassPath SavedNavigationClass;
			FSoftClassPath SavedFogOfWarClass;
		};

		struct FScopedLevelDataAdmissionClass
		{
			explicit FScopedLevelDataAdmissionClass(UClass* LevelDataClass)
				: Settings(GetMutableDefault<USeinARTSCoreSettings>())
				, SavedLevelDataClass(Settings->LevelDataClass)
				, SavedNavigationClass(Settings->NavigationClass)
				, SavedFogOfWarClass(Settings->FogOfWarClass)
			{
				Settings->LevelDataClass = FSoftClassPath(LevelDataClass);
				Settings->NavigationClass.Reset();
				Settings->FogOfWarClass.Reset();
			}

			~FScopedLevelDataAdmissionClass()
			{
				Settings->LevelDataClass = SavedLevelDataClass;
				Settings->NavigationClass = SavedNavigationClass;
				Settings->FogOfWarClass = SavedFogOfWarClass;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			FSoftClassPath SavedLevelDataClass;
			FSoftClassPath SavedNavigationClass;
			FSoftClassPath SavedFogOfWarClass;
		};

		USeinLevelDataDefaultAsset* MakeValidDefaultAsset(
			UObject* Outer)
		{
			USeinLevelDataDefaultAsset* Asset =
				NewObject<USeinLevelDataDefaultAsset>(Outer);
			if (!Asset)
			{
				return nullptr;
			}

			Asset->Width = 2;
			Asset->Height = 2;
			Asset->CellSize = FFixedPoint::FromInt(75);
			Asset->Origin = FFixedVector(
				FFixedPoint::FromInt(-250),
				FFixedPoint::FromInt(125),
				FFixedPoint::FromInt(17));
			Asset->HeightMin = FFixedPoint::FromInt(-30);
			Asset->HeightQuantum = FFixedPoint::FromInt(3);
			Asset->SharedHeightQ = { 0, 2, 5, 9 };
			Asset->SharedNormalZQ = { 0, 64, 128, 255 };
			Asset->CellFlags = {
				static_cast<uint8>(
					SeinLevelCellFlags::InBounds
					| SeinLevelCellFlags::HasSurface),
				SeinLevelCellFlags::HasSurface,
				SeinLevelCellFlags::InBounds,
				0,
			};
			Asset->CellTerrainType = { 3, 2, 1, 0 };

			FSeinLevelChannelBlock& Channel =
				Asset->Channels.AddDefaulted_GetRef();
			Channel.LayerId = TransactionFixtureLayerId;
			Channel.CellSizeMultiple = 2;
			Channel.Data = { 0x11, 0x22, 0x33, 0x44 };

			Asset->MinimapTexture =
				NewObject<UTexture2D>(Asset);
			return Asset;
		}

		void AddValidNavChannel(
			USeinLevelDataDefaultAsset& Asset)
		{
			const int32 NumCells =
				Asset.Width * Asset.Height;
			FSeinLevelChannelBlock& Channel =
				Asset.Channels.AddDefaulted_GetRef();
			Channel.LayerId = TEXT("Nav");
			Channel.CellSizeMultiple = 1;
			Channel.Data.SetNumZeroed(2 * NumCells);
			for (int32 Index = 0;
				Index < NumCells;
				++Index)
			{
				Channel.Data[Index] = 1;
			}
		}

		void AddValidFogChannel(
			USeinLevelDataDefaultAsset& Asset)
		{
			const int32 Width = Asset.Width;
			const int32 Height = Asset.Height;
			const int32 NumCells = Width * Height;
			const int32 HeaderBytes =
				2 * sizeof(int32) + 3 * sizeof(int64);

			FSeinLevelChannelBlock& Channel =
				Asset.Channels.AddDefaulted_GetRef();
			Channel.LayerId = TEXT("FogOfWar");
			Channel.CellSizeMultiple = 1;
			Channel.Data.SetNumZeroed(
				HeaderBytes + 3 * NumCells);

			uint8* Out = Channel.Data.GetData();
			auto Write = [&Out](const auto& Value)
			{
				FMemory::Memcpy(
					Out, &Value, sizeof(Value));
				Out += sizeof(Value);
			};
			Write(Width);
			Write(Height);
			const int64 CellSize = Asset.CellSize.Value;
			const int64 MinHeight =
				Asset.HeightMin.Value;
			const int64 HeightQuantum =
				Asset.HeightQuantum.Value;
			Write(CellSize);
			Write(MinHeight);
			Write(HeightQuantum);
		}

		bool CaptureRuntimeSnapshot(
			const USeinLevelDataDefault& LevelData,
			FDefaultLevelDataRuntimeSnapshot& OutSnapshot)
		{
			OutSnapshot.bHasRuntimeData =
				LevelData.HasRuntimeData();
			OutSnapshot.Dimensions = LevelData.GetDimensions();
			OutSnapshot.CellSize =
				LevelData.GetFinestCellSize();
			OutSnapshot.Origin = LevelData.GetOrigin();
			OutSnapshot.MinimapTexture =
				LevelData.GetMinimapTexture();

			const int64 NumCells64 =
				static_cast<int64>(OutSnapshot.Dimensions.X)
				* static_cast<int64>(OutSnapshot.Dimensions.Y);
			if (NumCells64 <= 0 || NumCells64 > MAX_int32)
			{
				return false;
			}

			OutSnapshot.Surfaces.SetNum(
				static_cast<int32>(NumCells64));
			for (int32 CellIndex = 0;
				CellIndex < OutSnapshot.Surfaces.Num();
				++CellIndex)
			{
				if (!LevelData.GetCellSurface(
					CellIndex,
					OutSnapshot.Surfaces[CellIndex]))
				{
					return false;
				}
			}

			return LevelData.GetLayerChannel(
				TransactionFixtureLayerId,
				OutSnapshot.LayerData);
		}

		bool MatchesRuntimeSnapshot(
			const USeinLevelDataDefault& LevelData,
			const FDefaultLevelDataRuntimeSnapshot& Expected)
		{
			if (LevelData.HasRuntimeData()
					!= Expected.bHasRuntimeData
				|| LevelData.GetDimensions()
					!= Expected.Dimensions
				|| LevelData.GetFinestCellSize()
					!= Expected.CellSize
				|| LevelData.GetOrigin() != Expected.Origin
				|| LevelData.GetMinimapTexture()
					!= Expected.MinimapTexture)
			{
				return false;
			}

			TArray<uint8> LayerData;
			if (!LevelData.GetLayerChannel(
					TransactionFixtureLayerId,
					LayerData)
				|| LayerData != Expected.LayerData)
			{
				return false;
			}

			for (int32 CellIndex = 0;
				CellIndex < Expected.Surfaces.Num();
				++CellIndex)
			{
				FSeinLevelCellSurface Actual;
				if (!LevelData.GetCellSurface(
						CellIndex, Actual))
				{
					return false;
				}

				const FSeinLevelCellSurface& Wanted =
					Expected.Surfaces[CellIndex];
				if (Actual.Height != Wanted.Height
					|| Actual.NormalZ != Wanted.NormalZ
					|| Actual.bHasSurface != Wanted.bHasSurface
					|| Actual.bInBounds != Wanted.bInBounds
					|| Actual.TerrainTypeIndex
						!= Wanted.TerrainTypeIndex)
				{
					return false;
				}
			}
			return true;
		}
	}

	TEST(CanonicalBootstrapPreparesLevelDataBeforeWorldBeginPlay,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinLevelDataSubsystem* LevelData =
			UnrealWorld.GetSubsystem<USeinLevelDataSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(LevelData));
		ASSERT_THAT(IsFalse(
			LevelData->IsInitialRuntimeDataPrepared()));

		FString Error;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0x50524550,
			TEXT("LevelData.PreBeginPlayBootstrap"),
			&Error)));
		ASSERT_THAT(IsTrue(
			LevelData->IsInitialRuntimeDataPrepared()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::LocallyReady),
			static_cast<uint8>(
				World->GetMatchBootstrapState())));
		ASSERT_THAT(IsTrue(
			World->GetCanonicalStateContractDigest().IsValid()));
	}

	TEST(NativeLevelDataSubclassMustClaimExactStateCoverage,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FScopedLevelDataAdmissionClass Admission(
			USeinLevelDataDefaultInheritedUnclaimedTest::StaticClass());
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		TestRunner->AddExpectedError(
			TEXT("must explicitly override ComputeStaticEnvironmentDigest"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("transaction closed (failed)"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FString Error;
		ASSERT_THAT(IsFalse(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0x4C44554E,
			TEXT("LevelData.UnclaimedNativeSubclass"),
			&Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("ComputeStaticEnvironmentDigest"))));
	}

	TEST(ExplicitlyClaimedLevelDataSubclassBootstraps,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FScopedLevelDataAdmissionClass Admission(
			USeinLevelDataDefaultClaimedTest::StaticClass());
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FString Error;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0x4C44434C,
			TEXT("LevelData.ClaimedNativeSubclass"),
			&Error)));
		ASSERT_THAT(IsTrue(
			World->GetCanonicalStateContractDigest().IsValid()));
	}

	TEST(PostFreezeLevelDataGenerationDriftFailStops,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FScopedLevelDataAdmissionClass Admission(
			USeinLevelDataDefaultClaimedTest::StaticClass());
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinLevelDataSubsystem* LevelDataSubsystem =
			UnrealWorld.GetSubsystem<USeinLevelDataSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(LevelDataSubsystem));
		USeinLevelDataDefaultClaimedTest* LevelData =
			Cast<USeinLevelDataDefaultClaimedTest>(
				LevelDataSubsystem->GetLevelData());
		ASSERT_THAT(IsNotNull(LevelData));

		FString Error;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0x4C444452,
			TEXT("LevelData.GenerationDrift"),
			&Error)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World, &Error)));
		LevelData->ForceStaticMutationForTests();

		TestRunner->AddExpectedError(
			TEXT("Level Data static substrate mutated in place"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		const int32 TickBefore = World->GetCurrentTick();
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(TickBefore, World->GetCurrentTick()));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
	}

	TEST(MissingNavChannelBlocksCanonicalBootstrap,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FScopedDefaultStaticEnvironmentClasses
			StaticEnvironmentClasses;
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinLevelDataSubsystem* LevelData =
			UnrealWorld.GetSubsystem<USeinLevelDataSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		USeinFogOfWarSubsystem* Fog =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(LevelData));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Fog));

		ASeinLevelVolume* Volume =
			&Spawner.SpawnActor<ASeinLevelVolume>();
		USeinLevelDataDefaultAsset* Asset =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(Volume));
		ASSERT_THAT(IsNotNull(Asset));
		AddValidFogChannel(*Asset);
		Volume->BakedAsset = Asset;

		TestRunner->AddExpectedError(
			TEXT("Nav: rejected the unified Level Data substrate"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		ASSERT_THAT(IsTrue(
			LevelData->EnsureInitialRuntimeDataPrepared(
				UnrealWorld)));
		ASSERT_THAT(IsFalse(
			Navigation->
				IsInitialStaticEnvironmentPrepared()));
		ASSERT_THAT(IsTrue(
			Navigation->
				GetInitialStaticEnvironmentAdoptionResult().
					IsRejected()));
		ASSERT_THAT(IsTrue(
			Navigation->
				GetInitialStaticEnvironmentAdoptionResult().
					Detail.Contains(
						TEXT("missing the required Nav channel"))));
		ASSERT_THAT(IsTrue(
			Fog->IsInitialStaticEnvironmentPrepared()));

		FString Error;
		TestRunner->AddExpectedError(
			TEXT("Navigation rejected the initial static environment"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)."),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestMatchBootstrap::Materialize(
				*World,
				FSeinMatchSettings(),
				0x4D49534E,
				TEXT("LevelData.MissingNavChannel"),
				&Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(
				TEXT("missing the required Nav channel"))));
		ASSERT_THAT(IsFalse(
			World->GetCanonicalStateContractDigest().
				IsValid()));
	}

	TEST(MissingFogChannelBlocksCanonicalBootstrap,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FScopedDefaultStaticEnvironmentClasses
			StaticEnvironmentClasses;
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinLevelDataSubsystem* LevelData =
			UnrealWorld.GetSubsystem<USeinLevelDataSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		USeinFogOfWarSubsystem* Fog =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(LevelData));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Fog));

		ASeinLevelVolume* Volume =
			&Spawner.SpawnActor<ASeinLevelVolume>();
		USeinLevelDataDefaultAsset* Asset =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(Volume));
		ASSERT_THAT(IsNotNull(Asset));
		AddValidNavChannel(*Asset);
		Volume->BakedAsset = Asset;

		TestRunner->AddExpectedError(
			TEXT("FoW: rejected the unified Level Data substrate"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		ASSERT_THAT(IsTrue(
			LevelData->EnsureInitialRuntimeDataPrepared(
				UnrealWorld)));
		ASSERT_THAT(IsTrue(
			Navigation->
				IsInitialStaticEnvironmentPrepared()));
		ASSERT_THAT(IsFalse(
			Fog->IsInitialStaticEnvironmentPrepared()));
		ASSERT_THAT(IsTrue(
			Fog->
				GetInitialStaticEnvironmentAdoptionResult().
					IsRejected()));
		ASSERT_THAT(IsTrue(
			Fog->
				GetInitialStaticEnvironmentAdoptionResult().
					Detail.Contains(
						TEXT("missing the required FogOfWar channel"))));

		FString Error;
		TestRunner->AddExpectedError(
			TEXT("Fog rejected the initial static environment"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)."),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestMatchBootstrap::Materialize(
				*World,
				FSeinMatchSettings(),
				0x4D495346,
				TEXT("LevelData.MissingFogChannel"),
				&Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(
				TEXT("missing the required FogOfWar channel"))));
		ASSERT_THAT(IsFalse(
			World->GetCanonicalStateContractDigest().
				IsValid()));
	}

	TEST(LevelDataMutationFrontDoorsCloseAtStateContractFreeze,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		USeinLevelDataTestDouble* LevelData =
			NewObject<USeinLevelDataTestDouble>(&UnrealWorld);
		ASSERT_THAT(IsNotNull(LevelData));
		ASSERT_THAT(IsTrue(LevelData->LoadFromAsset(nullptr)));
		ASSERT_THAT(IsTrue(LevelData->BeginBake(&UnrealWorld)));
		ASSERT_THAT(AreEqual(1, LevelData->LoadCallCount));
		ASSERT_THAT(AreEqual(1, LevelData->BakeCallCount));

		FString Error;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0x4C564C44,
			TEXT("LevelData.StaticMutationGuard"),
			&Error)));
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Start(*World, &Error)));

		TestRunner->AddExpectedError(
			TEXT("not legal after the match StateContract freezes"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		ASSERT_THAT(IsFalse(LevelData->LoadFromAsset(nullptr)));
		ASSERT_THAT(IsFalse(LevelData->BeginBake(&UnrealWorld)));
		ASSERT_THAT(AreEqual(1, LevelData->LoadCallCount));
		ASSERT_THAT(AreEqual(1, LevelData->BakeCallCount));

		FGuid Root;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(Root, Error)));
		ASSERT_THAT(IsTrue(Root.IsValid()));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));
		World->StopSimulation();
	}

	TEST(DefaultLevelDataAssetRejectionIsTransactional,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();

		USeinLevelDataDefault* LevelData =
			NewObject<USeinLevelDataDefault>(&UnrealWorld);
		ASSERT_THAT(IsNotNull(LevelData));

		int32 MutationCount = 0;
		const FDelegateHandle MutationHandle =
			LevelData->OnLevelDataMutated.AddLambda(
				[&MutationCount]() { ++MutationCount; });

		USeinLevelDataDefaultAsset* ValidAsset =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(ValidAsset));
		ASSERT_THAT(IsTrue(
			LevelData->LoadFromAsset(ValidAsset)));
		ASSERT_THAT(AreEqual(1, MutationCount));

		FDefaultLevelDataRuntimeSnapshot ValidRuntime;
		ASSERT_THAT(IsTrue(
			CaptureRuntimeSnapshot(*LevelData, ValidRuntime)));

		TestRunner->AddExpectedError(
			TEXT("rejecting (re-bake needed)"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("current runtime substrate was left unchanged"),
			EAutomationExpectedErrorFlags::Contains,
			3,
			false);
		USeinLevelDataDefaultAsset* OverflowedDimensions =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(OverflowedDimensions));
		// The true product exceeds int32, while an int32 multiply wraps
		// to four and would appear to match the fixture arrays.
		OverflowedDimensions->Width = 4;
		OverflowedDimensions->Height = 1073741825;
		OverflowedDimensions->CellSize =
			FFixedPoint::FromInt(999);
		OverflowedDimensions->Origin =
			FFixedVector::ZeroVector;
		OverflowedDimensions->Channels[0].Data =
			{ 0xDE, 0xAD };

		ASSERT_THAT(IsFalse(
			LevelData->LoadFromAsset(
				OverflowedDimensions)));
		ASSERT_THAT(AreEqual(1, MutationCount));
		ASSERT_THAT(IsTrue(
			MatchesRuntimeSnapshot(*LevelData, ValidRuntime)));

		USeinLevelDataDefaultAsset* TruncatedBlob =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(TruncatedBlob));
		TruncatedBlob->SharedNormalZQ.Pop();
		TruncatedBlob->CellSize =
			FFixedPoint::FromInt(333);
		TruncatedBlob->Origin =
			FFixedVector(
				FFixedPoint::FromInt(1),
				FFixedPoint::FromInt(2),
				FFixedPoint::FromInt(3));
		TruncatedBlob->Channels[0].Data =
			{ 0xBE, 0xEF };

		ASSERT_THAT(IsFalse(
			LevelData->LoadFromAsset(TruncatedBlob)));
		ASSERT_THAT(AreEqual(1, MutationCount));
		ASSERT_THAT(IsTrue(
			MatchesRuntimeSnapshot(*LevelData, ValidRuntime)));

		ASSERT_THAT(IsFalse(
			LevelData->LoadFromAsset(nullptr)));
		ASSERT_THAT(AreEqual(1, MutationCount));
		ASSERT_THAT(IsTrue(
			MatchesRuntimeSnapshot(*LevelData, ValidRuntime)));

		LevelData->OnLevelDataMutated.Remove(
			MutationHandle);
	}

	TEST(DefaultLevelDataDigestIsContentExactAndChannelOrderIndependent,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinLevelDataDefault* LevelData =
			NewObject<USeinLevelDataDefault>(&UnrealWorld);
		ASSERT_THAT(IsNotNull(LevelData));

		USeinLevelDataDefaultAsset* First =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(First));
		FSeinLevelChannelBlock& ExtraFirst =
			First->Channels.AddDefaulted_GetRef();
		ExtraFirst.LayerId = TEXT("Alpha");
		ExtraFirst.CellSizeMultiple = 1;
		ExtraFirst.Data = { 7, 8, 9 };
		ASSERT_THAT(IsTrue(LevelData->LoadFromAsset(First)));
		FGuid FirstDigest;
		FString Error;
		ASSERT_THAT(IsTrue(LevelData->ComputeStaticEnvironmentDigest(
			FirstDigest, Error)));

		USeinLevelDataDefaultAsset* Reordered =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(Reordered));
		Reordered->Channels.InsertDefaulted(0);
		FSeinLevelChannelBlock& ExtraReordered =
			Reordered->Channels[0];
		ExtraReordered.LayerId = TEXT("Alpha");
		ExtraReordered.CellSizeMultiple = 1;
		ExtraReordered.Data = { 7, 8, 9 };
		ASSERT_THAT(IsTrue(LevelData->LoadFromAsset(Reordered)));
		FGuid ReorderedDigest;
		ASSERT_THAT(IsTrue(LevelData->ComputeStaticEnvironmentDigest(
			ReorderedDigest, Error)));
		ASSERT_THAT(IsTrue(FirstDigest == ReorderedDigest));

		Reordered->SharedHeightQ[2] += 1;
		ASSERT_THAT(IsTrue(LevelData->LoadFromAsset(Reordered)));
		FGuid ChangedDigest;
		ASSERT_THAT(IsTrue(LevelData->ComputeStaticEnvironmentDigest(
			ChangedDigest, Error)));
		ASSERT_THAT(IsFalse(ChangedDigest == FirstDigest));
	}

	TEST(DefaultLevelDataRejectsDuplicateLayerIdentityTransactionally,
		"SeinARTS.Unit.LevelData.StaticEnvironment")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinLevelDataDefault* LevelData =
			NewObject<USeinLevelDataDefault>(&UnrealWorld);
		ASSERT_THAT(IsNotNull(LevelData));
		USeinLevelDataDefaultAsset* Valid =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(Valid));
		ASSERT_THAT(IsTrue(LevelData->LoadFromAsset(Valid)));
		FGuid Before;
		FString Error;
		ASSERT_THAT(IsTrue(LevelData->ComputeStaticEnvironmentDigest(
			Before, Error)));

		USeinLevelDataDefaultAsset* Duplicate =
			MakeValidDefaultAsset(&UnrealWorld);
		ASSERT_THAT(IsNotNull(Duplicate));
		const FSeinLevelChannelBlock DuplicateChannel =
			Duplicate->Channels[0];
		Duplicate->Channels.Add(DuplicateChannel);
		TestRunner->AddExpectedError(
			TEXT("layer channels require unique"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("current runtime substrate was left unchanged"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(LevelData->LoadFromAsset(Duplicate)));
		FGuid After;
		ASSERT_THAT(IsTrue(LevelData->ComputeStaticEnvironmentDigest(
			After, Error)));
		ASSERT_THAT(IsTrue(Before == After));
	}
}
