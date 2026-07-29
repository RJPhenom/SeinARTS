/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinReplayWireCodec.h
 * @brief Non-reflected bounded replay-body serialization.
 */

#pragma once

#include "CoreMinimal.h"
#include "Data/SeinReplayTurn.h"
#include "Input/SeinCommandWireCodec.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "SeinReplayFormat.h"

class SEINARTSNET_API FSeinReplayWireCodec
{
public:
	/**
	 * Conservative decoded-memory ceiling. Four times the 64 MiB body cap
	 * leaves bounded room for array/object layout and reflected scratch/copy
	 * duplication. Encode and decode use the same accounting.
	 */
	static constexpr uint64 DecodedAllocationExpansionFactor = 4;
	static constexpr uint64 MaxDecodedAllocationBytes =
		SeinReplayFormat::MaxBodyBytes * DecodedAllocationExpansionFactor;

	static bool Encode(
		const FSeinReplay& Replay,
		FSeinStructWireCatalogView HeaderCatalog,
		FSeinCommandWireSchemaLookup FindSchema,
		TArray<uint8>& OutBody,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);

	static bool Decode(
		TConstArrayView<uint8> Body,
		FSeinStructWireCatalogView HeaderCatalog,
		FSeinCommandWireSchemaLookup FindSchema,
		FSeinReplay& OutReplay,
		FString& OutError,
		uint64* OutDecodedAllocationBytes = nullptr);
};
