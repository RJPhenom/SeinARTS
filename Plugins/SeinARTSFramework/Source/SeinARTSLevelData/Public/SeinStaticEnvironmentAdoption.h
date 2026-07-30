/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStaticEnvironmentAdoption.h
 * @brief   Shared outcome contract for deterministic static-environment adoption.
 */

#pragma once

#include "CoreMinimal.h"

/**
 * Outcome of asking a deterministic runtime system to adopt one Level Data
 * substrate.
 *
 * NotApplicable is a valid fallback outcome: the substrate or implementation
 * does not provide data for this system. Rejected means data was offered but
 * could not be adopted safely; bootstrap must not freeze that world binding.
 */
enum class ESeinStaticEnvironmentAdoptionOutcome : uint8
{
	NotApplicable,
	Adopted,
	Rejected
};

/**
 * Value result carried from a pluggable implementation through its owning
 * subsystem. Detail is required for Rejected and may explain a valid
 * NotApplicable fallback. Callers must inspect the explicit outcome; there is
 * deliberately no bool conversion that could collapse Rejected and
 * NotApplicable into the same legacy "false" result.
 */
struct SEINARTSLEVELDATA_API FSeinStaticEnvironmentAdoptionResult
{
	FSeinStaticEnvironmentAdoptionResult();
	FSeinStaticEnvironmentAdoptionResult(
		const FSeinStaticEnvironmentAdoptionResult& Other);
	FSeinStaticEnvironmentAdoptionResult(
		FSeinStaticEnvironmentAdoptionResult&& Other);
	~FSeinStaticEnvironmentAdoptionResult();

	FSeinStaticEnvironmentAdoptionResult& operator=(
		const FSeinStaticEnvironmentAdoptionResult& Other);
	FSeinStaticEnvironmentAdoptionResult& operator=(
		FSeinStaticEnvironmentAdoptionResult&& Other);

	ESeinStaticEnvironmentAdoptionOutcome Outcome =
		ESeinStaticEnvironmentAdoptionOutcome::NotApplicable;
	FString Detail;

	static FSeinStaticEnvironmentAdoptionResult Adopted();
	static FSeinStaticEnvironmentAdoptionResult NotApplicable(
		FString InDetail = FString());
	static FSeinStaticEnvironmentAdoptionResult Rejected(FString InReason);

	bool IsAdopted() const;
	bool IsNotApplicable() const;
	bool IsRejected() const;
};
