/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesTypes.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements provider-neutral SOS value helpers.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Types/SeinOnlineServicesTypes.h"

FString FSeinOnlineOpaqueID::ToCanonicalString() const
{
	return Provider.ToString() + TEXT(":") + Value;
}

FSeinOnlineError FSeinOnlineError::Make(
	ESeinOnlineErrorCode InCode,
	FString InMessage,
	bool bInRetryable)
{
	FSeinOnlineError Result;
	Result.Code = InCode;
	Result.Message = MoveTemp(InMessage);
	Result.bRetryable = bInRetryable;
	return Result;
}
