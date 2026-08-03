#include "Validators/SeinAbilityContinuationAnalysis.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentAction.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Literal.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Self.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Switch.h"
#include "Serialization/SeinCanonicalStatePropertyPolicy.h"
#include "Serialization/SeinLatentActionCodecRegistry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "Util/SeinDeterminismRules.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	constexpr int32 MaxAnalyzedNodes = 16384;
	constexpr int32 MaxFunctionProofDepth = 32;

	const FName MoveToResultPinName(TEXT("Result"));
	const FName SeinContinuationSafeMeta(TEXT("SeinContinuationSafe"));
	const FName SeinCheckpointActionClassMeta(
		TEXT("SeinCheckpointActionClass"));
	const FString MoveToResultStructPath(
		TEXT("/Script/SeinARTSMovement.SeinMoveToResult"));

	FString DescribeNode(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return TEXT("<unknown>");
		}
		const FString Title =
			Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		return Title.IsEmpty() ? Node->GetClass()->GetName() : Title;
	}

	bool IsCheckpointSupportedAsync(
		const UK2Node_BaseAsyncTask& AsyncNode,
		FString* OutReason = nullptr)
	{
		auto Fail = [OutReason](FString Reason)
		{
			if (OutReason)
			{
				*OutReason = MoveTemp(Reason);
			}
			return false;
		};
		if (OutReason)
		{
			OutReason->Reset();
		}

		// Only the stock expansion is modeled by the checkpoint contract.
		// Custom BaseAsyncTask subclasses may generate additional persistent
		// frame state and must remain closed until explicitly supported.
		if (AsyncNode.GetClass() != UK2Node_AsyncAction::StaticClass())
		{
			return Fail(FString::Printf(
				TEXT("custom async K2 node class '%s' has no checkpoint expansion contract"),
				*AsyncNode.GetClass()->GetPathName()));
		}

		const UFunction* Factory = AsyncNode.GetFactoryFunction();
		if (!Factory)
		{
			return Fail(TEXT("the async factory function cannot be resolved"));
		}
		if (!Factory->HasMetaData(SeinCheckpointActionClassMeta))
		{
			return Fail(FString::Printf(
				TEXT("factory '%s' does not declare an exact SeinCheckpointActionClass"),
				*Factory->GetPathName()));
		}

		const FString ClassPath =
			Factory->GetMetaData(SeinCheckpointActionClassMeta);
		UClass* ActionClass = FindObject<UClass>(nullptr, *ClassPath);
		if (!ActionClass)
		{
			ActionClass = LoadObject<UClass>(nullptr, *ClassPath);
		}
		if (!ActionClass
			|| !ActionClass->IsChildOf(USeinLatentAction::StaticClass()))
		{
			return Fail(FString::Printf(
				TEXT("factory '%s' declares invalid Sein latent action class '%s'"),
				*Factory->GetPathName(),
				*ClassPath));
		}

		FString CodecError;
		if (!FSeinLatentActionCodecRegistry::
				HasRegisteredCodecForExactClass(ActionClass, &CodecError))
		{
			return Fail(MoveTemp(CodecError));
		}
		return true;
	}

	bool IsMoveResultStructPin(const UEdGraphPin& Pin)
	{
		const UObject* SubCategory = Pin.PinType.PinSubCategoryObject.Get();
		return Pin.Direction == EGPD_Output
			&& Pin.PinName == MoveToResultPinName
			&& SubCategory
			&& SubCategory->GetPathName() == MoveToResultStructPath;
	}

	bool IsUnsupportedOpaqueNode(const UEdGraphNode& Node)
	{
		return Node.IsA<UK2Node_MacroInstance>()
			|| Node.IsA<UK2Node_Tunnel>()
			|| Node.IsA<UK2Node_Composite>();
	}

	bool HasExecPins(const UEdGraphNode& Node)
	{
		return Node.Pins.ContainsByPredicate(
			[](const UEdGraphPin* Pin)
			{
				return Pin
					&& Pin->PinType.PinCategory
						== UEdGraphSchema_K2::PC_Exec;
			});
	}

	bool IsExplicitSynchronousExecNode(const UEdGraphNode& Node)
	{
		// Callback-capable K2 nodes do not share one reliable engine marker.
		// Keep the continuation proof intentionally closed: common primitive
		// control-flow nodes are synchronous, while Load Asset, Timeline, and
		// custom expansion nodes must be modeled explicitly before they may
		// sit in Result-dependent execution ancestry.
		return Node.IsA<UK2Node_CallFunction>()
			|| Node.IsA<UK2Node_VariableSet>()
			|| Node.IsA<UK2Node_Knot>()
			|| Node.IsA<UK2Node_IfThenElse>()
			|| Node.IsA<UK2Node_ExecutionSequence>()
			|| Node.IsA<UK2Node_Switch>()
			|| Node.IsA<UK2Node_DynamicCast>();
	}

	/**
	 * UK2Node_BaseAsyncTask places factory outputs before its delegate exec
	 * pins and callback outputs after them. Mirror that public pin contract so
	 * a nested async callback may read data produced at its own boundary while
	 * still rejecting the proxy/factory values retained from an earlier one.
	 */
	bool IsAsyncCallbackDataPin(
		const UEdGraphPin& Pin,
		const UK2Node_BaseAsyncTask& AsyncNode)
	{
		if (Pin.GetOwningNode() != &AsyncNode
			|| Pin.Direction != EGPD_Output
			|| Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return false;
		}

		bool bPassedDelegateExecPins = false;
		for (const UEdGraphPin* Candidate : AsyncNode.Pins)
		{
			if (!Candidate)
			{
				continue;
			}
			if (!bPassedDelegateExecPins
				&& Candidate->Direction == EGPD_Output
				&& Candidate->PinType.PinCategory
					== UEdGraphSchema_K2::PC_Exec
				&& Candidate->PinName
					!= UEdGraphSchema_K2::PN_Then)
			{
				bPassedDelegateExecPins = true;
			}
			if (Candidate == &Pin)
			{
				return bPassedDelegateExecPins;
			}
		}
		return false;
	}

	const UEdGraphPin* FindMoveResultResiduePin(
		const UK2Node_BaseAsyncTask& AsyncNode)
	{
		// UE 5.7 BaseAsyncTask expansion retains only generated internal
		// Result storage plus per-delegate custom-event Result parameters in
		// the persistent frame, not source-node provenance. Any supported
		// async task with this exact callback pin must therefore satisfy the
		// same liveness contract. Factory outputs bypass SpawnInternalVariable
		// and retain CallFunc_<Factory> provenance.
		for (const UEdGraphPin* Pin : AsyncNode.Pins)
		{
			if (Pin
				&& IsMoveResultStructPin(*Pin)
				&& IsAsyncCallbackDataPin(*Pin, AsyncNode))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	bool CanProduceOmittedMoveResultResidue(
		const UK2Node_BaseAsyncTask& AsyncNode)
	{
		// The runtime codec certifies the exact expansion topology emitted by
		// UK2Node_AsyncAction. Other BaseAsyncTask subclasses fail closed
		// instead of entering this explicit-state contract.
		return IsCheckpointSupportedAsync(AsyncNode)
			&& FindMoveResultResiduePin(AsyncNode);
	}

	enum class EResultCallbackAssignment : uint8
	{
		Assigns,
		DoesNotAssign,
		Unresolved
	};

	EResultCallbackAssignment ResolveResultCallbackAssignment(
		const UK2Node_BaseAsyncTask& AsyncNode,
		const UEdGraphPin& CallbackPin,
		const UEdGraphPin& ResultPin)
	{
		if (CallbackPin.PinName == UEdGraphSchema_K2::PN_Then)
		{
			return EResultCallbackAssignment::DoesNotAssign;
		}

		const UFunction* Factory = AsyncNode.GetFactoryFunction();
		const FObjectPropertyBase* ReturnProperty = Factory
			? CastField<FObjectPropertyBase>(
				Factory->GetReturnProperty())
			: nullptr;
		const UClass* ProxyClass = ReturnProperty
			? ReturnProperty->PropertyClass
			: nullptr;
		const FMulticastDelegateProperty* DelegateProperty =
			ProxyClass
				? FindFProperty<FMulticastDelegateProperty>(
					ProxyClass,
					CallbackPin.PinName)
				: nullptr;
		const UFunction* Signature = DelegateProperty
			? DelegateProperty->SignatureFunction
			: nullptr;
		if (!Signature)
		{
			return EResultCallbackAssignment::Unresolved;
		}

		const FStructProperty* ResultProperty =
			FindFProperty<FStructProperty>(
				Signature,
				ResultPin.PinName);
		const bool bIsInputParameter = ResultProperty
			&& ResultProperty->HasAnyPropertyFlags(CPF_Parm)
			&& (!ResultProperty->HasAnyPropertyFlags(CPF_OutParm)
				|| ResultProperty->HasAnyPropertyFlags(
					CPF_ReferenceParm));
		return bIsInputParameter
				&& ResultProperty->Struct
				&& ResultProperty->Struct->GetPathName()
					== MoveToResultStructPath
			? EResultCallbackAssignment::Assigns
			: EResultCallbackAssignment::DoesNotAssign;
	}

	bool GatherResultExecConsumers(
		const UEdGraphPin& ResultPin,
		TArray<const UEdGraphNode*>& OutDependentNodes,
		TArray<const UEdGraphNode*>& OutConsumers)
	{
		OutDependentNodes.Reset();
		OutConsumers.Reset();

		TArray<const UEdGraphPin*> ResultPins{&ResultPin};
		TSet<const UEdGraphPin*> VisitedResultPins;
		for (int32 Index = 0; Index < ResultPins.Num(); ++Index)
		{
			if (Index >= MaxAnalyzedNodes)
			{
				return false;
			}
			const UEdGraphPin* Pin = ResultPins[Index];
			if (!Pin || VisitedResultPins.Contains(Pin))
			{
				continue;
			}
			VisitedResultPins.Add(Pin);
			for (const UEdGraphPin* Child : Pin->SubPins)
			{
				if (!Child)
				{
					return false;
				}
				ResultPins.AddUnique(Child);
			}
		}

		TArray<const UEdGraphNode*> Queue;
		for (const UEdGraphPin* Pin : ResultPins)
		{
			if (!Pin)
			{
				return false;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode())
				{
					return false;
				}
				Queue.AddUnique(Linked->GetOwningNode());
			}
		}

		TSet<const UEdGraphNode*> Visited;
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			if (Index >= MaxAnalyzedNodes)
			{
				return false;
			}

			const UEdGraphNode* Node = Queue[Index];
			if (!Node || Visited.Contains(Node))
			{
				continue;
			}
			Visited.Add(Node);
			OutDependentNodes.Add(Node);
			if (IsUnsupportedOpaqueNode(*Node))
			{
				return false;
			}

			const UK2Node* K2Node = Cast<UK2Node>(Node);
			const bool bPureDataRelay =
				!HasExecPins(*Node)
				&& (Node->IsA<UK2Node_Knot>()
					|| (K2Node && K2Node->IsNodePure()));
			if (!bPureDataRelay)
			{
				OutConsumers.AddUnique(Node);
				continue;
			}

			for (const UEdGraphPin* Output : Node->Pins)
			{
				if (!Output || Output->Direction != EGPD_Output
					|| Output->PinType.PinCategory
						== UEdGraphSchema_K2::PC_Exec)
				{
					continue;
				}
				for (const UEdGraphPin* Linked : Output->LinkedTo)
				{
					if (!Linked || !Linked->GetOwningNode())
					{
						return false;
					}
					Queue.AddUnique(Linked->GetOwningNode());
				}
			}
		}

		OutDependentNodes.Sort(
			[](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				if (A.NodeGuid != B.NodeGuid)
				{
					return A.NodeGuid < B.NodeGuid;
				}
				return A.GetPathName() < B.GetPathName();
			});
		OutConsumers.Sort(
			[](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				if (A.NodeGuid != B.NodeGuid)
				{
					return A.NodeGuid < B.NodeGuid;
				}
				return A.GetPathName() < B.GetPathName();
		});
		return true;
	}

	bool ProveResultConsumerExecAncestry(
		const UK2Node_BaseAsyncTask& ResultSource,
		const UEdGraphPin& ResultPin,
		const UEdGraphNode& Consumer,
		FString& OutReason)
	{
		OutReason.Reset();
		bool bSawAssigningSourceCallback = false;
		int32 AnalyzedNodeCount = 0;
		enum class EProofState : uint8
		{
			Visiting,
			Passed
		};
		TMap<const UEdGraphNode*, EProofState> ProofStates;
		TFunction<bool(const UEdGraphNode&)> ProveNode;
		ProveNode = [&](const UEdGraphNode& Node)
		{
			if (++AnalyzedNodeCount > MaxAnalyzedNodes)
			{
				OutReason =
					TEXT("the Result consumer ancestry exceeds the bounded execution analysis");
				return false;
			}
			if (const EProofState* State = ProofStates.Find(&Node))
			{
				if (*State == EProofState::Passed)
				{
					return true;
				}
				OutReason = FString::Printf(
					TEXT("the Result consumer ancestry contains execution cycle '%s'"),
					*DescribeNode(&Node));
				return false;
			}
			ProofStates.Add(&Node, EProofState::Visiting);
			if (IsUnsupportedOpaqueNode(Node))
			{
				OutReason = FString::Printf(
					TEXT("the Result consumer ancestry crosses opaque node '%s'"),
					*DescribeNode(&Node));
				return false;
			}
			if (!Node.IsA<UK2Node_BaseAsyncTask>()
				&& !IsExplicitSynchronousExecNode(Node))
			{
				OutReason = FString::Printf(
					TEXT("the Result consumer ancestry crosses unclassified execution node '%s'; callback-capable K2 expansions fail closed"),
					*DescribeNode(&Node));
				return false;
			}

			bool bHasLinkedExecPredecessor = false;
			for (const UEdGraphPin* Input : Node.Pins)
			{
				if (!Input || Input->Direction != EGPD_Input
					|| Input->PinType.PinCategory
						!= UEdGraphSchema_K2::PC_Exec)
				{
					continue;
				}
				for (const UEdGraphPin* Linked : Input->LinkedTo)
				{
					bHasLinkedExecPredecessor = true;
					if (!Linked
						|| Linked->Direction != EGPD_Output
						|| Linked->PinType.PinCategory
							!= UEdGraphSchema_K2::PC_Exec
						|| !Linked->GetOwningNode())
					{
						OutReason =
							TEXT("the Result consumer has an unresolved execution predecessor");
						return false;
					}

					const UEdGraphNode* Predecessor =
						Linked->GetOwningNode();
					if (const UK2Node_CallFunction* LatentCall =
							Cast<UK2Node_CallFunction>(
								Predecessor);
						LatentCall
							&& LatentCall->IsLatentFunction())
					{
						OutReason = FString::Printf(
							TEXT("the Result consumer is rooted after latent callback '%s'"),
							*DescribeNode(LatentCall));
						return false;
					}
					if (const UK2Node_BaseAsyncTask* Async =
							Cast<UK2Node_BaseAsyncTask>(
								Predecessor))
					{
						if (Linked->PinName
							== UEdGraphSchema_K2::PN_Then)
						{
							if (Async == &ResultSource)
							{
								OutReason =
									TEXT("the Result consumer is reachable from the source async node's Then path before a producing callback");
								return false;
							}
							if (!ProveNode(*Async))
							{
								return false;
							}
							continue;
						}

						if (Async != &ResultSource)
						{
							OutReason = FString::Printf(
								TEXT("the Result consumer is rooted in downstream async callback '%s'"),
								*Linked->PinName.ToString());
							return false;
						}

						switch (ResolveResultCallbackAssignment(
							ResultSource,
							*Linked,
							ResultPin))
						{
						case EResultCallbackAssignment::Assigns:
							bSawAssigningSourceCallback = true;
							break;
						case EResultCallbackAssignment::DoesNotAssign:
							OutReason = FString::Printf(
								TEXT("source callback '%s' does not assign the shared Result temporary"),
								*Linked->PinName.ToString());
							return false;
						case EResultCallbackAssignment::Unresolved:
						default:
							OutReason = FString::Printf(
								TEXT("source callback '%s' cannot be resolved to an exact proxy delegate signature"),
								*Linked->PinName.ToString());
							return false;
						}
						continue;
					}

					if (!ProveNode(*Predecessor))
					{
						return false;
					}
				}
			}

			if (!bHasLinkedExecPredecessor)
			{
				OutReason = FString::Printf(
					TEXT("the Result consumer has unrelated or unresolved execution root '%s'"),
					*DescribeNode(&Node));
				return false;
			}
			ProofStates[&Node] = EProofState::Passed;
			return true;
		};

		if (!ProveNode(Consumer))
		{
			return false;
		}
		if (!bSawAssigningSourceCallback)
		{
			OutReason =
				TEXT("the Result consumer execution cycle has no assigning source callback root");
			return false;
		}
		return true;
	}

	bool HasExplicitContinuationProof(
		const UFunction& Function)
	{
		// Native/externally-authored impure endpoints opt in explicitly with
		// UFUNCTION(meta=(SeinContinuationSafe)). This promises that every
		// future-affecting mutation goes through canonical Sein state.
		if (Function.HasMetaData(SeinContinuationSafeMeta))
		{
			return true;
		}
		const UClass* Owner = Function.GetOwnerClass();
		if (Owner && Owner->HasMetaData(SeinContinuationSafeMeta))
		{
			return true;
		}
		return false;
	}

	bool IsSerializableSelfMember(
		const UBlueprint& Blueprint,
		const UK2Node_Variable& Variable,
		const UEdGraphPin* ValuePin,
		FString& OutReason)
	{
		if (Variable.VariableReference.IsLocalScope())
		{
			OutReason = TEXT("a local/frame variable");
			return false;
		}
		if (!Variable.VariableReference.IsSelfContext())
		{
			OutReason = TEXT("a non-Self object member access");
			return false;
		}

		if (!ValuePin
			|| !SeinDeterminism::IsPinTypeDeterministic(ValuePin->PinType))
		{
			OutReason =
				TEXT("a non-deterministic ability member");
			return false;
		}

		constexpr EPropertyFlags ExcludedFlags =
			CPF_Transient | CPF_DuplicateTransient
			| CPF_NonPIEDuplicateTransient | CPF_SkipSerialization
			| CPF_EditorOnly | CPF_Deprecated;
		const FProperty* Property =
			Variable.GetPropertyForVariableFromSkeleton();
		if (!Property)
		{
			Property = Variable.GetPropertyForVariable();
		}
		if (Property)
		{
			if (FSeinCanonicalStatePropertyPolicy::ShouldSkip(
					*Property))
			{
				OutReason =
					TEXT("an ability member omitted from canonical state");
				return false;
			}
			return true;
		}

		// A newly-added BP member can be present in NewVariables before the
		// skeleton property exists. The compile preflight still has enough
		// information to prove it is a serializable Self member.
		const FName MemberName = Variable.GetVarName();
		const FBPVariableDescription* Description =
			Blueprint.NewVariables.FindByPredicate(
				[MemberName](const FBPVariableDescription& Item)
				{
					return Item.VarName == MemberName;
				});
		if (!Description)
		{
			OutReason = TEXT("an unresolved member/local reference");
			return false;
		}
		if ((Description->PropertyFlags & ExcludedFlags) != CPF_None)
		{
			OutReason =
				TEXT("an ability member omitted from canonical state");
			return false;
		}
		return true;
	}

	const UEdGraphPin* FindVariableSetValuePin(
		const UK2Node_VariableSet& Variable)
	{
		const UEdGraphPin* Pin = Variable.FindPin(Variable.GetVarName());
		return Pin && Pin->Direction == EGPD_Input ? Pin : nullptr;
	}

	class FResultUseAnalysis
	{
	public:
		FResultUseAnalysis(
			const UBlueprint& InBlueprint,
			const UK2Node_BaseAsyncTask& InResultSourceTask,
			const UEdGraphPin& InTrackedResult,
			TArray<FSeinAbilityContinuationFinding>& InFindings,
			TSet<FString>& InFindingKeys)
			: Blueprint(InBlueprint)
			, ResultSourceTask(InResultSourceTask)
			, TrackedResult(InTrackedResult)
			, Findings(InFindings)
			, FindingKeys(InFindingKeys)
		{
		}

		void Analyze()
		{
			TArray<const UEdGraphNode*> DependentNodes;
			TArray<const UEdGraphNode*> Consumers;
			if (!GatherResultExecConsumers(
					TrackedResult,
					DependentNodes,
					Consumers))
			{
				AddFinding(
					&ResultSourceTask,
					&TrackedResult,
					TEXT("the Result data-dependency graph is opaque, unresolved, cyclic, or exceeds the bounded analysis"),
					&ResultSourceTask);
				return;
			}

			for (const UEdGraphNode* Node : DependentNodes)
			{
				if (const UK2Node_VariableSet* Variable =
						Cast<UK2Node_VariableSet>(Node))
				{
					FString MemberReason;
					if (!IsSerializableSelfMember(
							Blueprint,
							*Variable,
							FindVariableSetValuePin(*Variable),
							MemberReason))
					{
						AddFinding(
							Variable,
							FindVariableSetValuePin(*Variable),
							FString::Printf(
								TEXT("Result is promoted into %s"),
								*MemberReason),
							Variable);
					}
				}
				if (const UK2Node_CallFunction* Call =
						Cast<UK2Node_CallFunction>(Node))
				{
					FString Reason;
					if (!ProveCall(*Call, Reason, 0))
					{
						AddFinding(
							Call,
							nullptr,
							MoveTemp(Reason),
							Call);
					}
				}
			}

			for (const UEdGraphNode* Consumer : Consumers)
			{
				if (!Consumer)
				{
					continue;
				}
				FString Reason;
				if (!ProveResultConsumerExecAncestry(
						ResultSourceTask,
						TrackedResult,
						*Consumer,
						Reason))
				{
					AddFinding(
						&ResultSourceTask,
						&TrackedResult,
						MoveTemp(Reason),
						Consumer);
				}
			}
		}

	private:
		const UEdGraph* FindFunctionGraph(FName FunctionName) const
		{
			for (const UEdGraph* Graph : Blueprint.FunctionGraphs)
			{
				if (!Graph)
				{
					continue;
				}
				if (Graph->GetFName() == FunctionName)
				{
					return Graph;
				}
				for (const UEdGraphNode* Node : Graph->Nodes)
				{
					const UK2Node_FunctionEntry* Entry =
						Cast<UK2Node_FunctionEntry>(Node);
					if (Entry
						&& Entry->CustomGeneratedFunctionName
							== FunctionName)
					{
						return Graph;
					}
				}
			}
			return nullptr;
		}

		bool IsSameBlueprintCall(
			const UK2Node_CallFunction& Call,
			FName FunctionName) const
		{
			if (!FindFunctionGraph(FunctionName))
			{
				return false;
			}

			const UFunction* Function = Call.GetTargetFunction();
			if (!Function)
			{
				return Call.FunctionReference.IsSelfContext();
			}
			const UClass* Owner = Function->GetOwnerClass();
			return Owner
				&& (Owner == Blueprint.GeneratedClass
					|| Owner == Blueprint.SkeletonGeneratedClass
					|| Owner->ClassGeneratedBy == &Blueprint);
		}

		bool ProveCall(
			const UK2Node_CallFunction& Call,
			FString& OutReason,
			int32 Depth)
		{
			const UFunction* Function = Call.GetTargetFunction();
			const FName FunctionName = Function
				? Function->GetFName()
				: Call.FunctionReference.GetMemberName();
			if (FunctionName.IsNone())
			{
				OutReason = TEXT("an unresolved function call");
				return false;
			}
			if (Call.IsLatentFunction())
			{
				OutReason = FString::Printf(
					TEXT("latent function '%s' is a continuation boundary even when marked SeinContinuationSafe"),
					*FunctionName.ToString());
				return false;
			}
			if (Call.IsNodePure())
			{
				return true;
			}

			if (IsSameBlueprintCall(Call, FunctionName))
			{
				const UEdGraph* Graph =
					FindFunctionGraph(FunctionName);
				return Graph
					&& ProveFunctionGraph(
						FunctionName, *Graph, OutReason, Depth + 1);
			}

			if (Function
				&& HasExplicitContinuationProof(*Function))
			{
				return true;
			}

			OutReason = FString::Printf(
				TEXT("an opaque impure native/external function '%s' without explicit SeinContinuationSafe proof"),
				*FunctionName.ToString());
			return false;
		}

		bool ProveFunctionGraph(
			FName FunctionName,
			const UEdGraph& Graph,
			FString& OutReason,
			int32 Depth)
		{
			if (ProvenFunctions.Contains(FunctionName))
			{
				return true;
			}
			if (const FString* Failure =
					FailedFunctions.Find(FunctionName))
			{
				OutReason = *Failure;
				return false;
			}
			if (Depth > MaxFunctionProofDepth
				|| ActiveFunctions.Contains(FunctionName))
			{
				OutReason = FString::Printf(
					TEXT("a cyclic/over-deep same-Blueprint helper '%s'"),
					*FunctionName.ToString());
				FailedFunctions.Add(FunctionName, OutReason);
				return false;
			}
			if (Graph.Nodes.Num() > MaxAnalyzedNodes)
			{
				OutReason = FString::Printf(
					TEXT("same-Blueprint helper '%s' exceeds the bounded analysis limit"),
					*FunctionName.ToString());
				FailedFunctions.Add(FunctionName, OutReason);
				return false;
			}

			ActiveFunctions.Add(FunctionName);
			ON_SCOPE_EXIT { ActiveFunctions.Remove(FunctionName); };
			TArray<const UEdGraphNode*> Nodes;
			Nodes.Reserve(Graph.Nodes.Num());
			for (const UEdGraphNode* Node : Graph.Nodes)
			{
				if (Node)
				{
					Nodes.Add(Node);
				}
			}
			Nodes.Sort(
				[](const UEdGraphNode& A, const UEdGraphNode& B)
				{
					if (A.NodeGuid != B.NodeGuid)
					{
						return A.NodeGuid < B.NodeGuid;
					}
					return A.GetPathName() < B.GetPathName();
				});
			for (const UEdGraphNode* Node : Nodes)
			{
				if (IsUnsupportedOpaqueNode(*Node)
					|| Node->IsA<UK2Node_TemporaryVariable>()
					|| Node->IsA<UK2Node_BaseAsyncTask>())
				{
					OutReason = FString::Printf(
						TEXT("same-Blueprint helper '%s' contains an opaque/latent frame shape"),
						*FunctionName.ToString());
					FailedFunctions.Add(FunctionName, OutReason);
					return false;
				}

				if (const UK2Node_VariableSet* Variable =
						Cast<UK2Node_VariableSet>(Node))
				{
					FString MemberReason;
					if (!IsSerializableSelfMember(
							Blueprint,
							*Variable,
							FindVariableSetValuePin(*Variable),
							MemberReason))
					{
						OutReason = FString::Printf(
							TEXT("same-Blueprint helper '%s' writes %s"),
							*FunctionName.ToString(),
							*MemberReason);
						FailedFunctions.Add(FunctionName, OutReason);
						return false;
					}
				}

				if (const UK2Node_CallFunction* Nested =
						Cast<UK2Node_CallFunction>(Node))
				{
					FString NestedReason;
					if (!ProveCall(*Nested, NestedReason, Depth))
					{
						OutReason = FString::Printf(
							TEXT("same-Blueprint helper '%s' contains %s"),
							*FunctionName.ToString(),
							*NestedReason);
						FailedFunctions.Add(FunctionName, OutReason);
						return false;
					}
				}
			}

			ProvenFunctions.Add(FunctionName);
			return true;
		}

		void AddFinding(
			const UEdGraphNode* SourceNode,
			const UEdGraphPin* SourcePin,
			FString Reason,
			const UEdGraphNode* Consumer)
		{
			const FString Key = FString::Printf(
				TEXT("%s|%s|%s|%s"),
				*ResultSourceTask.NodeGuid.ToString(
					EGuidFormats::Digits),
				Consumer
					? *Consumer->NodeGuid.ToString(
						EGuidFormats::Digits)
					: TEXT("none"),
				SourceNode
					? *SourceNode->NodeGuid.ToString(EGuidFormats::Digits)
					: TEXT("none"),
				SourcePin ? *SourcePin->PinName.ToString() : TEXT("none"));
			if (FindingKeys.Contains(Key))
			{
				return;
			}
			FindingKeys.Add(Key);
			Findings.Add({
				&ResultSourceTask,
				Consumer,
				SourceNode,
				FName(TEXT("Result use")),
				SourcePin ? SourcePin->PinName : NAME_None,
				MoveTemp(Reason)
			});
		}

		const UBlueprint& Blueprint;
		const UK2Node_BaseAsyncTask& ResultSourceTask;
		const UEdGraphPin& TrackedResult;
		TArray<FSeinAbilityContinuationFinding>& Findings;
		TSet<FString>& FindingKeys;
		TSet<FName> ProvenFunctions;
		TMap<FName, FString> FailedFunctions;
		TSet<FName> ActiveFunctions;
	};

	struct FContinuationGraphScan
	{
		const UEdGraph* Graph = nullptr;
		/** The source-Blueprint node through which a nested graph is reached. */
		const UEdGraphNode* AttributionNode = nullptr;
	};

	void AddBoundaryFinding(
		ESeinAbilityContinuationFindingKind Kind,
		const UEdGraphNode* ReportNode,
		const UEdGraphNode* InternalNode,
		FString Reason,
		TArray<FSeinAbilityContinuationFinding>& Findings,
		TSet<FString>& FindingKeys)
	{
		const UEdGraphNode* Node = ReportNode ? ReportNode : InternalNode;
		const FString Key = FString::Printf(
			TEXT("%u|%s|%s"),
			static_cast<uint8>(Kind),
			Node ? *Node->GetPathName() : TEXT("none"),
			InternalNode
				? *InternalNode->GetPathName() : TEXT("none"));
		if (FindingKeys.Contains(Key))
		{
			return;
		}
		FindingKeys.Add(Key);

		if (ReportNode && InternalNode && ReportNode != InternalNode)
		{
			Reason += FString::Printf(
				TEXT(" (expanded node '%s' in '%s')"),
				*DescribeNode(InternalNode),
				*InternalNode->GetPathName());
		}
		FSeinAbilityContinuationFinding& Finding = Findings.AddDefaulted_GetRef();
		Finding.AsyncNode = Node;
		Finding.SourceNode = Node;
		Finding.Reason = MoveTemp(Reason);
		Finding.Kind = Kind;
	}

	void AnalyzeCheckpointBoundaries(
		const UBlueprint& Blueprint,
		TArray<FSeinAbilityContinuationFinding>& Findings,
		TSet<FString>& FindingKeys)
	{
		TArray<FContinuationGraphScan> Queue;
		auto AddRootGraphs = [&Queue](const TArray<TObjectPtr<UEdGraph>>& Graphs)
		{
			for (const UEdGraph* Graph : Graphs)
			{
				if (Graph)
				{
					Queue.Add({Graph, nullptr});
				}
			}
		};
		AddRootGraphs(Blueprint.UbergraphPages);
		AddRootGraphs(Blueprint.FunctionGraphs);
		AddRootGraphs(Blueprint.DelegateSignatureGraphs);

		TSet<FString> VisitedGraphs;
		int32 AnalyzedNodes = 0;
		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			const FContinuationGraphScan& Scan = Queue[QueueIndex];
			if (!Scan.Graph)
			{
				continue;
			}
			const FString VisitKey = FString::Printf(
				TEXT("%s|%s"),
				*Scan.Graph->GetPathName(),
				Scan.AttributionNode
					? *Scan.AttributionNode->GetPathName() : TEXT("root"));
			if (VisitedGraphs.Contains(VisitKey))
			{
				continue;
			}
			VisitedGraphs.Add(VisitKey);

			TArray<const UEdGraphNode*> Nodes;
			Nodes.Reserve(Scan.Graph->Nodes.Num());
			for (const UEdGraphNode* Node : Scan.Graph->Nodes)
			{
				if (Node)
				{
					Nodes.Add(Node);
				}
			}
			Nodes.Sort(
				[](const UEdGraphNode& A, const UEdGraphNode& B)
				{
					return A.NodeGuid != B.NodeGuid
						? A.NodeGuid < B.NodeGuid
						: A.GetPathName() < B.GetPathName();
				});

			for (const UEdGraphNode* Node : Nodes)
			{
				if (++AnalyzedNodes > MaxAnalyzedNodes)
				{
					AddBoundaryFinding(
						ESeinAbilityContinuationFindingKind::
							UninspectableContinuationGraph,
						Scan.AttributionNode,
						Node,
						TEXT("the ability's expanded continuation graph exceeds the bounded checkpoint analysis"),
						Findings,
						FindingKeys);
					return;
				}

				const UEdGraphNode* ReportNode =
					Scan.AttributionNode ? Scan.AttributionNode : Node;
				if (const UK2Node_BaseAsyncTask* Async =
						Cast<UK2Node_BaseAsyncTask>(Node))
				{
					FString Reason;
					if (!IsCheckpointSupportedAsync(*Async, &Reason))
					{
						AddBoundaryFinding(
							ESeinAbilityContinuationFindingKind::
								UnsupportedAsyncBoundary,
							ReportNode,
							Node,
							MoveTemp(Reason),
							Findings,
							FindingKeys);
					}
				}
				if (const UK2Node_CallFunction* Call =
						Cast<UK2Node_CallFunction>(Node);
					Call && Call->IsLatentFunction())
				{
					const UFunction* Function = Call->GetTargetFunction();
					AddBoundaryFinding(
						ESeinAbilityContinuationFindingKind::
							UnsupportedLatentFunction,
						ReportNode,
						Node,
						FString::Printf(
							TEXT("UE latent function '%s' stores resume state outside the Sein checkpoint"),
							Function
								? *Function->GetPathName()
								: *DescribeNode(Call)),
						Findings,
						FindingKeys);
				}
				if (Node->IsA<UK2Node_Timeline>())
				{
					AddBoundaryFinding(
						ESeinAbilityContinuationFindingKind::
							UnsupportedTimeline,
						ReportNode,
						Node,
						TEXT("Blueprint Timeline playback state is outside the Sein checkpoint"),
						Findings,
						FindingKeys);
				}

				const UEdGraphNode* NestedAttribution =
					Scan.AttributionNode ? Scan.AttributionNode : Node;
				if (const UK2Node_MacroInstance* Macro =
						Cast<UK2Node_MacroInstance>(Node))
				{
					if (const UEdGraph* MacroGraph = Macro->GetMacroGraph())
					{
						Queue.Add({MacroGraph, NestedAttribution});
					}
				}
				for (const UEdGraph* SubGraph : Node->GetSubGraphs())
				{
					if (SubGraph)
					{
						Queue.Add({SubGraph, NestedAttribution});
					}
				}
			}
		}
	}
}

