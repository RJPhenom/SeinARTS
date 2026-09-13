/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinSimulationContentEditorGuards.cpp
 * @author       RJ Macklem
 * @created      29 Jul 2026
 * @latest       7 Sep 2026
 * @brief        Applies saved-content validation only to explicitly strict editor sessions.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */

#include "Util/SeinSimulationContentEditorGuards.h"

#include "Serialization/SeinSimulationContentManifest.h"
#include "Util/SeinSimulationContentManifestBuilder.h"

TValueOrError<bool, FText> FSeinSimulationContentPIEAuthorizer::
	IsPIEAuthorizedInternal(bool /*bIsSimulateInEditor*/) const
{
	if (FSeinSimulationContentManifestCodec::UsesEditorSessionCompatibility())
	{
		return MakeValue(true);
	}
	FSeinSimulationContentManifestBuildResult Result;
	FString Error;
	if (FSeinSimulationContentManifestBuilder::ValidateConfiguredManifest(Result, Error))
	{
		return MakeValue(true);
	}
	return MakeError(FText::FromString(FString::Printf(
		TEXT("Strict saved-content compatibility test failed: %s"), *Error)));
}
