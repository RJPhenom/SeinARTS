#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SeinAbilityPayload.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/IConsoleManager.h"
#include "Input/SeinCommandWireCodec.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinActivationInputTestTypes.h"

USeinActivationInputTestAbility::USeinActivationInputTestAbility()
{
	AbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
	TargetType = ESeinAbilityTargetType::None;
	ExposeActivationInput(GET_MEMBER_NAME_CHECKED(USeinActivationInputTestAbility, QueueIndex));
	ExposeActivationInput(GET_MEMBER_NAME_CHECKED(USeinActivationInputTestAbility, Offset));
	ExposeActivationInput(GET_MEMBER_NAME_CHECKED(USeinActivationInputTestAbility, Choices));
}
bool USeinActivationInputTestAbility::CanActivateWithInputs_Implementation(const FSeinAbilityActivationInputs& Inputs) const
{
	TStrongObjectPtr<USeinActivationInputTestAbility> Candidate(NewObject<USeinActivationInputTestAbility>());
	FString Error;
	return Inputs.Decode(*Candidate, Error) && Candidate->QueueIndex >= 0;
}
void USeinActivationInputTestAbility::OnActivate_Implementation()
{
	Total += QueueIndex;
	++Activations;
	EndAbility();
}

namespace
{
	FSeinAbilityActivationInputs Make(int32 Index)
	{
		FSeinAbilityActivationInputs Result;
		FString Error;
		check(SeinMakeAbilityInputs<USeinActivationInputTestAbility>([Index](auto& A) { A.QueueIndex = Index; A.Total = 999; }, Result, Error));
		return Result;
	}
	struct FFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		FSeinEntityHandle Entity;
		int32 ID = INDEX_NONE;
		~FFixture() { if (World) World->StopSimulation(); }
		bool Initialize()
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			return World && SeinTestMatchBootstrap::Materialize(*World, [&]()
			{
				World->RegisterPlayer(FSeinPlayerID(1), FSeinFactionID(1));
				Entity = World->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID(1));
				World->AddComponent(Entity, FSeinAbilityPayload());
				ID = USeinAbilityBPFL::SeinGrantAbility(World, Entity, USeinActivationInputTestAbility::StaticClass());
			}, FSeinMatchSettings(), 9876, TEXT("ActivationInputs"))
				&& ID != INDEX_NONE && SeinTestMatchBootstrap::Start(*World);
		}
		USeinActivationInputTestAbility* Ability() const { return Cast<USeinActivationInputTestAbility>(World->GetAbilityInstance(ID)); }
		void Queue(const FSeinAbilityActivationInputs& Inputs)
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			FSeinCommand Cmd = FSeinCommand::MakeAbilityCommand(FSeinPlayerID(1), Entity, Ability()->AbilityTag);
			Cmd.ActivationInputs = Inputs;
			World->EnqueueDerivedCommand(Cmd);
		}
		void Tick() { FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds()); }
	};
	void Diagnostics(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("Component 'SeinAbilityPayload' has field(s) excluded from the legacy local state fingerprint"), EAutomationExpectedErrorFlags::Contains, 0, false);
	}
}

namespace UE::SeinARTSTests
{
	TEST(BrokerDispatchPreservesInputs, "SeinARTS.Sim.Abilities.ActivationInputs")
	{
		Diagnostics(*TestRunner);
		FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		FSeinBrokerOrderPayload Payload;
		Payload.PredeterminedAbilityTag = F.Ability()->AbilityTag;
		Payload.CommandContext.AddTag(SeinARTSTags::Command_Context_AbilityTriggered);
		Payload.ActivationInputs = Make(31);
		FSeinCommand Command;
		Command.PlayerID = FSeinPlayerID(1);
		Command.CommandType = SeinARTSTags::Command_Type_BrokerOrder;
		Command.SchemaVersion = SeinBrokerOrderProtocol::SchemaVersion;
		Command.EntityList.Add(F.Entity);
		Command.Payload = FInstancedStruct::Make(Payload);
		ASSERT_THAT(IsTrue(F.World->SubmitLocalCommandDraft(Command)));
		for (int32 Step = 0; Step < 5; ++Step) F.Tick();
		ASSERT_THAT(AreEqual(1, F.Ability()->Activations));
		ASSERT_THAT(AreEqual(31, F.Ability()->Total));
	}

