#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

#include <type_traits>

namespace UE::SeinARTSTests
{
	namespace
	{
		const FName StateTestOwner(
			TEXT("SeinFrameworkTests.CanonicalState"));
		const FName StateTestDomain(
			TEXT("SeinFrameworkTest.State.Registry"));

		FSeinCanonicalStateKey MakeKey(FName ContributorId)
		{
			FSeinCanonicalStateKey Key;
			Key.StableDomainId = StateTestDomain;
			Key.StableContributorId = ContributorId;
			return Key;
		}

		FSeinCanonicalStateDescriptor MakeDescriptor(
			FName ContributorId,
			uint32 SchemaVersion = 1,
			uint32 ImplementationRevision = 1,
			ESeinCanonicalStateRole Role =
				ESeinCanonicalStateRole::Authoritative)
		{
			FSeinCanonicalStateDescriptor Descriptor;
			Descriptor.Key = MakeKey(ContributorId);
			Descriptor.SchemaVersion = SchemaVersion;
			Descriptor.ImplementationRevision = ImplementationRevision;
			Descriptor.Role = Role;
			Descriptor.PayloadStruct =
				Role == ESeinCanonicalStateRole::DerivedCache
					? nullptr
					: FSeinCommandSchemaAlternateTestPayload::StaticStruct();
			// These fixtures simulate subsystem-owned providers; no test system
			// claims them, so they must declare external ownership to pass the
			// orphaned-contributor bootstrap gate.
			Descriptor.bExternallyOwned = true;
			return Descriptor;
		}

