/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentManifestCommandlet.h
 * @brief   CI-safe headless simulation-content manifest generation.
 */

#pragma once

#include "Commandlets/Commandlet.h"
#include "SeinSimulationContentManifestCommandlet.generated.h"

UCLASS()
class USeinSimulationContentManifestCommandlet final
	: public UCommandlet
{
	GENERATED_BODY()

public:
	USeinSimulationContentManifestCommandlet();

	virtual int32 Main(const FString& Params) override;
};
