/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinConsumerDependencyAuditCommandlet.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Audits consumer assets without treating authoring history as a dependency.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "Commandlets/Commandlet.h"
#include "SeinConsumerDependencyAuditCommandlet.generated.h"

UCLASS()
class USeinConsumerDependencyAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	USeinConsumerDependencyAuditCommandlet();
	virtual int32 Main(const FString& Params) override;
};
