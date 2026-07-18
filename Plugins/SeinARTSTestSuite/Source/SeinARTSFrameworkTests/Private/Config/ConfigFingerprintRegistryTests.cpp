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

		struct FScopedContributorRegistration
		{
			explicit FScopedContributorRegistration(FName InStableId)
				: StableId(InStableId)
			{
			}

			~FScopedContributorRegistration()
			{
				FSeinConfigFingerprintRegistry::UnregisterContributor(StableId);
			}

			FName StableId;
		};

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
		FScopedContributorRegistration Registration(StableId);
		USeinConfigFingerprintTestSettings* Settings =
			GetMutableDefault<USeinConfigFingerprintTestSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		FScopedTestSettingsRestore Restore(*Settings);

		ASSERT_THAT(IsTrue(FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId,
			Settings,
			{
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ValuesByGroup),
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, OrderedGroups),
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, OptionalGroup),
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ScalarValue),
			})));

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
		FScopedContributorRegistration Registration(StableId);
		const FName ScalarField =
			GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ScalarValue);

		TestRunner->AddExpectedError(
			TEXT("expected a non-empty ID, CDO, and field list"),
			EAutomationExpectedErrorFlags::Contains, 4, false);
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			NAME_None, Settings, { ScalarField })));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.NullCDO"), nullptr, { ScalarField })));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.NonCDO"), NewObject<USeinConfigFingerprintTestSettings>(),
			{ ScalarField })));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.EmptyFields"), Settings, {})));

		TestRunner->AddExpectedError(
			TEXT("is missing or duplicated"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.MissingField"), Settings, { TEXT("NotAProperty") })));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			TEXT("SeinFrameworkTest.DuplicateField"), Settings, { ScalarField, ScalarField })));

		ASSERT_THAT(IsTrue(FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId, Settings, { ScalarField })));
		ASSERT_THAT(IsTrue(FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId, Settings, { ScalarField })));

		TestRunner->AddExpectedError(
			TEXT("Rejected conflicting config-fingerprint contributor ID"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId,
			Settings,
			{
				ScalarField,
				GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintTestSettings, ValuesByGroup),
			})));
		ASSERT_THAT(IsFalse(FSeinConfigFingerprintRegistry::RegisterContributor(
			StableId,
			GetDefault<USeinConfigFingerprintAlternateTestSettings>(),
			{ GET_MEMBER_NAME_CHECKED(USeinConfigFingerprintAlternateTestSettings, ScalarValue) })));

		ASSERT_THAT(IsTrue(CaptureFingerprintContributors().Contains(
			StableId.ToString() + TEXT("|ScalarValue="))));
	}
}
