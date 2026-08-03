/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentEditorGuards.h
 * @brief   Opt-in PIE and mandatory cook admission gates for generated content evidence.
 */

#pragma once

#include "CoreMinimal.h"
#include "IPIEAuthorizer.h"

/** Strict manifest freshness check, disabled by default for normal iteration. */
class FSeinSimulationContentPIEAuthorizer final : public IPIEAuthorizer
{
public:
	virtual bool RequestPIEPermission(
		bool bIsSimulateInEditor,
		FString& OutReason) const override;
};

/** Owns the cook delegate lease and removes it on module shutdown/reload. */
class FSeinSimulationContentCookIntegration final
{
public:
	FSeinSimulationContentCookIntegration();
	~FSeinSimulationContentCookIntegration();

	FSeinSimulationContentCookIntegration(
		const FSeinSimulationContentCookIntegration&) = delete;
	FSeinSimulationContentCookIntegration& operator=(
		const FSeinSimulationContentCookIntegration&) = delete;

private:
	FDelegateHandle ModifyCookHandle;
};
