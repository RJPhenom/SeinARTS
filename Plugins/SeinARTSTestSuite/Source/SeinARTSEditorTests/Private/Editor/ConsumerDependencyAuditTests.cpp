#include "CQTest.h"
#include "Editor/SeinConsumerDependencyAuditTestTypes.h"
#include "Commandlets/Commandlet.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		struct FAuditFixture
		{
			FString Root = TEXT("/SeinDependencyAudit_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/");
			FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / Root.Mid(1));
			UPackage* Package = nullptr;
			FAuditFixture()
			{
				IFileManager::Get().MakeDirectory(*Directory, true);
				FPackageName::RegisterMountPoint(Root, Directory);
				Package = CreatePackage(*(Root + TEXT("Fixture")));
			}
			~FAuditFixture()
			{
				FText Error;
				UPackageTools::UnloadPackages({Package}, Error, true);
				FPackageName::UnRegisterMountPoint(Root, Directory);
				IFileManager::Get().DeleteDirectory(*Directory, false, true);
			}
		};
	}

	TEST(ConsumerAuditRejectsLiveStringAndSoftPathsWithoutSavingSource, "SeinARTS.Editor.ConsumerDependencies")
	{
		UClass* AuditClass = FindFirstObject<UClass>(TEXT("SeinConsumerDependencyAuditCommandlet"));
		ASSERT_THAT(IsNotNull(AuditClass));
		TStrongObjectPtr<UCommandlet> Audit(NewObject<UCommandlet>(GetTransientPackage(), AuditClass));
		for (int32 Variant = 0; Variant < 5; ++Variant)
		{
			FAuditFixture Fixture;
			auto* Asset = NewObject<USeinConsumerDependencyAuditTestAsset>(Fixture.Package, TEXT("Fixture"), RF_Public | RF_Standalone);
			if (Variant == 1) Asset->LivePath = TEXT("/Game/SeinARTSExamples/LiveString");
			if (Variant == 2) Asset->LivePath = TEXT("\u2603 /Game/SeinARTSExamples/UnicodeString");
			if (Variant == 3) Asset->SoftPath = FSoftObjectPath(TEXT("/Game/SeinARTSExamples/SoftAsset.SoftAsset"));
			if (Variant == 4) Asset->LivePath = TEXT("/Game/SeinARTSExamples/HiddenByPreSave");
			const FString File = Fixture.Directory / TEXT("Fixture.uasset");
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			ASSERT_THAT(IsTrue(UPackage::SavePackage(Fixture.Package, Asset, *File, Args)));
			Asset->bClearOnSave = Variant == 4;
			const FString Input = Fixture.Directory / TEXT("candidates.txt");
			ASSERT_THAT(IsTrue(FFileHelper::SaveStringToFile(File, *Input)));
			TArray<uint8> Before;
			ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(Before, *File)));
			if (Variant != 0)
				TestRunner->AddExpectedError(Variant == 3 ? TEXT("depends on host package") : TEXT("retains a host path outside level origin and bridge history"), EAutomationExpectedErrorFlags::Contains, 1);
			ASSERT_THAT(AreEqual(Variant == 0 ? 0 : 1, Audit->Main(FString::Printf(TEXT("-Input=\"%s\""), *Input))));
			TArray<uint8> After;
			ASSERT_THAT(IsTrue(FFileHelper::LoadFileToArray(After, *File)));
			ASSERT_THAT(IsTrue(Before == After));
		}
	}
}
