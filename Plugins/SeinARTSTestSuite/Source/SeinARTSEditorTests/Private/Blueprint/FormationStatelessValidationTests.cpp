#include "CQTest.h"

#include "AssetRegistry/AssetData.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Formations/SeinBlobFormation.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"
#include "Validators/SeinFormationDeterminismValidator.h"

namespace UE::SeinARTSTests
{
	namespace FormationStatelessValidation
	{
		UBlueprint* MakeFormationBlueprint()
		{
			return FKismetEditorUtilities::CreateBlueprint(
				USeinBlobFormation::StaticClass(),
				GetTransientPackage(),
				MakeUniqueObjectName(
					GetTransientPackage(), UBlueprint::StaticClass(),
					TEXT("BP_FormationStatelessValidation")),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				NAME_None);
		}

		void AddNode(UEdGraph& Graph, UEdGraphNode& Node)
		{
			Graph.AddNode(&Node, false, false);
			Node.CreateNewGuid();
			Node.PostPlacedNewNode();
			Node.AllocateDefaultPins();
		}

		FEdGraphPinType IntPinType()
		{
			FEdGraphPinType Type;
			Type.PinCategory = UEdGraphSchema_K2::PC_Int;
			return Type;
		}
	}

	TEST(FormationMemberWriteIsBlockingValidationError,
		"SeinARTS.Editor.Blueprint.FormationStateless")
	{
		using namespace FormationStatelessValidation;
		UBlueprint* Blueprint = MakeFormationBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));
		const FName CounterName(TEXT("Counter"));
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, CounterName, IntPinType())));

		UK2Node_CustomEvent* Entry = NewObject<UK2Node_CustomEvent>(Graph);
		Entry->CustomFunctionName = TEXT("MutateFormation");
		AddNode(*Graph, *Entry);
		UK2Node_VariableSet* Setter = NewObject<UK2Node_VariableSet>(Graph);
		Setter->VariableReference.SetSelfMember(CounterName);
		AddNode(*Graph, *Setter);

		USeinFormationDeterminismValidator* Validator =
			GetMutableDefault<USeinFormationDeterminismValidator>();
		ASSERT_THAT(IsNotNull(Validator));
		FDataValidationContext Context;
		const EDataValidationResult Result =
			Validator->ValidateLoadedAsset(
				FAssetData(Blueprint), Blueprint, Context);
		ASSERT_THAT(IsTrue(Result == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}

	TEST(FormationConfigWithoutWritesPassesValidation,
		"SeinARTS.Editor.Blueprint.FormationStateless")
	{
		using namespace FormationStatelessValidation;
		UBlueprint* Blueprint = MakeFormationBlueprint();
		ASSERT_THAT(IsNotNull(Blueprint));
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, TEXT("SpacingConfig"), IntPinType())));

		USeinFormationDeterminismValidator* Validator =
			GetMutableDefault<USeinFormationDeterminismValidator>();
		ASSERT_THAT(IsNotNull(Validator));
		FDataValidationContext Context;
		const EDataValidationResult Result =
			Validator->ValidateLoadedAsset(
				FAssetData(Blueprint), Blueprint, Context);
		ASSERT_THAT(IsTrue(Result == EDataValidationResult::Valid));
		ASSERT_THAT(AreEqual(0, Context.GetNumErrors()));
	}
}
