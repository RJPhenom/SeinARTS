#include "CQTest.h"

#include "Attributes/SeinModifier.h"
#include "Brokers/SeinBrokerTypes.h"
#include "Collision/SeinCollisionTypes.h"
#include "Data/SeinBasicMatchSettings.h"
#include "Data/SeinMatchSettings.h"
#include "Data/SeinResourceTypes.h"
#include "Serialization/SeinDeterministicValueDigest.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		ESeinDeterministicValueDigestResult Digest(
			const UScriptStruct* Struct,
			const void* Memory,
			FGuid& OutDigest,
			FSeinDeterministicValueDigestError* OutError = nullptr,
			const FSeinDeterministicValueDigestOptions& Options = {})
		{
			return FSeinDeterministicValueDigest::Compute(
				Struct, Memory, OutDigest, OutError, Options);
		}

		FSeinMatchSettings MakeMatchSettings()
		{
			FSeinMatchSettings Settings;
			FSeinMatchSlot& Slot = Settings.Slots.AddDefaulted_GetRef();
			Slot.SlotIndex = 2;
			Slot.State = ESeinSlotState::AI;
			Slot.FactionID = FSeinFactionID(3);
			Slot.TeamID = 1;
			Slot.AIProfile = SeinARTSTags::Command_Context_Target_Enemy;

			FSeinBasicMatchSettings Basic;
			Basic.bFriendlyFire = true;
			Basic.bResourceSharing = false;
			Basic.TimeLimit = 45;
			Settings.Extensions.Add(FInstancedStruct::Make(Basic));
			return Settings;
		}
	}

	TEST(DeterministicValueDigestCanonicalizesUnorderedValues,
		"SeinARTS.Unit.DeterministicValueDigest")
	{
		FSeinResourceCost Forward;
		Forward.Amounts.Add(
			SeinARTSTags::Command_Type_ActivateAbility, FFixedPoint::FromInt(25));
		Forward.Amounts.Add(
			SeinARTSTags::Command_Type_CancelAbility, FFixedPoint::FromInt(10));

		FSeinResourceCost Reverse;
		Reverse.Amounts.Add(
			SeinARTSTags::Command_Type_CancelAbility, FFixedPoint::FromInt(10));
		Reverse.Amounts.Add(
			SeinARTSTags::Command_Type_ActivateAbility, FFixedPoint::FromInt(25));

		FGuid ForwardDigest;
		FGuid ReverseDigest;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinResourceCost::StaticStruct(), &Forward, ForwardDigest)));
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinResourceCost::StaticStruct(), &Reverse, ReverseDigest)));
		ASSERT_THAT(IsTrue(ForwardDigest.IsValid()));
		ASSERT_THAT(IsTrue(ForwardDigest == ReverseDigest));

		FSeinBrokerOrderPayload TagsForward;
		TagsForward.CommandContext.AddTag(SeinARTSTags::Command_Context_RightClick);
		TagsForward.CommandContext.AddTag(SeinARTSTags::Command_Context_Target_Enemy);
		FSeinBrokerOrderPayload TagsReverse;
		TagsReverse.CommandContext.AddTag(SeinARTSTags::Command_Context_Target_Enemy);
		TagsReverse.CommandContext.AddTag(SeinARTSTags::Command_Context_RightClick);
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinBrokerOrderPayload::StaticStruct(), &TagsForward, ForwardDigest)));
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinBrokerOrderPayload::StaticStruct(), &TagsReverse, ReverseDigest)));
		ASSERT_THAT(IsTrue(ForwardDigest == ReverseDigest));

		FSeinCollisionObjectType NameA;
		NameA.Channel = FName(TEXT("Default"));
		FSeinCollisionObjectType NameB;
		NameB.Channel = FName(TEXT("default"));
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinCollisionObjectType::StaticStruct(), &NameA, ForwardDigest)));
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinCollisionObjectType::StaticStruct(), &NameB, ReverseDigest)));
		ASSERT_THAT(IsTrue(ForwardDigest == ReverseDigest));
	}

	TEST(DeterministicValueDigestSupportsDynamicMatchExtensionsAndOrderedArrays,
		"SeinARTS.Unit.DeterministicValueDigest")
	{
		FSeinMatchSettings Settings = MakeMatchSettings();
		FGuid Baseline;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinMatchSettings::StaticStruct(), &Settings, Baseline)));

		FGuid InstancedDigest;
		const FInstancedStruct InstancedSettings = FInstancedStruct::Make(Settings);
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			FSeinDeterministicValueDigest::Compute(InstancedSettings, InstancedDigest)));
		ASSERT_THAT(IsTrue(InstancedDigest.IsValid()));

		FSeinMatchSettings Mutated = Settings;
		Mutated.Extensions[0].GetMutable<FSeinBasicMatchSettings>().TimeLimit = 46;
		FGuid MutatedDigest;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinMatchSettings::StaticStruct(), &Mutated, MutatedDigest)));
		ASSERT_THAT(IsFalse(Baseline == MutatedDigest));

		FSeinMatchSlot ExtraSlot;
		ExtraSlot.SlotIndex = 1;
		Mutated = Settings;
		Mutated.Slots.Insert(ExtraSlot, 0);
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinMatchSettings::StaticStruct(), &Mutated, MutatedDigest)));
		Mutated.Slots.Swap(0, 1);
		FGuid ReorderedDigest;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::Success,
			Digest(FSeinMatchSettings::StaticStruct(), &Mutated, ReorderedDigest)));
		ASSERT_THAT(IsFalse(MutatedDigest == ReorderedDigest));
	}

	TEST(DeterministicValueDigestRejectsUnsafeAndInvalidValues,
		"SeinARTS.Unit.DeterministicValueDigest")
	{
		FGuid DigestValue;
		FSeinDeterministicValueDigestError Error;

		FSeinCommandSchemaUnmarkedTestPayload Unmarked;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::NonDeterministicStruct,
			Digest(Unmarked.StaticStruct(), &Unmarked, DigestValue, &Error)));
		ASSERT_THAT(IsFalse(DigestValue.IsValid()));

		FSeinCommandSchemaUnsupportedTestPayload FloatingPoint;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::UnsupportedProperty,
			Digest(FloatingPoint.StaticStruct(), &FloatingPoint, DigestValue, &Error)));

		FSeinModifier ObjectReference;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::UnsupportedProperty,
			Digest(FSeinModifier::StaticStruct(), &ObjectReference, DigestValue, &Error)));

		FSeinMatchSettings InvalidExtension;
		InvalidExtension.Extensions.AddDefaulted();
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::InvalidInstancedStruct,
			Digest(FSeinMatchSettings::StaticStruct(), &InvalidExtension, DigestValue, &Error)));
		const FSeinDeterministicValueDigestError FirstError = Error;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::InvalidInstancedStruct,
			Digest(FSeinMatchSettings::StaticStruct(), &InvalidExtension, DigestValue, &Error)));
		ASSERT_THAT(AreEqual(FirstError.FieldPath, Error.FieldPath));
		ASSERT_THAT(AreEqual(FirstError.Message, Error.Message));
	}

	TEST(DeterministicValueDigestEnforcesResourceLimits,
		"SeinARTS.Unit.DeterministicValueDigest")
	{
		const FSeinMatchSettings Settings = MakeMatchSettings();
		FGuid DigestValue;
		FSeinDeterministicValueDigestError Error;

		FSeinDeterministicValueDigestOptions Options;
		Options.MaxAggregateElements = 1;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::ElementLimitExceeded,
			Digest(FSeinMatchSettings::StaticStruct(), &Settings, DigestValue, &Error, Options)));

		Options = {};
		Options.MaxEncodedBytes = 32;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::ByteLimitExceeded,
			Digest(FSeinMatchSettings::StaticStruct(), &Settings, DigestValue, &Error, Options)));

		Options = {};
		Options.MaxRecursionDepth = 0;
		const FSeinPlayerID Player(1);
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::RecursionLimitExceeded,
			Digest(FSeinPlayerID::StaticStruct(), &Player, DigestValue, &Error, Options)));

		Options = {};
		Options.MaxEncodedBytes = static_cast<uint64>(MAX_int32) + 1;
		ASSERT_THAT(AreEqual(
			ESeinDeterministicValueDigestResult::InvalidOptions,
			Digest(FSeinPlayerID::StaticStruct(), &Player, DigestValue, &Error, Options)));
	}
}
