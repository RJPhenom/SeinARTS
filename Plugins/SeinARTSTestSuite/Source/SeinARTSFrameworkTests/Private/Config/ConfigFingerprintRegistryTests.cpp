#include "CQTest.h"
#include "Settings/SeinConfigFingerprintRegistry.h"
#include "TestTypes/SeinConfigFingerprintTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinConfigFingerprintNestedTestValue MakeNestedValue(int32 Marker, bool bReverseInsertion)
		{
			FSeinConfigFingerprintNestedTestValue Result;
			Result.Marker = Marker;
			if (bReverseInsertion)
			{
				Result.ValuesByName.Add(TEXT("Gamma"), 3);
				Result.ValuesByName.Add(TEXT("Beta"), 2);
				Result.ValuesByName.Add(TEXT("Alpha"), 1);
				Result.Labels.Add(TEXT("Third"));
				Result.Labels.Add(TEXT("Second"));
				Result.Labels.Add(TEXT("First"));
			}
			else
			{
				Result.ValuesByName.Add(TEXT("Alpha"), 1);
				Result.ValuesByName.Add(TEXT("Beta"), 2);
				Result.ValuesByName.Add(TEXT("Gamma"), 3);
				Result.Labels.Add(TEXT("First"));
				Result.Labels.Add(TEXT("Second"));
				Result.Labels.Add(TEXT("Third"));
			}
			Result.OrderedValues = { 5, 8, 13 };
			return Result;
		}

		void PopulateSettings(USeinConfigFingerprintTestSettings& Settings, bool bReverseInsertion)
		{
			Settings.ValuesByGroup.Reset();
			if (bReverseInsertion)
			{
				Settings.ValuesByGroup.Add(TEXT("Bravo"), MakeNestedValue(2, true));
				Settings.ValuesByGroup.Add(TEXT("Alpha"), MakeNestedValue(1, true));
			}
			else
			{
				Settings.ValuesByGroup.Add(TEXT("Alpha"), MakeNestedValue(1, false));
				Settings.ValuesByGroup.Add(TEXT("Bravo"), MakeNestedValue(2, false));
			}

			Settings.OrderedGroups = {
				MakeNestedValue(1, bReverseInsertion),
				MakeNestedValue(2, bReverseInsertion),
			};
			Settings.OptionalGroup = MakeNestedValue(3, bReverseInsertion);
			Settings.ScalarValue = 21;
		}

		FString CaptureFingerprintContributors()
		{
			FString Result;
			FSeinConfigFingerprintRegistry::AppendContributors(Result);
			return Result;
		}

		struct FScopedTestSettingsRestore
		{
			explicit FScopedTestSettingsRestore(USeinConfigFingerprintTestSettings& InSettings)
				: Settings(InSettings)
				, ValuesByGroup(InSettings.ValuesByGroup)
				, OrderedGroups(InSettings.OrderedGroups)
				, OptionalGroup(InSettings.OptionalGroup)
				, ScalarValue(InSettings.ScalarValue)
			{
			}

			~FScopedTestSettingsRestore()
			{
				Settings.ValuesByGroup = MoveTemp(ValuesByGroup);
				Settings.OrderedGroups = MoveTemp(OrderedGroups);
				Settings.OptionalGroup = MoveTemp(OptionalGroup);
				Settings.ScalarValue = ScalarValue;
			}

			USeinConfigFingerprintTestSettings& Settings;
			TMap<FString, FSeinConfigFingerprintNestedTestValue> ValuesByGroup;
			TArray<FSeinConfigFingerprintNestedTestValue> OrderedGroups;
			TOptional<FSeinConfigFingerprintNestedTestValue> OptionalGroup;
			int32 ScalarValue;
		};
	}

	TEST(ConfigFingerprintRecursivelyCanonicalizesContainers, "SeinARTS.Unit.ConfigFingerprint")
	{
		const FName StableId(TEXT("SeinFrameworkTest.RecursiveCanonicalization"));
		USeinConfigFingerprintTestSettings* Settings =
			GetMutableDefault<USeinConfigFingerprintTestSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		FScopedTestSettingsRestore Restore(*Settings);

		FSeinConfigFingerprintRegistrationHandle Registration =
			FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId,
			Settings,
			{
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ValuesByGroup),
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, OrderedGroups),
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, OptionalGroup),
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ScalarValue),
			});
		ASSERT_THAT(IsTrue(Registration.IsValid()));

		PopulateSettings(*Settings, false);
		const FString ForwardInsertion = CaptureFingerprintContributors();
		ASSERT_THAT(IsTrue(ForwardInsertion.Contains(StableId.ToString() + TEXT("|ValuesByGroup="))));

		PopulateSettings(*Settings, true);
		ASSERT_THAT(AreEqual(ForwardInsertion, CaptureFingerprintContributors()));

		Swap(Settings->OrderedGroups[0], Settings->OrderedGroups[1]);
		ASSERT_THAT(IsFalse(ForwardInsertion == CaptureFingerprintContributors()));

		PopulateSettings(*Settings, false);
		Settings->OptionalGroup.Reset();
		ASSERT_THAT(IsFalse(ForwardInsertion == CaptureFingerprintContributors()));

		PopulateSettings(*Settings, false);
		Settings->OptionalGroup->Marker = 4;
		ASSERT_THAT(IsFalse(ForwardInsertion == CaptureFingerprintContributors()));
	}

	TEST(ConfigFingerprintRejectsInvalidRegistrations, "SeinARTS.Unit.ConfigFingerprint")
	{
		USeinConfigFingerprintTestSettings* Settings =
			GetMutableDefault<USeinConfigFingerprintTestSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const FName StableId(TEXT("SeinFrameworkTest.RegistrationValidation"));
		const FName ScalarField =
			GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ScalarValue);

		TestRunner->AddExpectedError(
			TEXT("expected a non-empty ID, CDO, and field list"),
			EAutomationExpectedErrorFlags::Contains, 4, false);
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			NAME_None, Settings, { ScalarField }).IsValid()));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.NullCDO"), nullptr, { ScalarField }).IsValid()));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.NonCDO"), NewObject<USeinConfigFingerprintTestSettings>(),
			{ ScalarField }).IsValid()));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.EmptyFields"), Settings, {}).IsValid()));

		TestRunner->AddExpectedError(
			TEXT("is missing or duplicated"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.MissingField"), Settings, { TEXT("NotAProperty") }).IsValid()));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.DuplicateField"), Settings, { ScalarField, ScalarField }).IsValid()));

		FSeinConfigFingerprintRegistrationHandle FirstGeneration =
			FSeinConfigFingerprintRegistry::RegisterContributor(
				StableId, Settings, { ScalarField });
		FSeinConfigFingerprintRegistrationHandle SecondGeneration =
			FSeinConfigFingerprintRegistry::RegisterContributor(
				StableId, Settings, { ScalarField });
		ASSERT_THAT(IsTrue(FirstGeneration.IsValid()));
		ASSERT_THAT(IsTrue(SecondGeneration.IsValid()));

		TestRunner->AddExpectedError(
			TEXT("Rejected conflicting config-fingerprint contributor ID"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId,
			Settings,
			{
				ScalarField,
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ValuesByGroup),
			}).IsValid()));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId,
			GetDefault<USeinConfigFingerprintAlternateTestSettings>(),
			{ GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintAlternateTestSettings, ScalarValue) }).IsValid()));

		ASSERT_THAT(IsTrue(CaptureFingerprintContributors().Contains(
			StableId.ToString() + TEXT("|ScalarValue="))));

		FirstGeneration.Reset();
		ASSERT_THAT(IsTrue(CaptureFingerprintContributors().Contains(
			StableId.ToString() + TEXT("|ScalarValue="))));

		SecondGeneration.Reset();
		ASSERT_THAT(IsFalse(CaptureFingerprintContributors().Contains(
			StableId.ToString() + TEXT("|ScalarValue="))));
	}
}

