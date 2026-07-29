#include "CQTest.h"

#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "TestTypes/SeinPoolObjectCodecTestTypes.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		bool IncludePoolTestProperty(const FProperty&)
		{
			return true;
		}

		bool ExcludeLocalOnlyPoolTestProperty(
			const FProperty& Property)
		{
			return Property.GetFName()
				!= GET_MEMBER_NAME_CHECKED(
					FSeinPoolObjectFilteredElementTestValue,
					LocalOnlyValue);
		}

		FSeinStructWireLimits MakePoolTestLimits()
		{
			FSeinStructWireLimits Limits;
			Limits.MaxBytes = 4096;
			Limits.MaxAggregateElements = 8;
			Limits.MaxStringBytes = 128;
			Limits.MaxNativeAllocationBytes = 4096;
			return Limits;
		}
	}

	TEST_CLASS(
		PoolObjectCodec,
		"SeinARTS.Unit.CoreEntity.PoolObjectCodec")
	{
		TEST_METHOD(BoundedObjectStateRoundTrips)
		{
			USeinPoolObjectCodecTestObject* Source =
				NewObject<USeinPoolObjectCodecTestObject>();
			ASSERT_THAT(IsNotNull(Source));
			Source->Values = { 3, 5, 8 };
			Source->Label = TEXT("bounded");
			Source->ExistingName =
				FName(TEXT("SeinPoolObjectCodecExistingName"));

			FString Error;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::ValidateObjectClass(
					Source->GetClass(),
					&IncludePoolTestProperty,
					Error)));

			TArray<uint8> Bytes;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::EncodeObject(
					*Source,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));

			USeinPoolObjectCodecTestObject* Restored =
				NewObject<USeinPoolObjectCodecTestObject>();
			ASSERT_THAT(IsNotNull(Restored));
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::DecodeObject(
					Bytes,
					*Restored,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(AreEqual(3, Restored->Values.Num()));
			ASSERT_THAT(AreEqual(3, Restored->Values[0]));
			ASSERT_THAT(AreEqual(5, Restored->Values[1]));
			ASSERT_THAT(AreEqual(8, Restored->Values[2]));
			ASSERT_THAT(IsTrue(
				Restored->Label == TEXT("bounded")));
			ASSERT_THAT(IsTrue(
				Source->ExistingName
					== Restored->ExistingName));
		}

		TEST_METHOD(EnumDomainsRejectInvalidCaptureAndHostileDecode)
		{
			FString Error;
			TArray<uint8> Bytes;

			USeinPoolObjectSignedEnumTestObject* SignedSource =
				NewObject<USeinPoolObjectSignedEnumTestObject>();
			ASSERT_THAT(IsNotNull(SignedSource));
			SignedSource->Value =
				static_cast<ESeinPoolObjectSignedEnumTest>(7);
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::EncodeObject(
					*SignedSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			ASSERT_THAT(IsTrue(Bytes.IsEmpty()));
			ASSERT_THAT(IsTrue(
				Error.Contains(TEXT("declared domain"))));

			USeinPoolObjectSignedEnumTestObject* SignedCandidate =
				NewObject<USeinPoolObjectSignedEnumTestObject>();
			ASSERT_THAT(IsNotNull(SignedCandidate));
			SignedCandidate->Value =
				ESeinPoolObjectSignedEnumTest::Positive;
			const TArray<uint8> InvalidSignedBytes = { 7 };
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::DecodeObject(
					InvalidSignedBytes,
					*SignedCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(IsTrue(
				SignedCandidate->Value
					== ESeinPoolObjectSignedEnumTest::Positive));

			const int64 SignedMax =
				StaticEnum<ESeinPoolObjectSignedEnumTest>()
					->GetMaxEnumValue();
			SignedSource->Value =
				static_cast<ESeinPoolObjectSignedEnumTest>(
					SignedMax);
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::EncodeObject(
					*SignedSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			const TArray<uint8> SignedMaxBytes = {
				static_cast<uint8>(SignedMax),
			};
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::DecodeObject(
					SignedMaxBytes,
					*SignedCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(IsTrue(
				SignedCandidate->Value
					== ESeinPoolObjectSignedEnumTest::Positive));

			USeinPoolObjectByteEnumTestObject* ByteSource =
				NewObject<USeinPoolObjectByteEnumTestObject>();
			ASSERT_THAT(IsNotNull(ByteSource));
			ByteSource->Value =
				static_cast<ESeinPoolObjectByteEnumTest>(2);
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::EncodeObject(
					*ByteSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			ASSERT_THAT(IsTrue(Bytes.IsEmpty()));

			USeinPoolObjectByteEnumTestObject* ByteCandidate =
				NewObject<USeinPoolObjectByteEnumTestObject>();
			ASSERT_THAT(IsNotNull(ByteCandidate));
			ByteCandidate->Value =
				SeinPoolObjectByteEnum_Second;
			const TArray<uint8> InvalidByteBytes = { 2 };
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::DecodeObject(
					InvalidByteBytes,
					*ByteCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(IsTrue(
				ByteCandidate->Value
					== SeinPoolObjectByteEnum_Second));

			const int64 ByteMax =
				StaticEnum<ESeinPoolObjectByteEnumTest>()
					->GetMaxEnumValue();
			ByteSource->Value =
				static_cast<ESeinPoolObjectByteEnumTest>(
					ByteMax);
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::EncodeObject(
					*ByteSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			const TArray<uint8> ByteMaxBytes = {
				static_cast<uint8>(ByteMax),
			};
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::DecodeObject(
					ByteMaxBytes,
					*ByteCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(IsTrue(
				ByteCandidate->Value
					== SeinPoolObjectByteEnum_Second));

			USeinPoolObjectTypedFlagsTestObject* FlagSource =
				NewObject<USeinPoolObjectTypedFlagsTestObject>();
			ASSERT_THAT(IsNotNull(FlagSource));
			FlagSource->Value =
				static_cast<ESeinPoolObjectTypedFlagsTest>(
					0x40);
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::EncodeObject(
					*FlagSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));

			USeinPoolObjectTypedFlagsTestObject* FlagCandidate =
				NewObject<USeinPoolObjectTypedFlagsTestObject>();
			ASSERT_THAT(IsNotNull(FlagCandidate));
			FlagCandidate->Value =
				ESeinPoolObjectTypedFlagsTest::Second;
			const TArray<uint8> InvalidFlagBytes = { 0x40 };
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::DecodeObject(
					InvalidFlagBytes,
					*FlagCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(IsTrue(
				FlagCandidate->Value
					== ESeinPoolObjectTypedFlagsTest::Second));
		}

		TEST_METHOD(SignedEnumsFlagsAndIntegerBitmasksKeepValidWire)
		{
			FString Error;
			TArray<uint8> Bytes;

			USeinPoolObjectSignedEnumTestObject* SignedSource =
				NewObject<USeinPoolObjectSignedEnumTestObject>();
			ASSERT_THAT(IsNotNull(SignedSource));
			SignedSource->Value =
				ESeinPoolObjectSignedEnumTest::Negative;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::EncodeObject(
					*SignedSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			ASSERT_THAT(AreEqual(1, Bytes.Num()));
			ASSERT_THAT(AreEqual(
				0xff, static_cast<int32>(Bytes[0])));

			USeinPoolObjectSignedEnumTestObject* SignedCandidate =
				NewObject<USeinPoolObjectSignedEnumTestObject>();
			ASSERT_THAT(IsNotNull(SignedCandidate));
			SignedCandidate->Value =
				ESeinPoolObjectSignedEnumTest::Positive;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::DecodeObject(
					Bytes,
					*SignedCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(IsTrue(
				SignedCandidate->Value
					== ESeinPoolObjectSignedEnumTest::Negative));

			USeinPoolObjectTypedFlagsTestObject* FlagSource =
				NewObject<USeinPoolObjectTypedFlagsTestObject>();
			ASSERT_THAT(IsNotNull(FlagSource));
			FlagSource->Value =
				ESeinPoolObjectTypedFlagsTest::First
				| ESeinPoolObjectTypedFlagsTest::Second;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::EncodeObject(
					*FlagSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			ASSERT_THAT(AreEqual(1, Bytes.Num()));
			ASSERT_THAT(AreEqual(
				3, static_cast<int32>(Bytes[0])));

			USeinPoolObjectTypedFlagsTestObject* FlagCandidate =
				NewObject<USeinPoolObjectTypedFlagsTestObject>();
			ASSERT_THAT(IsNotNull(FlagCandidate));
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::DecodeObject(
					Bytes,
					*FlagCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(IsTrue(
				FlagCandidate->Value
					== (ESeinPoolObjectTypedFlagsTest::First
						| ESeinPoolObjectTypedFlagsTest::Second)));

			USeinPoolObjectIntegerMaskTestObject* MaskSource =
				NewObject<USeinPoolObjectIntegerMaskTestObject>();
			ASSERT_THAT(IsNotNull(MaskSource));
			MaskSource->Mask = 0x40;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::EncodeObject(
					*MaskSource,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			ASSERT_THAT(AreEqual(4, Bytes.Num()));
			ASSERT_THAT(AreEqual(
				0x40, static_cast<int32>(Bytes[3])));

			USeinPoolObjectIntegerMaskTestObject* MaskCandidate =
				NewObject<USeinPoolObjectIntegerMaskTestObject>();
			ASSERT_THAT(IsNotNull(MaskCandidate));
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::DecodeObject(
					Bytes,
					*MaskCandidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(AreEqual(0x40, MaskCandidate->Mask));
		}

		TEST_METHOD(HostileArrayCountFailsBeforeResize)
		{
			// This fixture has exactly one reflected field, so the first four
			// bytes are unambiguously its big-endian array element count.
			TArray<uint8> HostileBytes = {
				0x7f, 0xff, 0xff, 0xff,
			};
			USeinPoolObjectArrayOnlyTestObject* Candidate =
				NewObject<USeinPoolObjectArrayOnlyTestObject>();
			ASSERT_THAT(IsNotNull(Candidate));

			FString Error;
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::DecodeObject(
					HostileBytes,
					*Candidate,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Error)));
			ASSERT_THAT(AreEqual(0, Candidate->Values.Num()));
			ASSERT_THAT(IsTrue(
				Error.Contains(TEXT("element limit"))));
		}

		TEST_METHOD(ArrayAllocatorSlackIsPrecharged)
		{
			USeinPoolObjectArrayOnlyTestObject* Source =
				NewObject<USeinPoolObjectArrayOnlyTestObject>();
			ASSERT_THAT(IsNotNull(Source));

			FString Error;
			TArray<uint8> Bytes;
			uint64 EmptyNativeBytes = 0;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::EncodeObject(
					*Source,
					{},
					MakePoolTestLimits(),
					&IncludePoolTestProperty,
					Bytes,
					Error,
					&EmptyNativeBytes)));

			Source->Values = { 1, 2, 3 };
			FSeinStructWireLimits Limits = MakePoolTestLimits();
			Limits.MaxNativeAllocationBytes =
				static_cast<int32>(
					EmptyNativeBytes
					+ Source->Values.Num() * sizeof(int32));
			ASSERT_THAT(IsFalse(
				FSeinCanonicalStateCodec::EncodeObject(
					*Source,
					{},
					Limits,
					&IncludePoolTestProperty,
					Bytes,
					Error)));
			ASSERT_THAT(IsTrue(
				Error.Contains(TEXT("native-allocation"))));
		}

		TEST_METHOD(ArrayDecodePreservesExcludedDefaults)
		{
			USeinPoolObjectFilteredArrayTestObject* Source =
				NewObject<USeinPoolObjectFilteredArrayTestObject>();
			ASSERT_THAT(IsNotNull(Source));
			Source->Values[0].RestoredValue = 42;
			Source->Values[0].LocalOnlyValue = 999;

			FString Error;
			TArray<uint8> Bytes;
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::EncodeObject(
					*Source,
					{},
					MakePoolTestLimits(),
					&ExcludeLocalOnlyPoolTestProperty,
					Bytes,
					Error)));

			USeinPoolObjectFilteredArrayTestObject* Candidate =
				NewObject<USeinPoolObjectFilteredArrayTestObject>();
			ASSERT_THAT(IsNotNull(Candidate));
			ASSERT_THAT(AreEqual(
				73, Candidate->Values[0].LocalOnlyValue));
			ASSERT_THAT(IsTrue(
				FSeinCanonicalStateCodec::DecodeObject(
					Bytes,
					*Candidate,
					{},
					MakePoolTestLimits(),
					&ExcludeLocalOnlyPoolTestProperty,
					Error)));
			ASSERT_THAT(AreEqual(
				42, Candidate->Values[0].RestoredValue));
			ASSERT_THAT(AreEqual(
				73, Candidate->Values[0].LocalOnlyValue));
		}

		TEST_METHOD(PoolRejectsHiddenNativeVariableState)
		{
			FString Error;
			ASSERT_THAT(IsFalse(
				FSeinPoolObjectCodecRegistry::
					ValidateReflectedClassSchema(
						USeinPoolObjectHiddenNativeArrayTestObject::
							StaticClass(),
						Error)));
			ASSERT_THAT(IsTrue(
				Error.Contains(
					TEXT("defaults can allocate variable storage"))));
		}

		TEST_METHOD(PoolRejectsUnvalidatedGameplayTagQuery)
		{
			FString Error;
			ASSERT_THAT(IsFalse(
				FSeinPoolObjectCodecRegistry::
					ValidateReflectedClassSchema(
						USeinPoolObjectQueryTestObject::
							StaticClass(),
						Error)));
			ASSERT_THAT(IsTrue(
				Error.Contains(TEXT("token grammar"))));
		}
	};
}
