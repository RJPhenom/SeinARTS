#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraphNode;

/**
 * One fail-closed finding at an async boundary whose compiler-frame Result
 * residue is indistinguishable from Move To's.
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
