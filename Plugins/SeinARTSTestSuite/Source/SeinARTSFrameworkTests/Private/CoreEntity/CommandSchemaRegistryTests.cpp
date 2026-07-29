#include "CQTest.h"
#include "Hash/Blake3.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Input/SeinCommandWireCodec.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

USeinCommandSchemaConfiguredTestHandler::USeinCommandSchemaConfiguredTestHandler()
{
	StableSchemaId = TEXT("SeinFrameworkTest.CommandSchema.CDO");
	CommandType = SeinARTSTags::Command_Type_Ping;
	SchemaVersion = 8301;
	ImplementationRevision = 8302;
	AuthorityScope = ESeinCommandAuthorityScope::PublicObserver;
	MaxEntityListEntries = 4;
	MaxTargeterPoints = 2;
	MaxPayloadBytes = 128;
	MaxPayloadAggregateElements = 8;
	AllowedPayloadNames = { TEXT("SeinFrameworkTest.ConfiguredName") };
	AllowedExecutionContexts =
		static_cast<int32>(ESeinCommandExecutionAllowance::Spectator)
		| static_cast<int32>(ESeinCommandExecutionAllowance::HardPause);
}

namespace UE::SeinARTSTests
{
	namespace
	{
		const FName TestOwner(TEXT("SeinFrameworkTests.CommandSchema"));

		FSeinCommandSchemaDescriptor MakeSchema(
			FName StableSchemaId,
			FGameplayTag CommandType,
			int32 Version,
			const UScriptStruct* PayloadStruct = nullptr)
		{
			FSeinCommandSchemaDescriptor Descriptor;
			Descriptor.StableSchemaId = StableSchemaId;
			Descriptor.CommandType = CommandType;
			Descriptor.SchemaVersion = Version;
			Descriptor.PayloadStruct = PayloadStruct;
			Descriptor.AuthorityScope = ESeinCommandAuthorityScope::Entity;
			Descriptor.MaxEntityListEntries = 2;
			Descriptor.MaxTargeterPoints = 1;
			Descriptor.MaxPayloadBytes = 4096;
			Descriptor.MaxPayloadAggregateElements = 256;
			Descriptor.HandlerClass = USeinCommandSchemaTestHandler::StaticClass();
			return Descriptor;
		}

		struct FScopedSchemaHandles
		{
			~FScopedSchemaHandles()
			{
				Reset();
			}

			FSeinCommandSchemaRegistrationHandle& Add(
				FSeinCommandSchemaRegistrationHandle Handle)
			{
				return Handles.Add_GetRef(MoveTemp(Handle));
			}

			void Reset()
			{
				for (FSeinCommandSchemaRegistrationHandle& Handle : Handles)
				{
					FSeinCommandSchemaRegistry::UnregisterSchema(Handle);
				}
				Handles.Reset();
			}

			TArray<FSeinCommandSchemaRegistrationHandle> Handles;
		};

		FSeinCommandSchemaTestPayload MakeBudgetedPayload()
		{
			FSeinCommandSchemaTestPayload Payload;
			Payload.Groups.SetNum(2);
			Payload.Groups[0].Values = { 1, 2, 3 };
			Payload.Groups[1].Values = { 4, 5, 6 };
			return Payload;
		}
	}

	TEST(CommandSchemaRegistrationIsExactAndReloadSafe, "SeinARTS.Unit.CommandSchema")
	{
		const int32 BaselineCount = FSeinCommandSchemaRegistry::GetRegisteredSchemaCount();
		FScopedSchemaHandles Registrations;
		const FSeinCommandSchemaDescriptor Descriptor = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Registration"),
			SeinARTSTags::Command_Type_ActivateAbility,
			8001,
			FSeinCommandSchemaTestPayload::StaticStruct());