		FSeinCanonicalStateContributorOps MakePersistentOps()
		{
			FSeinCanonicalStateContributorOps Ops;
			Ops.Capture = [](
				const FSeinCanonicalStateCaptureContext&,
				FInstancedStruct& OutState,
				FString&)
			{
				OutState = FInstancedStruct::Make(
					FSeinCommandSchemaAlternateTestPayload());
				return true;
			};
			Ops.StageRestore = [](
				const FSeinCanonicalStateStageContext&,
				const FInstancedStruct&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>&,
				FString&)
			{
				return true;
			};
			Ops.CommitRestore = [](
				FSeinCanonicalStateCommitContext&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
			{
			};
			return Ops;
		}

		FSeinCanonicalStateContributorOps MakeDerivedOps()
		{
			FSeinCanonicalStateContributorOps Ops;
			Ops.StageDerived = [](
				const FSeinCanonicalStateStageContext&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>&,
				FString&)
			{
				return true;
			};
			Ops.CommitDerived = [](
				FSeinCanonicalStateCommitContext&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
			{
			};
			return Ops;
		}

		const FSeinFrozenCanonicalStateContributor* FindFrozen(
			const FSeinCanonicalStateSchemaSnapshot& Snapshot,
			const FSeinCanonicalStateKey& Key)
		{
			for (const FSeinFrozenCanonicalStateContributor& Contributor :
				Snapshot.GetContributors())
			{
				if (Contributor.Descriptor.Key == Key)
				{
					return &Contributor;
				}
			}
			return nullptr;
		}

		int32 FindFrozenIndex(
			const FSeinCanonicalStateSchemaSnapshot& Snapshot,
			const FSeinCanonicalStateKey& Key)
		{
			const TConstArrayView<FSeinFrozenCanonicalStateContributor>
				Contributors = Snapshot.GetContributors();
			for (int32 Index = 0; Index < Contributors.Num(); ++Index)
			{
				if (Contributors[Index].Descriptor.Key == Key)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		bool CaptureContract(
			const FSeinCanonicalStateDescriptor& Descriptor,
			FString& OutManifest,
			FGuid& OutDigest,
			FString& OutError)
		{
			FSeinCanonicalStateRegistrationHandle Handle =
				FSeinCanonicalStateRegistry::Register(
					StateTestOwner,
					Descriptor,
					MakePersistentOps(),
					&OutError);
			if (!Handle.IsValid())
			{
				return false;
			}

			const FSeinCanonicalStateSchemaSnapshot Snapshot =
				FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(
					&OutError);
			if (!Snapshot.IsValid())
			{
				return false;
			}
			OutManifest = Snapshot.GetCanonicalManifest();
			OutDigest = Snapshot.GetContractDigest();
			return true;
		}

		class FHostileUnregisterRestoreStage final
			: public ISeinCanonicalStateRestoreStage
		{
		public:
			FHostileUnregisterRestoreStage(
				FSeinCanonicalStateRegistrationHandle& InTarget,
				bool& InAttempted,
				bool& InRejected,
				bool& InTargetRemainedValid)
				: Target(InTarget)
				, bAttempted(InAttempted)
				, bRejected(InRejected)
				, bTargetRemainedValid(InTargetRemainedValid)
			{
			}

			virtual ~FHostileUnregisterRestoreStage() override
			{
				bAttempted = true;
				bRejected =
					!FSeinCanonicalStateRegistry::Unregister(Target);
				bTargetRemainedValid = Target.IsValid();
			}

		private:
			FSeinCanonicalStateRegistrationHandle& Target;
			bool& bAttempted;
			bool& bRejected;
			bool& bTargetRemainedValid;
		};
	}

	TEST(CanonicalStateDuplicateRegistrationRollsBackToPreviousProvider,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		const int32 BaselineCount =
			FSeinCanonicalStateRegistry::GetRegisteredContributorCount();
		const FSeinCanonicalStateDescriptor Descriptor = MakeDescriptor(
			TEXT("DuplicateReloadRollback"));
		const FSeinCanonicalStateKey Key = Descriptor.Key;
		FString Error;

		FSeinCanonicalStateRegistrationHandle Previous =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Descriptor, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(Previous.IsValid()));
		ASSERT_THAT(IsTrue(Error.IsEmpty()));
		const FSeinCanonicalStateSchemaSnapshot PreviousSnapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		const FSeinFrozenCanonicalStateContributor* PreviousFrozen =
			FindFrozen(PreviousSnapshot, Key);
		ASSERT_THAT(IsNotNull(PreviousFrozen));
		const uint64 PreviousToken = PreviousFrozen
			? PreviousFrozen->ProviderToken
			: 0;

		{
			FSeinCanonicalStateRegistrationHandle Reloaded =
				FSeinCanonicalStateRegistry::Register(
					StateTestOwner, Descriptor, MakePersistentOps(), &Error);
			ASSERT_THAT(IsTrue(Reloaded.IsValid()));
			ASSERT_THAT(AreEqual(
				BaselineCount + 1,
				FSeinCanonicalStateRegistry::
					GetRegisteredContributorCount()));

			const FSeinCanonicalStateSchemaSnapshot ReloadedSnapshot =
				FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
			const FSeinFrozenCanonicalStateContributor* ReloadedFrozen =
				FindFrozen(ReloadedSnapshot, Key);
			ASSERT_THAT(IsNotNull(ReloadedFrozen));
			ASSERT_THAT(IsTrue(
				ReloadedFrozen
				&& ReloadedFrozen->ProviderToken != PreviousToken));
		}

		const FSeinCanonicalStateSchemaSnapshot RolledBackSnapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		const FSeinFrozenCanonicalStateContributor* RolledBackFrozen =
			FindFrozen(RolledBackSnapshot, Key);
		ASSERT_THAT(IsNotNull(RolledBackFrozen));
		ASSERT_THAT(IsTrue(
			RolledBackFrozen
			&& RolledBackFrozen->ProviderToken == PreviousToken));

		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(Previous)));
		ASSERT_THAT(IsFalse(Previous.IsValid()));
		ASSERT_THAT(IsFalse(
			FSeinCanonicalStateRegistry::Unregister(Previous)));
		ASSERT_THAT(AreEqual(
			BaselineCount,
			FSeinCanonicalStateRegistry::GetRegisteredContributorCount()));
	}

	TEST(CanonicalStateRegistrationRejectsConflictingDescriptor,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		FSeinCanonicalStateDescriptor Descriptor = MakeDescriptor(
			TEXT("DescriptorConflict"));
		FString Error;
		FSeinCanonicalStateRegistrationHandle Registered =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Descriptor, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(Registered.IsValid()));

		FSeinCanonicalStateDescriptor Conflict = Descriptor;
		++Conflict.ImplementationRevision;
		TestRunner->AddExpectedError(
			TEXT("Conflicting canonical state contributor"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FSeinCanonicalStateRegistrationHandle Rejected =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Conflict, MakePersistentOps(), &Error);
		ASSERT_THAT(IsFalse(Rejected.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("Conflicting canonical state contributor"))));
	}

