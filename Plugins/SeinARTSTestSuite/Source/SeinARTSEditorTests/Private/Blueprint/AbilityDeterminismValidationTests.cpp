/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         AbilityDeterminismValidationTests.cpp
 * @author       RJ Macklem
 * @created      14 Aug 2026
 * @latest       14 Aug 2026
 * @brief        Verifies blocking determinism validation for Ability Blueprints.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "AssetRegistry/AssetData.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lib/MathBPFL.h"
#include "Lib/SeinTargeterBPFL.h"
#include "Misc/DataValidation.h"
#include "Movement/SeinBasicUnitMovement.h"
#include "Types/FixedPoint.h"
#include "Types/Random.h"
#include "TestTypes/SeinAbilityDeterminismValidationTestTypes.h"
#include "UObject/Package.h"
#include "Validators/SeinAbilityDeterminismValidator.h"
#include "Validators/SeinMovementDeterminismValidator.h"

namespace UE::SeinARTSTests
{
	namespace AbilityDeterminismValidation
	{
		UBlueprint* MakeAbilityBlueprint()
		{
			return FKismetEditorUtilities::CreateBlueprint(
				USeinAbility::StaticClass(),
				GetTransientPackage(),
				MakeUniqueObjectName(
					GetTransientPackage(),
					USeinAbilityBlueprint::StaticClass(),
					TEXT("BP_AbilityDeterminismValidation")),
				BPTYPE_Normal,
				USeinAbilityBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				NAME_None);
		}

		EDataValidationResult Validate(
			UBlueprint& Blueprint,
			FDataValidationContext& Context)
		{
			USeinAbilityDeterminismValidator* Validator =
				GetMutableDefault<USeinAbilityDeterminismValidator>();
			return Validator->ValidateLoadedAsset(
				FAssetData(&Blueprint), &Blueprint, Context);
		}

		FEdGraphPinType FixedPointPinType()
		{
			FEdGraphPinType Type;
			Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Type.PinSubCategoryObject = FFixedPoint::StaticStruct();
			return Type;
		}

		FEdGraphPinType FixedRandomPinType()
		{
			FEdGraphPinType Type;
			Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Type.PinSubCategoryObject = FFixedRandom::StaticStruct();
			return Type;
		}

		UK2Node_CallFunction* AddCall(
			UEdGraph& Graph,
			const UFunction& Function)
		{
			UK2Node_CallFunction* Call =
				NewObject<UK2Node_CallFunction>(&Graph);
			Call->SetFromFunction(&Function);
			Graph.AddNode(Call, false, false);
			Call->CreateNewGuid();
			Call->PostPlacedNewNode();
			Call->AllocateDefaultPins();
			return Call;
		}
	}