FString FSeinAbilityContinuationFinding::ToDiagnostic() const
{
	if (Kind != ESeinAbilityContinuationFindingKind::
			UnsafeMoveToResultResidue)
	{
		return FString::Printf(
			TEXT("[SEIN-CHECKPOINT-CONTINUATION] Unsupported checkpoint continuation at '%s' (%s). Ability Blueprints may cross time only through an exact admitted Sein async factory whose declared latent-action class has a registered checkpoint codec. Persist ordinary values in deterministic ability/component state; replace UE latent, Timeline, or unregistered async work with a checkpoint-aware Sein action."),
			*DescribeNode(AsyncNode),
			*Reason);
	}

	const FString SourceLabel = SourcePin.IsNone()
		? DescribeNode(SourceNode)
		: FString::Printf(
			TEXT("%s @ %s"),
			*SourcePin.ToString(),
			*DescribeNode(SourceNode));
	return FString::Printf(
		TEXT("[SEIN-MOVETO-CONTINUATION] Async continuation callback '%s' at '%s' uses unsafe source '%s' from '%s' (%s). The Move To checkpoint contract omits FSeinMoveToResult compiler-frame temporaries. Persist future-needed values in deterministic ability member/component state before any downstream async boundary, then read or recompute them in the later callback."),
		*CallbackPin.ToString(),
		*DescribeNode(CallbackNode),
		*SourceLabel,
		*DescribeNode(AsyncNode),
		*Reason);
}

