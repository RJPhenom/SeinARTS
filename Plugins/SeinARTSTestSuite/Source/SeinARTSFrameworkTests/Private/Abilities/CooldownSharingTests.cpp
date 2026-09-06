#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/IConsoleManager.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCooldownSharingTestTypes.h"

USeinCooldownSharingTestAbility::USeinCooldownSharingTestAbility()
{
	AbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
	Cooldown = FFixedPoint::FromInt(2);
	CooldownScope = ESeinCooldownScope::SharedGroup;
	bRefundCooldownOnCancel = true;
}

namespace
{
	struct FCooldownFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		FSeinEntityHandle Broker;
		TArray<FSeinEntityHandle> Members;
		TArray<int32> IDs;

		~FCooldownFixture()
		{
			if (World) World->StopSimulation();
		}

		bool Initialize(bool bShares = true)
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World) return false;
			const auto Author = [&]()
			{
				Broker = World->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID::Neutral());
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const FSeinEntityHandle Member = World->SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
					Members.Add(Member);
					World->AddComponent(Member, FSeinAbilityPayload());
					IDs.Add(USeinAbilityBPFL::SeinGrantAbility(World, Member,
						USeinCooldownSharingTestAbility::StaticClass()));
					FSeinBrokerMembershipData Membership;
					Membership.CurrentBrokerHandle = Broker;
					World->AddComponent(Member, Membership);
				}
				FSeinCommandBrokerData Data;
				Data.Members = Members;
				Data.bSelfCullOnEmpty = false;
				Data.bSharesAbilityCooldowns = bShares;
				World->AddComponent(Broker, Data);
			};
			if (!SeinTestMatchBootstrap::Materialize(*World, Author, FSeinMatchSettings(),
				0x434F4F4C, TEXT("CooldownSharing")) || IDs.Contains(INDEX_NONE)
				|| !SeinTestMatchBootstrap::Start(*World)) return false;
			return true;
		}

		USeinAbility* Ability(int32 Index) const { return World->GetAbilityInstance(IDs[Index]); }
		bool Activate(int32 Index) const
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			return Ability(Index)->ActivateAbility(FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector);
		}
		void Cancel(int32 Index) const
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			Ability(Index)->CancelAbility();
		}
	};

	void ExpectAbilityDiagnostic(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("Component 'SeinAbilityPayload' has field(s) excluded from the legacy local state fingerprint"),
			EAutomationExpectedErrorFlags::Contains, 0, false);
	}

	struct FParallelScope
	{
		IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.Parallel"));
		int32 Previous = Cvar->GetInt();
		explicit FParallelScope(bool bParallel) { Cvar->SetWithCurrentPriority(bParallel ? 1 : 0); }
		~FParallelScope() { Cvar->SetWithCurrentPriority(Previous); }
	};

	bool CooldownTrace(bool bParallel, TArray<FGuid>& Roots)
	{
		FParallelScope Parallel(bParallel);
		FCooldownFixture Fixture;
		if (!Fixture.Initialize()) return false;
		for (int32 Step = 0; Step < 10; ++Step)
		{
			if (Step == 0 && !Fixture.Activate(0)) return false;
			if (Step == 3) Fixture.Cancel(0);
			if (Step == 4 && !Fixture.Activate(1)) return false;
			if (Step == 7) Fixture.Cancel(1);
			FTSTicker::GetCoreTicker().Tick(Fixture.World->GetFixedDeltaTimeSeconds());
			FGuid Root;
			FString Error;
			if (Fixture.World->GetCurrentTick() != Step + 1
				|| !Fixture.World->ComputeCanonicalStateRoot(Root, Error)) return false;
			Roots.Add(Root);
			UE_LOG(LogTemp, Display, TEXT("[CooldownSharingTrace] tick=%d root=%s"),
				Step + 1, *Root.ToString(EGuidFormats::Digits));
		}
		return true;
	}
}

