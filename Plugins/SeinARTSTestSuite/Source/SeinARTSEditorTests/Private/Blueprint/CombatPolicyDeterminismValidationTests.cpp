#include "CQTest.h"

#include "AssetRegistry/AssetData.h"
#include "Combat/SeinTargetScorer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"
#include "Validators/SeinCombatPolicyDeterminismValidator.h"

namespace UE::SeinARTSTests
{
	namespace CombatPolicyDeterminismValidation
	{
		UBlueprint* MakePolicyBlueprint(UClass* ParentClass, const TCHAR* BaseName)
		{
			return FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				GetTransientPackage(),
				MakeUniqueObjectName(
					GetTransientPackage(), UBlueprint::StaticClass(), BaseName),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				NAME_None);
		}

		FEdGraphPinType IntPinType()
		{
			FEdGraphPinType Type;
			Type.PinCategory = UEdGraphSchema_K2::PC_Int;
			return Type;
		}

		void AddNode(UEdGraph& Graph, UEdGraphNode& Node)
		{
			Graph.AddNode(&Node, false, false);
			Node.CreateNewGuid();
			Node.PostPlacedNewNode();
			Node.AllocateDefaultPins();
		}
	}

	TEST(StatelessTargetScorerConfigPassesValidation,
		"SeinARTS.Editor.Blueprint.CombatPolicyDeterminism")
	{
		using namespace CombatPolicyDeterminismValidation;
		UBlueprint* Blueprint = MakePolicyBlueprint(
			USeinTargetScorer::StaticClass(),
			TEXT("BP_CombatPolicyConfigValidation"));
		ASSERT_THAT(IsNotNull(Blueprint));
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, TEXT("PriorityConfig"), IntPinType())));

		USeinCombatPolicyDeterminismValidator* Validator =
			GetMutableDefault<USeinCombatPolicyDeterminismValidator>();
		ASSERT_THAT(IsNotNull(Validator));
		FDataValidationContext Context;
		const EDataValidationResult Result = Validator->ValidateLoadedAsset(
			FAssetData(Blueprint), Blueprint, Context);
		ASSERT_THAT(IsTrue(Result == EDataValidationResult::Valid));
		ASSERT_THAT(AreEqual(0, Context.GetNumErrors()));
	}

	TEST(TargetScorerMemberWriteIsBlockingValidationError,
		"SeinARTS.Editor.Blueprint.CombatPolicyDeterminism")
	{
		using namespace CombatPolicyDeterminismValidation;
		UBlueprint* Blueprint = MakePolicyBlueprint(
			USeinTargetScorer::StaticClass(),
			TEXT("BP_TargetScorerWriteValidation"));
		ASSERT_THAT(IsNotNull(Blueprint));
		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		ASSERT_THAT(IsNotNull(Graph));
		const FName CounterName(TEXT("Counter"));
		ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(
			Blueprint, CounterName, IntPinType())));

		UK2Node_CustomEvent* Entry = NewObject<UK2Node_CustomEvent>(Graph);
		Entry->CustomFunctionName = TEXT("MutateTargetScorer");
		AddNode(*Graph, *Entry);
		UK2Node_VariableSet* Setter = NewObject<UK2Node_VariableSet>(Graph);
		Setter->VariableReference.SetSelfMember(CounterName);
		AddNode(*Graph, *Setter);

		USeinCombatPolicyDeterminismValidator* Validator =
			GetMutableDefault<USeinCombatPolicyDeterminismValidator>();
		ASSERT_THAT(IsNotNull(Validator));
		FDataValidationContext Context;
		const EDataValidationResult Result = Validator->ValidateLoadedAsset(
			FAssetData(Blueprint), Blueprint, Context);
		ASSERT_THAT(IsTrue(Result == EDataValidationResult::Invalid));
		ASSERT_THAT(IsTrue(Context.GetNumErrors() > 0));
	}
}
