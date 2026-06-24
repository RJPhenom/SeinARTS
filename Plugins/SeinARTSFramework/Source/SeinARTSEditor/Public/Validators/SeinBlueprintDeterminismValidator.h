/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBlueprintDeterminismValidator.h
 * @brief   Shared base for determinism validators over SIM Blueprint graphs (movement modes,
 *          formations, ...). Walks the BP's function-call nodes — directly AND recursively through any
 *          macro instances — and flags any call that isn't whitelisted as deterministic: the target (or
 *          its owning class) carries the `SeinDeterministic` meta, OR its whole signature is
 *          deterministic-typed (with an explicit denylist for stateful-but-deterministic-signature calls
 *          like unseeded engine RNG). A non-deterministic node in a sim graph desyncs lockstep (and, for
 *          formations, splits preview from commit).
 *
 *          Findings are warnings by default; a subclass may opt into blocking errors (ShouldEscalateToError).
 *          Auto-gathered at editor start (UEditorValidatorBase); runs on save and on "Validate Assets".
 *          Subclasses declare WHICH blueprints they cover (IsTargetBlueprint), a label + toolkit hint for
 *          the message, and whether to escalate. `UCLASS(Abstract)` so the Data Validation system gathers
 *          only the concrete subclasses, not this base.
 */

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "SeinBlueprintDeterminismValidator.generated.h"

class UBlueprint;

UCLASS(Abstract)
class USeinBlueprintDeterminismValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	USeinBlueprintDeterminismValidator();

	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;

protected:
	/** True if this validator covers the given (loaded) blueprint — typically "its parent class is a sim
	 *  base that must stay deterministic." Subclasses implement the scope check. */
	virtual bool IsTargetBlueprint(UBlueprint* Blueprint) const { return false; }

	/** Short label naming the asset kind for the warning text, e.g. "Movement mode" / "Formation". */
	virtual FText GetAssetKindLabel() const;

	/** Toolkit hint for the warning text — the deterministic nodes the author should use instead,
	 *  e.g. "Sein Mover Handle" / "formation toolkit". */
	virtual FText GetToolkitHintText() const;

	/** When true, findings are reported as blocking errors (AssetFails) rather than warnings — a team can
	 *  enforce lockstep-safety once its graphs are clean. Default false (warn only). */
	virtual bool ShouldEscalateToError() const { return false; }
};
