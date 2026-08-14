#include "CQTest.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "AssetRegistry/AssetData.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lib/SeinMovementPlusBPFL.h"
#include "Misc/DataValidation.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "UObject/Package.h"
#include "Validators/SeinAbilityDeterminismValidator.h"
#include "Validators/SeinMovementDeterminismValidator.h"

namespace UE::SeinARTSTests
{
	TEST(MovementPlusPresentationTelemetryIsRejectedInSimGraphs,
		"SeinARTS.Editor.MovementPlus.Telemetry")
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			USeinWheeledVehicleMovement::StaticClass(),
			GetTransientPackage(),
			MakeUniqueObjectName(
				GetTransientPackage(),
				UBlueprint::StaticClass(),
				TEXT("BP_MovementPlusTelemetryValidation")),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph =
			FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* Getter =
			USeinMovementPlusBPFL::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					USeinMovementPlusBPFL,
					SeinGetMovementPlusPresentationState));
		ASSERT_THAT(IsNotNull(Getter));
		UK2Node_CallFunction* Call =
			NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(Getter);
		Graph->AddNode(Call, false, false);
		Call->CreateNewGuid();
		Call->PostPlacedNewNode();
		Call->AllocateDefaultPins();

		USeinMovementDeterminismValidator* Validator =
			GetMutableDefault<USeinMovementDeterminismValidator>();
		ASSERT_THAT(IsNotNull(Validator));
		FDataValidationContext Context;
		const EDataValidationResult Result =
			Validator->ValidateLoadedAsset(
				FAssetData(Blueprint), Blueprint, Context);
		ASSERT_THAT(IsTrue(Result == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}

	TEST(MovementPlusPresentationTelemetryIsRejectedInAbilityGraphs,
		"SeinARTS.Editor.MovementPlus.Telemetry")
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			USeinAbility::StaticClass(),
			GetTransientPackage(),
			MakeUniqueObjectName(
				GetTransientPackage(),
				USeinAbilityBlueprint::StaticClass(),
				TEXT("BP_MovementPlusAbilityTelemetryValidation")),
			BPTYPE_Normal,
			USeinAbilityBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* Getter =
			USeinMovementPlusBPFL::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					USeinMovementPlusBPFL,
					SeinGetMovementPlusPresentationState));
		ASSERT_THAT(IsNotNull(Getter));
		ASSERT_THAT(IsTrue(Getter->HasMetaData(TEXT("SeinPresentationOnly"))));
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(Getter);
		Graph->AddNode(Call, false, false);
		Call->CreateNewGuid();
		Call->PostPlacedNewNode();
		Call->AllocateDefaultPins();

		USeinAbilityDeterminismValidator* Validator =
			GetMutableDefault<USeinAbilityDeterminismValidator>();
		ASSERT_THAT(IsNotNull(Validator));
		FDataValidationContext Context;
		const EDataValidationResult Result =
			Validator->ValidateLoadedAsset(
				FAssetData(Blueprint), Blueprint, Context);
		ASSERT_THAT(IsTrue(Result == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}
}
