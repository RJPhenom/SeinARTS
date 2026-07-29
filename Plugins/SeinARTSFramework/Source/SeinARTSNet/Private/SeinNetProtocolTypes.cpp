/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinNetProtocolTypes.cpp
 */

#include "SeinNetProtocolTypes.h"
#include "Engine/World.h"
#include "Hash/Blake3.h"
#include "Misc/PackageName.h"

bool FSeinOpaqueCommandBatch::NetSerialize(
	FArchive& Ar,
	UPackageMap* Map,
	bool& bOutSuccess)
{
	(void)Map;
	uint32 ByteCount = Ar.IsLoading() ? 0u : static_cast<uint32>(Bytes.Num());
	Ar.SerializeIntPacked(ByteCount);
	if (Ar.IsError() || ByteCount > MaxBytes)
	{
		Ar.SetError();
		bOutSuccess = false;
		if (Ar.IsLoading()) Bytes.Reset();
		return false;
	}
	if (Ar.IsLoading())
	{
		// The hard count check above intentionally precedes this allocation.
		Bytes.SetNumUninitialized(static_cast<int32>(ByteCount));
	}
	if (ByteCount > 0)
	{
		Ar.Serialize(Bytes.GetData(), static_cast<int64>(ByteCount));
	}
	bOutSuccess = !Ar.IsError() && !Ar.IsCriticalError();
	if (!bOutSuccess && Ar.IsLoading()) Bytes.Reset();
	return bOutSuccess;
}

namespace
{
	FString GuidToCanonicalString(const FGuid& Guid)
	{
		return Guid.IsValid()
			? Guid.ToString(EGuidFormats::DigitsWithHyphensLower)
			: TEXT("invalid");
	}

	int32 CompareGuids(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A ? -1 : 1;
		if (A.B != B.B) return A.B < B.B ? -1 : 1;
		if (A.C != B.C) return A.C < B.C ? -1 : 1;
		if (A.D != B.D) return A.D < B.D ? -1 : 1;
		return 0;
	}

	void AppendByte(TArray<uint8>& Bytes, uint8 Value)
	{
		Bytes.Add(Value);
	}

	void AppendUInt32(TArray<uint8>& Bytes, uint32 Value)
	{
		AppendByte(Bytes, static_cast<uint8>(Value >> 24));
		AppendByte(Bytes, static_cast<uint8>(Value >> 16));
		AppendByte(Bytes, static_cast<uint8>(Value >> 8));
		AppendByte(Bytes, static_cast<uint8>(Value));
	}

	void AppendUInt64(TArray<uint8>& Bytes, uint64 Value)
	{
		AppendUInt32(Bytes, static_cast<uint32>(Value >> 32));
		AppendUInt32(Bytes, static_cast<uint32>(Value));
	}

	void AppendGuid(TArray<uint8>& Bytes, const FGuid& Value)
	{
		AppendUInt32(Bytes, Value.A);
		AppendUInt32(Bytes, Value.B);
		AppendUInt32(Bytes, Value.C);
		AppendUInt32(Bytes, Value.D);
	}

	uint32 ReadUInt32(const uint8* Bytes)
	{
		return static_cast<uint32>(Bytes[0]) << 24
			| static_cast<uint32>(Bytes[1]) << 16
			| static_cast<uint32>(Bytes[2]) << 8
			| static_cast<uint32>(Bytes[3]);
	}
}

FString FSeinMatchInstanceID::ToCanonicalString() const
{
	return GuidToCanonicalString(Value);
}

FString FSeinNetworkParticipantID::ToCanonicalString() const
{
	return GuidToCanonicalString(Value);
}

bool FSeinProtocolContext::IsValid() const
{
	return ProtocolVersion == CurrentProtocolVersion
		&& MatchInstanceID.IsValid()
		&& LockstepEpoch > 0
		&& CoordinatorParticipantID.IsValid()
		&& CoordinatorTerm > 0
		&& MembershipRevision > 0
		&& MembershipDigest.IsValid()
		&& DestinationWorldDigest.IsValid()
		&& MatchSettingsDigest.IsValid()
		&& SimulationContentDigest.IsValid()
		&& CommandProtocolDigest.IsValid();
}

