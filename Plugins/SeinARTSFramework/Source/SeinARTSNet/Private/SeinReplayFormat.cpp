/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinReplayFormat.cpp
 */

#include "SeinReplayFormat.h"

#include "Serialization/SeinCanonicalInitialStateDigest.h"

bool SeinReplayFormat::ComputeBootstrapAuthorizationContextDigest(
	const FSeinReplayHeader& Header,
	FGuid& OutDigest,
	FString& OutError)
{
	OutDigest.Invalidate();
	OutError.Reset();
	if (!Header.CommandProtocolDigest.IsValid()
		|| !Header.MatchSettingsDigest.IsValid()
		|| !Header.BootstrapReceipt.IsValid()
		|| Header.BootstrapReceipt.ContractDigest
			!= Header.MatchSettingsDigest
		|| Header.FrameworkVersion.IsEmpty()
		|| Header.GameVersion.IsEmpty()
		|| Header.MapIdentifier.IsEmpty())
	{
		OutError = TEXT("Replay bootstrap authorization requires a complete compatibility envelope.");
		return false;
	}

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.ReplayBootstrapAuthorization"), 2);
	Writer.WriteUInt32(FileFormatVersion);
	Writer.WriteGuid(Header.CommandProtocolDigest);
	Writer.WriteGuid(Header.MatchSettingsDigest);
	Writer.WriteInt32(Header.BootstrapReceipt.FormatVersion);
	Writer.WriteGuid(Header.BootstrapReceipt.ContractDigest);
	Writer.WriteGuid(
		Header.BootstrapReceipt.SimulationContentDigest);
	Writer.WriteGuid(Header.BootstrapReceipt.StateContractDigest);
	Writer.WriteGuid(Header.BootstrapReceipt.PlanDigest);
	Writer.WriteGuid(Header.BootstrapReceipt.InitialStateDigest);
	Writer.WriteInt32(Header.ConfigFingerprint);
	Writer.WriteInt64(Header.RandomSeed);
	Writer.WriteString(Header.FrameworkVersion);
	Writer.WriteString(Header.GameVersion);
	Writer.WriteString(Header.MapIdentifier);
	return Writer.Finalize(OutDigest, OutError);
}
