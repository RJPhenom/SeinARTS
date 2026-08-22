/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesContract.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares the frozen SOS operation-to-schema registry and validation API.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/SeinOnlineServicesTypes.h"

namespace SeinOnlineContract
{
	/** Returns the exact request struct required by Operation, or null for None. */
	SEINARTSONLINESERVICES_API const UScriptStruct* GetRequestStruct(
		ESeinOnlineOperation Operation);

	/** Returns the exact successful result struct required by Operation. */
	SEINARTSONLINESERVICES_API const UScriptStruct* GetResultStruct(
		ESeinOnlineOperation Operation);

	/** Returns a stable diagnostic name for an operation. */
	SEINARTSONLINESERVICES_API FString GetOperationName(
		ESeinOnlineOperation Operation);

	/** Validates envelope revision, schema, bounds, and operation invariants. */
	SEINARTSONLINESERVICES_API bool ValidateRequest(
		const FSeinOnlineProviderRequest& Request,
		FSeinOnlineError& OutError);

	/** Validates response identity and its exact success/failure schema. */
	SEINARTSONLINESERVICES_API bool ValidateResponse(
		const FSeinOnlineProviderRequest& Request,
		const FSeinOnlineProviderResponse& Response,
		FSeinOnlineError& OutError);

	/** True when an operation requires a non-empty idempotency key. */
	SEINARTSONLINESERVICES_API bool IsDurableMutation(
		ESeinOnlineOperation Operation);
}