bool FSeinAbilityContinuationAnalysis::IsAbilityBlueprint(
	const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return false;
	}
	const UClass* AbilityClass = USeinAbility::StaticClass();
	return (Blueprint->GeneratedClass
			&& Blueprint->GeneratedClass->IsChildOf(AbilityClass))
		|| (Blueprint->SkeletonGeneratedClass
			&& Blueprint->SkeletonGeneratedClass->IsChildOf(AbilityClass))
		|| (Blueprint->ParentClass
			&& Blueprint->ParentClass->IsChildOf(AbilityClass));
}

void FSeinAbilityContinuationAnalysis::Analyze(
	const UBlueprint& Blueprint,
	TArray<FSeinAbilityContinuationFinding>& OutFindings)
{
	OutFindings.Reset();
	if (!IsAbilityBlueprint(&Blueprint))
	{
		return;
	}

	TArray<UK2Node_BaseAsyncTask*> AsyncNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass(&Blueprint, AsyncNodes);
	AsyncNodes.RemoveAll(
		[](const UK2Node_BaseAsyncTask* Node)
		{
			return !Node
				|| !CanProduceOmittedMoveResultResidue(*Node);
		});
	AsyncNodes.Sort(
		[](const UK2Node_BaseAsyncTask& A,
			const UK2Node_BaseAsyncTask& B)
		{
			const UEdGraph* GraphA = A.GetGraph();
			const UEdGraph* GraphB = B.GetGraph();
			const FString PathA = GraphA ? GraphA->GetPathName() : FString();
			const FString PathB = GraphB ? GraphB->GetPathName() : FString();
			if (PathA != PathB)
			{
				return PathA < PathB;
			}
			return A.NodeGuid < B.NodeGuid;
		});

	TSet<FString> FindingKeys;
	AnalyzeCheckpointBoundaries(
		Blueprint,
		OutFindings,
		FindingKeys);
	for (const UK2Node_BaseAsyncTask* AsyncNode : AsyncNodes)
	{
		const UEdGraphPin* ResultPin =
			FindMoveResultResiduePin(*AsyncNode);
		check(ResultPin);
		FResultUseAnalysis(
			Blueprint,
			*AsyncNode,
			*ResultPin,
			OutFindings,
			FindingKeys)
			.Analyze();
	}

	OutFindings.Sort(
		[](const FSeinAbilityContinuationFinding& A,
			const FSeinAbilityContinuationFinding& B)
		{
			const FGuid AAsync =
				A.AsyncNode ? A.AsyncNode->NodeGuid : FGuid();
			const FGuid BAsync =
				B.AsyncNode ? B.AsyncNode->NodeGuid : FGuid();
			if (AAsync != BAsync)
			{
				return AAsync < BAsync;
			}
			if (A.CallbackPin != B.CallbackPin)
			{
				return A.CallbackPin.LexicalLess(B.CallbackPin);
			}
			const FGuid ACallback =
				A.CallbackNode
					? A.CallbackNode->NodeGuid : FGuid();
			const FGuid BCallback =
				B.CallbackNode
					? B.CallbackNode->NodeGuid : FGuid();
			if (ACallback != BCallback)
			{
				return ACallback < BCallback;
			}
			const FGuid ASource =
				A.SourceNode ? A.SourceNode->NodeGuid : FGuid();
			const FGuid BSource =
				B.SourceNode ? B.SourceNode->NodeGuid : FGuid();
			return ASource < BSource;
		});
}