		FSeinCommandSchemaRegistrationHandle& First = Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, Descriptor));
		FSeinCommandSchemaRegistrationHandle& Reloaded = Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, Descriptor));
		ASSERT_THAT(IsTrue(First.IsValid()));
		ASSERT_THAT(IsTrue(Reloaded.IsValid()));
		ASSERT_THAT(AreEqual(BaselineCount + 1,
			FSeinCommandSchemaRegistry::GetRegisteredSchemaCount()));

		FSeinCommandSchemaDescriptor Found;
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::FindSchema(
			SeinARTSTags::Command_Type_ActivateAbility, 8001, Found)));
		ASSERT_THAT(AreEqual(Descriptor.StableSchemaId, Found.StableSchemaId));
		ASSERT_THAT(AreEqual(Descriptor.PayloadStruct->GetPathName(),
			Found.PayloadStruct->GetPathName()));

		FSeinCommandSchemaDescriptor KeyConflict = Descriptor;
		KeyConflict.MaxPayloadBytes += 1;
		TestRunner->AddExpectedError(
			TEXT("Rejected conflicting command schema key"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::RegisterSchema(
			TestOwner, KeyConflict).IsValid()));

		FSeinCommandSchemaDescriptor IdConflict = Descriptor;
		IdConflict.CommandType = SeinARTSTags::Command_Type_CancelAbility;
		TestRunner->AddExpectedError(
			TEXT("Rejected conflicting command StableSchemaId"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::RegisterSchema(
			TestOwner, IdConflict).IsValid()));

		FSeinCommandSchemaDescriptor Unmarked = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Unmarked"),
			SeinARTSTags::Command_Type_CancelAbility,
			8002,
			FSeinCommandSchemaUnmarkedTestPayload::StaticStruct());
		TestRunner->AddExpectedError(
			TEXT("is not SeinDeterministic"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::RegisterSchema(
			TestOwner, Unmarked).IsValid()));

		FSeinCommandSchemaDescriptor Unsupported = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Unsupported"),
			SeinARTSTags::Command_Type_CancelAbility,
			8003,
			FSeinCommandSchemaUnsupportedTestPayload::StaticStruct());
		TestRunner->AddExpectedError(
			TEXT("contains an unsupported or non-serialized field type"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::RegisterSchema(
			TestOwner, Unsupported).IsValid()));

		FSeinCommandSchemaDescriptor Unordered = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Unordered"),
			SeinARTSTags::Command_Type_CancelAbility,
			8004,
			FSeinCommandSchemaUnorderedTestPayload::StaticStruct());
		TestRunner->AddExpectedError(
			TEXT("contains an unsupported or non-serialized field type"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::RegisterSchema(
			TestOwner, Unordered).IsValid()));

		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::UnregisterSchema(First)));
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::FindSchema(
			SeinARTSTags::Command_Type_ActivateAbility, 8001, Found)));
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::UnregisterSchema(Reloaded)));
		ASSERT_THAT(IsFalse(FSeinCommandSchemaRegistry::FindSchema(
			SeinARTSTags::Command_Type_ActivateAbility, 8001, Found)));
		ASSERT_THAT(AreEqual(BaselineCount,
			FSeinCommandSchemaRegistry::GetRegisteredSchemaCount()));
	}

	TEST(CommandHandlerCDOAuthorsSchemaWithoutNativeDescriptorCode, "SeinARTS.Unit.CommandSchema")
	{
		FScopedSchemaHandles Registrations;
		FSeinCommandSchemaRegistrationHandle& Handle = Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterHandlerClass(
				TestOwner, USeinCommandSchemaConfiguredTestHandler::StaticClass()));
		ASSERT_THAT(IsTrue(Handle.IsValid()));

		FSeinCommandSchemaDescriptor Found;
		ASSERT_THAT(IsTrue(FSeinCommandSchemaRegistry::FindSchema(
			SeinARTSTags::Command_Type_Ping, 8301, Found)));
		ASSERT_THAT(AreEqual(FName(TEXT("SeinFrameworkTest.CommandSchema.CDO")),
			Found.StableSchemaId));
		ASSERT_THAT(AreEqual(128, Found.MaxPayloadBytes));
		ASSERT_THAT(AreEqual(8302, Found.ImplementationRevision));
		ASSERT_THAT(AreEqual(8, Found.MaxPayloadAggregateElements));
		ASSERT_THAT(AreEqual(1, Found.AllowedPayloadNames.Num()));
		ASSERT_THAT(AreEqual(
			FName(TEXT("SeinFrameworkTest.ConfiguredName")),
			Found.AllowedPayloadNames[0]));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinCommandExecutionAllowance::Spectator)
				| static_cast<int32>(ESeinCommandExecutionAllowance::HardPause),
			Found.AllowedExecutionContexts));
	}

	TEST(CommandSchemaManifestIsCanonicalAcrossRegistrationOrder, "SeinARTS.Unit.CommandSchema")
	{
		const FString BaselineManifest = FSeinCommandSchemaRegistry::BuildCanonicalManifest();
		const FSeinCommandSchemaDescriptor A = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.ManifestA"),
			SeinARTSTags::Command_Type_ActivateAbility, 8101);
		FSeinCommandSchemaDescriptor B = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.ManifestB"),
			SeinARTSTags::Command_Type_CancelAbility, 8102,
			FSeinCommandSchemaAlternateTestPayload::StaticStruct());
		B.AuthorityScope = ESeinCommandAuthorityScope::MatchControl;
		B.ImplementationRevision = 7;
		B.AllowedExecutionContexts =
			static_cast<int32>(ESeinCommandExecutionAllowance::Starting);

		FScopedSchemaHandles Forward;
		ASSERT_THAT(IsTrue(Forward.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, A)).IsValid()));
		ASSERT_THAT(IsTrue(Forward.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, B)).IsValid()));
		const FString ForwardManifest = FSeinCommandSchemaRegistry::BuildCanonicalManifest();
		const FGuid ForwardDigest = FSeinCommandSchemaRegistry::ComputeCanonicalManifestDigest();
		ASSERT_THAT(IsTrue(ForwardManifest.Contains(TEXT("seinframeworktest.commandschema.manifesta"))));
		ASSERT_THAT(IsTrue(ForwardManifest.Contains(TEXT("seinframeworktest.commandschema.manifestb"))));
		ASSERT_THAT(IsTrue(ForwardManifest.Contains(TEXT("StructLayout|3|"))));
		Forward.Reset();
		ASSERT_THAT(AreEqual(BaselineManifest,
			FSeinCommandSchemaRegistry::BuildCanonicalManifest()));

		FScopedSchemaHandles Reverse;
		ASSERT_THAT(IsTrue(Reverse.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, B)).IsValid()));
		ASSERT_THAT(IsTrue(Reverse.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, A)).IsValid()));
		ASSERT_THAT(AreEqual(ForwardManifest,
			FSeinCommandSchemaRegistry::BuildCanonicalManifest()));
		ASSERT_THAT(IsTrue(ForwardDigest ==
			FSeinCommandSchemaRegistry::ComputeCanonicalManifestDigest()));

		FSeinCommandSchemaRegistrationHandle& Duplicate = Reverse.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, A));
		ASSERT_THAT(IsTrue(Duplicate.IsValid()));
		ASSERT_THAT(AreEqual(ForwardManifest,
			FSeinCommandSchemaRegistry::BuildCanonicalManifest()));
	}

	TEST(CommandSchemaNameCatalogManifestIgnoresOrderAndDisplayCasing,
		"SeinARTS.Unit.CommandSchema")
	{
		FSeinCommandSchemaDescriptor First = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.NameCatalog"),
			SeinARTSTags::Command_Type_Ping, 8111,
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct());
		First.AllowedPayloadNames = { TEXT("Bravo"), TEXT("alpha"), TEXT("ALPHA") };
		FScopedSchemaHandles Registrations;
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(
				TEXT("SeinFrameworkTests.NameCatalog"), First)).IsValid()));
		const FString FirstManifest =
			FSeinCommandSchemaRegistry::BuildCanonicalManifest();
		const FGuid FirstDigest =
			FSeinCommandSchemaRegistry::ComputeCanonicalManifestDigest();
		Registrations.Reset();

		FSeinCommandSchemaDescriptor Second = First;
		Second.StableSchemaId = TEXT("seinframeworktest.commandschema.namecatalog");
		Second.AllowedPayloadNames = { TEXT("ALPHA"), TEXT("bravo") };
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(
				TEXT("seinframeworktests.namecatalog"), Second)).IsValid()));
		ASSERT_THAT(AreEqual(
			FirstManifest, FSeinCommandSchemaRegistry::BuildCanonicalManifest()));
		ASSERT_THAT(IsTrue(FirstDigest
			== FSeinCommandSchemaRegistry::ComputeCanonicalManifestDigest()));
	}

	TEST(CommandWireNameCatalogPreservesNumberedIdentityCanonically,
		"SeinARTS.Unit.CommandSchema")
	{
		const FName NumberOne(TEXT("SeinNumberedWireCatalog"), 1);
		const FName NumberTwo(TEXT("SeinNumberedWireCatalog"), 2);
		const FName NumberOneDifferentCase(
			TEXT("SEINNUMBEREDWIRECATALOG"), 1);
		TArray<FName> FirstCatalog;
		TArray<FName> SecondCatalog;
		FString FirstManifest;
		FString SecondManifest;
		const TArray<FName> FirstAuthored{
			NumberTwo, NumberOneDifferentCase, NAME_None, NumberTwo};
		const TArray<FName> SecondAuthored{NumberOne, NumberTwo};
		SeinBuildCanonicalWireNameCatalog(
			FirstAuthored, FirstCatalog, FirstManifest);
		SeinBuildCanonicalWireNameCatalog(
			SecondAuthored, SecondCatalog, SecondManifest);

		ASSERT_THAT(AreEqual(2, FirstCatalog.Num()));
		ASSERT_THAT(IsTrue(FirstCatalog == SecondCatalog));
		ASSERT_THAT(AreEqual(FirstManifest, SecondManifest));
		ASSERT_THAT(AreEqual(1, FirstCatalog[0].GetNumber()));
		ASSERT_THAT(AreEqual(2, FirstCatalog[1].GetNumber()));
		ASSERT_THAT(AreEqual(NumberOne, FirstCatalog[0]));
		ASSERT_THAT(AreEqual(NumberTwo, FirstCatalog[1]));
	}

	TEST(CommandProtocolDigestAlwaysBindsNormalizedSubmissionCap,
		"SeinARTS.Unit.CommandSchema")
	{
		const FGuid SchemaDigest(1, 2, 3, 4);
		const FString PolicyPath = TEXT("/Script/SeinARTSCoreEntity.TestAuthorityPolicy");
		const FGuid Cap32 = SeinComputeCommandProtocolDigest(
			SchemaDigest, PolicyPath, 7, 32);
		const FGuid Cap64 = SeinComputeCommandProtocolDigest(
			SchemaDigest, PolicyPath, 7, 64);

		ASSERT_THAT(IsTrue(Cap32.IsValid()));
		ASSERT_THAT(IsTrue(Cap32 != Cap64));
		ASSERT_THAT(IsTrue(Cap32 == SeinComputeCommandProtocolDigest(
			SchemaDigest, PolicyPath, 7, 32)));
		ASSERT_THAT(IsTrue(SeinComputeCommandProtocolDigest(
			SchemaDigest, PolicyPath, 7, 0)
			== SeinComputeCommandProtocolDigest(
				SchemaDigest, PolicyPath, 7, 1)));
		ASSERT_THAT(IsTrue(SeinComputeCommandProtocolDigest(
			SchemaDigest, PolicyPath, 7, MAX_int32)
			== SeinComputeCommandProtocolDigest(
				SchemaDigest, PolicyPath, 7,
				SeinCommandProtocolLimits::MaxCommandsPerAuthor)));
	}

	TEST(CommandSchemaManifestFramesWireSerializationOrder, "SeinARTS.Unit.CommandSchema")
	{
		FScopedSchemaHandles Registrations;
		const FSeinCommandSchemaDescriptor Descriptor = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.WireOrder"),
			SeinARTSTags::Command_Type_ActivateAbility,
			8151,
			FSeinCommandSchemaWireOrderTestPayload::StaticStruct());
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, Descriptor)).IsValid()));

		const FString Manifest = FSeinCommandSchemaRegistry::BuildCanonicalManifest();
		auto Frame = [](const FString& Value)
		{
			return FString::Printf(TEXT("%d:%s"), Value.Len(), *Value);
		};
		// Entries remain name-sorted, but each carries its pre-sort reflected
		// ordinal. Reordering either declaration therefore changes the digest.
		const FString FirstDeclaration =
			Frame(TEXT("ZetaDeclaredFirst")) + Frame(TEXT("1")) + Frame(TEXT("0"));
		const FString SecondDeclaration =
			Frame(TEXT("AlphaDeclaredSecond")) + Frame(TEXT("1")) + Frame(TEXT("1"));
		ASSERT_THAT(IsTrue(Manifest.Contains(FirstDeclaration)));
		ASSERT_THAT(IsTrue(Manifest.Contains(SecondDeclaration)));

		FString ReorderedManifest = Manifest;
		const FString FirstMovedSecond =
			Frame(TEXT("ZetaDeclaredFirst")) + Frame(TEXT("1")) + Frame(TEXT("1"));
		const FString SecondMovedFirst =
			Frame(TEXT("AlphaDeclaredSecond")) + Frame(TEXT("1")) + Frame(TEXT("0"));
		ASSERT_THAT(AreEqual(1, ReorderedManifest.ReplaceInline(
			*FirstDeclaration, *FirstMovedSecond, ESearchCase::CaseSensitive)));
		ASSERT_THAT(AreEqual(1, ReorderedManifest.ReplaceInline(
			*SecondDeclaration, *SecondMovedFirst, ESearchCase::CaseSensitive)));

		auto DigestManifest = [](const FString& Value)
		{
			FTCHARToUTF8 Utf8(*Value);
			const FBlake3Hash Hash = FBlake3::HashBuffer(Utf8.Get(), Utf8.Length());
			const uint8* Bytes = Hash.GetBytes();
			auto ReadBigEndian = [Bytes](int32 Offset)
			{
				return static_cast<uint32>(Bytes[Offset]) << 24
					| static_cast<uint32>(Bytes[Offset + 1]) << 16
					| static_cast<uint32>(Bytes[Offset + 2]) << 8
					| static_cast<uint32>(Bytes[Offset + 3]);
			};
			return FGuid(
				ReadBigEndian(0), ReadBigEndian(4),
				ReadBigEndian(8), ReadBigEndian(12));
		};
		ASSERT_THAT(IsTrue(DigestManifest(Manifest) ==
			FSeinCommandSchemaRegistry::ComputeCanonicalManifestDigest()));
		ASSERT_THAT(IsTrue(DigestManifest(Manifest) != DigestManifest(ReorderedManifest)));
	}

	TEST(CommandSchemaValidationRejectsShapeTypeAndRecursiveBudgetViolations,
		"SeinARTS.Unit.CommandSchema")
	{
		FScopedSchemaHandles Registrations;
		FSeinCommandSchemaDescriptor Passing = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Validation.Pass"),
			SeinARTSTags::Command_Type_ActivateAbility,
			8201,
			FSeinCommandSchemaTestPayload::StaticStruct());
		Passing.MaxPayloadBytes = 36;
		Passing.MaxPayloadAggregateElements = 8;
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, Passing)).IsValid()));

		FSeinCommandSchemaDescriptor ByteLimited = Passing;
		ByteLimited.StableSchemaId = TEXT("SeinFrameworkTest.CommandSchema.Validation.Bytes");
		ByteLimited.SchemaVersion = 8202;
		ByteLimited.MaxPayloadBytes = 35;
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, ByteLimited)).IsValid()));

		FSeinCommandSchemaDescriptor ElementLimited = Passing;
		ElementLimited.StableSchemaId = TEXT("SeinFrameworkTest.CommandSchema.Validation.Elements");
		ElementLimited.SchemaVersion = 8203;
		ElementLimited.MaxPayloadAggregateElements = 7;
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, ElementLimited)).IsValid()));

		FSeinCommandSchemaDescriptor NoPayload = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Validation.None"),
			SeinARTSTags::Command_Type_CancelAbility,
			8204);
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, NoPayload)).IsValid()));

		FSeinCommandSchemaDescriptor Dynamic = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.Validation.Dynamic"),
			SeinARTSTags::Command_Type_Ping,
			8205,
			FSeinCommandSchemaDynamicTestPayload::StaticStruct());
		Dynamic.DynamicPayloadStructs.Add(
			FSeinCommandSchemaAlternateTestPayload::StaticStruct());
		ASSERT_THAT(IsTrue(Registrations.Add(
			FSeinCommandSchemaRegistry::RegisterSchema(TestOwner, Dynamic)).IsValid()));

		FSeinCommand Command;
		Command.CommandType = SeinARTSTags::Command_Type_ActivateAbility;
		Command.SchemaVersion = 8201;
		Command.Payload = FInstancedStruct::Make(MakeBudgetedPayload());
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::Valid,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));

		Command.SchemaVersion = 8202;
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::PayloadTooLarge,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.SchemaVersion = 8203;
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::PayloadTooLarge,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));

		Command.SchemaVersion = 8201;
		Command.Payload.Reset();
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::MissingPayload,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.Payload = FInstancedStruct::Make(FSeinCommandSchemaAlternateTestPayload());
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::WrongPayloadType,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.Payload = FInstancedStruct::Make(FSeinCommandSchemaUnmarkedTestPayload());
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::NonDeterministicPayload,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));

		Command.CommandType = SeinARTSTags::Command_Type_CancelAbility;
		Command.SchemaVersion = 8204;
		Command.Payload = FInstancedStruct::Make(FSeinCommandSchemaAlternateTestPayload());
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::UnexpectedPayload,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.Payload.Reset();
		Command.EntityList.SetNum(3);
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::EntityListTooLarge,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.EntityList.Reset();
		Command.TargeterPoints.SetNum(2);
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::TargeterPointsTooLarge,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));

		Command = {};
		Command.CommandType = SeinARTSTags::Command_Type_Ping;
		Command.SchemaVersion = 8205;
		FSeinCommandSchemaDynamicTestPayload DynamicPayload;
		DynamicPayload.Extensions.Add(FInstancedStruct::Make(
			FSeinCommandSchemaAlternateTestPayload()));
		Command.Payload = FInstancedStruct::Make(DynamicPayload);
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::Valid,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		DynamicPayload.Extensions[0] = FInstancedStruct::Make(MakeBudgetedPayload());
		Command.Payload = FInstancedStruct::Make(DynamicPayload);
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::UnsupportedPayloadField,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		TArray<const UScriptStruct*> AdditionalDynamicTypes{
			FSeinCommandSchemaTestPayload::StaticStruct()
		};
		const FSeinCommandSchemaSnapshot ExtendedSnapshot =
			FSeinCommandSchemaRegistry::CaptureSnapshot(AdditionalDynamicTypes);
		ASSERT_THAT(IsTrue(ExtendedSnapshot.IsValid()));
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::Valid,
			ExtendedSnapshot.ValidateStructure(Command)));
		DynamicPayload.Extensions[0] = FInstancedStruct::Make(
			FSeinCommandSchemaUnmarkedTestPayload());
		Command.Payload = FInstancedStruct::Make(DynamicPayload);
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::NonDeterministicPayload,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		DynamicPayload.Extensions[0] = FInstancedStruct::Make(
			FSeinCommandSchemaUnsupportedTestPayload());
		Command.Payload = FInstancedStruct::Make(DynamicPayload);
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::UnsupportedPayloadField,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));

		Command = {};
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::InvalidCommandType,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.CommandType = SeinARTSTags::Command_Type_ActivateAbility;
		Command.SchemaVersion = 0;
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::InvalidSchemaVersion,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.SchemaVersion = 9999;
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::UnsupportedSchemaVersion,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
		Command.CommandType = SeinARTSTags::Command_Context_AbilityTriggered;
		Command.SchemaVersion = 1;
		ASSERT_THAT(AreEqual(ESeinCommandStructureResult::UnknownCommandType,
			FSeinCommandSchemaRegistry::ValidateStructure(Command)));
	}

	TEST(CanonicalStateCodecRejectsArrayCountBeforeMutation,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		FSeinCommandSchemaTestPayload Source;
		Source.Groups.SetNum(1);
		Source.Groups[0].Values = { 7, 8 };
		const FSeinStructWireLimits Limits{ 4096, 8, 128, 64 };
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Encode(
			FSeinCommandSchemaTestPayload::StaticStruct(), &Source, {},
			Limits, Bytes, Error)));
		ASSERT_THAT(IsTrue(Bytes.Num() >= 4));

		Bytes[0] = Bytes[1] = Bytes[2] = Bytes[3] = 0xff;
		FSeinCommandSchemaTestPayload Destination;
		Destination.Groups.SetNum(1);
		Destination.Groups[0].Values = { 99 };
		ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::Decode(
			Bytes, FSeinCommandSchemaTestPayload::StaticStruct(), &Destination,
			{}, Limits, Error)));
		ASSERT_THAT(AreEqual(1, Destination.Groups.Num()));
		ASSERT_THAT(AreEqual(1, Destination.Groups[0].Values.Num()));
		ASSERT_THAT(AreEqual(99, Destination.Groups[0].Values[0]));
	}

	TEST(CanonicalStateCodecRejectsDynamicTypeOutsideFrozenCatalog,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		FSeinCommandSchemaDynamicTestPayload Source;
		FInstancedStruct& Extension = Source.Extensions.AddDefaulted_GetRef();
		Extension.InitializeAs<FSeinCommandSchemaAlternateTestPayload>();
		const TArray<const UScriptStruct*> Allowed{
			FSeinCommandSchemaAlternateTestPayload::StaticStruct() };
		const FSeinStructWireLimits Limits{ 4096, 8, 128, 64 };
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Encode(
			FSeinCommandSchemaDynamicTestPayload::StaticStruct(), &Source,
			{ Allowed, {} }, Limits, Bytes, Error)));
		ASSERT_THAT(IsTrue(Bytes.Num() >= 12));
		Bytes[8] = 0;
		Bytes[9] = 0;
		Bytes[10] = 0;
		Bytes[11] = 1;
		FSeinCommandSchemaDynamicTestPayload Destination;
		ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::Decode(
			Bytes, FSeinCommandSchemaDynamicTestPayload::StaticStruct(), &Destination,
			{ Allowed, {} }, Limits, Error)));
		ASSERT_THAT(AreEqual(0, Destination.Extensions.Num()));
	}

	TEST(CanonicalStateCodecRequiresCanonicalEntityHandlesTransactionally,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		const FSeinStructWireLimits Limits{ 64, 4, 128, 16 };
		FSeinCommandSchemaEntityHandleWireTestPayload Source;
		Source.Entity = FSeinEntityHandle(7, 9);
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Encode(
			FSeinCommandSchemaEntityHandleWireTestPayload::StaticStruct(),
			&Source, {}, Limits, Bytes, Error)));
		ASSERT_THAT(AreEqual(8, Bytes.Num()));
		const TArray<uint8> ExpectedBytes{
			0, 0, 0, 7,
			0, 0, 0, 9 };
		ASSERT_THAT(IsTrue(Bytes == ExpectedBytes));

		FSeinCommandSchemaEntityHandleWireTestPayload Destination;
		Destination.Entity = FSeinEntityHandle(19, 23);
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Decode(
			Bytes,
			FSeinCommandSchemaEntityHandleWireTestPayload::StaticStruct(),
			&Destination, {}, Limits, Error)));
		ASSERT_THAT(AreEqual(7, Destination.Entity.Index));
		ASSERT_THAT(AreEqual(9, Destination.Entity.Generation));

		Source.Entity = FSeinEntityHandle::Invalid();
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Encode(
			FSeinCommandSchemaEntityHandleWireTestPayload::StaticStruct(),
			&Source, {}, Limits, Bytes, Error)));
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Decode(
			Bytes,
			FSeinCommandSchemaEntityHandleWireTestPayload::StaticStruct(),
			&Destination, {}, Limits, Error)));
		ASSERT_THAT(AreEqual(0, Destination.Entity.Index));
		ASSERT_THAT(AreEqual(0, Destination.Entity.Generation));

		const TArray<FSeinEntityHandle> NonCanonical{
			FSeinEntityHandle(0, 1),
			FSeinEntityHandle(1, 0),
			FSeinEntityHandle(-1, 1),
			FSeinEntityHandle(1, -1),
			FSeinEntityHandle(-1, -1)
		};
		for (const FSeinEntityHandle Handle : NonCanonical)
		{
			Source.Entity = Handle;
			ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::Encode(
				FSeinCommandSchemaEntityHandleWireTestPayload::StaticStruct(),
				&Source, {}, Limits, Bytes, Error)));
			ASSERT_THAT(AreEqual(0, Bytes.Num()));
		}

		auto WriteI32 = [](TArray<uint8>& Target, int32 Offset, int32 Value)
		{
			const uint32 Bits = BitCast<uint32>(Value);
			for (int32 Byte = 0; Byte < 4; ++Byte)
			{
				Target[Offset + Byte] =
					static_cast<uint8>(Bits >> ((3 - Byte) * 8));
			}
		};
		for (const FSeinEntityHandle Handle : NonCanonical)
		{
			TArray<uint8> Malformed;
			Malformed.SetNumZeroed(8);
			WriteI32(Malformed, 0, Handle.Index);
			WriteI32(Malformed, 4, Handle.Generation);
			Destination.Entity = FSeinEntityHandle(19, 23);
			ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::Decode(
				Malformed,
				FSeinCommandSchemaEntityHandleWireTestPayload::StaticStruct(),
				&Destination, {}, Limits, Error)));
			ASSERT_THAT(AreEqual(19, Destination.Entity.Index));
			ASSERT_THAT(AreEqual(23, Destination.Entity.Generation));
		}
	}

	TEST(OpaqueCommandWireRequiresCanonicalEntityHandlesTransactionally,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		const FSeinCommandSchemaDescriptor Schema = MakeSchema(
			TEXT("SeinFrameworkTest.CommandWire.EntityHandle"),
			SeinARTSTags::Command_Type_Ping, 9907);
		auto FindSchema = [&Schema](
			FGameplayTag Type, int32 Version,
			FSeinCommandSchemaDescriptor& Out)
		{
			if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
				return false;
			Out = Schema;
			return true;
		};

		FSeinCommand Source;
		Source.CommandType = Schema.CommandType;
		Source.SchemaVersion = Schema.SchemaVersion;
		Source.EntityHandle = FSeinEntityHandle(4, 2);
		Source.EntityList = { FSeinEntityHandle(6, 3) };
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(
			Source, Schema, Bytes, Error)));

		FSeinCommand Destination;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Decode(
			Bytes, FindSchema, Destination, Error)));
		ASSERT_THAT(AreEqual(4, Destination.EntityHandle.Index));
		ASSERT_THAT(AreEqual(2, Destination.EntityHandle.Generation));
		ASSERT_THAT(AreEqual(1, Destination.EntityList.Num()));
		ASSERT_THAT(AreEqual(6, Destination.EntityList[0].Index));
		ASSERT_THAT(AreEqual(3, Destination.EntityList[0].Generation));

		FSeinCommand NonCanonical = Source;
		NonCanonical.EntityHandle = FSeinEntityHandle(0, 1);
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Encode(
			NonCanonical, Schema, Bytes, Error)));
		NonCanonical = Source;
		NonCanonical.TargetEntity = FSeinEntityHandle(-1, 1);
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Encode(
			NonCanonical, Schema, Bytes, Error)));
		NonCanonical = Source;
		NonCanonical.EntityList = { FSeinEntityHandle(1, -1) };
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Encode(
			NonCanonical, Schema, Bytes, Error)));

		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(
			Source, Schema, Bytes, Error)));
		auto ReadU32 = [&Bytes](int32 Offset)
		{
			return static_cast<uint32>(Bytes[Offset]) << 24
				| static_cast<uint32>(Bytes[Offset + 1]) << 16
				| static_cast<uint32>(Bytes[Offset + 2]) << 8
				| static_cast<uint32>(Bytes[Offset + 3]);
		};
		int32 EntityOffset = 6;
		const uint32 TypeBytes = ReadU32(EntityOffset);
		EntityOffset += 4 + static_cast<int32>(TypeBytes);
		EntityOffset += 4 + 3;
		ASSERT_THAT(IsTrue(EntityOffset + 8 <= Bytes.Num()));
		for (int32 Byte = 0; Byte < 8; ++Byte)
		{
			Bytes[EntityOffset + Byte] = 0;
		}
		Bytes[EntityOffset + 7] = 1;

		Destination = {};
		Destination.Tick = 777;
		Destination.EntityHandle = FSeinEntityHandle(19, 23);
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Decode(
			Bytes, FindSchema, Destination, Error)));
		ASSERT_THAT(AreEqual(777, Destination.Tick));
		ASSERT_THAT(AreEqual(19, Destination.EntityHandle.Index));
		ASSERT_THAT(AreEqual(23, Destination.EntityHandle.Generation));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("non-canonical entity handle"))));
	}

	TEST(OpaqueCommandWireRejectsCommonArrayCountBeforeMutation,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		FSeinCommandSchemaDescriptor Schema = MakeSchema(
			TEXT("SeinFrameworkTest.CommandWire.Count"),
			SeinARTSTags::Command_Type_Ping, 9901);
		Schema.MaxTargeterPoints = 1;
		FSeinCommand Source;
		Source.CommandType = Schema.CommandType;
		Source.SchemaVersion = Schema.SchemaVersion;
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(
			Source, Schema, Bytes, Error)));

		auto ReadU32 = [&Bytes](int32 Offset)
		{
			return static_cast<uint32>(Bytes[Offset]) << 24
				| static_cast<uint32>(Bytes[Offset + 1]) << 16
				| static_cast<uint32>(Bytes[Offset + 2]) << 8
				| static_cast<uint32>(Bytes[Offset + 3]);
		};
		int32 Offset = 6;
		const uint32 TypeBytes = ReadU32(Offset);
		Offset += 4 + static_cast<int32>(TypeBytes);
		Offset += 4 + 3 + 8;
		const uint8 AbilityTagValid = Bytes[Offset++];
		ASSERT_THAT(AreEqual(static_cast<uint8>(0), AbilityTagValid));
		Offset += 8 + 24 + 4 + 4 + 1 + 24;
		ASSERT_THAT(IsTrue(Offset + 4 <= Bytes.Num()));
		Bytes[Offset] = Bytes[Offset + 1] = Bytes[Offset + 2] = Bytes[Offset + 3] = 0xff;

		FSeinCommand Destination;
		Destination.Tick = 12345;
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Decode(
			Bytes,
			[&Schema](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
			{
				if (Type != Schema.CommandType || Version != Schema.SchemaVersion) return false;
				Out = Schema;
				return true;
			},
			Destination,
			Error)));
		ASSERT_THAT(AreEqual(12345, Destination.Tick));
		ASSERT_THAT(AreEqual(0, Destination.TargeterPoints.Num()));
	}

	TEST(CanonicalStateCodecRoundTripsInvalidAndValidNamesAndTags,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		const FSeinStructWireLimits Limits{4096, 8, 128, 64};
		FString Error;
		TArray<uint8> Bytes;
		FSeinCommandSchemaIdentityWireTestPayload Source;
		FSeinCommandSchemaIdentityWireTestPayload Destination;
		Destination.Name = TEXT("PreservedUntilCommit");
		Destination.Tag = SeinARTSTags::Command_Type_Ping;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Encode(
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct(), &Source,
			{}, Limits, Bytes, Error)));
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Decode(
			Bytes, FSeinCommandSchemaIdentityWireTestPayload::StaticStruct(),
			&Destination, {}, Limits, Error)));
		ASSERT_THAT(IsTrue(Destination.Name.IsNone()));
		ASSERT_THAT(IsFalse(Destination.Tag.IsValid()));

		Source.Name = TEXT("SeinCommandWireKnownName");
		Source.Tag = SeinARTSTags::Command_Type_Ping;
		ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::Encode(
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct(), &Source,
			{}, Limits, Bytes, Error)));
		TArray<FName> CanonicalNames;
		FString NameManifest;
		const TArray<FName> AuthoredNames{
			Source.Name, FName(TEXT("seincommandwireknownname")) };
		SeinBuildCanonicalWireNameCatalog(
			AuthoredNames,
			CanonicalNames, NameManifest);
		ASSERT_THAT(AreEqual(1, CanonicalNames.Num()));
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Encode(
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct(), &Source,
			{ {}, CanonicalNames }, Limits, Bytes, Error)));
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::Decode(
			Bytes, FSeinCommandSchemaIdentityWireTestPayload::StaticStruct(),
			&Destination, { {}, CanonicalNames }, Limits, Error)));
		ASSERT_THAT(AreEqual(Source.Name, Destination.Name));
		ASSERT_THAT(IsTrue(Source.Tag == Destination.Tag));

		// The first field is a uint32 catalog index. An out-of-range value is
		// rejected transactionally and cannot nominate text for FName interning.
		Bytes[0] = Bytes[1] = Bytes[2] = 0;
		Bytes[3] = 2;
		Destination.Name = TEXT("PreservedUntilCommit");
		ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::Decode(
			Bytes, FSeinCommandSchemaIdentityWireTestPayload::StaticStruct(),
			&Destination, { {}, CanonicalNames }, Limits, Error)));
		ASSERT_THAT(AreEqual(
			FName(TEXT("PreservedUntilCommit")), Destination.Name));
	}

	TEST(CommandStructureRejectsRawNameOutsideFrozenCatalog,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		FSeinCommandSchemaDescriptor Schema = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.RawName"),
			SeinARTSTags::Command_Type_Ping, 9904,
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct());
		Schema.AllowedPayloadNames = { TEXT("AllowedIdentifier") };
		FSeinCommandSchemaIdentityWireTestPayload Payload;
		Payload.Name = TEXT("RejectedIdentifier");
		FSeinCommand Command;
		Command.CommandType = Schema.CommandType;
		Command.SchemaVersion = Schema.SchemaVersion;
		Command.Payload = FInstancedStruct::Make(Payload);
		ASSERT_THAT(AreEqual(
			ESeinCommandStructureResult::PayloadNameOutsideCatalog,
			SeinValidateCommandAgainstSchema(Command, Schema)));
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Encode(
			Command, Schema, Bytes, Error)));

		Payload.Name = TEXT("allowedidentifier");
		Command.Payload = FInstancedStruct::Make(Payload);
		ASSERT_THAT(AreEqual(
			ESeinCommandStructureResult::Valid,
			SeinValidateCommandAgainstSchema(Command, Schema)));
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(
			Command, Schema, Bytes, Error)));
	}

	TEST(StandaloneAndOpaqueNetworkCanonicalizeMixedCaseNamesIdentically,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		FSeinCommandSchemaDescriptor Schema = MakeSchema(
			TEXT("SeinFrameworkTest.CommandSchema.NameParity"),
			SeinARTSTags::Command_Type_Ping, 9905,
			FSeinCommandSchemaIdentityWireTestPayload::StaticStruct());
		TArray<FName> CanonicalNames;
		FString NameManifest;
		const TArray<FName> AuthoredNames{TEXT("CanonicalParityIdentifier")};
		SeinBuildCanonicalWireNameCatalog(
			AuthoredNames, CanonicalNames, NameManifest);
		Schema.AllowedPayloadNames = CanonicalNames;

		FSeinCommandSchemaIdentityWireTestPayload Payload;
		Payload.Name = TEXT("CANONICALPARITYIDENTIFIER");
		FSeinCommand Source;
		Source.CommandType = Schema.CommandType;
		Source.SchemaVersion = Schema.SchemaVersion;
		Source.Payload = FInstancedStruct::Make(Payload);

		// This is the exact canonicalization seam used by standalone dispatch.
		FSeinCommand Standalone = Source;
		ASSERT_THAT(IsTrue(SeinCanonicalizeCommandPayloadNames(
			Standalone, Schema)));

		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(
			Source, Schema, Bytes, Error)));
		auto FindSchema = [&Schema](
			FGameplayTag Type, int32 Version,
			FSeinCommandSchemaDescriptor& Out)
		{
			if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
				return false;
			Out = Schema;
			return true;
		};
		FSeinCommand Network;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Decode(
			Bytes, FindSchema, Network, Error)));
		const FName StandaloneName = Standalone.Payload
			.Get<FSeinCommandSchemaIdentityWireTestPayload>().Name;
		const FName NetworkName = Network.Payload
			.Get<FSeinCommandSchemaIdentityWireTestPayload>().Name;
		ASSERT_THAT(AreEqual(CanonicalNames[0], StandaloneName));
		ASSERT_THAT(AreEqual(StandaloneName, NetworkName));
	}

	TEST(OpaqueCommandWireRoundTripsInvalidAbilityTagAndRejectsEmbeddedNull,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		FSeinCommandSchemaDescriptor Schema = MakeSchema(
			TEXT("SeinFrameworkTest.CommandWire.Identity"),
			SeinARTSTags::Command_Type_Ping, 9902);
		FSeinCommand Source;
		Source.CommandType = Schema.CommandType;
		Source.SchemaVersion = Schema.SchemaVersion;
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(
			Source, Schema, Bytes, Error)));
		auto FindSchema = [&Schema](
			FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
				return false;
			Out = Schema;
			return true;
		};
		FSeinCommand Destination;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Decode(
			Bytes, FindSchema, Destination, Error)));
		ASSERT_THAT(IsFalse(Destination.AbilityTag.IsValid()));

		// Prefix is magic(4), version(2), UTF-8 byte count(4), then tag bytes.
		ASSERT_THAT(IsTrue(Bytes.Num() > 10));
		Bytes[10] = 0;
		Destination.Tick = 777;
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Decode(
			Bytes, FindSchema, Destination, Error)));
		ASSERT_THAT(AreEqual(777, Destination.Tick));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("embedded null"))));
	}

	TEST(OpaqueCommandWireRejectsVersionTwoTransactionally,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		const FSeinCommandSchemaDescriptor Schema = MakeSchema(
			TEXT("SeinFrameworkTest.CommandWire.Version"),
			SeinARTSTags::Command_Type_Ping, 9906);
		FSeinCommand Source;
		Source.CommandType = Schema.CommandType;
		Source.SchemaVersion = Schema.SchemaVersion;
		TArray<uint8> Bytes;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::Encode(
			Source, Schema, Bytes, Error)));
		ASSERT_THAT(IsTrue(Bytes.Num() >= 6));
		// Command prefix is magic(4), then the big-endian uint16 wire version.
		Bytes[4] = 0;
		Bytes[5] = 2;
		auto FindSchema = [&Schema](
			FGameplayTag Type, int32 Version,
			FSeinCommandSchemaDescriptor& Out)
		{
			if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
				return false;
			Out = Schema;
			return true;
		};
		FSeinCommand Destination;
		Destination.Tick = 777;
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::Decode(
			Bytes, FindSchema, Destination, Error)));
		ASSERT_THAT(AreEqual(777, Destination.Tick));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("invalid opaque command prefix"))));
	}

	TEST(CanonicalStateCodecSeparatesCanonicalCostFromNativeExpansion,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		FSeinCommandSchemaLargeWireArrayPayload PaddedSource;
		PaddedSource.Values.AddDefaulted();
		PaddedSource.Values[0].Marker = 17;
		FSeinCommandSchemaCompactWireArrayPayload CompactSource;
		CompactSource.Values.AddDefaulted();
		CompactSource.Values[0].Marker = 17;

		// The reflected shape and exact wire are identical. Native-only padding
		// may affect the local safety charge, never the consensus-facing cost.
		const FSeinStructWireLimits EncodeLimits{ 64, 4, 128, 64, 8192 };
		TArray<uint8> PaddedBytes;
		TArray<uint8> CompactBytes;
		FString Error;
		FSeinWireCost PaddedEncodeCost;
		FSeinWireCost CompactEncodeCost;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::EncodeWithCost(
			FSeinCommandSchemaLargeWireArrayPayload::StaticStruct(), &PaddedSource,
			{}, EncodeLimits, PaddedBytes, Error, PaddedEncodeCost)));
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::EncodeWithCost(
			FSeinCommandSchemaCompactWireArrayPayload::StaticStruct(), &CompactSource,
			{}, EncodeLimits, CompactBytes, Error, CompactEncodeCost)));
		ASSERT_THAT(IsTrue(PaddedBytes == CompactBytes));
		ASSERT_THAT(AreEqual(
			PaddedEncodeCost.CanonicalCostBytes,
			CompactEncodeCost.CanonicalCostBytes));
		ASSERT_THAT(IsTrue(
			PaddedEncodeCost.NativeAllocationBytes
				> CompactEncodeCost.NativeAllocationBytes));

		const FSeinStructWireLimits DecodeLimits{ 64, 4, 128, 64, 256 };
		FSeinCommandSchemaCompactWireArrayPayload CompactDestination;
		FSeinWireCost CompactDecodeCost;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::DecodeWithCost(
			CompactBytes, FSeinCommandSchemaCompactWireArrayPayload::StaticStruct(),
			&CompactDestination, {}, DecodeLimits, Error, CompactDecodeCost)));
		ASSERT_THAT(AreEqual(1, CompactDestination.Values.Num()));
		ASSERT_THAT(AreEqual(
			CompactEncodeCost.CanonicalCostBytes,
			CompactDecodeCost.CanonicalCostBytes));

		FSeinCommandSchemaLargeWireArrayPayload PaddedDestination;
		FSeinWireCost RejectedCost;
		ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::DecodeWithCost(
			PaddedBytes, FSeinCommandSchemaLargeWireArrayPayload::StaticStruct(),
			&PaddedDestination, {}, DecodeLimits, Error, RejectedCost)));
		ASSERT_THAT(AreEqual(0, PaddedDestination.Values.Num()));
		ASSERT_THAT(AreEqual(static_cast<uint64>(0), RejectedCost.CanonicalCostBytes));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("allocation"))));
	}

	TEST(CanonicalStateCodecChargesNestedStringBackingBeforeConstruction,
		"SeinARTS.Unit.CoreEntity.CommandSchema.Security")
	{
		auto MakePayload = [](bool bFilled)
		{
			FSeinCommandSchemaNestedStringPayload Payload;
			Payload.Groups.SetNum(2);
			for (FSeinCommandSchemaStringGroup& Group : Payload.Groups)
			{
				Group.Values.SetNum(32);
			if (bFilled)
			{
				FString Repeated;
				Repeated.Reserve(64);
				for (int32 Index = 0; Index < 64; ++Index)
				{
					Repeated.AppendChar(TEXT('x'));
				}
				for (FString& Value : Group.Values)
				{
					Value = Repeated;
				}
			}
			}
			return Payload;
		};

		const FSeinCommandSchemaNestedStringPayload Empty = MakePayload(false);
		const FSeinCommandSchemaNestedStringPayload Filled = MakePayload(true);
		const FSeinStructWireLimits EncodeLimits{ 16384, 128, 128, 64, 1024 * 1024 };
		TArray<uint8> EmptyBytes;
		TArray<uint8> FilledBytes;
		FString Error;
		FSeinWireCost EmptyCost;
		FSeinWireCost FilledCost;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::EncodeWithCost(
			FSeinCommandSchemaNestedStringPayload::StaticStruct(), &Empty,
			{}, EncodeLimits, EmptyBytes, Error, EmptyCost)));
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::EncodeWithCost(
			FSeinCommandSchemaNestedStringPayload::StaticStruct(), &Filled,
			{}, EncodeLimits, FilledBytes, Error, FilledCost)));
		// Includes bounded UTF conversion + canonical round-trip scratch at
		// the decode boundary, not only the retained FString allocation.
		const uint64 ExpectedBackingBytes =
			64u * 65u * (4u * sizeof(TCHAR));
		ASSERT_THAT(AreEqual(
			ExpectedBackingBytes,
			FilledCost.NativeAllocationBytes - EmptyCost.NativeAllocationBytes));

		const FSeinStructWireLimits TightLimits{
			16384, 128, 128, 64,
			static_cast<int32>(EmptyCost.NativeAllocationBytes) };
		FSeinCommandSchemaNestedStringPayload EmptyDestination;
		FSeinWireCost EmptyDecodeCost;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::DecodeWithCost(
			EmptyBytes, FSeinCommandSchemaNestedStringPayload::StaticStruct(),
			&EmptyDestination, {}, TightLimits, Error, EmptyDecodeCost)));
		ASSERT_THAT(AreEqual(
			EmptyCost.NativeAllocationBytes,
			EmptyDecodeCost.NativeAllocationBytes));

		FSeinCommandSchemaNestedStringPayload Preserved = MakePayload(false);
		Preserved.Groups[0].Values[0] = TEXT("Preserved");
		FSeinWireCost RejectedCost;
		ASSERT_THAT(IsFalse(FSeinCanonicalStateCodec::DecodeWithCost(
			FilledBytes, FSeinCommandSchemaNestedStringPayload::StaticStruct(),
			&Preserved, {}, TightLimits, Error, RejectedCost)));
		ASSERT_THAT(AreEqual(FString(TEXT("Preserved")), Preserved.Groups[0].Values[0]));
		ASSERT_THAT(AreEqual(static_cast<uint64>(0), RejectedCost.NativeAllocationBytes));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("allocation"))));

		// Command schemas intentionally reject FString even though the generic
		// replay/metadata codec supports it. Use a valid nested-array payload to
		// verify the command envelope's two-live-copy allocation charge.
		FSeinCommandSchemaTestPayload CommandPayload;
		CommandPayload.Groups.SetNum(2);
		for (FSeinCommandSchemaNestedTestValue& Group : CommandPayload.Groups)
		{
			Group.Values.SetNum(32);
		}
		TArray<uint8> CommandPayloadBytes;
		FSeinWireCost CommandPayloadCost;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::EncodeWithCost(
			FSeinCommandSchemaTestPayload::StaticStruct(), &CommandPayload,
			{}, EncodeLimits, CommandPayloadBytes, Error, CommandPayloadCost)));

		FSeinCommandSchemaDescriptor Schema = MakeSchema(
			TEXT("SeinFrameworkTest.CommandWire.NestedArrays"),
			SeinARTSTags::Command_Type_Ping, 9903,
			FSeinCommandSchemaTestPayload::StaticStruct());
		Schema.MaxPayloadBytes = 16384;
		Schema.MaxPayloadAggregateElements = 128;
		FSeinCommand Command;
		Command.CommandType = Schema.CommandType;
		Command.SchemaVersion = Schema.SchemaVersion;
		Command.Payload = FInstancedStruct::Make(CommandPayload);
		TArray<uint8> CommandBytes;
		FSeinWireCost CommandCost;
		ASSERT_THAT(IsTrue(FSeinCommandWireCodec::EncodeWithCost(
			Command, Schema, CommandBytes, Error, CommandCost)));
		ASSERT_THAT(IsTrue(
			CommandBytes.Num() >= CommandPayloadBytes.Num()));
		const int32 CommandPayloadOffset =
			CommandBytes.Num() - CommandPayloadBytes.Num();
		ASSERT_THAT(IsTrue(FMemory::Memcmp(
			CommandBytes.GetData() + CommandPayloadOffset,
			CommandPayloadBytes.GetData(),
			CommandPayloadBytes.Num()) == 0));
		ASSERT_THAT(IsTrue(
			CommandCost.NativeAllocationBytes
				>= CommandPayloadCost.NativeAllocationBytes * 2u));

		auto FindSchema = [&Schema](
			FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
		{
			if (Type != Schema.CommandType || Version != Schema.SchemaVersion)
				return false;
			Out = Schema;
			return true;
		};
		FSeinCommand PreservedCommand;
		PreservedCommand.Tick = 777;
		FSeinWireCost RejectedCommandCost;
		ASSERT_THAT(IsFalse(FSeinCommandWireCodec::DecodeWithCost(
			CommandBytes, FindSchema, PreservedCommand, Error,
			static_cast<int32>(CommandCost.NativeAllocationBytes - 1u),
			RejectedCommandCost)));
		ASSERT_THAT(AreEqual(777, PreservedCommand.Tick));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("allocation"))));
	}
}
