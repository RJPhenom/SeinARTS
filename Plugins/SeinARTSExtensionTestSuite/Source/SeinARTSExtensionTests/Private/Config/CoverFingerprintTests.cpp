#include "CQTest.h"
#include "Settings/PluginSettings.h"
#include "Settings/SeinConfigFingerprintRegistry.h"
#include "Settings/SeinARTSCoverSettings.h"
#include "Tags/SeinCoverGameplayTags.h"

namespace UE::SeinARTSTests
{
	struct FScopedCoverSettingsRestore
	{
		explicit FScopedCoverSettingsRestore(USeinARTSCoverSettings& InSettings)
			: Settings(InSettings)
			, CoverSystemClass(InSettings.CoverSystemClass)
			, CoverSnapRadius(InSettings.CoverSnapRadius)
			, TerrainCoverQuality(InSettings.TerrainCoverQuality)
		{
		}

		~FScopedCoverSettingsRestore()
		{
			Settings.CoverSystemClass = CoverSystemClass;
			Settings.CoverSnapRadius = CoverSnapRadius;
			Settings.TerrainCoverQuality = TerrainCoverQuality;
		}

		USeinARTSCoverSettings& Settings;
		FSoftClassPath CoverSystemClass;
		FFixedPoint CoverSnapRadius;
		TMap<FGameplayTag, FGameplayTag> TerrainCoverQuality;
	};

	FString CaptureContributors()
	{
		FString Result;
		FSeinConfigFingerprintRegistry::AppendContributors(Result);
		return Result;
	}

	TEST(CoverFingerprintCanonicalMap, "SeinARTS.Integration.Config")
	{
		USeinARTSCoverSettings* Settings = GetMutableDefault<USeinARTSCoverSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		FScopedCoverSettingsRestore Restore(*Settings);

		const FString Initial = CaptureContributors();
		ASSERT_THAT(IsTrue(Initial.Contains(TEXT("CoverExtension|CoverSystemClass="))));
		ASSERT_THAT(IsTrue(Initial.Contains(TEXT("CoverExtension|CoverSnapRadius="))));
		ASSERT_THAT(IsTrue(Initial.Contains(TEXT("CoverExtension|TerrainCoverQuality="))));

		Settings->CoverSnapRadius += FFixedPoint::One;
		ASSERT_THAT(IsFalse(CaptureContributors() == Initial));
		Settings->CoverSnapRadius = Restore.CoverSnapRadius;

		Settings->TerrainCoverQuality.Reset();
		Settings->TerrainCoverQuality.Add(SeinCoverTags::Cover_Heavy, SeinCoverTags::Cover_Light);
		Settings->TerrainCoverQuality.Add(SeinCoverTags::Cover_Light, SeinCoverTags::Cover_Negative);
		const FString ForwardInsertion = CaptureContributors();

		Settings->TerrainCoverQuality.Reset();
		Settings->TerrainCoverQuality.Add(SeinCoverTags::Cover_Light, SeinCoverTags::Cover_Negative);
		Settings->TerrainCoverQuality.Add(SeinCoverTags::Cover_Heavy, SeinCoverTags::Cover_Light);
		ASSERT_THAT(AreEqual(ForwardInsertion, CaptureContributors()));
	}
}
