#include "CQTest.h"

#include "Abilities/SeinAbilityBlueprint.h"
#include "Actor/SeinEntityComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Balance/SeinBalanceProfile.h"
#include "Data/SeinResourceTypes.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Settings/PluginSettings.h"
#include "StructUtils/UserDefinedStruct.h"
#include "TestTypes/SeinBalanceDataEditorTestTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Util/SeinBalanceTableExport.h"

namespace UE::SeinARTSTests::BalanceData
{
	USeinBalanceProfile* MakeProfile(const TCHAR* Prefix)
	{
		const FString UniqueName = FString::Printf(
			TEXT("%s_%s"),
			Prefix,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FString Directory = FString::Printf(
			TEXT("/Game/__SeinAutomation/BalanceData/%s"),
			*UniqueName);
		UPackage* Package = CreatePackage(*(Directory / UniqueName));
		if (!Package)
		{
			return nullptr;
		}

		USeinBalanceProfile* Profile = NewObject<USeinBalanceProfile>(
			Package,
			FName(*UniqueName),
			RF_Transient);
		if (Profile)
		{
			Profile->OutputDir.Path = Directory;
		}
		return Profile;
	}

	FProperty* FindAuthoredField(
		const UUserDefinedStruct& RowStruct,
		const FString& AuthoredName)
	{
		for (TFieldIterator<FProperty> It(
			&RowStruct,
			EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (RowStruct.GetAuthoredNameForField(*It) == AuthoredName)
			{
				return *It;
			}
		}
		return nullptr;
	}

	FGuid FindAuthoredFieldGuid(
		const UUserDefinedStruct& RowStruct,
		const FString& AuthoredName)
	{
		for (const FStructVariableDescription& Variable :
			FStructureEditorUtils::GetVarDesc(&RowStruct))
		{
			if (Variable.FriendlyName == AuthoredName)
			{
				return Variable.VarGuid;
			}
		}
		return {};
	}

	int32 CountFields(const UUserDefinedStruct& RowStruct)
	{
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(
			&RowStruct,
			EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	FFloatProperty* FindFloatField(
		const UUserDefinedStruct& RowStruct,
		const FString& AuthoredName)
	{
		return CastField<FFloatProperty>(
			FindAuthoredField(RowStruct, AuthoredName));
	}

	FSeinBalanceEditorTestComponent* FindMutableTestComponent(UClass& Class)
	{
		ASeinActor* CDO = Cast<ASeinActor>(Class.GetDefaultObject());
		USeinEntityComponent* Bridge = CDO ? CDO->GetEntityBridge() : nullptr;
		if (!Bridge)
		{
			return nullptr;
		}
		for (FInstancedStruct& Entry : Bridge->ComponentData)
		{
			if (Entry.GetScriptStruct() ==
				FSeinBalanceEditorTestComponent::StaticStruct())
			{
				return Entry.GetMutablePtr<FSeinBalanceEditorTestComponent>();
			}
		}
		return nullptr;
	}

	UBlueprint* CreateEntityBlueprint(
		const FString& PackageName,
		FFixedPoint Speed)
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		const FName AssetName(*FPackageName::GetLongPackageAssetName(
			PackageName));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ASeinBalanceEditorDuplicateRoot::StaticClass(),
			Package,
			AssetName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			return nullptr;
		}

		ASeinActor* CDO = Cast<ASeinActor>(
			Blueprint->GeneratedClass->GetDefaultObject());
		USeinEntityComponent* Bridge = CDO ? CDO->GetEntityBridge() : nullptr;
		if (!Bridge)
		{
			return nullptr;
		}

		FSeinBalanceEditorTestNestedData Nested;
		Nested.TurnRate = Speed;
		Nested.GearCount = 4;
		FSeinBalanceEditorTestComponent Component;
		Component.Speed = Speed;
		Component.Armor = 10;
		Component.ModeData = FInstancedStruct::Make(Nested);
		Bridge->ComponentData.Add(FInstancedStruct::Make(Component));
		Blueprint->Status = BS_UpToDate;
		return Blueprint;
	}

	struct FScopedContentMount
	{
		FString RootPath;
		FString DiskDirectory;
		TArray<FString> PackageNames;

		FScopedContentMount()
		{
			const FString Suffix =
				FGuid::NewGuid().ToString(EGuidFormats::Digits);
			RootPath = FString::Printf(
				TEXT("/SeinBalanceAutomation_%s/"),
				*Suffix);
			DiskDirectory = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectSavedDir()
				/ TEXT("Automation/BalanceDataAssets") / Suffix);
			IFileManager::Get().MakeDirectory(*DiskDirectory, true);
			FPackageName::RegisterMountPoint(
				RootPath,
				DiskDirectory / TEXT(""));
		}

		~FScopedContentMount()
		{
			TArray<UPackage*> LoadedPackages;
			for (const FString& PackageName : PackageNames)
			{
				if (UPackage* Package = FindPackage(nullptr, *PackageName))
				{
					LoadedPackages.Add(Package);
				}
			}
			if (LoadedPackages.Num() > 0)
			{
				FText UnloadError;
				UPackageTools::UnloadPackages(
					LoadedPackages,
					UnloadError,
					true);
			}
			FPackageName::UnRegisterMountPoint(RootPath, DiskDirectory);
			IFileManager::Get().DeleteDirectory(
				*DiskDirectory,
				false,
				true);
		}

		FString MakePackageName(const FString& AssetName)
		{
			const FString PackageName = RootPath + AssetName;
			PackageNames.Add(PackageName);
			return PackageName;
		}
	};

	bool SaveAssetPackage(UPackage& Package, UObject& Asset)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package.GetName(),
			FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_None;
		SaveArgs.Error = GError;
		return UPackage::SavePackage(
			&Package,
			&Asset,
			*Filename,
			SaveArgs);
	}

