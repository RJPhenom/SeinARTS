/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSnapshotTransfer.cpp
 * @author       RJ Macklem
 * @created      30 Jul 2026
 * @latest       12 Aug 2026
 * @brief        Encodes and decodes versioned deterministic snapshot envelopes.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Serialization/SeinSnapshotTransfer.h"

#include "Serialization/SeinSnapshotTransferTestHooks.h"

#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace SeinSnapshotTransfer
{
	const TCHAR* const CheckpointSectionId =
		TEXT("seinarts.net/checkpoint");

	namespace
	{
		/** Deterministic constant binding the section to the exact snapshot
		 *  wire schema. Version participates so a future v15 cannot alias. */
		bool ComputeCheckpointSchemaDigest(
			FGuid& OutDigest, FString& OutError)
		{
			FSeinCanonicalDigestWriter Writer(
				TEXT("SeinARTS.Net.CheckpointSection.Schema"), 1);
			if (!Writer.WriteInt32(FSeinWorldSnapshot::CurrentVersion))
			{
				OutError = Writer.GetError();
				return false;
			}
			return Writer.Finalize(OutDigest, OutError);
		}

		bool ComputeCheckpointDescriptorDigest(
			FGuid& OutDigest, FString& OutError)
		{
			FSeinCanonicalDigestWriter Writer(
				TEXT("SeinARTS.Net.CheckpointSection.Descriptor"), 1);
			if (!Writer.WriteString(CheckpointSectionId)
				|| !Writer.WriteInt32(FSeinWorldSnapshot::CurrentVersion))
			{
				OutError = Writer.GetError();
				return false;
			}
			return Writer.Finalize(OutDigest, OutError);
		}

		bool EncodeCheckpointEnvelopeInternal(
			const FSeinWorldSnapshot& Snapshot,
			TArray<uint8>& OutBytes,
			FSeinSnapshotEnvelopeMetadata& OutMetadata,
			FString& OutError,
			TFunctionRef<bool(FString&)> AfterPayloadSerialized)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(
				Sein_SnapshotTransfer_EncodeCheckpoint);
			OutError.Reset();
			if (Snapshot.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion)
			{
				OutError =
					TEXT("Checkpoint transfer requires a freshly captured current-version snapshot.");
				return false;
			}

			FSeinSnapshotEnvelopeSection Section;
			Section.SectionId = CheckpointSectionId;
			Section.Role = ESeinSnapshotSectionRole::Authoritative;
			Section.Codec = ESeinSnapshotSectionCodec::CanonicalBytes;
			Section.SchemaVersion =
				static_cast<uint32>(FSeinWorldSnapshot::CurrentVersion);
			if (!ComputeCheckpointSchemaDigest(Section.SchemaDigest, OutError)
				|| !ComputeCheckpointDescriptorDigest(
					Section.DescriptorDigest, OutError))
			{
				return false;
			}

			FSeinWorldSnapshotReferenceGuard SnapshotGCGuard(Snapshot);
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(
					Sein_SnapshotTransfer_SerializePayload);
				FMemoryWriter MemWriter(Section.Payload, /*bIsPersistent*/ true);
				FObjectAndNameAsStringProxyArchive Writer(
					MemWriter, /*bInLoadIfFindFails*/ false);
				FSeinWorldSnapshot::StaticStruct()->SerializeItem(
					Writer,
					// SerializeItem is non-const by signature; saving does not mutate.
					const_cast<FSeinWorldSnapshot*>(&Snapshot),
					nullptr);
				if (Writer.IsError() || Writer.IsCriticalError()
					|| MemWriter.IsError() || MemWriter.IsCriticalError()
					|| MemWriter.Tell() != Section.Payload.Num())
				{
					OutError =
						TEXT("Checkpoint payload serialization failed; no envelope was produced.");
					return false;
				}
			}
			if (!AfterPayloadSerialized(OutError))
			{
				return false;
			}

			FSeinSnapshotEnvelope Envelope;
			Envelope.SnapshotTick = Snapshot.CurrentTick;
			Envelope.CommandProtocolDigest = Snapshot.CommandProtocolDigest;
			Envelope.CompatibilityDigest =
				Snapshot.BootstrapCheckpoint.Receipt.StateContractDigest;
			Envelope.Sections.Add(MoveTemp(Section));
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(
					Sein_SnapshotTransfer_FrameEnvelope);
				return FSeinSnapshotEnvelopeCodec::Encode(
					Envelope, OutBytes, OutMetadata, OutError);
			}
		}
	}

	bool EncodeCheckpointEnvelope(
		const FSeinWorldSnapshot& Snapshot,
		TArray<uint8>& OutBytes,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError)
	{
		return EncodeCheckpointEnvelopeInternal(
			Snapshot,
			OutBytes,
			OutMetadata,
			OutError,
			[](FString&) { return true; });
	}

