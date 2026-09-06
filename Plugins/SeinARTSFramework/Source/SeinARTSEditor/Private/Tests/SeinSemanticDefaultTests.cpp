/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSemanticDefaultTests.cpp
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Verifies constructed defaults for array elements and selected
 *               FInstancedStruct data in entity-component authoring fields.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Authoring/SeinEntityComponent.h"
#include "Details/SeinSemanticDefault.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "InstancedStructDetails.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSeinSemanticDefaultTest,
	"SeinARTS.Editor.EntityComponents.SemanticDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSeinSemanticDefaultTest::RunTest(const FString& Parameters)
{
	USeinExtentsComponent* ExtentsComponent =
		NewObject<USeinExtentsComponent>();
	ExtentsComponent->Extents.Shapes.Add(FSeinExtentsShape());

	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(
			TEXT("PropertyEditor"));
	const TSharedPtr<ISinglePropertyView> ExtentsView =
		PropertyEditor.CreateSingleProperty(
			ExtentsComponent,
			GET_MEMBER_NAME_CHECKED(USeinExtentsComponent, Extents),
			FSinglePropertyParams{});
	if (!TestTrue(TEXT("Extents property view is valid"), ExtentsView.IsValid()))
	{
		return false;
	}

	const TSharedPtr<IPropertyHandle> ExtentsHandle =
		ExtentsView->GetPropertyHandle();
	const TSharedPtr<IPropertyHandle> ShapesHandle =
		ExtentsHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FSeinExtentsPayload, Shapes));
	const TSharedPtr<IPropertyHandleArray> ShapesArray =
		ShapesHandle.IsValid() ? ShapesHandle->AsArray() : nullptr;
	if (!ShapesArray.IsValid())
	{
		AddError(TEXT("Could not resolve the Extents.Shapes array."));
		return false;
	}
	const TSharedPtr<IPropertyHandle> ShapeHandle =
		ShapesArray->GetElement(0);

	const TSharedPtr<IPropertyHandle> RadiusHandle =
		ShapeHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FSeinExtentsShape, Radius));
	if (!TestTrue(TEXT("Extents radius handle is valid"),
		RadiusHandle.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Nested handle reports one edited object"),
		RadiusHandle->GetNumPerObjectValues(), 1);
	TArray<const void*> RadiusValues;
	RadiusHandle->AccessRawData(RadiusValues);
	TestEqual(TEXT("Nested handle exposes one raw value"),
		RadiusValues.Num(), 1);

	const FResetToDefaultOverride ArrayReset =
		SeinSemanticDefault::MakeResetOverride();
	TestFalse(TEXT("A newly added shape radius is already at its default"),
		ArrayReset.IsResetToDefaultVisible(RadiusHandle));

	ExtentsComponent->Extents.Shapes[0].Radius = FFixedPoint::FromInt(777);
	TestTrue(TEXT("A tuned shape radius exposes reset"),
		ArrayReset.IsResetToDefaultVisible(RadiusHandle));
	ArrayReset.GetPropertyResetToDefaultDelegate().Execute(RadiusHandle);
	TestEqual(TEXT("Shape radius resets to its constructed default"),
		ExtentsComponent->Extents.Shapes[0].Radius,
		FFixedPoint::FromInt(40));
	TestFalse(TEXT("The shape reset arrow hides after reset"),
		ArrayReset.IsResetToDefaultVisible(RadiusHandle));

	USeinMovementComponent* MovementComponent =
		NewObject<USeinMovementComponent>();
	MovementComponent->Movement.MovementClassData
		.InitializeAs<FSeinExtentsShape>();
	FSeinExtentsShape& MovementData = MovementComponent->Movement
		.MovementClassData.GetMutable<FSeinExtentsShape>();
	MovementData.Radius = FFixedPoint::FromInt(888);

	const TSharedPtr<ISinglePropertyView> MovementView =
		PropertyEditor.CreateSingleProperty(
			MovementComponent,
			GET_MEMBER_NAME_CHECKED(USeinMovementComponent, Movement),
			FSinglePropertyParams{});
	if (!TestTrue(TEXT("Movement property view is valid"), MovementView.IsValid()))
	{
		return false;
	}

	const TSharedPtr<IPropertyHandle> MovementHandle =
		MovementView->GetPropertyHandle();
	const TSharedPtr<IPropertyHandle> InstancedHandle =
		MovementHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FSeinMovementPayload, MovementClassData));
	if (!TestTrue(TEXT("Movement class data handle is valid"),
		InstancedHandle.IsValid()))
	{
		return false;
	}

	const TSharedRef<FInstancedStructProvider> Provider =
		MakeShared<FInstancedStructProvider>(InstancedHandle);
	const TArray<TSharedPtr<IPropertyHandle>> InstancedChildren =
		InstancedHandle->AddChildStructure(Provider);
	TSharedPtr<IPropertyHandle> InstancedRadiusHandle;
	for (const TSharedPtr<IPropertyHandle>& Child : InstancedChildren)
	{
		if (Child.IsValid()
			&& Child->GetProperty()
			&& Child->GetProperty()->GetFName()
				== GET_MEMBER_NAME_CHECKED(FSeinExtentsShape, Radius))
		{
			InstancedRadiusHandle = Child;
			break;
		}
	}
	if (!TestTrue(TEXT("Selected struct radius handle is valid"),
		InstancedRadiusHandle.IsValid()))
	{
		return false;
	}

	const FResetToDefaultOverride InstancedChildReset =
		SeinSemanticDefault::MakeResetOverride(InstancedHandle);
	TestTrue(TEXT("A tuned selected-struct field exposes reset"),
		InstancedChildReset.IsResetToDefaultVisible(InstancedRadiusHandle));
	InstancedChildReset.GetPropertyResetToDefaultDelegate().Execute(
		InstancedRadiusHandle);
	TestEqual(TEXT("Selected-struct field resets to its constructed default"),
		MovementComponent->Movement.MovementClassData
			.Get<FSeinExtentsShape>().Radius,
		FFixedPoint::FromInt(40));
	TestFalse(TEXT("The selected-struct field arrow hides after reset"),
		InstancedChildReset.IsResetToDefaultVisible(InstancedRadiusHandle));

	MovementComponent->Movement.MovementClassData
		.GetMutable<FSeinExtentsShape>().Radius = FFixedPoint::FromInt(999);
	const FResetToDefaultOverride InstancedRootReset =
		SeinSemanticDefault::MakeResetOverride();
	TestTrue(TEXT("Tuned class data exposes a root reset"),
		InstancedRootReset.IsResetToDefaultVisible(InstancedHandle));
	InstancedRootReset.GetPropertyResetToDefaultDelegate().Execute(
		InstancedHandle);
	TestTrue(TEXT("Root reset preserves the selected struct type"),
		MovementComponent->Movement.MovementClassData.GetScriptStruct()
			== FSeinExtentsShape::StaticStruct());
	TestEqual(TEXT("Root reset reconstructs all selected-struct defaults"),
		MovementComponent->Movement.MovementClassData
			.Get<FSeinExtentsShape>().Radius,
		FFixedPoint::FromInt(40));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
