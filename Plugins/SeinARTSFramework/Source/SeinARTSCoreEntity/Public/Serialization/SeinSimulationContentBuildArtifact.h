/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinSimulationContentBuildArtifact.h
 * @author       RJ Macklem
 * @created      6 Sep 2026
 * @latest       6 Sep 2026
 * @brief        Bounded cooked compatibility evidence, independent of source asset settings.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once

#include "Serialization/SeinSimulationContentManifest.h"

class SEINARTSCOREENTITY_API FSeinSimulationContentBuildArtifact
{
public:
	static constexpr int64 MaxBytes = 64 * 1024 * 1024;
	static const TCHAR* RelativeFilename() { return TEXT("SeinARTS/SimulationCompatibility.bin"); }
	static bool Encode(const FSeinSimulationContentManifestProfile& Profile,
		TArray<uint8>& OutBytes, FString& OutError);
	static bool Decode(TConstArrayView<uint8> Bytes,
		FSeinSimulationContentManifestProfile& OutProfile, FString& OutError);
	static bool Load(const FString& Filename,
		FSeinSimulationContentManifestProfile& OutProfile, FString& OutError);
};