	FEdGraphPinType FixedPointPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = FFixedPoint::StaticStruct();
		return Type;
	}

	struct FScopedEntityDefaults
	{
		FSeinBalanceEditorTestComponent* A = nullptr;
		FSeinBalanceEditorTestComponent* B = nullptr;
		FSeinBalanceEditorTestComponent OriginalA;
		FSeinBalanceEditorTestComponent OriginalB;

		FScopedEntityDefaults()
		{
			A = FindMutableTestComponent(
				*ASeinBalanceEditorTestEntityA::StaticClass());
			B = FindMutableTestComponent(
				*ASeinBalanceEditorTestEntityB::StaticClass());
			if (A)
			{
				OriginalA = *A;
			}
			if (B)
			{
				OriginalB = *B;
			}
		}

		~FScopedEntityDefaults()
		{
			if (A)
			{
				*A = OriginalA;
			}
			if (B)
			{
				*B = OriginalB;
			}
		}
	};

	struct FScopedAbilityDefaults
	{
		USeinBalanceEditorTestAbilityA* A = nullptr;
		USeinBalanceEditorTestAbilityB* B = nullptr;
		FFixedPoint CooldownA;
		FFixedPoint CooldownB;
		FSeinResourceCost CostA;
		FSeinResourceCost CostB;

		FScopedAbilityDefaults()
		{
			A = GetMutableDefault<USeinBalanceEditorTestAbilityA>();
			B = GetMutableDefault<USeinBalanceEditorTestAbilityB>();
			CooldownA = A->Cooldown;
			CooldownB = B->Cooldown;
			CostA = A->ResourceCost;
			CostB = B->ResourceCost;
		}

		~FScopedAbilityDefaults()
		{
			A->Cooldown = CooldownA;
			B->Cooldown = CooldownB;
			A->ResourceCost = CostA;
			B->ResourceCost = CostB;
		}
	};

	struct FScopedSiblingAbilityDefaults
	{
		USeinBalanceEditorSiblingAbilityA* A = nullptr;
		USeinBalanceEditorSiblingAbilityB* B = nullptr;
		FFixedPoint OriginalA;
		FFixedPoint OriginalB;

		FScopedSiblingAbilityDefaults()
		{
			A = GetMutableDefault<USeinBalanceEditorSiblingAbilityA>();
			B = GetMutableDefault<USeinBalanceEditorSiblingAbilityB>();
			OriginalA = A->SiblingTuning;
			OriginalB = B->SiblingTuning;
		}

		~FScopedSiblingAbilityDefaults()
		{
			A->SiblingTuning = OriginalA;
			B->SiblingTuning = OriginalB;
		}
	};

	TEST(EntityGatherPushAndRegatherRoundTrip, "SeinARTS.Editor.BalanceData")
	{
		FScopedEntityDefaults Defaults;
		ASSERT_THAT(IsNotNull(Defaults.A));
		ASSERT_THAT(IsNotNull(Defaults.B));

		USeinBalanceProfile* Profile = MakeProfile(TEXT("EntityBalance"));
		ASSERT_THAT(IsNotNull(Profile));
		Profile->IncludedRoots.Add(
			ASeinBalanceEditorTestEntityRoot::StaticClass());
		Profile->TrackedComponents.Add(
			FSeinBalanceEditorTestComponent::StaticStruct());

		TArray<UClass*> Targets;
		Profile->ResolveTargetClasses(Targets);
		ASSERT_THAT(IsTrue(Targets.Num() == 2));
		ASSERT_THAT(IsTrue(Targets.Contains(
			ASeinBalanceEditorTestEntityA::StaticClass())));
		ASSERT_THAT(IsTrue(Targets.Contains(
			ASeinBalanceEditorTestEntityB::StaticClass())));

		UDataTable* Table =
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile);
		ASSERT_THAT(IsNotNull(Table));
		ASSERT_THAT(IsTrue(Table->GetRowMap().Num() == 2));
		UUserDefinedStruct* RowStruct =
			Cast<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNotNull(RowStruct));
		ASSERT_THAT(IsNull(FindAuthoredField(
			*RowStruct,
			TEXT("BalanceEditorTest_RuntimeOnly"))));

		FFloatProperty* SpeedField = FindFloatField(
			*RowStruct,
			TEXT("BalanceEditorTest_Speed"));
		FFloatProperty* TurnRateField = FindFloatField(
			*RowStruct,
			TEXT("BalanceEditorTestNestedData_TurnRate"));
		ASSERT_THAT(IsNotNull(SpeedField));
		ASSERT_THAT(IsNotNull(TurnRateField));

		uint8* RowA = Table->FindRowUnchecked(
			TEXT("SeinBalanceEditorTestEntityA"));
		ASSERT_THAT(IsNotNull(RowA));
		ASSERT_THAT(IsTrue(
			SpeedField->GetPropertyValue_InContainer(RowA) == 3.0f));
		ASSERT_THAT(IsTrue(
			TurnRateField->GetPropertyValue_InContainer(RowA) == 2.0f));

		const int32 FieldCount = CountFields(*RowStruct);
		int32 CellsChecked = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));

		SpeedField->SetPropertyValue_InContainer(RowA, 7.25f);
		Table->HandleDataTableChanged();
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 1));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));

		int32 SkippedCells = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::PushToEntitiesWithoutUI(
				Profile,
				SkippedCells) == 1));
		ASSERT_THAT(IsTrue(SkippedCells == 0));
		ASSERT_THAT(IsTrue(
			Defaults.A->Speed == FFixedPoint::FromFloat(7.25f)));
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));

		Defaults.A->Speed = Defaults.OriginalA.Speed;
		UDataTable* Regathered =
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile);
		ASSERT_THAT(IsTrue(Regathered == Table));
		RowA = Table->FindRowUnchecked(
			TEXT("SeinBalanceEditorTestEntityA"));
		ASSERT_THAT(IsNotNull(RowA));
		SpeedField = FindFloatField(
			*CastChecked<UUserDefinedStruct>(Table->RowStruct),
			TEXT("BalanceEditorTest_Speed"));
		ASSERT_THAT(IsNotNull(SpeedField));
		ASSERT_THAT(IsTrue(
			SpeedField->GetPropertyValue_InContainer(RowA) == 3.0f));
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));

		Profile->TrackedComponents.Reset();
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile) ==
			Table));
		RowStruct = CastChecked<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNotNull(FindAuthoredField(
			*RowStruct,
			TEXT("Identity_DisplayName"))));
		ASSERT_THAT(IsNotNull(FindAuthoredField(
			*RowStruct,
			TEXT("Identity_IdentityTag"))));
		ASSERT_THAT(IsTrue(CountFields(*RowStruct) == FieldCount + 2));
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));

		Profile->TrackedComponents.Add(
			FSeinBalanceEditorTestComponent::StaticStruct());
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile) ==
			Table));
		RowStruct = CastChecked<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNull(FindAuthoredField(
			*RowStruct,
			TEXT("Identity_DisplayName"))));
		ASSERT_THAT(IsTrue(CountFields(*RowStruct) == FieldCount));

		Profile->ExcludedClasses.Add(
			ASeinBalanceEditorTestEntityB::StaticClass());
		CellsChecked = INDEX_NONE;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 1));
		ASSERT_THAT(IsTrue(CellsChecked == 0));
		SkippedCells = INDEX_NONE;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::PushToEntitiesWithoutUI(
				Profile,
				SkippedCells) == -1));
		ASSERT_THAT(IsTrue(SkippedCells == 0));
		Profile->ExcludedClasses.Reset();
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));

		Table->EmptyTable();
		const FGuid StaleSpeedGuid = FindAuthoredFieldGuid(
			*RowStruct,
			TEXT("BalanceEditorTest_Speed"));
		ASSERT_THAT(IsTrue(StaleSpeedGuid.IsValid()));
		FEdGraphPinType WrongSpeedType;
		WrongSpeedType.PinCategory = UEdGraphSchema_K2::PC_Int;
		ASSERT_THAT(IsTrue(FStructureEditorUtils::ChangeVariableType(
			RowStruct,
			StaleSpeedGuid,
			WrongSpeedType)));
		FStructureEditorUtils::OnStructureChanged(
			RowStruct,
			FStructureEditorUtils::Unknown);

		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile) ==
			Table));
		RowStruct = CastChecked<UUserDefinedStruct>(Table->RowStruct);
		SpeedField = FindFloatField(
			*RowStruct,
			TEXT("BalanceEditorTest_Speed"));
		ASSERT_THAT(IsNotNull(SpeedField));
		RowA = Table->FindRowUnchecked(
			TEXT("SeinBalanceEditorTestEntityA"));
		ASSERT_THAT(IsNotNull(RowA));
		ASSERT_THAT(IsTrue(
			SpeedField->GetPropertyValue_InContainer(RowA) == 3.0f));
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));
	}

	TEST(AbilityGatherPushAndExactSyncAccounting, "SeinARTS.Editor.BalanceData")
	{
		FScopedAbilityDefaults Defaults;
		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));

		FGameplayTag ResourceTag;
		for (const FSeinResourceDefinition& Definition :
			Settings->ResourceCatalog)
		{
			if (Definition.ResourceTag.IsValid())
			{
				ResourceTag = Definition.ResourceTag;
				break;
			}
		}
		ASSERT_THAT(IsTrue(ResourceTag.IsValid()));
		Defaults.A->ResourceCost.Amounts.FindOrAdd(ResourceTag) =
			FFixedPoint::FromInt(10);
		Defaults.B->ResourceCost.Amounts.FindOrAdd(ResourceTag) =
			FFixedPoint::FromInt(20);

		USeinBalanceProfile* Profile = MakeProfile(TEXT("AbilityBalance"));
		ASSERT_THAT(IsNotNull(Profile));
		Profile->TargetKind = ESeinBalanceTargetKind::Abilities;
		Profile->AbilityRoots.Add(
			USeinBalanceEditorTestAbilityRoot::StaticClass());

		TArray<UClass*> Targets;
		Profile->ResolveTargetClasses(Targets);
		ASSERT_THAT(IsTrue(Targets.Num() == 2));

		UDataTable* Table =
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile);
		ASSERT_THAT(IsNotNull(Table));
		ASSERT_THAT(IsTrue(Table->GetRowMap().Num() == 2));
		UUserDefinedStruct* RowStruct =
			Cast<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNotNull(RowStruct));

		FFloatProperty* CooldownField = FindFloatField(
			*RowStruct,
			TEXT("Cooldown"));
		ASSERT_THAT(IsNotNull(CooldownField));
		FString ResourceLeaf;
		if (!ResourceTag.ToString().Split(
			TEXT("."),
			nullptr,
			&ResourceLeaf,
			ESearchCase::IgnoreCase,
			ESearchDir::FromEnd))
		{
			ResourceLeaf = ResourceTag.ToString();
		}
		FFloatProperty* CostField = FindFloatField(
			*RowStruct,
			FString(TEXT("Cost_")) + ResourceLeaf);
		ASSERT_THAT(IsNotNull(CostField));

		const int32 FieldCount = CountFields(*RowStruct);
		int32 CellsChecked = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));

		uint8* RowA = Table->FindRowUnchecked(
			TEXT("SeinBalanceEditorTestAbilityA"));
		ASSERT_THAT(IsNotNull(RowA));
		CooldownField->SetPropertyValue_InContainer(RowA, 9.5f);
		CostField->SetPropertyValue_InContainer(RowA, 25.0f);
		Table->HandleDataTableChanged();
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 2));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));

		int32 SkippedCells = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::PushToEntitiesWithoutUI(
				Profile,
				SkippedCells) == 2));
		ASSERT_THAT(IsTrue(SkippedCells == 0));
		ASSERT_THAT(IsTrue(
			Defaults.A->Cooldown == FFixedPoint::FromFloat(9.5f)));
		ASSERT_THAT(IsTrue(
			Defaults.A->ResourceCost.Amounts.FindRef(ResourceTag) ==
			FFixedPoint::FromInt(25)));
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
		ASSERT_THAT(IsTrue(CellsChecked == FieldCount * 2));
	}

	TEST(SiblingAbilityFieldsKeepDistinctOwnerIdentity, "SeinARTS.Editor.BalanceData")
	{
		FScopedSiblingAbilityDefaults Defaults;
		USeinBalanceProfile* Profile = MakeProfile(TEXT("SiblingAbilityBalance"));
		ASSERT_THAT(IsNotNull(Profile));
		Profile->TargetKind = ESeinBalanceTargetKind::Abilities;
		Profile->AbilityRoots.Add(
			USeinBalanceEditorSiblingAbilityRoot::StaticClass());

		UDataTable* Table =
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile);
		ASSERT_THAT(IsNotNull(Table));
		ASSERT_THAT(IsTrue(Table->GetRowMap().Num() == 2));
		UUserDefinedStruct* RowStruct =
			Cast<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNotNull(RowStruct));

		TArray<FFloatProperty*> SiblingFields;
		for (TFieldIterator<FProperty> It(
			RowStruct,
			EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (RowStruct->GetAuthoredNameForField(*It).StartsWith(
				TEXT("SiblingTuning__")))
			{
				if (FFloatProperty* Field = CastField<FFloatProperty>(*It))
				{
					SiblingFields.Add(Field);
				}
			}
		}
		ASSERT_THAT(IsTrue(SiblingFields.Num() == 2));

		uint8* RowA = Table->FindRowUnchecked(
			TEXT("SeinBalanceEditorSiblingAbilityA"));
		uint8* RowB = Table->FindRowUnchecked(
			TEXT("SeinBalanceEditorSiblingAbilityB"));
		ASSERT_THAT(IsNotNull(RowA));
		ASSERT_THAT(IsNotNull(RowB));
		bool bChangedA = false;
		bool bChangedB = false;
		for (FFloatProperty* Field : SiblingFields)
		{
			if (Field->GetPropertyValue_InContainer(RowA) == 101.0f)
			{
				Field->SetPropertyValue_InContainer(RowA, 111.0f);
				bChangedA = true;
			}
			if (Field->GetPropertyValue_InContainer(RowB) == 202.0f)
			{
				Field->SetPropertyValue_InContainer(RowB, 222.0f);
				bChangedB = true;
			}
		}
		ASSERT_THAT(IsTrue(bChangedA));
		ASSERT_THAT(IsTrue(bChangedB));
		Table->HandleDataTableChanged();

		int32 CellsChecked = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 2));
		ASSERT_THAT(IsTrue(
			CellsChecked == CountFields(*RowStruct) * 2 - 2));
		int32 SkippedCells = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::PushToEntitiesWithoutUI(
				Profile,
				SkippedCells) == 2));
		ASSERT_THAT(IsTrue(SkippedCells == 0));
		ASSERT_THAT(IsTrue(
			Defaults.A->SiblingTuning == FFixedPoint::FromInt(111)));
		ASSERT_THAT(IsTrue(
			Defaults.B->SiblingTuning == FFixedPoint::FromInt(222)));
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
	}

	TEST(DuplicateBlueprintNamesRemainDistinctAndWritable, "SeinARTS.Editor.BalanceData")
	{
		const FString UniqueRoot = FString::Printf(
			TEXT("/Temp/SeinARTSTests/BalanceData/Duplicate_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		UBlueprint* BlueprintA = CreateEntityBlueprint(
			UniqueRoot / TEXT("Foo/Bar/BP_Duplicate"),
			FFixedPoint::FromInt(11));
		UBlueprint* BlueprintB = CreateEntityBlueprint(
			UniqueRoot / TEXT("Foo_Bar/BP_Duplicate"),
			FFixedPoint::FromInt(22));
		ASSERT_THAT(IsNotNull(BlueprintA));
		ASSERT_THAT(IsNotNull(BlueprintB));
		ASSERT_THAT(IsTrue(
			BlueprintA->GeneratedClass != BlueprintB->GeneratedClass));

		USeinBalanceProfile* Profile = MakeProfile(TEXT("DuplicateBalance"));
		ASSERT_THAT(IsNotNull(Profile));
		Profile->IncludedRoots.Add(TSoftClassPtr<ASeinActor>(
			BlueprintA->GeneratedClass.Get()));
		Profile->TrackedComponents.Add(
			FSeinBalanceEditorTestComponent::StaticStruct());

		UDataTable* Table =
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile);
		ASSERT_THAT(IsNotNull(Table));
		ASSERT_THAT(IsTrue(Table->GetRowMap().Num() == 1));
		UUserDefinedStruct* RowStruct =
			Cast<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNotNull(RowStruct));
		FFloatProperty* SpeedField = FindFloatField(
			*RowStruct,
			TEXT("BalanceEditorTest_Speed"));
		ASSERT_THAT(IsNotNull(SpeedField));
		uint8* SingleRow = Table->FindRowUnchecked(TEXT("BP_Duplicate"));
		ASSERT_THAT(IsNotNull(SingleRow));
		SpeedField->SetPropertyValue_InContainer(SingleRow, 999.0f);
		Table->HandleDataTableChanged();

		Profile->IncludedRoots[0] = TSoftClassPtr<ASeinActor>(
			BlueprintB->GeneratedClass.Get());
		int32 CellsChecked = INDEX_NONE;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 1));
		ASSERT_THAT(IsTrue(CellsChecked == 0));
		int32 SkippedCells = INDEX_NONE;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::PushToEntitiesWithoutUI(
				Profile,
				SkippedCells) == -1));
		ASSERT_THAT(IsTrue(SkippedCells == 0));
		FSeinBalanceEditorTestComponent* ComponentB =
			FindMutableTestComponent(*BlueprintB->GeneratedClass.Get());
		ASSERT_THAT(IsNotNull(ComponentB));
		ASSERT_THAT(IsTrue(
			ComponentB->Speed == FFixedPoint::FromInt(22)));

		Profile->IncludedRoots.Reset();
		Profile->IncludedRoots.Add(TSoftClassPtr<ASeinActor>(
			BlueprintA->GeneratedClass.Get()));
		Profile->IncludedRoots.Add(TSoftClassPtr<ASeinActor>(
			BlueprintB->GeneratedClass.Get()));
		Table = SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile);
		ASSERT_THAT(IsNotNull(Table));
		ASSERT_THAT(IsTrue(Table->GetRowMap().Num() == 2));
		RowStruct =
			Cast<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNotNull(RowStruct));
		SpeedField = FindFloatField(
			*RowStruct,
			TEXT("BalanceEditorTest_Speed"));
		ASSERT_THAT(IsNotNull(SpeedField));

		bool bSawA = false;
		bool bSawB = false;
		for (const TPair<FName, uint8*>& RowPair : Table->GetRowMap())
		{
			ASSERT_THAT(IsTrue(RowPair.Key.ToString().StartsWith(
				TEXT("BP_Duplicate__"))));
			const float Speed =
				SpeedField->GetPropertyValue_InContainer(RowPair.Value);
			if (Speed == 11.0f)
			{
				SpeedField->SetPropertyValue_InContainer(
					RowPair.Value,
					111.0f);
				bSawA = true;
			}
			else if (Speed == 22.0f)
			{
				SpeedField->SetPropertyValue_InContainer(
					RowPair.Value,
					222.0f);
				bSawB = true;
			}
		}
		ASSERT_THAT(IsTrue(bSawA));
		ASSERT_THAT(IsTrue(bSawB));
		Table->HandleDataTableChanged();

		CellsChecked = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 2));
		ASSERT_THAT(IsTrue(
			CellsChecked == CountFields(*RowStruct) * 2));
		SkippedCells = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::PushToEntitiesWithoutUI(
				Profile,
				SkippedCells) == 2));
		ASSERT_THAT(IsTrue(SkippedCells == 0));

		FSeinBalanceEditorTestComponent* ComponentA =
			FindMutableTestComponent(*BlueprintA->GeneratedClass.Get());
		ComponentB = FindMutableTestComponent(
			*BlueprintB->GeneratedClass.Get());
		ASSERT_THAT(IsNotNull(ComponentA));
		ASSERT_THAT(IsNotNull(ComponentB));
		ASSERT_THAT(IsTrue(
			ComponentA->Speed == FFixedPoint::FromInt(111)));
		ASSERT_THAT(IsTrue(
			ComponentB->Speed == FFixedPoint::FromInt(222)));
		ASSERT_THAT(IsTrue(BlueprintA->Status != BS_UpToDate));
		ASSERT_THAT(IsTrue(BlueprintB->Status != BS_UpToDate));
	}

	TEST(InvalidOutputDirectoryFailsBeforeMutation, "SeinARTS.Editor.BalanceData")
	{
		USeinBalanceProfile* Profile = MakeProfile(TEXT("InvalidOutput"));
		ASSERT_THAT(IsNotNull(Profile));
		Profile->IncludedRoots.Add(
			ASeinBalanceEditorTestEntityRoot::StaticClass());
		Profile->TrackedComponents.Add(
			FSeinBalanceEditorTestComponent::StaticStruct());
		Profile->OutputDir.Path = TEXT("/Game/Balance.Invalid");

		ASSERT_THAT(IsNull(
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile)));
		ASSERT_THAT(IsTrue(Profile->GeneratedTable.IsNull()));
	}

	TEST(PersistedBlueprintAbilitySurvivesReinstanceAndReload, "SeinARTS.Editor.BalanceData")
	{
		FScopedContentMount Mount;
		const FString PackageName =
			Mount.MakePackageName(TEXT("BP_PersistedAbility"));
		UPackage* Package = CreatePackage(*PackageName);
		ASSERT_THAT(IsNotNull(Package));

		const FName AssetName(TEXT("BP_PersistedAbility"));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			USeinBalanceEditorPersistedAbilityRoot::StaticClass(),
			Package,
			AssetName,
			BPTYPE_Normal,
			USeinAbilityBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		ASSERT_THAT(IsNotNull(Blueprint));
		FAssetRegistryModule::AssetCreated(Blueprint);

		const FName TuningName(TEXT("PersistedTuning"));
		const FFixedPoint InitialValue = FFixedPoint::FromInt(13);
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint,
			TuningName,
			FixedPointPinType(),
			FString::Printf(TEXT("(Value=%lld)"), InitialValue.Value))));
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		ASSERT_THAT(IsNotNull(Blueprint->GeneratedClass));

		FStructProperty* TuningProperty = FindFProperty<FStructProperty>(
			Blueprint->GeneratedClass,
			TuningName);
		ASSERT_THAT(IsNotNull(TuningProperty));
		ASSERT_THAT(IsTrue(
			TuningProperty->Struct == FFixedPoint::StaticStruct()));
		const FString TuningColumnName =
			Blueprint->GeneratedClass->GetAuthoredNameForField(TuningProperty);
		const FFixedPoint* InitialCDOValue =
			TuningProperty->ContainerPtrToValuePtr<FFixedPoint>(
				Blueprint->GeneratedClass->GetDefaultObject());
		ASSERT_THAT(IsNotNull(InitialCDOValue));
		ASSERT_THAT(IsTrue(*InitialCDOValue == InitialValue));

		USeinBalanceProfile* Profile = MakeProfile(TEXT("PersistedAbility"));
		ASSERT_THAT(IsNotNull(Profile));
		TStrongObjectPtr<USeinBalanceProfile> StrongProfile(Profile);
		Profile->TargetKind = ESeinBalanceTargetKind::Abilities;
		Profile->AbilityRoots.Add(TSoftClassPtr<USeinAbility>(
			Blueprint->GeneratedClass.Get()));

		UDataTable* Table =
			SeinBalanceTable::Testing::GatherToTableWithoutUI(Profile);
		ASSERT_THAT(IsNotNull(Table));
		TStrongObjectPtr<UDataTable> StrongTable(Table);
		ASSERT_THAT(IsTrue(Table->GetRowMap().Num() == 1));
		UUserDefinedStruct* RowStruct =
			Cast<UUserDefinedStruct>(Table->RowStruct);
		ASSERT_THAT(IsNotNull(RowStruct));
		FFloatProperty* TuningField = FindFloatField(
			*RowStruct,
			TuningColumnName);
		ASSERT_THAT(IsNotNull(TuningField));

		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		ASSERT_THAT(IsNotNull(Blueprint->GeneratedClass));
		int32 CellsChecked = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::CheckSync(Profile, CellsChecked) == 0));
		ASSERT_THAT(IsTrue(CellsChecked > 0));

		uint8* Row = Table->FindRowUnchecked(AssetName);
		ASSERT_THAT(IsNotNull(Row));
		TuningField->SetPropertyValue_InContainer(Row, 31.0f);
		Table->HandleDataTableChanged();
		int32 SkippedCells = 0;
		ASSERT_THAT(IsTrue(
			SeinBalanceTable::Testing::PushToEntitiesWithoutUI(
				Profile,
				SkippedCells) == 1));
		ASSERT_THAT(IsTrue(SkippedCells == 0));

		TuningProperty = FindFProperty<FStructProperty>(
			Blueprint->GeneratedClass,
			TuningName);
		ASSERT_THAT(IsNotNull(TuningProperty));
		const FFixedPoint* PushedCDOValue =
			TuningProperty->ContainerPtrToValuePtr<FFixedPoint>(
				Blueprint->GeneratedClass->GetDefaultObject());
		ASSERT_THAT(IsNotNull(PushedCDOValue));
		ASSERT_THAT(IsTrue(
			*PushedCDOValue == FFixedPoint::FromInt(31)));
		ASSERT_THAT(IsTrue(SaveAssetPackage(*Package, *Blueprint)));

		const FString ObjectPath = Blueprint->GetPathName();
		Profile->AbilityRoots.Reset();
		Profile->GeneratedTable.Reset();
		FText UnloadError;
		ASSERT_THAT(IsTrue(UPackageTools::UnloadPackages(
			{ Package },
			UnloadError,
			true)));
		Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		ASSERT_THAT(IsNotNull(Blueprint));
		ASSERT_THAT(IsNotNull(Blueprint->GeneratedClass));
		TuningProperty = FindFProperty<FStructProperty>(
			Blueprint->GeneratedClass,
			TuningName);
		ASSERT_THAT(IsNotNull(TuningProperty));
		const FFixedPoint* ReloadedValue =
			TuningProperty->ContainerPtrToValuePtr<FFixedPoint>(
				Blueprint->GeneratedClass->GetDefaultObject());
		ASSERT_THAT(IsNotNull(ReloadedValue));
		ASSERT_THAT(IsTrue(
			*ReloadedValue == FFixedPoint::FromInt(31)));
	}
}
