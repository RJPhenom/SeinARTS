#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Containers/Ticker.h"
#include "Data/SeinReplayHeader.h"
#include "Data/SeinWheeledMovementData.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/FileManager.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Movement/SeinVehicleGymTestTypes.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinMovementSubsystem.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinSnapshotRestoreAuthority.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 CheckpointTick = 4;
	constexpr int32 ReplayEndTick = 8;

	FFixedVector Point(int32 X, int32 Y = 0, int32 Z = 0)
	{
		return FFixedVector(
			FFixedPoint::FromInt(X),
			FFixedPoint::FromInt(Y),
			FFixedPoint::FromInt(Z));
	}

	struct FScopedVehicleGymSettings
	{
		explicit FScopedVehicleGymSettings(
			const FSeinVehicleGymNavigationRecipe& Recipe)
			: Settings(GetMutableDefault<USeinARTSCoreSettings>())
			, SavedNavigationClass(Settings->NavigationClass)
			, bSavedAsyncPathfinding(Settings->bAsyncPathfinding)
			, SavedPathBudget(Settings->PathRequestsPerTickBudget)
		{
			USeinVehicleGymNavigation::InstallRecipe(Recipe);
			Settings->NavigationClass = FSoftClassPath(
				USeinVehicleGymNavigation::StaticClass());
			Settings->bAsyncPathfinding = false;
			Settings->PathRequestsPerTickBudget = 1024;
			Settings->ApplySimPerformanceCvars();
		}

		~FScopedVehicleGymSettings()
		{
			Settings->NavigationClass = SavedNavigationClass;
			Settings->bAsyncPathfinding = bSavedAsyncPathfinding;
			Settings->PathRequestsPerTickBudget = SavedPathBudget;
			Settings->ApplySimPerformanceCvars();
			USeinVehicleGymNavigation::ResetRecipe();
		}

		USeinARTSCoreSettings* Settings = nullptr;
		FSoftClassPath SavedNavigationClass;
		bool bSavedAsyncPathfinding = false;
		int32 SavedPathBudget = 0;
	};

	struct FScopedReplayFile
	{
		FString Path;

		~FScopedReplayFile()
		{
			if (!Path.IsEmpty())
			{
				IFileManager::Get().Delete(*Path, false, true);
			}
		}
	};

	FSeinReplayHeader MakeReplayHeader(USeinWorldSubsystem& World)
	{
		FSeinReplayHeader Header;
		SeinReplayCompatibility::StampCurrent(Header, World.GetWorld());
		Header.CommandProtocolDigest = World.GetCommandProtocolDigest();
		Header.MatchSettingsDigest = World.GetMatchSettingsDigest();
		Header.BootstrapReceipt = World.GetMatchBootstrapReceipt();
		Header.ConfigFingerprint = World.GetConfigFingerprint();
		Header.RandomSeed = World.GetSessionSeed();
		Header.SettingsSnapshot = World.GetMatchSettings();
		Header.StartTick = World.GetCurrentTick();
		Header.RecordedAt = FDateTime::UtcNow();
		return Header;
	}

	bool ReadFixedState(
		const UObject& Object,
		FName PropertyName,
		FFixedPoint& OutValue)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(
			Object.GetClass(), PropertyName);
		if (!Property || Property->Struct != FFixedPoint::StaticStruct())
		{
			return false;
		}
		OutValue = *Property->ContainerPtrToValuePtr<FFixedPoint>(&Object);
		return true;
	}

	void RecordRequiredEmptyTurns(
		USeinReplayWriter& Writer,
		int32 EndTick)
	{
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		const int32 TicksPerTurn = Settings && Settings->TurnRate > 0
			? FMath::Max(
				1, Settings->SimulationTickRate / Settings->TurnRate)
			: 1;
		const int32 FirstTurn = Settings && Settings->InputDelayTurns > 0
			? Settings->InputDelayTurns
			: 3;
		for (int32 Turn = FirstTurn;
			Turn <= EndTick / TicksPerTurn;
			++Turn)
		{
			Writer.RecordTurn(Turn, {});
		}
	}

	bool MaterializeAndStartWheeledMove(
		USeinWorldSubsystem& World,
		const FSeinVehicleGymNavigationRecipe& Recipe,
		FName FixtureId,
		FSeinEntityHandle& OutVehicle,
		FString& OutError)
	{
		int32 AbilityId = INDEX_NONE;
		if (!SeinTestMatchBootstrap::Materialize(
			World,
			[&]()
			{
				OutVehicle = World.SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());

				FSeinMovementComponent Movement;
				Movement.MovementClass = FSoftClassPath(
					USeinWheeledVehicleMovement::StaticClass());
				Movement.TopSpeed = FFixedPoint::FromInt(900);
				Movement.TurnRate = FFixedPoint::FromInt(3)
					/ FFixedPoint::FromInt(2);
				Movement.ReverseTopSpeed =
					Movement.TopSpeed * FFixedPoint::Half;
				Movement.ReverseEngageDistanceThreshold =
					FFixedPoint::FromInt(600);
				FSeinWheeledMovementData Wheeled;
				Wheeled.Wheelbase = FFixedPoint::FromInt(240);
				Movement.MovementClassData =
					FInstancedStruct::Make(Wheeled);

				FSeinNavigationComponent Navigation;
				Navigation.FallbackFootprintRadius =
					FFixedPoint::FromInt(85);
				Navigation.AcceptanceRadius = FFixedPoint::FromInt(80);
				Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
				Navigation.OffPathThreshold = FFixedPoint::FromInt(10000);

				World.AddComponent(OutVehicle, Movement);
				World.AddComponent(OutVehicle, Navigation);
				World.AddComponent(OutVehicle, FSeinAbilityComponent());
				AbilityId = USeinAbilityBPFL::SeinGrantAbility(
					&World,
					OutVehicle,
					USeinVehicleGymAbility::StaticClass());
			},
			FSeinMatchSettings(),
			0x4D505246,
			FixtureId,
			&OutError)
			|| !SeinTestMatchBootstrap::Authorize(World, &OutError)
			|| !SeinTestMatchBootstrap::Start(World, &OutError))
		{
			return false;
		}

		USeinAbility* Ability = World.GetAbilityInstance(AbilityId);
		if (!Ability)
		{
			OutError = TEXT("Failed to resolve Movement+ test ability");
			return false;
		}

		auto SimScope = FSeinSimContextTestAccess::Enter(World);
		if (!Ability->ActivateAbility(
			FSeinEntityHandle::Invalid(), Recipe.Route.Last()))
		{
			OutError = TEXT("Failed to activate Movement+ test ability");
			return false;
		}
		USeinMoveToProxy* Proxy = USeinMoveToProxy::SeinMoveTo(
			Ability, Recipe.Route.Last());
		if (!Proxy)
		{
			OutError = TEXT("Failed to create Movement+ MoveTo proxy");
			return false;
		}
		Proxy->Activate();
		return true;
	}
}

