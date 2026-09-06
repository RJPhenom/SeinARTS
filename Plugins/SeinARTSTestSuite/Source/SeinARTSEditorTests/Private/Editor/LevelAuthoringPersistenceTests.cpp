/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         LevelAuthoringPersistenceTests.cpp
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Verifies editor grid replacement and saved player-start transforms.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "GameMode/SeinPlayerStart.h"
#include "SeinLevelDataDefault.h"
#include "SeinLevelDataDefaultAsset.h"
#include "SeinLevelDataSubsystem.h"
#include "SeinNavigationAStar.h"
#include "SeinNavigationSubsystem.h"
#include "Default/SeinFogOfWarDefault.h"
#include "SeinFogOfWarSubsystem.h"
#include "Settings/PluginSettings.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UnrealType.h"

namespace UE::SeinARTSTests::LevelAuthoring
{
	struct FEditorWorldSpawner : FActorTestSpawner
	{
		UWorld* CreateWorld() override
		{
			const FName Name = MakeUniqueObjectName(nullptr, UWorld::StaticClass());
			UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, Name, GetTransientPackage());
			World->AddToRoot();
			GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
			return World;
		}
	};

	struct FDefaultLayers
	{
		USeinARTSCoreSettings* Settings = GetMutableDefault<USeinARTSCoreSettings>();
		FSoftClassPath Level = Settings->LevelDataClass;
		FSoftClassPath Nav = Settings->NavigationClass;
		FSoftClassPath Fog = Settings->FogOfWarClass;
		FDefaultLayers()
		{
			Settings->LevelDataClass = FSoftClassPath(USeinLevelDataDefault::StaticClass());
			Settings->NavigationClass = FSoftClassPath(USeinNavigationAStar::StaticClass());
			Settings->FogOfWarClass = FSoftClassPath(USeinFogOfWarDefault::StaticClass());
		}
		~FDefaultLayers()
		{
			Settings->LevelDataClass = Level;
			Settings->NavigationClass = Nav;
			Settings->FogOfWarClass = Fog;
		}
	};

	USeinLevelDataDefaultAsset* MakeGrid(UObject* Outer, bool bBlocked)
	{
		USeinLevelDataDefaultAsset* Asset = NewObject<USeinLevelDataDefaultAsset>(Outer);
		Asset->Width = Asset->Height = 2;
		Asset->CellSize = FFixedPoint::FromInt(100);
		Asset->HeightQuantum = FFixedPoint::One;
		Asset->SharedHeightQ = {0, 0, 0, 0};
		Asset->SharedNormalZQ = {255, 255, 255, 255};
		Asset->CellFlags = {3, 3, 3, 3};
		FSeinLevelChannelBlock& Nav = Asset->Channels.AddDefaulted_GetRef();
		Nav.LayerId = TEXT("Nav");
		Nav.Data = {static_cast<uint8>(bBlocked ? 0 : 1), 1, 1, 1, 0, 0, 0, 0};
		FSeinLevelChannelBlock& Fog = Asset->Channels.AddDefaulted_GetRef();
		Fog.LayerId = TEXT("FogOfWar");
		Fog.Data.SetNumZeroed(32 + 3 * 4);
		uint8* Out = Fog.Data.GetData();
		auto Write = [&Out](const auto& Value)
		{
			FMemory::Memcpy(Out, &Value, sizeof(Value));
			Out += sizeof(Value);
		};
		Write(Asset->Width);
		Write(Asset->Height);
		Write(Asset->CellSize.Value);
		Write(Asset->HeightMin.Value);
		Write(Asset->HeightQuantum.Value);
		Fog.Data[32 + 4] = bBlocked ? 100 : 0;
		Fog.Data[32 + 8] = bBlocked ? 2 : 0;
		return Asset;
	}

	TEST(EditorGridReplacementClearsBlockersWithoutBeginPlay, "SeinARTS.Editor.LevelAuthoring")
	{
		FDefaultLayers Settings;
		FEditorWorldSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		USeinLevelDataSubsystem* Level = World.GetSubsystem<USeinLevelDataSubsystem>();
		USeinNavigation* Nav = World.GetSubsystem<USeinNavigationSubsystem>()->GetNavigation();
		USeinFogOfWar* Fog = World.GetSubsystem<USeinFogOfWarSubsystem>()->GetFogOfWar();
		ASSERT_THAT(IsNotNull(Level));
		ASSERT_THAT(IsNotNull(Nav));
		ASSERT_THAT(IsNotNull(Fog));
		ASSERT_THAT(IsFalse(Level->IsInitialRuntimeDataPrepared()));
		for (bool bBlocked : {true, false, true, false})
		{
			// This is the same public adoption/broadcast used by a completed bake.
			ASSERT_THAT(IsTrue(Level->GetLevelData()->LoadFromAsset(MakeGrid(&World, bBlocked))));
			ASSERT_THAT(IsFalse(Level->IsInitialRuntimeDataPrepared()));
			TArray<FVector> Centers;
			TArray<FColor> Colors;
			float Extent = 0;
			Fog->CollectDebugCellQuads(FSeinPlayerID(), 1, Centers, Colors, Extent);
			ASSERT_THAT(AreEqual(4, Colors.Num()));
			ASSERT_THAT(IsTrue(Colors[0] == (bBlocked ? FColor(200, 0, 0) : FColor::Black)));
			ASSERT_THAT(IsTrue(Nav->HasRuntimeData()));
			Centers.Reset();
			Colors.Reset();
			Nav->CollectDebugCellQuads(Centers, Colors, Extent);
			ASSERT_THAT(AreEqual(4, Colors.Num()));
			ASSERT_THAT(IsTrue((Colors[0] != Colors[1]) == bBlocked));
		}
	}

	TEST(PlayerStartDetailsAndParentMovementRefreshSnapshot, "SeinARTS.Editor.LevelAuthoring")
	{
		FEditorWorldSpawner Spawner;
		ASeinPlayerStart& Start = Spawner.SpawnActor<ASeinPlayerStart>();
		ASSERT_THAT(IsTrue(Start.bSimTransformBaked));
		USceneComponent* Root = Start.GetRootComponent();
		FProperty* Location = FindFProperty<FProperty>(USceneComponent::StaticClass(), TEXT("RelativeLocation"));
		ASSERT_THAT(IsNotNull(Location));
		Root->PreEditChange(Location);
		Root->SetRelativeLocation_Direct(FVector(1234, -567, 90));
		FPropertyChangedEvent Event(Location, EPropertyChangeType::ValueSet);
		Root->PostEditChangeProperty(Event);
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == FFixedTransform::FromTransform(Start.GetActorTransform())));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform.Location == FFixedVector::FromVector(FVector(1234, -567, 90))));

		ASeinPlayerStart& Parent = Spawner.SpawnActor<ASeinPlayerStart>();
		Start.AttachToActor(&Parent, FAttachmentTransformRules::KeepWorldTransform);
		Parent.SetActorLocationAndRotation(FVector(500, 600, 700), FRotator(0, 35, 0));
		Parent.SetActorScale3D(FVector(2));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == FFixedTransform::FromTransform(Start.GetActorTransform())));
	}

	TEST(PlayerStartRegistrationAndSaveRepairAlreadyBakedSnapshot, "SeinARTS.Editor.LevelAuthoring")
	{
		FEditorWorldSpawner Spawner;
		ASeinPlayerStart& Start = Spawner.SpawnActor<ASeinPlayerStart>();
		Start.SetActorLocation(FVector(900, 800, 700));
		const FFixedTransform Expected = FFixedTransform::FromTransform(Start.GetActorTransform());
		Start.PlacedSimTransform = FFixedTransform();
		Start.ReregisterAllComponents();
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == Expected));
		Start.PlacedSimTransform = FFixedTransform();
		FObjectSaveContextData SaveData;
		Start.PreSave(FObjectPreSaveContext(SaveData));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == Expected));
		Start.RerunConstructionScripts();
		Start.SetActorLocation(FVector(700, 600, 500));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == FFixedTransform::FromTransform(Start.GetActorTransform())));
	}

	TEST(PlayerStartRuntimeWorldsPreserveSerializedBits, "SeinARTS.Editor.LevelAuthoring")
	{
		FActorTestSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		ASeinPlayerStart& Start = Spawner.SpawnActor<ASeinPlayerStart>();
		Start.PlacedSimTransform = FFixedTransform();
		Start.bSimTransformBaked = true;
		const FFixedTransform Saved = Start.PlacedSimTransform;
		for (EWorldType::Type Type : {EWorldType::Game, EWorldType::PIE})
		{
			TGuardValue<TEnumAsByte<EWorldType::Type>> WorldType(World.WorldType, Type);
			Start.ReregisterAllComponents();
			Start.SetActorLocation(FVector(100, 200, 300));
			Start.PostEditMove(true);
			FObjectSaveContextData SaveData;
			Start.PreSave(FObjectPreSaveContext(SaveData));
			ASSERT_THAT(IsTrue(Start.PlacedSimTransform == Saved));
		}
	}

	TEST(PlayerStartUndoRedoRestoresAuthoredSpawn, "SeinARTS.Editor.LevelAuthoring")
	{
		FEditorWorldSpawner Spawner;
		ASeinPlayerStart& Start = Spawner.SpawnActor<ASeinPlayerStart>();
		Start.SetFlags(RF_Transactional);
		Start.GetRootComponent()->SetFlags(RF_Transactional);
		const FFixedTransform Before = Start.PlacedSimTransform;
		{
			FScopedTransaction Transaction(FText::FromString(TEXT("Test player start movement")));
			Start.Modify();
			Start.GetRootComponent()->Modify();
			Start.SetActorLocation(FVector(1200, 3400, 560));
		}
		const FFixedTransform After = Start.PlacedSimTransform;
		ASSERT_THAT(IsTrue(After != Before));
		ASSERT_THAT(IsTrue(GEditor->UndoTransaction()));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == Before));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == FFixedTransform::FromTransform(Start.GetActorTransform())));
		ASSERT_THAT(IsTrue(GEditor->RedoTransaction()));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == After));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == FFixedTransform::FromTransform(Start.GetActorTransform())));
	}

	TEST(PlayerStartBlueprintRecompileKeepsTransformSubscription, "SeinARTS.Editor.LevelAuthoring")
	{
		FEditorWorldSpawner Spawner;
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ASeinPlayerStart::StaticClass(), GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass()),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		ASSERT_THAT(IsNotNull(Blueprint));
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		ASeinPlayerStart& Start = Spawner.SpawnActor<ASeinPlayerStart>(FActorSpawnParameters(), Blueprint->GeneratedClass);
		const FName ActorName = Start.GetFName();
		Start.SetActorLocation(FVector(1000, 2000, 300));
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		ASeinPlayerStart* Reinstanced = FindObject<ASeinPlayerStart>(Spawner.GetWorld().PersistentLevel, *ActorName.ToString());
		ASSERT_THAT(IsNotNull(Reinstanced));
		ASSERT_THAT(IsTrue(Reinstanced->PlacedSimTransform == FFixedTransform::FromTransform(Reinstanced->GetActorTransform())));
		Reinstanced->GetRootComponent()->SetRelativeLocation(FVector(4000, 5000, 600));
		ASSERT_THAT(IsTrue(Reinstanced->PlacedSimTransform.Location == FFixedVector::FromVector(FVector(4000, 5000, 600))));
	}

	TEST(PlayerStartDiskSaveReloadRetainsRepairedSnapshot, "SeinARTS.Editor.LevelAuthoring")
	{
		struct FDiskWorldSpawner : FEditorWorldSpawner
		{
			UWorld* CreateWorld() override
			{
				UPackage* Package = CreatePackage(*(TEXT("/Temp/SeinPlayerStartSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
				UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("AuthoringSave"), Package);
				World->SetFlags(RF_Public | RF_Standalone);
				World->AddToRoot();
				GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
				return World;
			}
		};
		FDiskWorldSpawner Spawner;
		UWorld& World = Spawner.GetWorld();
		ASeinPlayerStart& Start = Spawner.SpawnActor<ASeinPlayerStart>();
		Start.SetActorTransform(FTransform(FRotator(0, 45, 0), FVector(345, 678, 910), FVector(2)));
		const FFixedTransform Expected = FFixedTransform::FromTransform(Start.GetActorTransform());
		Start.PlacedSimTransform = FFixedTransform(); // Reproduce the old saved-stale state.
		const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
			/ TEXT("Automation") / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".umap")));
		struct FDeleteFile
		{
			FString Path;
			~FDeleteFile() { IFileManager::Get().Delete(*Path); }
		} Cleanup{Filename};
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		ASSERT_THAT(IsTrue(UPackage::SavePackage(World.GetPackage(), &World, *Filename, Args)));
		ASSERT_THAT(IsTrue(Start.PlacedSimTransform == Expected));
		UPackage* Destination = CreatePackage(*(TEXT("/Temp/SeinPlayerStartReload_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		UPackage* LoadedPackage = LoadPackage(Destination, *Filename, LOAD_None);
		ASSERT_THAT(IsNotNull(LoadedPackage));
		UWorld* LoadedWorld = UWorld::FindWorldInPackage(LoadedPackage);
		ASSERT_THAT(IsNotNull(LoadedWorld));
		ASeinPlayerStart* LoadedStart = nullptr;
		for (AActor* Actor : LoadedWorld->PersistentLevel->Actors)
		{
			if (ASeinPlayerStart* Candidate = Cast<ASeinPlayerStart>(Actor)) LoadedStart = Candidate;
		}
		ASSERT_THAT(IsNotNull(LoadedStart));
		ASSERT_THAT(IsTrue(LoadedStart != &Start));
		ASSERT_THAT(IsTrue(LoadedStart->bSimTransformBaked));
		ASSERT_THAT(IsTrue(LoadedStart->PlacedSimTransform == Expected));
	}
}