	TEST(CanonicalStateManifestIgnoresRegistrationOrder,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		const FSeinCanonicalStateDescriptor A = MakeDescriptor(
			TEXT("ManifestOrderA"), 11, 21);
		const FSeinCanonicalStateDescriptor B = MakeDescriptor(
			TEXT("ManifestOrderB"), 12, 22,
			ESeinCanonicalStateRole::Continuation);
		FString Error;

		FSeinCanonicalStateRegistrationHandle ForwardA =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, A, MakePersistentOps(), &Error);
		FSeinCanonicalStateRegistrationHandle ForwardB =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, B, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(ForwardA.IsValid()));
		ASSERT_THAT(IsTrue(ForwardB.IsValid()));
		const FSeinCanonicalStateSchemaSnapshot Forward =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsTrue(Forward.IsValid()));
		const FString ForwardManifest = Forward.GetCanonicalManifest();
		const FGuid ForwardDigest = Forward.GetContractDigest();
		ForwardA.Reset();
		ForwardB.Reset();

		FSeinCanonicalStateRegistrationHandle ReverseB =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, B, MakePersistentOps(), &Error);
		FSeinCanonicalStateRegistrationHandle ReverseA =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, A, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(ReverseB.IsValid()));
		ASSERT_THAT(IsTrue(ReverseA.IsValid()));
		const FSeinCanonicalStateSchemaSnapshot Reverse =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsTrue(Reverse.IsValid()));
		ASSERT_THAT(AreEqual(
			ForwardManifest, Reverse.GetCanonicalManifest()));
		ASSERT_THAT(IsTrue(
			ForwardDigest == Reverse.GetContractDigest()));
	}

	TEST(CanonicalStateManifestTracksSchemaImplementationAndRole,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		const FName ContributorId(TEXT("ManifestSensitivity"));
		FString Error;
		FString BaselineManifest;
		FGuid BaselineDigest;
		ASSERT_THAT(IsTrue(CaptureContract(
			MakeDescriptor(ContributorId, 31, 41),
			BaselineManifest, BaselineDigest, Error)));

		FString SchemaManifest;
		FGuid SchemaDigest;
		ASSERT_THAT(IsTrue(CaptureContract(
			MakeDescriptor(ContributorId, 32, 41),
			SchemaManifest, SchemaDigest, Error)));
		ASSERT_THAT(IsFalse(BaselineManifest == SchemaManifest));
		ASSERT_THAT(IsFalse(BaselineDigest == SchemaDigest));

		FString ImplementationManifest;
		FGuid ImplementationDigest;
		ASSERT_THAT(IsTrue(CaptureContract(
			MakeDescriptor(ContributorId, 31, 42),
			ImplementationManifest, ImplementationDigest, Error)));
		ASSERT_THAT(IsFalse(BaselineManifest == ImplementationManifest));
		ASSERT_THAT(IsFalse(BaselineDigest == ImplementationDigest));

		FString RoleManifest;
		FGuid RoleDigest;
		ASSERT_THAT(IsTrue(CaptureContract(
			MakeDescriptor(
				ContributorId, 31, 41,
				ESeinCanonicalStateRole::Continuation),
			RoleManifest, RoleDigest, Error)));
		ASSERT_THAT(IsFalse(BaselineManifest == RoleManifest));
		ASSERT_THAT(IsFalse(BaselineDigest == RoleDigest));
	}

	TEST(CanonicalStateSnapshotRejectsMissingDependencyAndCycle,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		FString Error;
		FSeinCanonicalStateDescriptor Missing = MakeDescriptor(
			TEXT("MissingDependency"));
		Missing.RestoreAfter.Add(MakeKey(TEXT("NotRegistered")));
		FSeinCanonicalStateRegistrationHandle MissingHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Missing, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(MissingHandle.IsValid()));
		TestRunner->AddExpectedError(
			TEXT("depends on a missing contributor"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		const FSeinCanonicalStateSchemaSnapshot MissingSnapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsFalse(MissingSnapshot.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("depends on a missing contributor"))));
		MissingHandle.Reset();

		FSeinCanonicalStateDescriptor CycleA = MakeDescriptor(
			TEXT("CycleA"));
		FSeinCanonicalStateDescriptor CycleB = MakeDescriptor(
			TEXT("CycleB"));
		CycleA.RestoreAfter.Add(CycleB.Key);
		CycleB.RestoreAfter.Add(CycleA.Key);
		FSeinCanonicalStateRegistrationHandle CycleAHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, CycleA, MakePersistentOps(), &Error);
		FSeinCanonicalStateRegistrationHandle CycleBHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, CycleB, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(CycleAHandle.IsValid()));
		ASSERT_THAT(IsTrue(CycleBHandle.IsValid()));
		TestRunner->AddExpectedError(
			TEXT("restore dependencies contain a cycle"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		const FSeinCanonicalStateSchemaSnapshot CycleSnapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsFalse(CycleSnapshot.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("restore dependencies contain a cycle"))));
	}

	TEST(CanonicalStateSnapshotUsesTopologicalRestoreOrder,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		FSeinCanonicalStateDescriptor First = MakeDescriptor(
			TEXT("TopologyFirst"));
		FSeinCanonicalStateDescriptor Second = MakeDescriptor(
			TEXT("TopologySecond"));
		FSeinCanonicalStateDescriptor Third = MakeDescriptor(
			TEXT("TopologyThird"));
		Second.RestoreAfter.Add(First.Key);
		Third.RestoreAfter.Add(Second.Key);
		FString Error;

		FSeinCanonicalStateRegistrationHandle ThirdHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Third, MakePersistentOps(), &Error);
		FSeinCanonicalStateRegistrationHandle SecondHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Second, MakePersistentOps(), &Error);
		FSeinCanonicalStateRegistrationHandle FirstHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, First, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(ThirdHandle.IsValid()));
		ASSERT_THAT(IsTrue(SecondHandle.IsValid()));
		ASSERT_THAT(IsTrue(FirstHandle.IsValid()));

		const FSeinCanonicalStateSchemaSnapshot Snapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsTrue(Snapshot.IsValid()));
		const int32 FirstIndex = FindFrozenIndex(Snapshot, First.Key);
		const int32 SecondIndex = FindFrozenIndex(Snapshot, Second.Key);
		const int32 ThirdIndex = FindFrozenIndex(Snapshot, Third.Key);
		ASSERT_THAT(IsTrue(FirstIndex != INDEX_NONE));
		ASSERT_THAT(IsTrue(SecondIndex != INDEX_NONE));
		ASSERT_THAT(IsTrue(ThirdIndex != INDEX_NONE));
		ASSERT_THAT(IsTrue(FirstIndex < SecondIndex));
		ASSERT_THAT(IsTrue(SecondIndex < ThirdIndex));
	}

	TEST(CanonicalStateSnapshotEnforcesImplicitRoleBarriers,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		const FSeinCanonicalStateDescriptor Derived = MakeDescriptor(
			TEXT("RoleBarrierA"),
			1,
			1,
			ESeinCanonicalStateRole::DerivedCache);
		const FSeinCanonicalStateDescriptor Continuation = MakeDescriptor(
			TEXT("RoleBarrierB"),
			1,
			1,
			ESeinCanonicalStateRole::Continuation);
		const FSeinCanonicalStateDescriptor Authoritative = MakeDescriptor(
			TEXT("RoleBarrierZ"),
			1,
			1,
			ESeinCanonicalStateRole::Authoritative);
		FString Error;

		FSeinCanonicalStateRegistrationHandle DerivedHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Derived, MakeDerivedOps(), &Error);
		FSeinCanonicalStateRegistrationHandle ContinuationHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				Continuation,
				MakePersistentOps(),
				&Error);
		FSeinCanonicalStateRegistrationHandle AuthoritativeHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				Authoritative,
				MakePersistentOps(),
				&Error);
		ASSERT_THAT(IsTrue(DerivedHandle.IsValid()));
		ASSERT_THAT(IsTrue(ContinuationHandle.IsValid()));
		ASSERT_THAT(IsTrue(AuthoritativeHandle.IsValid()));

		const FSeinCanonicalStateSchemaSnapshot Snapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsTrue(Snapshot.IsValid()));
		const int32 AuthoritativeIndex =
			FindFrozenIndex(Snapshot, Authoritative.Key);
		const int32 ContinuationIndex =
			FindFrozenIndex(Snapshot, Continuation.Key);
		const int32 DerivedIndex =
			FindFrozenIndex(Snapshot, Derived.Key);
		ASSERT_THAT(IsTrue(AuthoritativeIndex != INDEX_NONE));
		ASSERT_THAT(IsTrue(ContinuationIndex != INDEX_NONE));
		ASSERT_THAT(IsTrue(DerivedIndex != INDEX_NONE));
		ASSERT_THAT(IsTrue(AuthoritativeIndex < ContinuationIndex));
		ASSERT_THAT(IsTrue(ContinuationIndex < DerivedIndex));
	}

	TEST(CanonicalStateSnapshotRejectsBackwardRoleDependency,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		FSeinCanonicalStateDescriptor Continuation = MakeDescriptor(
			TEXT("BackwardRoleContinuation"),
			1,
			1,
			ESeinCanonicalStateRole::Continuation);
		FSeinCanonicalStateDescriptor Authoritative = MakeDescriptor(
			TEXT("BackwardRoleAuthoritative"));
		Authoritative.RestoreAfter.Add(Continuation.Key);
		FString Error;

		FSeinCanonicalStateRegistrationHandle ContinuationHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				Continuation,
				MakePersistentOps(),
				&Error);
		FSeinCanonicalStateRegistrationHandle AuthoritativeHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				Authoritative,
				MakePersistentOps(),
				&Error);
		ASSERT_THAT(IsTrue(ContinuationHandle.IsValid()));
		ASSERT_THAT(IsTrue(AuthoritativeHandle.IsValid()));

		TestRunner->AddExpectedError(
			TEXT("canonical state role barrier"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		const FSeinCanonicalStateSchemaSnapshot Snapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsFalse(Snapshot.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("canonical state role barrier"))));
	}

	TEST(CanonicalStateFrozenProviderDoesNotRetargetAcrossReload,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinCanonicalStateDescriptor Descriptor = MakeDescriptor(
			TEXT("FrozenProviderGeneration"));
		FString Error;
		FSeinCanonicalStateRegistrationHandle Previous =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Descriptor, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(Previous.IsValid()));
		const FSeinCanonicalStateSchemaSnapshot PreviousSnapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		const FSeinFrozenCanonicalStateContributor* PreviousFrozen =
			FindFrozen(PreviousSnapshot, Descriptor.Key);
		ASSERT_THAT(IsNotNull(PreviousFrozen));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::PrepareWorldBindings(
				PreviousSnapshot,
				{
					*World,
					ESeinCanonicalStateWorldBindingDisposition::
						Provisional
				},
				Error)));
		const uint64 PreviousToken = PreviousFrozen
			? PreviousFrozen->ProviderToken
			: 0;

		FSeinCanonicalStateRegistrationHandle Reloaded =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner, Descriptor, MakePersistentOps(), &Error);
		ASSERT_THAT(IsTrue(Reloaded.IsValid()));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(Previous)));

		const FSeinCanonicalStateCaptureContext CaptureContext{
			*World, 19
		};
		TArray<FSeinCanonicalStateContributorRecord> StaleRecords;
		ASSERT_THAT(IsFalse(
			FSeinCanonicalStateRegistry::CaptureContributorRecords(
				PreviousSnapshot,
				CaptureContext,
				StaleRecords,
				Error)));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("frozen provider generation is unavailable"))));
		ASSERT_THAT(IsTrue(StaleRecords.IsEmpty()));

		const FSeinCanonicalStateSchemaSnapshot ReloadedSnapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		const FSeinFrozenCanonicalStateContributor* ReloadedFrozen =
			FindFrozen(ReloadedSnapshot, Descriptor.Key);
		ASSERT_THAT(IsNotNull(ReloadedFrozen));
		ASSERT_THAT(IsTrue(
			ReloadedFrozen
			&& ReloadedFrozen->ProviderToken != PreviousToken));

		TArray<FSeinCanonicalStateContributorRecord> ReloadedRecords;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::CaptureContributorRecords(
				ReloadedSnapshot,
				CaptureContext,
				ReloadedRecords,
				Error)));
		// Capture skips binding-only DerivedCache contributors (collision /
		// cover world bindings) by design — records cover the PERSISTENT
		// contributors only.
		int32 PersistentCount = 0;
		for (const FSeinFrozenCanonicalStateContributor& Contributor :
			ReloadedSnapshot.GetContributors())
		{
			if (Contributor.Descriptor.Role
				!= ESeinCanonicalStateRole::DerivedCache)
			{
				++PersistentCount;
			}
		}
		ASSERT_THAT(AreEqual(PersistentCount, ReloadedRecords.Num()));
		ASSERT_THAT(IsTrue(
			ReloadedRecords.ContainsByPredicate(
				[&Descriptor](
					const FSeinCanonicalStateContributorRecord& Record)
				{
					return Record.Key == Descriptor.Key;
				})));
	}

	TEST(CanonicalStateDescriptorRejectsNullSchemasAndOversizedPayloads,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		const int32 BaselineCount =
			FSeinCanonicalStateRegistry::GetRegisteredContributorCount();
		FString Error;

		FSeinCanonicalStateDescriptor NullSchema = MakeDescriptor(
			TEXT("NullDynamicSchema"));
		NullSchema.DynamicPayloadStructs.Add(
			FSeinCommandSchemaTestPayload::StaticStruct());
		NullSchema.DynamicPayloadStructs.Add(nullptr);
		TestRunner->AddExpectedError(
			TEXT("must be non-null and unique"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FSeinCanonicalStateRegistrationHandle NullHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				NullSchema,
				MakePersistentOps(),
				&Error);
		ASSERT_THAT(IsFalse(NullHandle.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("must be non-null and unique"))));

		FSeinCanonicalStateDescriptor Oversized = MakeDescriptor(
			TEXT("OversizedPayload"));
		Oversized.Limits.MaxEncodedBytes = 64 * 1024 * 1024 + 1;
		TestRunner->AddExpectedError(
			TEXT("positive bounded revisions and limits"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FSeinCanonicalStateRegistrationHandle OversizedHandle =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				Oversized,
				MakePersistentOps(),
				&Error);
		ASSERT_THAT(IsFalse(OversizedHandle.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("positive bounded revisions and limits"))));
		ASSERT_THAT(AreEqual(
			BaselineCount,
			FSeinCanonicalStateRegistry::GetRegisteredContributorCount()));
	}

	TEST(CanonicalStateCaptureRejectsProviderLifecycleMutation,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		const FSeinCanonicalStateDescriptor LaterDescriptor =
			MakeDescriptor(TEXT("LifecycleMutationB"));
		FSeinCanonicalStateRegistrationHandle Later =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				LaterDescriptor,
				MakePersistentOps(),
				&Error);
		ASSERT_THAT(IsTrue(Later.IsValid()));

		bool bMutationAttempted = false;
		bool bMutationRejected = false;
		bool bHandleRemainedValid = false;
		FSeinCanonicalStateContributorOps EarlierOps =
			MakePersistentOps();
		EarlierOps.Capture =
			[&](
				const FSeinCanonicalStateCaptureContext&,
				FInstancedStruct& OutState,
				FString&)
		{
			bMutationAttempted = true;
			bMutationRejected =
				!FSeinCanonicalStateRegistry::Unregister(Later);
			bHandleRemainedValid = Later.IsValid();
			OutState = FInstancedStruct::Make(
				FSeinCommandSchemaAlternateTestPayload());
			return true;
		};
		FSeinCanonicalStateRegistrationHandle Earlier =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				MakeDescriptor(TEXT("LifecycleMutationA")),
				MoveTemp(EarlierOps),
				&Error);
		ASSERT_THAT(IsTrue(Earlier.IsValid()));

		const FSeinCanonicalStateSchemaSnapshot Snapshot =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(&Error);
		ASSERT_THAT(IsTrue(Snapshot.IsValid()));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::PrepareWorldBindings(
				Snapshot,
				{
					*World,
					ESeinCanonicalStateWorldBindingDisposition::
						Provisional
				},
				Error)));
		TestRunner->AddExpectedError(
			TEXT("may not unregister during a provider callback transaction"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		TArray<FSeinCanonicalStateContributorRecord> Records;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::CaptureContributorRecords(
				Snapshot,
				{ *World, 23 },
				Records,
				Error)));
		ASSERT_THAT(IsTrue(bMutationAttempted));
		ASSERT_THAT(IsTrue(bMutationRejected));
		ASSERT_THAT(IsTrue(bHandleRemainedValid));
		ASSERT_THAT(IsTrue(Later.IsValid()));
	}

	TEST(CanonicalStateWorldPreparationCannotBorrowBootstrapMutationCapability,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Registry")
	{
		bool bPreparationCalled = false;
		FSeinEntityHandle UnauthorizedEntity;
		FSeinCanonicalStateContributorOps Ops =
			MakePersistentOps();
		Ops.PrepareWorldBinding =
			[&](
				const FSeinCanonicalStateWorldBindingContext& Context,
				FString&)
		{
			static_assert(std::is_const_v<
				std::remove_reference_t<
					decltype(Context.Services)>>,
				"World-binding preparation must expose only const Core services.");
			bPreparationCalled = true;
			// Even a trusted native provider that deliberately resolves the
			// mutable subsystem through UWorld cannot borrow bootstrap's
			// applying authority while the preparation guard is active.
			UWorld* ProviderWorld =
				Context.Services.GetWorld();
			USeinWorldSubsystem* MutableServices =
				ProviderWorld
					? ProviderWorld->GetSubsystem<
						USeinWorldSubsystem>()
					: nullptr;
			if (MutableServices)
			{
				UnauthorizedEntity =
					MutableServices->SpawnAbstractEntity(
					FFixedTransform(),
					FSeinPlayerID(0));
			}
			return true;
		};

		FString Error;
		FSeinCanonicalStateRegistrationHandle Provider =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				MakeDescriptor(
					TEXT("WorldPreparationReadOnly")),
				MoveTemp(Ops),
				&Error);
		ASSERT_THAT(IsTrue(Provider.IsValid()));

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<
				USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		TestRunner->AddExpectedError(
			TEXT("SpawnAbstractEntity rejected outside bootstrap Applying or deterministic simulation context"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Materialize(
				*World,
				FSeinMatchSettings(),
				0x50524550,
				TEXT("CanonicalState.WorldPreparationReadOnly"),
				&Error)));
		ASSERT_THAT(IsTrue(bPreparationCalled));
		ASSERT_THAT(IsFalse(
			UnauthorizedEntity.IsValid()));
	}

	TEST(CanonicalStateProviderWithdrawalTerminatesFrozenWorld,
		"SeinARTS.Unit.Snapshot.Latent.ProviderLifecycle")
	{
		FString Error;
		FSeinCanonicalStateRegistrationHandle Provider =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				MakeDescriptor(TEXT("WithdrawalTerminatesWorld")),
				MakePersistentOps(),
				&Error);
		ASSERT_THAT(IsTrue(Provider.IsValid()));

		// World initialization freezes the exact provider token selected above.
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsFalse(
			World->IsTerminalAfterModuleUnload()));

		TestRunner->AddExpectedError(
			TEXT("withdrew live state"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		TestRunner->AddExpectedError(
			TEXT("SeinMatchBootstrap: transaction closed (failed)."),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(Provider)));
		ASSERT_THAT(IsFalse(Provider.IsValid()));
		ASSERT_THAT(IsTrue(
			World->IsTerminalAfterModuleUnload()));
	}

	TEST(CanonicalStateDiscardedRestoreStageRejectsLifecycleMutation,
		"SeinARTS.Unit.Snapshot.Latent.ProviderTransaction")
	{
		FString Error;
		FSeinCanonicalStateRegistrationHandle Target =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				MakeDescriptor(TEXT("DiscardedStageMutationTarget")),
				MakePersistentOps(),
				&Error);
		ASSERT_THAT(IsTrue(Target.IsValid()));

		bool bMutationAttempted = false;
		bool bMutationRejected = false;
		bool bTargetRemainedValid = false;
		FSeinCanonicalStateContributorOps HostileOps =
			MakePersistentOps();
		HostileOps.StageRestore =
			[&](
				const FSeinCanonicalStateStageContext&,
				const FInstancedStruct&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>& OutStage,
				FString&)
		{
			OutStage = MakeUnique<FHostileUnregisterRestoreStage>(
				Target,
				bMutationAttempted,
				bMutationRejected,
				bTargetRemainedValid);
			return true;
		};
		FSeinCanonicalStateRegistrationHandle Hostile =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				MakeDescriptor(TEXT("DiscardedStageMutationHostile")),
				MoveTemp(HostileOps),
				&Error);
		ASSERT_THAT(IsTrue(Hostile.IsValid()));

		{
			// The world must freeze these exact provider generations, so create
			// it only after both registrations are live.
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
				*World,
				[]() {},
				FSeinMatchSettings(),
				0,
				TEXT("SeinARTS.DiscardedRestoreStage"))));
			ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
			World->StopSimulation();

			FSeinWorldSnapshot Snapshot;
			World->CaptureSnapshot(Snapshot);
			ASSERT_THAT(IsTrue(
				Snapshot.LatentActionSequenceDigest.IsValid()));
			const int32 HashBefore = World->ComputeStateHash();

			// Keep the envelope structurally valid while invalidating only the
			// latent sequence authentication. Native staging therefore succeeds
			// first, then its contributor-owned stages are discarded.
			Snapshot.LatentActionSequenceDigest =
				Snapshot.LatentActionSequenceDigest
					== FGuid(1, 2, 3, 4)
					? FGuid(5, 6, 7, 8)
					: FGuid(1, 2, 3, 4);
			TestRunner->AddExpectedError(
				TEXT("Canonical-state providers may not unregister during a provider callback transaction."),
				EAutomationExpectedErrorFlags::Contains,
				1,
				false);
			TestRunner->AddExpectedError(
				TEXT("RestoreSnapshot: latent continuation state failed staging"),
				EAutomationExpectedErrorFlags::Contains,
				1,
				false);
			ASSERT_THAT(IsFalse(
				SeinTestSnapshotRestore::RestoreTrusted(
					*World, Snapshot)));
			ASSERT_THAT(IsTrue(bMutationAttempted));
			ASSERT_THAT(IsTrue(bMutationRejected));
			ASSERT_THAT(IsTrue(bTargetRemainedValid));
			ASSERT_THAT(IsTrue(Target.IsValid()));
			ASSERT_THAT(AreEqual(HashBefore, World->ComputeStateHash()));
		}

		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(Hostile)));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(Target)));
	}

	TEST(CanonicalStateCommitCannotBorrowCoreRestoreAuthority,
		"SeinARTS.Unit.Snapshot.Latent.ProviderTransaction")
	{
		bool bCommitCalled = false;
		FSeinEntityHandle UnauthorizedEntity;
		FSeinCanonicalStateContributorOps Ops =
			MakePersistentOps();
		Ops.CommitRestore =
			[&](
				FSeinCanonicalStateCommitContext& Context,
				TUniquePtr<
					ISeinCanonicalStateRestoreStage>&&)
		{
			static_assert(std::is_const_v<
				std::remove_reference_t<
					decltype(Context.World)>>,
				"Canonical contributor commits must receive const Core services.");
			bCommitCalled = true;

			// A trusted native callback can deliberately resolve the mutable
			// subsystem through UWorld, but the callback guard must still
			// deny every sanctioned Core mutation front door.
			UWorld* ProviderWorld = Context.World.GetWorld();
			USeinWorldSubsystem* MutableWorld =
				ProviderWorld
					? ProviderWorld->GetSubsystem<
						USeinWorldSubsystem>()
					: nullptr;
			if (MutableWorld)
			{
				UnauthorizedEntity =
					MutableWorld->SpawnAbstractEntity(
						FFixedTransform(),
						FSeinPlayerID(0));
			}
		};

		FString Error;
		FSeinCanonicalStateRegistrationHandle Provider =
			FSeinCanonicalStateRegistry::Register(
				StateTestOwner,
				MakeDescriptor(
					TEXT("CommitRestoreReadOnly")),
				MoveTemp(Ops),
				&Error);
		ASSERT_THAT(IsTrue(Provider.IsValid()));

		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<
					USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			ASSERT_THAT(IsTrue(
				SeinTestMatchBootstrap::Materialize(
					*World,
					FSeinMatchSettings(),
					0x434F4D4D,
					TEXT(
						"CanonicalState.CommitRestoreReadOnly"),
					&Error)));
			ASSERT_THAT(IsTrue(
				SeinTestMatchBootstrap::Start(
					*World, &Error)));

			FGuid RootBefore;
			ASSERT_THAT(IsTrue(
				World->ComputeCanonicalStateRoot(
					RootBefore, Error)));
			FSeinWorldSnapshot Snapshot;
			World->CaptureSnapshot(Snapshot);
			ASSERT_THAT(AreEqual(
				FSeinWorldSnapshot::CurrentVersion,
				Snapshot.SnapshotVersion));

			TestRunner->AddExpectedError(
				TEXT("SpawnAbstractEntity rejected outside bootstrap Applying or deterministic simulation context"),
				EAutomationExpectedErrorFlags::Contains,
				1,
				false);
			ASSERT_THAT(IsTrue(
				SeinTestSnapshotRestore::RestoreTrusted(
					*World, Snapshot)));
			ASSERT_THAT(IsTrue(bCommitCalled));
			ASSERT_THAT(IsFalse(
				UnauthorizedEntity.IsValid()));

			FGuid RootAfter;
			ASSERT_THAT(IsTrue(
				World->ComputeCanonicalStateRoot(
					RootAfter, Error)));
			ASSERT_THAT(IsTrue(RootAfter == RootBefore));
		}

		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRegistry::Unregister(
				Provider)));
	}
}