TEST(MovementPlusReplayFileCheckpointRestoresExactCompletionState,
	"SeinARTS.Determinism.MovementPlus.ReplayFile")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("MovementPlusReplayFile");
	Recipe.Route = {Point(-400)};
	FScopedVehicleGymSettings SettingsScope(Recipe);

	FActorTestSpawner SourceSpawner;
	USeinWorldSubsystem* Source =
		SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Source));

	FSeinEntityHandle Vehicle;
	int32 AbilityId = INDEX_NONE;
	FString Error;
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
		*Source,
		[&]()
		{
			Vehicle = Source->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());

			FSeinMovementComponent Movement;
			Movement.MovementClass = FSoftClassPath(
				USeinWheeledVehicleMovement::StaticClass());
			Movement.TopSpeed = FFixedPoint::FromInt(900);
			Movement.TurnRate = FFixedPoint::FromInt(3)
				/ FFixedPoint::FromInt(2);
			Movement.ReverseTopSpeed =
				Movement.TopSpeed * FFixedPoint::Half;
			Movement.ReverseEngageDistanceThreshold =
				FFixedPoint::FromInt(600);
			FSeinWheeledMovementData Wheeled;
			Wheeled.Wheelbase = FFixedPoint::FromInt(240);
			Movement.MovementClassData =
				FInstancedStruct::Make(Wheeled);

			FSeinNavigationComponent Navigation;
			Navigation.FallbackFootprintRadius =
				FFixedPoint::FromInt(85);
			Navigation.AcceptanceRadius = FFixedPoint::FromInt(80);
			Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
			Navigation.OffPathThreshold = FFixedPoint::FromInt(10000);

			Source->AddComponent(Vehicle, Movement);
			Source->AddComponent(Vehicle, Navigation);
			Source->AddComponent(Vehicle, FSeinAbilityComponent());
			AbilityId = USeinAbilityBPFL::SeinGrantAbility(
				Source, Vehicle, USeinVehicleGymAbility::StaticClass());
		},
		FSeinMatchSettings(),
		0x4D505246,
		TEXT("MovementPlus.ReplayFile"),
		&Error)));
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Authorize(*Source, &Error)));

	USeinReplayWriter* Writer = NewObject<USeinReplayWriter>(Source);
	ASSERT_THAT(IsNotNull(Writer));
	Writer->StartRecording(MakeReplayHeader(*Source));
	ASSERT_THAT(IsTrue(Writer->IsRecording()));
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source, &Error)));
	ASSERT_THAT(IsTrue(Writer->CaptureCheckpoint(/*bRequired=*/true)));
	RecordRequiredEmptyTurns(*Writer, ReplayEndTick);

	USeinAbility* Ability = Source->GetAbilityInstance(AbilityId);
	ASSERT_THAT(IsNotNull(Ability));
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Source);
		ASSERT_THAT(IsTrue(Ability->ActivateAbility(
			FSeinEntityHandle::Invalid(), Recipe.Route.Last())));
		USeinMoveToProxy* Proxy = USeinMoveToProxy::SeinMoveTo(
			Ability, Recipe.Route.Last());
		ASSERT_THAT(IsNotNull(Proxy));
		Proxy->Activate();
	}

	for (int32 ExpectedTick = 1;
		ExpectedTick <= ReplayEndTick;
		++ExpectedTick)
	{
		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(ExpectedTick, Source->GetCurrentTick()));
		Writer->ObserveCompletedTick(ExpectedTick);
		if (ExpectedTick == CheckpointTick)
		{
			const FSeinEntity* MidVehicle = Source->GetEntity(Vehicle);
			ASSERT_THAT(IsNotNull(MidVehicle));
			ASSERT_THAT(IsTrue(
				MidVehicle->Transform.GetLocation() != Point(0)));
			ASSERT_THAT(AreEqual(
				1, Source->LatentActionManager->GetActiveActionCount()));
			ASSERT_THAT(IsTrue(
				Writer->CaptureCheckpoint(/*bRequired=*/false)));
		}
	}

	FGuid SourceRoot;
	ASSERT_THAT(IsTrue(
		Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
	const int32 SourceStateHash = Source->ComputeStateHash();
	const FSeinEntity* SourceVehicle = Source->GetEntity(Vehicle);
	const FSeinMovementComponent* SourceMovement =
		Source->GetComponent<FSeinMovementComponent>(Vehicle);
	USeinMovementSubsystem* SourceMovementSubsystem =
		SourceSpawner.GetWorld().GetSubsystem<USeinMovementSubsystem>();
	ASSERT_THAT(IsNotNull(SourceVehicle));
	ASSERT_THAT(IsNotNull(SourceMovement));
	ASSERT_THAT(IsNotNull(SourceMovementSubsystem));
	UObject* SourceMovementInstance =
		SourceMovementSubsystem->FindMovementInstance(Vehicle);
	ASSERT_THAT(IsNotNull(SourceMovementInstance));
	FFixedPoint SourceSteer;
	ASSERT_THAT(IsTrue(ReadFixedState(
		*SourceMovementInstance, TEXT("CurrentSteer"), SourceSteer)));
	const FFixedTransform SourceTransform = SourceVehicle->Transform;
	const FFixedVector SourceVelocity = SourceMovement->Velocity;
	const int32 SourceActionCount =
		Source->LatentActionManager->GetActiveActionCount();

	Source->StopSimulation();
	FScopedReplayFile ReplayFile{Writer->FinishRecording()};
	ASSERT_THAT(IsFalse(ReplayFile.Path.IsEmpty()));
	ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(*ReplayFile.Path)));

	FActorTestSpawner TargetSpawner;
	USeinWorldSubsystem* Target =
		TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Target));
	USeinReplayReader* Reader =
		NewObject<USeinReplayReader>(&TargetSpawner.GetWorld());
	ASSERT_THAT(IsNotNull(Reader));
	ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
	ASSERT_THAT(IsTrue(Reader->PlayFromTick(CheckpointTick + 1)));
	for (int32 Pump = 0;
		Pump < ReplayEndTick * 4 && Reader->IsPlaying();
		++Pump)
	{
		FTSTicker::GetCoreTicker().Tick(
			Target->GetFixedDeltaTimeSeconds());
	}
	ASSERT_THAT(AreEqual(ReplayEndTick, Target->GetCurrentTick()));
	ASSERT_THAT(IsFalse(Reader->IsPlaying()));
	ASSERT_THAT(IsTrue(Target->StartSimulation()));

	FGuid TargetRoot;
	ASSERT_THAT(IsTrue(
		Target->ComputeCanonicalStateRoot(TargetRoot, Error)));
	ASSERT_THAT(AreEqual(
		SourceRoot.ToString(EGuidFormats::Digits),
		TargetRoot.ToString(EGuidFormats::Digits)));
	ASSERT_THAT(AreEqual(SourceStateHash, Target->ComputeStateHash()));

	const FSeinEntity* TargetVehicle = Target->GetEntity(Vehicle);
	const FSeinMovementComponent* TargetMovement =
		Target->GetComponent<FSeinMovementComponent>(Vehicle);
	USeinMovementSubsystem* TargetMovementSubsystem =
		TargetSpawner.GetWorld().GetSubsystem<USeinMovementSubsystem>();
	ASSERT_THAT(IsNotNull(TargetVehicle));
	ASSERT_THAT(IsNotNull(TargetMovement));
	ASSERT_THAT(IsNotNull(TargetMovementSubsystem));
	ASSERT_THAT(IsTrue(TargetVehicle->Transform == SourceTransform));
	ASSERT_THAT(IsTrue(TargetMovement->Velocity == SourceVelocity));
	ASSERT_THAT(AreEqual(
		SourceActionCount,
		Target->LatentActionManager->GetActiveActionCount()));
	UObject* TargetMovementInstance =
		TargetMovementSubsystem->FindMovementInstance(Vehicle);
	ASSERT_THAT(IsNotNull(TargetMovementInstance));
	FFixedPoint TargetSteer;
	ASSERT_THAT(IsTrue(ReadFixedState(
		*TargetMovementInstance, TEXT("CurrentSteer"), TargetSteer)));
	ASSERT_THAT(IsTrue(SourceSteer == TargetSteer));

	Target->StopSimulation();
}

