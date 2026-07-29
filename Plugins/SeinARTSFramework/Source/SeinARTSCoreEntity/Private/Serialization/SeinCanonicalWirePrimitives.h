/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalWirePrimitives.h
 * @brief   Private byte primitives shared by canonical state and command framing.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "UObject/UnrealType.h"

namespace UE::Sein::CanonicalWirePrivate
{
	constexpr uint64 ArrayElementFrameBytes = 4;

	inline int32 EffectiveNativeAllocationLimit(
		const FSeinStructWireLimits& Limits)
	{
		return Limits.MaxNativeAllocationBytes >= 0
			? Limits.MaxNativeAllocationBytes
			: Limits.MaxBytes;
	}

	inline bool BuildCanonicalCost(
		int32 WireBytes,
		uint64 LogicalElements,
		uint64& OutCanonicalCostBytes,
		FString& Error)
	{
		OutCanonicalCostBytes = 0;
		if (WireBytes < 0
			|| LogicalElements > (MAX_uint64 - static_cast<uint64>(WireBytes))
				/ FSeinWireCost::CanonicalBytesPerLogicalElement)
		{
			if (Error.IsEmpty()) Error = TEXT("wire canonical-cost overflow");
			return false;
		}
		OutCanonicalCostBytes = static_cast<uint64>(WireBytes)
			+ LogicalElements
				* FSeinWireCost::CanonicalBytesPerLogicalElement;
		return true;
	}

	class FWireWriter
	{
	public:
		FWireWriter(
			TArray<uint8>& InBytes,
			int32 InMaxBytes,
			FString& InError,
			bool bResetError = false)
			: Bytes(InBytes), MaxBytes(InMaxBytes), Error(InError)
		{
			Bytes.Reset();
			if (bResetError) Error.Reset();
		}

		bool Fail(const FString& Message)
		{
			if (Error.IsEmpty()) Error = Message;
			return false;
		}

		bool Raw(const void* Data, int32 Count)
		{
			if (Count < 0 || MaxBytes < 0 || Count > MaxBytes - Bytes.Num())
			{
				return Fail(TEXT("wire byte limit exceeded"));
			}
			if (Count > 0)
			{
				Bytes.Append(static_cast<const uint8*>(Data), Count);
			}
			return true;
		}

		bool U8(uint8 Value) { return Raw(&Value, 1); }

		bool UInt(uint64 Value, int32 Width)
		{
			if (Width != 1 && Width != 2 && Width != 4 && Width != 8)
			{
				return Fail(TEXT("unsupported integer width"));
			}
			uint8 Encoded[8];
			for (int32 Index = 0; Index < Width; ++Index)
			{
				Encoded[Index] = static_cast<uint8>(
					Value >> ((Width - 1 - Index) * 8));
			}
			return Raw(Encoded, Width);
		}

		bool U16(uint16 Value) { return UInt(Value, 2); }
		bool U32(uint32 Value) { return UInt(Value, 4); }
		bool I32(int32 Value) { return UInt(static_cast<uint32>(Value), 4); }
		bool I64(int64 Value) { return UInt(static_cast<uint64>(Value), 8); }

		bool Utf8(const FString& Value, int32 MaxStringBytes)
		{
			if (Value.Len() != FCString::Strlen(*Value))
			{
				return Fail(TEXT("wire string contains an embedded null"));
			}
			FTCHARToUTF8 Converted(*Value, Value.Len());
			if (Converted.Length() < 0
				|| Converted.Length() > MaxStringBytes)
			{
				return Fail(TEXT("wire string exceeds its byte limit"));
			}
			return U32(static_cast<uint32>(Converted.Length()))
				&& Raw(Converted.Get(), Converted.Length());
		}

		TArray<uint8>& Bytes;
		int32 MaxBytes;
		FString& Error;
	};

	class FWireReader
	{
	public:
		FWireReader(
			TConstArrayView<uint8> InBytes,
			FString& InError,
			bool bResetError = false)
			: Bytes(InBytes), Error(InError)
		{
			if (bResetError) Error.Reset();
		}

		bool Fail(const FString& Message)
		{
			if (Error.IsEmpty()) Error = Message;
			return false;
		}