	TEST(AbilityFloatMemberIsBlockingValidationError,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));

		FEdGraphPinType FloatType;
		FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
		FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, TEXT("UnsafeElapsedTime"), FloatType)));

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}

	TEST(AbilityUnseededRandomCallIsBlockingValidationError,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* RandomInteger =
			UKismetMathLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					UKismetMathLibrary, RandomInteger));
		ASSERT_THAT(IsNotNull(RandomInteger));
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(RandomInteger);
		Graph->AddNode(Call, false, false);
		Call->CreateNewGuid();
		Call->PostPlacedNewNode();
		Call->AllocateDefaultPins();

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}

	TEST(AbilityPresentationConversionIsBlockingValidationError,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* ToFloat = UMathBPFL::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UMathBPFL, FixedPointToFloat));
		ASSERT_THAT(IsNotNull(ToFloat));
		ASSERT_THAT(IsTrue(ToFloat->HasMetaData(TEXT("SeinPresentationOnly"))));
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(ToFloat);
		Graph->AddNode(Call, false, false);
		Call->CreateNewGuid();
		Call->PostPlacedNewNode();
		Call->AllocateDefaultPins();

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}

	TEST(AbilityFrameCountCallIsBlockingValidationError,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* GetFrameCount =
			UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					UKismetSystemLibrary, GetFrameCount));
		ASSERT_THAT(IsNotNull(GetFrameCount));
		AddCall(*Graph, *GetFrameCount);

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}

	TEST(AbilityDeterministicRandomMemberAndCallPassValidation,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, TEXT("RandomState"), FixedRandomPinType())));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* RandomBool = UMathBPFL::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UMathBPFL, RandomBool));
		ASSERT_THAT(IsNotNull(RandomBool));
		AddCall(*Graph, *RandomBool);

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Valid));
		ASSERT_THAT(AreEqual(0, Context.GetNumErrors()));
	}

	TEST(PresentationOwnerOverridesDeterministicFunctionMetadata,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* Conflict =
			USeinPresentationOnlyValidationLibrary::StaticClass()
				->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(
					USeinPresentationOnlyValidationLibrary,
					DeterministicSignatureOnPresentationOwner));
		ASSERT_THAT(IsNotNull(Conflict));
		ASSERT_THAT(IsTrue(Conflict->HasMetaData(TEXT("SeinDeterministic"))));
		ASSERT_THAT(IsTrue(Conflict->GetOwnerClass()->HasMetaData(
			TEXT("SeinPresentationOnly"))));
		AddCall(*Graph, *Conflict);

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}

	TEST(AbilityDeterministicTargeterAccessorPassesValidation,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* Accessor =
			USeinTargeterBPFL::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					USeinTargeterBPFL, GetTargeterPointTransform));
		ASSERT_THAT(IsNotNull(Accessor));
		ASSERT_THAT(IsTrue(Accessor->HasMetaData(TEXT("SeinDeterministic"))));
		AddCall(*Graph, *Accessor);

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Valid));
		ASSERT_THAT(AreEqual(0, Context.GetNumErrors()));
	}

	TEST(AbilityFixedPointMemberPassesDeterminismValidation,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		using namespace AbilityDeterminismValidation;
		UBlueprint* Blueprint = MakeAbilityBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, TEXT("ElapsedSimTime"), FixedPointPinType())));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));
		const UFunction* Enqueue = USeinAbility::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(USeinAbility, EnqueueProduction));
		ASSERT_THAT(IsNotNull(Enqueue));
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(Enqueue);
		Graph->AddNode(Call, false, false);
		Call->CreateNewGuid();
		Call->PostPlacedNewNode();
		Call->AllocateDefaultPins();

		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validate(*Blueprint, Context) == EDataValidationResult::Valid));
		ASSERT_THAT(AreEqual(0, Context.GetNumErrors()));
	}

	TEST(MovementPresentationConversionIsBlockingValidationError,
		"SeinARTS.Editor.Blueprint.AbilityDeterminism")
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			USeinBasicUnitMovement::StaticClass(),
			GetTransientPackage(),
			MakeUniqueObjectName(
				GetTransientPackage(),
				UBlueprint::StaticClass(),
				TEXT("BP_MovementPresentationConversionValidation")),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));

		const UFunction* ToFloat = UMathBPFL::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UMathBPFL, FixedPointToFloat));
		ASSERT_THAT(IsNotNull(ToFloat));
		ASSERT_THAT(IsTrue(ToFloat->HasMetaData(TEXT("SeinPresentationOnly"))));
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(ToFloat);
		Graph->AddNode(Call, false, false);
		Call->CreateNewGuid();
		Call->PostPlacedNewNode();
		Call->AllocateDefaultPins();

		USeinMovementDeterminismValidator* Validator =
			GetMutableDefault<USeinMovementDeterminismValidator>();
		FDataValidationContext Context;
		ASSERT_THAT(IsTrue(
			Validator->ValidateLoadedAsset(
				FAssetData(Blueprint), Blueprint, Context)
			== EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}
}