#if WITH_DEV_AUTOMATION_TESTS
	bool EncodeCheckpointEnvelopeWithMidpointForTests(
		const FSeinWorldSnapshot& Snapshot,
		TArray<uint8>& OutBytes,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError,
		TFunctionRef<bool(FString&)> AfterPayloadSerialized)
	{
		return EncodeCheckpointEnvelopeInternal(
			Snapshot,
			OutBytes,
			OutMetadata,
			OutError,
			AfterPayloadSerialized);
	}
#endif

	bool DecodeCheckpointEnvelope(
		TConstArrayView<uint8> Bytes,
		FSeinWorldSnapshot& OutSnapshot,
		FSeinSnapshotEnvelopeMetadata& OutMetadata,
		FString& OutError)
	{
		OutError.Reset();
		FSeinSnapshotEnvelope Envelope;
		FSeinSnapshotEnvelopeMetadata Metadata;
		if (!FSeinSnapshotEnvelopeCodec::Decode(
			Bytes, Envelope, Metadata, OutError))
		{
			return false;
		}
		if (Envelope.Sections.Num() != 1)
		{
			OutError =
				TEXT("A checkpoint transfer envelope must frame exactly one section.");
			return false;
		}
		const FSeinSnapshotEnvelopeSection& Section = Envelope.Sections[0];
		FGuid ExpectedSchemaDigest;
		FGuid ExpectedDescriptorDigest;
		if (!ComputeCheckpointSchemaDigest(ExpectedSchemaDigest, OutError)
			|| !ComputeCheckpointDescriptorDigest(
				ExpectedDescriptorDigest, OutError))
		{
			return false;
		}
		if (Section.SectionId != CheckpointSectionId
			|| Section.Role != ESeinSnapshotSectionRole::Authoritative
			|| Section.Codec != ESeinSnapshotSectionCodec::CanonicalBytes
			|| Section.SchemaVersion
				!= static_cast<uint32>(FSeinWorldSnapshot::CurrentVersion)
			|| Section.SchemaDigest != ExpectedSchemaDigest
			|| Section.DescriptorDigest != ExpectedDescriptorDigest)
		{
			OutError =
				TEXT("The checkpoint section's identity or schema binding does not match this build's exact checkpoint contract.");
			return false;
		}

		FSeinWorldSnapshot Decoded;
		FSeinWorldSnapshotReferenceGuard DecodedGCGuard(Decoded);
		FMemoryReader MemReader(Section.Payload, /*bIsPersistent*/ true);
		FObjectAndNameAsStringProxyArchive Reader(
			MemReader, /*bInLoadIfFindFails*/ true);
		FSeinWorldSnapshot::StaticStruct()->SerializeItem(
			Reader, &Decoded, nullptr);
		if (Reader.IsError() || Reader.IsCriticalError()
			|| MemReader.IsError() || MemReader.IsCriticalError()
			|| MemReader.Tell() != Section.Payload.Num())
		{
			OutError =
				TEXT("Checkpoint payload deserialization failed or left trailing bytes.");
			return false;
		}
		if (Decoded.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion
			|| Decoded.CurrentTick != Envelope.SnapshotTick
			|| Decoded.CommandProtocolDigest
				!= Envelope.CommandProtocolDigest
			|| Decoded.BootstrapCheckpoint.Receipt.StateContractDigest
				!= Envelope.CompatibilityDigest)
		{
			OutError =
				TEXT("The decoded checkpoint contradicts its own envelope prefix (tick or compatibility digests).");
			return false;
		}

		OutSnapshot = MoveTemp(Decoded);
		OutMetadata = Metadata;
		return true;
	}
}