FGuid SeinComputeDestinationWorldDigest(const FString& WorldPackageName)
{
	FString CanonicalPackage = UWorld::RemovePIEPrefix(
		WorldPackageName.TrimStartAndEnd());
	if (CanonicalPackage.Contains(TEXT(".")))
	{
		CanonicalPackage = FPackageName::ObjectPathToPackageName(
			CanonicalPackage);
	}
	if (!FPackageName::IsValidLongPackageName(CanonicalPackage))
	{
		return FGuid();
	}
	CanonicalPackage.ToLowerInline();

	TArray<uint8> Bytes;
	static constexpr ANSICHAR Domain[] = "SeinARTS.DestinationWorld";
	Bytes.Append(
		reinterpret_cast<const uint8*>(Domain),
		UE_ARRAY_COUNT(Domain) - 1);
	AppendUInt32(Bytes, 2); // framing version
	FTCHARToUTF8 PackageUtf8(*CanonicalPackage);
	AppendUInt32(Bytes, static_cast<uint32>(PackageUtf8.Length()));
	Bytes.Append(
		reinterpret_cast<const uint8*>(PackageUtf8.Get()),
		PackageUtf8.Length());

	const FBlake3Hash Hash = FBlake3::HashBuffer(Bytes.GetData(), Bytes.Num());
	const uint8* HashBytes = Hash.GetBytes();
	FGuid Digest(
		ReadUInt32(HashBytes),
		ReadUInt32(HashBytes + 4),
		ReadUInt32(HashBytes + 8),
		ReadUInt32(HashBytes + 12));
	if (!Digest.IsValid()) Digest.D = 1;
	return Digest;
}

FGuid SeinComputeBootstrapAuthorizationContextDigest(
	const FSeinProtocolContext& Context,
	int64 SessionSeed)
{
	if (!Context.IsValid() || SessionSeed == 0)
	{
		return FGuid();
	}

	TArray<uint8> Bytes;
	static constexpr ANSICHAR Domain[] = "SeinARTS.BootstrapAuthorization";
	Bytes.Append(
		reinterpret_cast<const uint8*>(Domain),
		UE_ARRAY_COUNT(Domain) - 1);
	AppendUInt32(Bytes, 2); // framing version
	AppendUInt32(Bytes, static_cast<uint32>(Context.ProtocolVersion));
	AppendGuid(Bytes, Context.MatchInstanceID.Value);
	AppendUInt64(Bytes, static_cast<uint64>(Context.LockstepEpoch));
	AppendGuid(Bytes, Context.CoordinatorParticipantID.Value);
	AppendUInt64(Bytes, static_cast<uint64>(Context.CoordinatorTerm));
	AppendUInt64(Bytes, static_cast<uint64>(Context.MembershipRevision));
	AppendGuid(Bytes, Context.MembershipDigest);
	AppendGuid(Bytes, Context.DestinationWorldDigest);
	AppendGuid(Bytes, Context.MatchSettingsDigest);
	AppendGuid(Bytes, Context.SimulationContentDigest);
	AppendGuid(Bytes, Context.CommandProtocolDigest);
	AppendUInt64(Bytes, static_cast<uint64>(SessionSeed));

	const FBlake3Hash Hash = FBlake3::HashBuffer(Bytes.GetData(), Bytes.Num());
	const uint8* HashBytes = Hash.GetBytes();
	FGuid Digest(
		ReadUInt32(HashBytes),
		ReadUInt32(HashBytes + 4),
		ReadUInt32(HashBytes + 8),
		ReadUInt32(HashBytes + 12));
	if (!Digest.IsValid()) Digest.D = 1;
	return Digest;
}

