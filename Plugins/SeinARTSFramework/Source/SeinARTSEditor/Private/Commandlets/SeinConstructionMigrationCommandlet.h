/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConstructionMigrationCommandlet.h
 * @author       RJ Macklem
 * @created      11 Sep 2026
 * @latest       11 Sep 2026
 * @brief        Audits and migrates the demo construction graphs through native editor APIs.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "Commandlets/Commandlet.h"
#include "SeinConstructionMigrationCommandlet.generated.h"

UCLASS()
class USeinConstructionMigrationCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USeinConstructionMigrationCommandlet();
	virtual int32 Main(const FString& Params) override;
};