TEST(MovementPlusReconnectTransferContinuesToIdenticalCanonicalState,
	"SeinARTS.Determinism.MovementPlus.Reconnect")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("MovementPlusReconnect");
	Recipe.Route = {Point(-400)};
	FScopedVehicleGymSettings SettingsScope(Recipe);

	FActorTestSpawner SourceSpawner;
	USeinWorldSubsystem* Source =
		SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Source));

	FSeinEntityHandle Vehicle;
	FString Error;
	ASSERT_THAT(IsTrue(MaterializeAndStartWheeledMove(
		*Source,
		Recipe,
		TEXT("MovementPlus.Reconnect"),
		Vehicle,
		Error)));

	for (int32 ExpectedTick = 1;
		ExpectedTick <= CheckpointTick;
		++ExpectedTick)
	{
		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(ExpectedTick, Source->GetCurrentTick()));
	}

	FSeinWorldSnapshot Checkpoint;
	FSeinWorldSnapshotReferenceGuard CheckpointGuard(Checkpoint);
	Source->CaptureSnapshot(Checkpoint);
	ASSERT_THAT(AreEqual(CheckpointTick, Checkpoint.CurrentTick));

	TArray<uint8> EnvelopeBytes;
	FSeinSnapshotEnvelopeMetadata EnvelopeMetadata;
	ASSERT_THAT(IsTrue(SeinSnapshotTransfer::EncodeCheckpointEnvelope(
		Checkpoint, EnvelopeBytes, EnvelopeMetadata, Error)));
	FSeinWorldSnapshot Transferred;
	FSeinWorldSnapshotReferenceGuard TransferredGuard(Transferred);
	FSeinSnapshotEnvelopeMetadata TransferredMetadata;
	ASSERT_THAT(IsTrue(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
		EnvelopeBytes, Transferred, TransferredMetadata, Error)));

	for (int32 ExpectedTick = CheckpointTick + 1;
		ExpectedTick <= ReplayEndTick;
		++ExpectedTick)
	{
		FTSTicker::GetCoreTicker().Tick(
			Source->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(ExpectedTick, Source->GetCurrentTick()));
	}

	FGuid SourceRoot;
	ASSERT_THAT(IsTrue(
		Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
	const int32 SourceStateHash = Source->ComputeStateHash();
	const FSeinEntity* SourceVehicle = Source->GetEntity(Vehicle);
	const FSeinMovementComponent* SourceMovement =
		Source->GetComponent<FSeinMovementComponent>(Vehicle);
	USeinMovementSubsystem* SourceMovementSubsystem =
		SourceSpawner.GetWorld().GetSubsystem<USeinMovementSubsystem>();
	ASSERT_THAT(IsNotNull(SourceVehicle));
	ASSERT_THAT(IsNotNull(SourceMovement));
	ASSERT_THAT(IsNotNull(SourceMovementSubsystem));
	UObject* SourceMovementInstance =
		SourceMovementSubsystem->FindMovementInstance(Vehicle);
	ASSERT_THAT(IsNotNull(SourceMovementInstance));
	FFixedPoint SourceSteer;
	ASSERT_THAT(IsTrue(ReadFixedState(
		*SourceMovementInstance, TEXT("CurrentSteer"), SourceSteer)));
	const FFixedTransform SourceTransform = SourceVehicle->Transform;
	const FFixedVector SourceVelocity = SourceMovement->Velocity;
	const int32 SourceActionCount =
		Source->LatentActionManager->GetActiveActionCount();
	Source->StopSimulation();

	FActorTestSpawner TargetSpawner;
	USeinWorldSubsystem* Target =
		TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Target));
	ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
		*Target,
		Transferred,
		FSeinSnapshotRestoreOptions(
			ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent,
			ESeinSnapshotResumePolicy::RemainStopped),
		&Error)));
	ASSERT_THAT(AreEqual(CheckpointTick, Target->GetCurrentTick()));
	ASSERT_THAT(IsTrue(Target->StartSimulation()));

	for (int32 ExpectedTick = CheckpointTick + 1;
		ExpectedTick <= ReplayEndTick;
		++ExpectedTick)
	{
		FTSTicker::GetCoreTicker().Tick(
			Target->GetFixedDeltaTimeSeconds());
		ASSERT_THAT(AreEqual(ExpectedTick, Target->GetCurrentTick()));
	}

	FGuid TargetRoot;
	ASSERT_THAT(IsTrue(
		Target->ComputeCanonicalStateRoot(TargetRoot, Error)));
	ASSERT_THAT(IsTrue(TargetRoot == SourceRoot));
	ASSERT_THAT(AreEqual(SourceStateHash, Target->ComputeStateHash()));

	const FSeinEntity* TargetVehicle = Target->GetEntity(Vehicle);
	const FSeinMovementComponent* TargetMovement =
		Target->GetComponent<FSeinMovementComponent>(Vehicle);
	USeinMovementSubsystem* TargetMovementSubsystem =
		TargetSpawner.GetWorld().GetSubsystem<USeinMovementSubsystem>();
	ASSERT_THAT(IsNotNull(TargetVehicle));
	ASSERT_THAT(IsNotNull(TargetMovement));
	ASSERT_THAT(IsNotNull(TargetMovementSubsystem));
	ASSERT_THAT(IsTrue(TargetVehicle->Transform == SourceTransform));
	ASSERT_THAT(IsTrue(TargetMovement->Velocity == SourceVelocity));
	ASSERT_THAT(AreEqual(
		SourceActionCount,
		Target->LatentActionManager->GetActiveActionCount()));
	UObject* TargetMovementInstance =
		TargetMovementSubsystem->FindMovementInstance(Vehicle);
	ASSERT_THAT(IsNotNull(TargetMovementInstance));
	FFixedPoint TargetSteer;
	ASSERT_THAT(IsTrue(ReadFixedState(
		*TargetMovementInstance, TEXT("CurrentSteer"), TargetSteer)));
	ASSERT_THAT(IsTrue(SourceSteer == TargetSteer));

	Target->StopSimulation();
}