#include "Settings/PluginSettings.h"

namespace UE::SeinARTSTests
{
	TEST(ConfigFingerprintIgnoresPresentationText, "SeinARTS.Unit.ConfigFingerprint")
	{
		// Regression for the separate-process PIE config-parity kick: an editor
		// process exports config FText with the package-localization namespace
		// ("[/Script/Module]") while a -game process exports an empty one. The
		// fingerprint must be blind to FText entirely — namespace, key, AND
		// source string — because display names are presentation, not sim.
		USeinARTSCoreSettings* Settings = GetMutableDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const TArray<FSeinResourceDefinition> SavedCatalog = Settings->ResourceCatalog;

		FSeinResourceDefinition Definition;
		Definition.ResourceTag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("SeinARTS.Resource")), /*ErrorIfNotFound=*/false);
		Definition.DisplayName = FText::AsCultureInvariant(TEXT("Money"));
		Settings->ResourceCatalog = {Definition};
		const FString GameLike = Settings->BuildConfigFingerprintSource();
		const int32 GameLikeFingerprint = Settings->ComputeConfigFingerprint();

		// Same entry, text re-keyed into an editor-style namespace + different
		// display string — must not move the fingerprint by a single bit.
		Settings->ResourceCatalog[0].DisplayName = FText::ChangeKey(
			TEXT("[/Script/SeinARTSCoreEntity]"),
			TEXT("8DB6BEF544D7B093406E2C81E9FCC1DC"),
			FText::AsCultureInvariant(TEXT("Credits")));
		const FString EditorLike = Settings->BuildConfigFingerprintSource();
		const int32 EditorLikeFingerprint = Settings->ComputeConfigFingerprint();

		Settings->ResourceCatalog = SavedCatalog;

		ASSERT_THAT(AreEqual(GameLike, EditorLike));
		ASSERT_THAT(AreEqual(GameLikeFingerprint, EditorLikeFingerprint));
		ASSERT_THAT(IsFalse(GameLike.Contains(TEXT("NSLOCTEXT"))));
		ASSERT_THAT(IsFalse(GameLike.Contains(TEXT("INVTEXT"))));
		ASSERT_THAT(IsTrue(GameLike.Contains(TEXT("Text[omitted]"))));
	}
}