	TEST(SerialAndParallelInputTracesMatch, "SeinARTS.Determinism.ActivationInputs")
	{
		Diagnostics(*TestRunner);
		IConsoleVariable* Parallel = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.Parallel"));
		const int32 Previous = Parallel->GetInt();
		struct FRestore { IConsoleVariable* Cvar; int32 Value; ~FRestore() { Cvar->SetWithCurrentPriority(Value); } } Restore{Parallel, Previous};
		TArray<FGuid> Traces[2];
		for (int32 Mode = 0; Mode < 2; ++Mode)
		{
			Parallel->SetWithCurrentPriority(Mode);
			FFixture F;
			ASSERT_THAT(IsTrue(F.Initialize()));
			for (int32 Step = 0; Step < 5; ++Step)
			{
				F.Queue(Make(Step + 1)); F.Tick();
				FGuid Root; FString Error;
				ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(Root, Error)));
				Traces[Mode].Add(Root);
			}
		}
		ASSERT_THAT(IsTrue(Traces[0] == Traces[1]));
	}

	TEST(CaptureCopiesOnlyExposedValuesAndRejectsBadSchema, "SeinARTS.Unit.Abilities.ActivationInputs")
	{
		FSeinAbilityActivationInputs Inputs = Make(4);
		TStrongObjectPtr<USeinActivationInputTestAbility> Candidate(NewObject<USeinActivationInputTestAbility>());
		FString Error;
		ASSERT_THAT(IsTrue(Inputs.Decode(*Candidate, Error)));
		ASSERT_THAT(AreEqual(4, Candidate->QueueIndex));
		ASSERT_THAT(AreEqual(0, Candidate->Total));
		ASSERT_THAT(AreEqual(2, Candidate->Choices.Num()));
		Inputs.SchemaA ^= 1;
		ASSERT_THAT(IsFalse(Inputs.Decode(*Candidate, Error)));
		Inputs = Make(8);
		Inputs.Data.Add(0);
		ASSERT_THAT(IsFalse(Inputs.Decode(*Candidate, Error)));
	}

	TEST(CommandWirePreservesCapturedInputs, "SeinARTS.Unit.Abilities.ActivationInputs")
	{
		FSeinCommandSchemaDescriptor Schema;
		Schema.CommandType = SeinARTSTags::Command_Type_ActivateAbility;
		Schema.SchemaVersion = 1;
		FSeinCommand Source = FSeinCommand::MakeAbilityCommand(FSeinPlayerID(1), FSeinEntityHandle(1, 1), SeinARTSTags::Command_Context_AbilityTriggered);
		Source.ActivationInputs = Make(13);
		FString Error;
		TArray<uint8> Bytes;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(Source, Schema, Bytes, Error)));
		FSeinCommand Decoded;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Decode(Bytes, [&Schema](FGameplayTag Tag, int32 Version, FSeinCommandSchemaDescriptor& Out)
			{ Out = Schema; return Tag == Schema.CommandType && Version == 1; }, Decoded, Error)));
		ASSERT_THAT(IsTrue(Decoded.ActivationInputs.Data == Source.ActivationInputs.Data));
		ASSERT_THAT(IsTrue(Decoded.ActivationInputs.GetSchemaDigest() == Source.ActivationInputs.GetSchemaDigest()));
	}

	TEST(QueuedRequestsOwnValuesAndDefaultResetIsPerActivation, "SeinARTS.Sim.Abilities.ActivationInputs")
	{
		Diagnostics(*TestRunner);
		FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Queue(Make(2)); F.Queue(Make(5));
		ASSERT_THAT(AreEqual(7, F.Ability()->QueueIndex));
		F.Tick();
		ASSERT_THAT(AreEqual(7, F.Ability()->Total));
		ASSERT_THAT(AreEqual(2, F.Ability()->Activations));
		F.Queue({}); F.Tick();
		ASSERT_THAT(AreEqual(14, F.Ability()->Total));
		ASSERT_THAT(AreEqual(7, F.Ability()->QueueIndex));
		F.Queue(Make(-1)); F.Tick();
		ASSERT_THAT(AreEqual(3, F.Ability()->Activations));
		ASSERT_THAT(AreEqual(7, F.Ability()->QueueIndex));
	}

	TEST(PendingInputsRestoreAndContinueInFreshWorld, "SeinARTS.Determinism.ActivationInputs")
	{
		Diagnostics(*TestRunner);
		FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Queue(Make(11)); F.Tick();
		F.Queue(Make(17));
		F.World->StopSimulation();
		FSeinWorldSnapshot Snapshot;
		F.World->CaptureSnapshot(Snapshot);
		FActorTestSpawner OtherSpawner;
		USeinWorldSubsystem* Other = OtherSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Other, Snapshot)));
		ASSERT_THAT(IsTrue(F.World->StartSimulation()));
		ASSERT_THAT(IsTrue(Other->StartSimulation()));
		for (int32 Step = 0; Step < 3; ++Step)
		{
			F.Tick();
			FGuid A, B;
			FString Error;
			ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(A, Error)));
			ASSERT_THAT(IsTrue(Other->ComputeCanonicalStateRoot(B, Error)));
			ASSERT_THAT(IsTrue(A == B));
		}
		ASSERT_THAT(AreEqual(28, CastChecked<USeinActivationInputTestAbility>(Other->GetAbilityInstance(F.ID))->Total));
		Other->StopSimulation();
	}
}
