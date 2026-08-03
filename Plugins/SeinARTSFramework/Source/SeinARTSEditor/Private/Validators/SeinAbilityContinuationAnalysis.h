#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraphNode;

enum class ESeinAbilityContinuationFindingKind : uint8
{
	UnsafeMoveToResultResidue,
	UnsupportedAsyncBoundary,
	UnsupportedLatentFunction,
	UnsupportedTimeline,
	UninspectableContinuationGraph
};

/**
 * One fail-closed checkpoint-authoring finding. This covers both the specific
 * Move To compiler-frame Result liveness proof and unsupported Blueprint
 * continuation mechanisms whose state is absent from Sein checkpoints.
 *
 * The analysis is editor-private but shared by the real compiler gate and the
 * asset validator. Keeping both callers on this record prevents save-time
 * validation from drifting away from compile-time behavior.
 */
struct FSeinAbilityContinuationFinding
{
	const UEdGraphNode* AsyncNode = nullptr;
	const UEdGraphNode* CallbackNode = nullptr;
	const UEdGraphNode* SourceNode = nullptr;
	FName CallbackPin;
	FName SourcePin;
	FString Reason;
	ESeinAbilityContinuationFindingKind Kind =
		ESeinAbilityContinuationFindingKind::UnsafeMoveToResultResidue;

	FString ToDiagnostic() const;
};

class FSeinAbilityContinuationAnalysis
{
public:
	static bool IsAbilityBlueprint(const UBlueprint* Blueprint);

	/** Analyze source graphs only; never expands or mutates the Blueprint. */
	static void Analyze(
		const UBlueprint& Blueprint,
		TArray<FSeinAbilityContinuationFinding>& OutFindings);
};