		int32 Remaining() const { return Bytes.Num() - Offset; }
		bool AtEnd() const { return Offset == Bytes.Num(); }

		bool Raw(void* Out, int32 Count)
		{
			if (Count < 0 || Count > Remaining())
			{
				return Fail(TEXT("truncated wire value"));
			}
			if (Count > 0)
			{
				FMemory::Memcpy(Out, Bytes.GetData() + Offset, Count);
			}
			Offset += Count;
			return true;
		}

		bool UInt(uint64& Out, int32 Width)
		{
			if (Width != 1 && Width != 2 && Width != 4 && Width != 8)
			{
				return Fail(TEXT("unsupported integer width"));
			}
			if (Width > Remaining())
			{
				return Fail(TEXT("truncated wire integer"));
			}
			Out = 0;
			for (int32 Index = 0; Index < Width; ++Index)
			{
				Out = (Out << 8) | Bytes[Offset++];
			}
			return true;
		}

		bool U8(uint8& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 1)) return false;
			Out = static_cast<uint8>(Value);
			return true;
		}

		bool U16(uint16& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 2)) return false;
			Out = static_cast<uint16>(Value);
			return true;
		}

		bool U32(uint32& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 4)) return false;
			Out = static_cast<uint32>(Value);
			return true;
		}

		bool I32(int32& Out)
		{
			uint32 Value = 0;
			if (!U32(Value)) return false;
			Out = BitCast<int32>(Value);
			return true;
		}

		bool I64(int64& Out)
		{
			uint64 Value = 0;
			if (!UInt(Value, 8)) return false;
			Out = BitCast<int64>(Value);
			return true;
		}

		bool Utf8(
			FString& Out,
			int32 MaxStringBytes,
			TFunctionRef<bool(uint64)> ChargeNativeAllocation)
		{
			uint32 Count = 0;
			if (!U32(Count)) return false;
			if (Count > static_cast<uint32>(MaxStringBytes)
				|| Count > static_cast<uint32>(Remaining()))
			{
				return Fail(
					TEXT("wire string length exceeds its bound or remaining bytes"));
			}
			if (Count == 0)
			{
				Out.Reset();
				return true;
			}
			const uint64 NativeStringBytes =
				(static_cast<uint64>(Count) + 1u)
					* (4u * sizeof(TCHAR));
			if (!ChargeNativeAllocation(NativeStringBytes))
			{
				return Fail(TEXT("wire string exceeds its native-allocation budget"));
			}

			const ANSICHAR* Source = reinterpret_cast<const ANSICHAR*>(
				Bytes.GetData() + Offset);
			bool bContainsNull = false;
			for (uint32 Index = 0; Index < Count; ++Index)
			{
				bContainsNull |= Source[Index] == 0;
			}
			if (bContainsNull)
			{
				return Fail(TEXT("wire string contains an embedded null"));
			}

			FUTF8ToTCHAR Converted(Source, static_cast<int32>(Count));
			Out = FString(Converted.Length(), Converted.Get());
			if (Out.Len() != FCString::Strlen(*Out))
			{
				return Fail(TEXT("wire string contains an embedded null"));
			}
			FTCHARToUTF8 RoundTrip(*Out, Out.Len());
			if (RoundTrip.Length() != static_cast<int32>(Count)
				|| FMemory::Memcmp(RoundTrip.Get(), Source, Count) != 0)
			{
				return Fail(TEXT("wire string is not canonical UTF-8"));
			}
			Offset += static_cast<int32>(Count);
			return true;
		}

		bool Utf8(FString& Out, int32 MaxStringBytes)
		{
			auto NoNativeCharge = [](uint64) { return true; };
			return Utf8(Out, MaxStringBytes, NoNativeCharge);
		}

		bool Slice(int32 Count, TConstArrayView<uint8>& Out)
		{
			if (Count < 0 || Count > Remaining())
			{
				return Fail(TEXT("wire frame exceeds remaining bytes"));
			}
			Out = TConstArrayView<uint8>(Bytes.GetData() + Offset, Count);
			Offset += Count;
			return true;
		}

		TConstArrayView<uint8> Bytes;
		int32 Offset = 0;
		FString& Error;
	};

	inline bool ResolveExistingName(
		const FString& Text,
		FName& OutName,
		FString& Error)
	{
		if (Text.IsEmpty())
		{
			Error = TEXT("wire valid-name value is empty");
			return false;
		}
		OutName = FName(*Text, FNAME_Find);
		if (OutName.IsNone())
		{
			Error = FString::Printf(
				TEXT("wire name '%s' is not present in the frozen runtime image"),
				*Text);
			return false;
		}
		return true;
	}

	inline uint64 NativeDecodedStringBytes(const FString& Text)
	{
		if (Text.IsEmpty()) return 0;
		FTCHARToUTF8 Converted(*Text, Text.Len());
		return (static_cast<uint64>(Converted.Length()) + 1u)
			* (4u * sizeof(TCHAR));
	}

	inline bool ResolveExistingTag(
		const FString& Text,
		FGameplayTag& OutTag,
		FString& Error)
	{
		if (Text.IsEmpty())
		{
			Error = TEXT("wire valid-gameplay-tag value is empty");
			return false;
		}
		FName TagName;
		if (!ResolveExistingName(Text, TagName, Error)) return false;
		OutTag = FGameplayTag::RequestGameplayTag(TagName, false);
		if (!OutTag.IsValid())
		{
			Error = FString::Printf(
				TEXT("wire gameplay tag '%s' is not registered"), *Text);
			return false;
		}
		return true;
	}

	inline bool WriteTag(
		FWireWriter& Writer,
		FGameplayTag Tag,
		int32 MaxStringBytes)
	{
		return Writer.U8(Tag.IsValid() ? 1 : 0)
			&& (!Tag.IsValid()
				|| Writer.Utf8(Tag.ToString(), MaxStringBytes));
	}

	inline bool ReadTag(
		FWireReader& Reader,
		FGameplayTag& OutTag,
		int32 MaxStringBytes,
		TFunctionRef<bool(uint64)> ChargeNativeAllocation,
		FString& Error)
	{
		uint8 bValid = 0;
		if (!Reader.U8(bValid) || bValid > 1)
		{
			if (Error.IsEmpty())
			{
				Error = TEXT("invalid wire gameplay-tag marker");
			}
			return false;
		}
		if (bValid == 0)
		{
			OutTag = FGameplayTag();
			return true;
		}
		FString Text;
		return Reader.Utf8(
			Text, MaxStringBytes, ChargeNativeAllocation)
			&& ResolveExistingTag(Text, OutTag, Error);
	}

	inline int32 IntegerWireWidth(const FNumericProperty& Numeric)
	{
		if (Numeric.IsA<FByteProperty>() || Numeric.IsA<FInt8Property>())
			return 1;
		if (Numeric.IsA<FInt16Property>() || Numeric.IsA<FUInt16Property>())
			return 2;
		if (Numeric.IsA<FIntProperty>() || Numeric.IsA<FUInt32Property>())
			return 4;
		if (Numeric.IsA<FInt64Property>() || Numeric.IsA<FUInt64Property>())
			return 8;
		return 0;
	}

	inline bool WriteEntity(
		FWireWriter& Writer,
		const FSeinEntityHandle& Entity)
	{
		const bool bCanonicalInvalid =
			Entity.Index == 0 && Entity.Generation == 0;
		if (!bCanonicalInvalid
			&& (Entity.Index <= 0 || Entity.Generation <= 0))
		{
			return Writer.Fail(
				TEXT("non-canonical entity handle on wire encode"));
		}
		return Writer.I32(Entity.Index) && Writer.I32(Entity.Generation);
	}

	inline bool ReadEntity(
		FWireReader& Reader,
		FSeinEntityHandle& Entity)
	{
		int32 Index = 0;
		int32 Generation = 0;
		if (!Reader.I32(Index) || !Reader.I32(Generation))
		{
			return false;
		}
		const bool bCanonicalInvalid = Index == 0 && Generation == 0;
		if (!bCanonicalInvalid && (Index <= 0 || Generation <= 0))
		{
			return Reader.Fail(
				TEXT("non-canonical entity handle on wire decode"));
		}
		Entity = FSeinEntityHandle(Index, Generation);
		return true;
	}
}