namespace UE::SeinARTSTests
{
	TEST(SharingRequiresExplicitGroupAndMatchingTag, "SeinARTS.Unit.Abilities.CooldownSharing")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		FCooldownFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(false)));
		ASSERT_THAT(IsTrue(F.Activate(0)));
		ASSERT_THAT(IsFalse(F.Ability(1)->IsOnCooldown()));
		F.Cancel(0);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			F.World->GetComponentMutable<FSeinCommandBrokerData>(F.Broker)->bSharesAbilityCooldowns = true;
		}
		F.Ability(0)->CooldownScope = ESeinCooldownScope::OwnerOnly;
		ASSERT_THAT(IsTrue(F.Activate(0)));
		ASSERT_THAT(IsFalse(F.Ability(1)->IsOnCooldown()));
		F.Cancel(0);
		F.Ability(0)->CooldownScope = ESeinCooldownScope::SharedGroup;
		F.Ability(2)->AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
		ASSERT_THAT(IsTrue(F.Activate(0)));
		ASSERT_THAT(AreEqual(F.Ability(0)->Cooldown, F.Ability(1)->CooldownRemaining));
		ASSERT_THAT(IsFalse(F.Ability(2)->IsOnCooldown()));
		ASSERT_THAT(IsFalse(F.Ability(1)->bCooldownStarted));
		F.Cancel(0);
		ASSERT_THAT(IsFalse(F.Ability(1)->IsOnCooldown()));
		F.Ability(0)->Cooldown = FFixedPoint::Zero;
		ASSERT_THAT(IsTrue(F.Activate(0)));
		ASSERT_THAT(IsFalse(F.Ability(1)->IsOnCooldown()));
	}

	TEST(RefundUsesCapturedRecipientsAndLeavesNewerCooldowns, "SeinARTS.Unit.Abilities.CooldownSharing")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		FCooldownFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		ASSERT_THAT(IsTrue(F.Activate(0)));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			F.World->GetComponentMutable<FSeinBrokerMembershipData>(F.Members[1])->CurrentBrokerHandle = {};
			F.World->GetComponentMutable<FSeinCommandBrokerData>(F.Broker)->Members.Remove(F.Members[1]);
		}
		// Direct lifecycle calls deliberately exercise overlapping source identities.
		F.Ability(2)->CooldownScope = ESeinCooldownScope::OwnerOnly;
		ASSERT_THAT(IsTrue(F.Activate(2)));
		F.Cancel(0);
		ASSERT_THAT(IsFalse(F.Ability(0)->IsOnCooldown()));
		ASSERT_THAT(IsFalse(F.Ability(1)->IsOnCooldown()));
		ASSERT_THAT(IsTrue(F.Ability(2)->IsOnCooldown()));
		F.Cancel(2);
		ASSERT_THAT(IsFalse(F.Ability(2)->IsOnCooldown()));
	}

	TEST(RecycledRecipientIsNotRefunded, "SeinARTS.Unit.Abilities.CooldownSharing")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		FCooldownFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		ASSERT_THAT(IsTrue(F.Activate(0)));
		const int32 OldID = F.IDs[1];
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			USeinAbilityBPFL::SeinForceRevokeAbilityByClass(F.World, F.Members[1], USeinCooldownSharingTestAbility::StaticClass());
			F.IDs[1] = USeinAbilityBPFL::SeinGrantAbility(F.World, F.Members[1], USeinCooldownSharingTestAbility::StaticClass());
		}
		ASSERT_THAT(AreEqual(OldID, F.IDs[1]));
		F.Ability(1)->CooldownScope = ESeinCooldownScope::OwnerOnly;
		ASSERT_THAT(IsTrue(F.Activate(1)));
		F.Cancel(0);
		ASSERT_THAT(IsTrue(F.Ability(1)->IsOnCooldown()));
		ASSERT_THAT(IsFalse(F.Ability(2)->IsOnCooldown()));
	}

	TEST(UnrefundedCooldownExpiresAndCanActivateAgain, "SeinARTS.Unit.Abilities.CooldownSharing")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		FCooldownFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Ability(0)->bRefundCooldownOnCancel = false;
		ASSERT_THAT(IsTrue(F.Activate(0)));
		F.Cancel(0);
		ASSERT_THAT(IsTrue(F.Ability(0)->IsOnCooldown()));
		ASSERT_THAT(IsTrue(F.Ability(1)->IsOnCooldown()));
		for (int32 Tick = 0; Tick < 300 && F.Ability(1)->IsOnCooldown(); ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(F.World->GetFixedDeltaTimeSeconds());
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			ASSERT_THAT(IsFalse(F.Ability(Index)->IsOnCooldown()));
			ASSERT_THAT(AreEqual(int64(0), F.Ability(Index)->CooldownSourceActivationID));
		}
		ASSERT_THAT(IsTrue(F.Activate(1)));
		ASSERT_THAT(IsTrue(F.Ability(0)->IsOnCooldown()));
	}

	TEST(SharedWriteDoesNotSuppressRecipientsOnEndTiming, "SeinARTS.Unit.Abilities.CooldownSharing")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		FCooldownFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Ability(1)->CooldownStartTiming = ESeinCooldownStartTiming::OnEnd;
		ASSERT_THAT(IsTrue(F.Activate(1)));
		ASSERT_THAT(IsFalse(F.Ability(1)->IsOnCooldown()));
		ASSERT_THAT(IsTrue(F.Activate(0)));
		ASSERT_THAT(IsFalse(F.Ability(1)->bCooldownStarted));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			F.Ability(1)->EndAbility();
		}
		ASSERT_THAT(AreEqual(F.Ability(1)->GetActivationID(), F.Ability(0)->CooldownSourceActivationID));
		F.Cancel(0);
		ASSERT_THAT(IsTrue(F.Ability(0)->IsOnCooldown()));
		ASSERT_THAT(IsTrue(F.Ability(1)->IsOnCooldown()));
		ASSERT_THAT(IsTrue(F.Ability(2)->IsOnCooldown()));
	}

	TEST(SharedCooldownSnapshotContinuesAndRefundsInFreshWorld, "SeinARTS.Determinism.CooldownSharing")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		FCooldownFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		ASSERT_THAT(IsTrue(F.Activate(0)));
		FTSTicker::GetCoreTicker().Tick(F.World->GetFixedDeltaTimeSeconds());
		F.World->StopSimulation();
		FSeinWorldSnapshot Snapshot;
		F.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(FSeinWorldSnapshot::CurrentVersion, Snapshot.SnapshotVersion));
		FActorTestSpawner OtherSpawner;
		USeinWorldSubsystem* Other = OtherSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Other, Snapshot)));
		ASSERT_THAT(IsTrue(F.World->StartSimulation()));
		ASSERT_THAT(IsTrue(Other->StartSimulation()));
		for (int32 Step = 0; Step < 5; ++Step)
		{
			if (Step == 2)
			{
				F.Cancel(0);
				auto Scope = FSeinSimContextTestAccess::Enter(*Other);
				Other->GetAbilityInstance(F.IDs[0])->CancelAbility();
			}
			FTSTicker::GetCoreTicker().Tick(F.World->GetFixedDeltaTimeSeconds());
			FGuid A, B;
			FString Error;
			ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(A, Error)));
			ASSERT_THAT(IsTrue(Other->ComputeCanonicalStateRoot(B, Error)));
			ASSERT_THAT(IsTrue(A == B));
		}
		ASSERT_THAT(IsFalse(Other->GetAbilityInstance(F.IDs[1])->IsOnCooldown()));
		F.World->StopSimulation();
		Other->StopSimulation();
	}

	TEST(SerialParallelCooldownRootsMatch, "SeinARTS.Determinism.CooldownSharing")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		TArray<FGuid> Serial, Parallel;
		ASSERT_THAT(IsTrue(CooldownTrace(false, Serial)));
		ASSERT_THAT(IsTrue(CooldownTrace(true, Parallel)));
		ASSERT_THAT(IsTrue(Serial == Parallel));
	}

	TEST(SerialCooldownTrace, "SeinARTS.Determinism.CooldownSharing.Process")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		TArray<FGuid> Roots;
		ASSERT_THAT(IsTrue(CooldownTrace(false, Roots)));
	}

	TEST(ParallelCooldownTrace, "SeinARTS.Determinism.CooldownSharing.Process")
	{
		ExpectAbilityDiagnostic(*TestRunner);
		TArray<FGuid> Roots;
		ASSERT_THAT(IsTrue(CooldownTrace(true, Roots)));
	}
}
