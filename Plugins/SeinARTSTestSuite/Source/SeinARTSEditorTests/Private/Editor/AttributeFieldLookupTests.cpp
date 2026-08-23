/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    AttributeFieldLookupTests.cpp
 * @brief   Designer-authored UDS fields must resolve by the name the designer
 *          typed AND by the sanitized stem a cooked build reports, so
 *          `Apply Field Delta` / modifiers address the same property in PIE
 *          and in a packaged client (lockstep cannot tolerate the two
 *          disagreeing).
 */

#include "CQTest.h"

#include "Attributes/SeinAttributeResolver.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Types/FixedPoint.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace UE::SeinARTSTests
{
	TEST(AttributeFieldLookupResolvesAuthoredUdsNamesAcrossBuildSpellings,
		"SeinARTS.Editor.Attributes.FieldLookup")
	{
		UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(
			GetTransientPackage(),
			MakeUniqueObjectName(
				GetTransientPackage(), UUserDefinedStruct::StaticClass(),
				TEXT("SeinFieldLookupUDS")),
			RF_Transient);
		ASSERT_THAT(IsNotNull(Struct));
		const TArray<FStructVariableDescription>& Variables =
			FStructureEditorUtils::GetVarDesc(Struct);
		ASSERT_THAT(IsTrue(Variables.Num() == 1));

		FEdGraphPinType FixedType;
		FixedType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		FixedType.PinSubCategoryObject = FFixedPoint::StaticStruct();
		ASSERT_THAT(IsTrue(FStructureEditorUtils::ChangeVariableType(
			Struct, Variables[0].VarGuid, FixedType)));
		// A display name with a space: the editor keeps "Current Health" as the
		// authored name, while the internal stem (what a cooked build reports)
		// is the slugged "CurrentHealth".
		ASSERT_THAT(IsTrue(FStructureEditorUtils::RenameVariable(
			Struct, Variables[0].VarGuid, TEXT("Current Health"))));
		const FName InternalName = Variables[0].VarName;
		ASSERT_THAT(IsTrue(InternalName.ToString().StartsWith(TEXT("CurrentHealth_"))));

		FSeinAttributeResolver::ClearPropertyCache();
		FProperty* ByInternal = FSeinAttributeResolver::FindFieldProperty(
			Struct, InternalName);
		FProperty* ByAuthored = FSeinAttributeResolver::FindFieldProperty(
			Struct, TEXT("Current Health"));
		FProperty* ByCookedStem = FSeinAttributeResolver::FindFieldProperty(
			Struct, TEXT("CurrentHealth"));
		ASSERT_THAT(IsNotNull(ByInternal));
		ASSERT_THAT(IsTrue(ByInternal == ByAuthored));
		ASSERT_THAT(IsTrue(ByInternal == ByCookedStem));
		ASSERT_THAT(IsTrue(FSeinAttributeResolver::IsFixedPointField(ByInternal)));

		// A different valid spelling is NOT the same field: the sanitizer only
		// collapses characters the struct editor itself would have slugged.
		ASSERT_THAT(IsNull(FSeinAttributeResolver::FindFieldProperty(
			Struct, TEXT("Current_Health"))));
		ASSERT_THAT(IsNull(FSeinAttributeResolver::FindFieldProperty(
			Struct, TEXT("Health"))));
		FSeinAttributeResolver::ClearPropertyCache();
	}
}
