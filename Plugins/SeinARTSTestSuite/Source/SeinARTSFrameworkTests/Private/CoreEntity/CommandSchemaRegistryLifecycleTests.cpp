#include "CQTest.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		const FName LifecycleTestOwner(TEXT("SeinFrameworkTests.CommandSchemaLifecycle"));

		FSeinCommandSchemaDescriptor MakeLifecycleSchema(
			FName StableSchemaId,
			FGameplayTag CommandType,
			int32 SchemaVersion,
			const UScriptStruct* PayloadStruct = nullptr)
		{
			FSeinCommandSchemaDescriptor Descriptor;
			Descriptor.StableSchemaId = StableSchemaId;
			Descriptor.CommandType = CommandType;
			Descriptor.SchemaVersion = SchemaVersion;
			Descriptor.PayloadStruct = PayloadStruct;
			Descriptor.AuthorityScope = ESeinCommandAuthorityScope::Entity;
			Descriptor.MaxPayloadBytes = 256;
			Descriptor.MaxPayloadAggregateElements = 16;
			Descriptor.HandlerClass = USeinCommandSchemaTestHandler::StaticClass();
			return Descriptor;
		}

		struct FScopedSchemaClaim
		{
			explicit FScopedSchemaClaim(FSeinCommandSchemaRegistrationHandle InHandle)
				: Handle(MoveTemp(InHandle))
			{
			}

			~FScopedSchemaClaim()
			{
				FSeinCommandSchemaRegistry::UnregisterSchema(Handle);
			}

			FSeinCommandSchemaRegistrationHandle Handle;
		};
	}

	TEST(CommandSchemaReloadRollbackKeepsThePreviousClaim,
		"SeinARTS.Unit.CommandSchema.Lifecycle")
	{
		const int32 BaselineCount = FSeinCommandSchemaRegistry::GetRegisteredSchemaCount();
		const FSeinCommandSchemaDescriptor Descriptor = MakeLifecycleSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Lifecycle.Reload"),
			SeinARTSTags::Command_Type_ActivateAbility,
			8901,
			FSeinCommandSchemaAlternateTestPayload::StaticStruct());

		FScopedSchemaClaim Previous(FSeinCommandSchemaRegistry::RegisterSchema(
			LifecycleTestOwner, Descriptor));
		ASSERT_THAT(IsTrue(Previous.Handle.IsValid()));
		const FString SingleClaimManifest =
			FSeinCommandSchemaRegistry::BuildCanonicalManifest();

		FScopedSchemaClaim ReloadAttempt(FSeinCommandSchemaRegistry::RegisterSchema(
			LifecycleTestOwner, Descriptor));
		ASSERT_THAT(IsTrue(ReloadAttempt.Handle.IsValid()));
		ASSERT_THAT(AreEqual(SingleClaimManifest,
			FSeinCommandSchemaRegistry::BuildCanonicalManifest()));
		ASSERT_THAT(AreEqual(BaselineCount + 1,
			FSeinCommandSchemaRegistry::GetRegisteredSchemaCount()));

		// A failed overlapping reload releases its newest claim first. The previous
		// module claim must become active again rather than inheriting reload roots.
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::UnregisterSchema(
			ReloadAttempt.Handle)));
		FSeinCommandSchemaDescriptor Found;
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::FindSchema(
			Descriptor.CommandType, Descriptor.SchemaVersion, Found)));
		ASSERT_THAT(IsTrue(Found.HandlerClass.Get() == Descriptor.HandlerClass.Get()));
		ASSERT_THAT(IsTrue(Found.PayloadStruct == Descriptor.PayloadStruct));
		ASSERT_THAT(AreEqual(SingleClaimManifest,
			FSeinCommandSchemaRegistry::BuildCanonicalManifest()));

		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::UnregisterSchema(Previous.Handle)));
		ASSERT_THAT(AreEqual(BaselineCount,
			FSeinCommandSchemaRegistry::GetRegisteredSchemaCount()));
	}

	TEST(CommandSchemaSnapshotIsImmutableAcrossRegistryMutation,
		"SeinARTS.Unit.CommandSchema.Lifecycle")
	{
		const FSeinCommandSchemaDescriptor CapturedDescriptor = MakeLifecycleSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Lifecycle.SnapshotA"),
			SeinARTSTags::Command_Type_ActivateAbility,
			8911);
		FScopedSchemaClaim CapturedClaim(FSeinCommandSchemaRegistry::RegisterSchema(
			LifecycleTestOwner, CapturedDescriptor));
		ASSERT_THAT(IsTrue(CapturedClaim.Handle.IsValid()));

		const FSeinCommandSchemaSnapshot Snapshot =
			FSeinCommandSchemaRegistry::CaptureSnapshot();
		ASSERT_THAT(IsTrue(Snapshot.IsValid()));
		const int32 CapturedCount = Snapshot.GetSchemaCount();
		const FString CapturedManifest = Snapshot.GetCanonicalManifest();
		const FGuid CapturedDigest = Snapshot.GetCanonicalManifestDigest();
		ASSERT_THAT(AreEqual(FSeinCommandSchemaRegistry::BuildCanonicalManifest(),
			CapturedManifest));
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::ComputeCanonicalManifestDigest()
			== CapturedDigest));

		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::UnregisterSchema(
			CapturedClaim.Handle)));
		const FSeinCommandSchemaDescriptor ReplacementDescriptor = MakeLifecycleSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Lifecycle.SnapshotB"),
			SeinARTSTags::Command_Type_CancelAbility,
			8912);
		FScopedSchemaClaim ReplacementClaim(FSeinCommandSchemaRegistry::RegisterSchema(
			LifecycleTestOwner, ReplacementDescriptor));
		ASSERT_THAT(IsTrue(ReplacementClaim.Handle.IsValid()));

		FSeinCommandSchemaDescriptor Found;
		ASSERT_THAT(IsTrue(Snapshot.FindSchema(
			CapturedDescriptor.CommandType, CapturedDescriptor.SchemaVersion, Found)));
		ASSERT_THAT(IsFalse(Snapshot.FindSchema(
			ReplacementDescriptor.CommandType, ReplacementDescriptor.SchemaVersion, Found)));
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::FindSchema(
			CapturedDescriptor.CommandType, CapturedDescriptor.SchemaVersion, Found)));
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::FindSchema(
			ReplacementDescriptor.CommandType, ReplacementDescriptor.SchemaVersion, Found)));

		FSeinCommand Command;
		Command.CommandType = CapturedDescriptor.CommandType;
		Command.SchemaVersion = CapturedDescriptor.SchemaVersion;
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::Valid,
			Snapshot.ValidateStructure(Command)));
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::ValidateStructure(Command)
			== ESeinCommandStructureResult::Valid));
		ASSERT_THAT(AreEqual(CapturedCount, Snapshot.GetSchemaCount()));
		ASSERT_THAT(AreEqual(CapturedManifest, Snapshot.GetCanonicalManifest()));
		ASSERT_THAT(IsTrue(CapturedDigest == Snapshot.GetCanonicalManifestDigest()));
		ASSERT_THAT(IsFalse(CapturedManifest
			== FSeinCommandSchemaRegistry::BuildCanonicalManifest()));
	}

	TEST(CommandSchemaRegistrationRejectsUnknownAuthorityScope,
		"SeinARTS.Unit.CommandSchema.Lifecycle")
	{
		FSeinCommandSchemaDescriptor Descriptor = MakeLifecycleSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Lifecycle.InvalidAuthority"),
			SeinARTSTags::Command_Type_ActivateAbility,
			8921);
		Descriptor.AuthorityScope = static_cast<ESeinCommandAuthorityScope>(255);

		TestRunner->AddExpectedError(
			TEXT("known authority scope"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedSchemaClaim Rejected(FSeinCommandSchemaRegistry::RegisterSchema(
			LifecycleTestOwner, Descriptor));
		ASSERT_THAT(IsFalse(Rejected.Handle.IsValid()));
	}
}