FString FSeinProtocolContext::ToCanonicalDebugString() const
{
	return FString::Printf(
		TEXT("protocol=%d match=%s epoch=%lld coordinator=%s/%lld membership=%lld/%s destination=%s settings=%s content=%s commands=%s"),
		ProtocolVersion,
		*MatchInstanceID.ToCanonicalString(),
		static_cast<long long>(LockstepEpoch),
		*CoordinatorParticipantID.ToCanonicalString(),
		static_cast<long long>(CoordinatorTerm),
		static_cast<long long>(MembershipRevision),
		*GuidToCanonicalString(MembershipDigest),
		*GuidToCanonicalString(DestinationWorldDigest),
		*GuidToCanonicalString(MatchSettingsDigest),
		*GuidToCanonicalString(SimulationContentDigest),
		*GuidToCanonicalString(CommandProtocolDigest));
}

bool FSeinParticipantBinding::IsValid() const
{
	if (!ParticipantID.IsValid() || (bReportsWorldRoots && !bSimulates)) return false;

	TSet<FSeinPlayerID> UniqueSlots;
	for (const FSeinPlayerID Slot : CommandSlots)
	{
		if (!Slot.IsValid() || UniqueSlots.Contains(Slot)) return false;
		UniqueSlots.Add(Slot);
	}
	return true;
}

FGuid SeinComputeMembershipDigest(const TArray<FSeinParticipantBinding>& Bindings)
{
	TArray<FSeinParticipantBinding> CanonicalBindings = Bindings;
	CanonicalBindings.Sort([](const FSeinParticipantBinding& A, const FSeinParticipantBinding& B)
	{
		return CompareGuids(A.ParticipantID.Value, B.ParticipantID.Value) < 0;
	});

	TArray<uint8> Bytes;
	Bytes.Reserve(4 + CanonicalBindings.Num() * 21);
	AppendUInt32(Bytes, static_cast<uint32>(CanonicalBindings.Num()));
	for (const FSeinParticipantBinding& Binding : CanonicalBindings)
	{
		AppendUInt32(Bytes, Binding.ParticipantID.Value.A);
		AppendUInt32(Bytes, Binding.ParticipantID.Value.B);
		AppendUInt32(Bytes, Binding.ParticipantID.Value.C);
		AppendUInt32(Bytes, Binding.ParticipantID.Value.D);

		uint8 Capabilities = Binding.bSimulates ? 1u : 0u;
		Capabilities |= Binding.bReportsWorldRoots ? 2u : 0u;
		Capabilities |= Binding.bCanCoordinate ? 4u : 0u;
		Capabilities |= Binding.bCanAdministerMatch ? 8u : 0u;
		AppendByte(Bytes, Capabilities);

		TArray<FSeinPlayerID> Slots = Binding.CommandSlots;
		Slots.Sort([](const FSeinPlayerID A, const FSeinPlayerID B)
		{
			return A.Value < B.Value;
		});
		AppendUInt32(Bytes, static_cast<uint32>(Slots.Num()));
		for (const FSeinPlayerID Slot : Slots) AppendByte(Bytes, Slot.Value);
	}

	const FBlake3Hash Hash = FBlake3::HashBuffer(Bytes.GetData(), Bytes.Num());
	const uint8* HashBytes = Hash.GetBytes();
	FGuid Digest(
		ReadUInt32(HashBytes),
		ReadUInt32(HashBytes + 4),
		ReadUInt32(HashBytes + 8),
		ReadUInt32(HashBytes + 12));
	if (!Digest.IsValid()) Digest.D = 1;
	return Digest;
}

FString FSeinTurnAuthor::ToCanonicalDebugString() const
{
	return FString::Printf(
		TEXT("participant=%s slot=%u"),
		*ParticipantID.ToCanonicalString(),
		CommandSlot.Value);
}

bool FSeinTurnAuthor::CanonicalLess(const FSeinTurnAuthor& A, const FSeinTurnAuthor& B)
{
	if (A.CommandSlot.Value != B.CommandSlot.Value)
	{
		return A.CommandSlot.Value < B.CommandSlot.Value;
	}
	return CompareGuids(A.ParticipantID.Value, B.ParticipantID.Value) < 0;
}
