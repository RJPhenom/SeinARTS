#include "CQTest.h"

#include "Misc/ScopeExit.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCanonicalStateValueStore.h"
#include "TestTypes/SeinCanonicalStateValueStoreTestTypes.h"
#include "UObject/GarbageCollection.h"

#include <initializer_list>

namespace UE::SeinARTSTests
{
	namespace
	{
		const FName ValueStoreDomain(
			TEXT("SeinFrameworkTest.State.ValueStore"));
		const FName ValueStoreOwner(
			TEXT("SeinFrameworkTests.CanonicalStateValueStore"));

		FSeinCanonicalStateKey MakeValueStoreKey(FName ContributorId)
		{
			FSeinCanonicalStateKey Key;
			Key.StableDomainId = ValueStoreDomain;
			Key.StableContributorId = ContributorId;
			return Key;
		}

		FSeinCanonicalStateValueSlotDefinition MakeDefinition(
			FName ContributorId)
		{
			FSeinCanonicalStateValueSlotDefinition Definition;
			Definition.Key = MakeValueStoreKey(ContributorId);
			return Definition;
		}

		FInstancedStruct MakeValue(
			int32 Marker,
			std::initializer_list<int32> OrderedValues = {})
		{
			FSeinCanonicalStateValueStoreTestPayload Value;
			Value.Marker = Marker;
			for (const int32 Element : OrderedValues)
			{
				Value.OrderedValues.Add(Element);
			}
			return FInstancedStruct::Make(Value);
		}

		FInstancedStruct MakeLargeValue(
			int32 Marker,
			int32 TargetCanonicalBytes)
		{
			FInstancedStruct Value =
				FInstancedStruct::Make<
					FSeinCanonicalStateValueStoreLargeTestPayload>();
			FSeinCanonicalStateValueStoreLargeTestPayload& Payload =
				Value.GetMutable<
					FSeinCanonicalStateValueStoreLargeTestPayload>();
			Payload.Marker = Marker;
			// Array prefix + Marker consume 8 bytes; each int64 element uses an
			// 8-byte value inside a 4-byte element frame. This drives the
			// canonical aggregate bound without conflating it with the
			// intentionally larger hostile UTF conversion budget.
			const int32 WordCount = FMath::Max(
				0, (TargetCanonicalBytes - 8) / 12);
			Payload.Words.SetNum(WordCount);
			return Value;
		}

		FSeinCanonicalStateValueSlotDefinition MakeLargeDefinition(
			FName ContributorId)
		{
			FSeinCanonicalStateValueSlotDefinition Definition =
				MakeDefinition(ContributorId);
			Definition.Limits.MaxEncodedBytes =
				static_cast<int32>(
					FSeinCanonicalStateValueStore::
						MaxAggregatePayloadBytes);
			Definition.Limits.MaxAggregateElements =
				8 * 1024 * 1024;
			return Definition;
		}

		FSeinCanonicalStateSchemaSnapshot CaptureNativeSchema(
			FString& OutError)
		{
			return FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(
				&OutError);
		}

		FSeinCanonicalStateContributorOps MakeNativeOps()
		{
			FSeinCanonicalStateContributorOps Ops;
			Ops.Capture = [](
				const FSeinCanonicalStateCaptureContext&,
				FInstancedStruct& OutState,
				FString&)
			{
				OutState = MakeValue(0);
				return true;
			};
			Ops.StageRestore = [](
				const FSeinCanonicalStateStageContext&,
				const FInstancedStruct&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>&,
				FString&)
			{
				return true;
			};
			Ops.CommitRestore = [](
				FSeinCanonicalStateCommitContext&,
				TUniquePtr<ISeinCanonicalStateRestoreStage>&&)
			{
			};
			return Ops;
		}

		bool SameRecord(
			const FSeinCanonicalStateValueRecord& A,
			const FSeinCanonicalStateValueRecord& B)
		{
			return A.Key == B.Key
				&& A.SchemaVersion == B.SchemaVersion
				&& A.ImplementationRevision == B.ImplementationRevision
				&& A.PayloadStructPath == B.PayloadStructPath
				&& A.DynamicPayloadStructPaths
					== B.DynamicPayloadStructPaths
				&& A.AllowedNames == B.AllowedNames
				&& A.Limits.MaxRecursionDepth
					== B.Limits.MaxRecursionDepth
				&& A.Limits.MaxEncodedBytes
					== B.Limits.MaxEncodedBytes
				&& A.Limits.MaxAggregateElements
					== B.Limits.MaxAggregateElements
				&& A.DescriptorDigest == B.DescriptorDigest
				&& A.PayloadBytes == B.PayloadBytes
				&& A.LeafDigest == B.LeafDigest;
		}
	}

	TEST(CanonicalStateValueStoreCopiesValuesAndSealsSchema,
		"SeinARTS.Unit.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		FSeinCanonicalStateValueStore Store;
		const FSeinCanonicalStateValueSlotDefinition Definition =
			MakeDefinition(TEXT("CopyAndSeal"));
		FInstancedStruct Initial = MakeValue(7, {1, 2});
		ASSERT_THAT(IsTrue(
			Store.RegisterSlot(
				NativeSchema, Definition, Initial, Error)));
		Initial.GetMutable<FSeinCanonicalStateValueStoreTestPayload>()
			.Marker = 99;

		FInstancedStruct Read;
		ASSERT_THAT(IsTrue(Store.GetValue(Definition.Key, Read)));
		ASSERT_THAT(AreEqual(
			7,
			Read.Get<FSeinCanonicalStateValueStoreTestPayload>().Marker));
		ASSERT_THAT(AreEqual(
			2,
			Read.Get<FSeinCanonicalStateValueStoreTestPayload>()
				.OrderedValues.Num()));
		Read.GetMutable<FSeinCanonicalStateValueStoreTestPayload>()
			.Marker = 101;
		ASSERT_THAT(IsTrue(Store.GetValue(Definition.Key, Read)));
		ASSERT_THAT(AreEqual(
			7,
			Read.Get<FSeinCanonicalStateValueStoreTestPayload>().Marker));

		ASSERT_THAT(IsTrue(Store.Seal(NativeSchema, Error)));
		ASSERT_THAT(IsTrue(Store.IsSealed()));
		ASSERT_THAT(IsTrue(Store.GetContractDigest().IsValid()));
		ASSERT_THAT(IsFalse(Store.GetCanonicalManifest().IsEmpty()));

		FInstancedStruct Replacement = MakeValue(8, {3, 4});
		ASSERT_THAT(IsTrue(
			Store.SetValue(
				Definition.Key, Replacement, Error)));
		Replacement
			.GetMutable<FSeinCanonicalStateValueStoreTestPayload>()
			.Marker = 100;
		ASSERT_THAT(IsTrue(Store.GetValue(Definition.Key, Read)));
		ASSERT_THAT(AreEqual(
			8,
			Read.Get<FSeinCanonicalStateValueStoreTestPayload>().Marker));

		ASSERT_THAT(IsFalse(
			Store.RegisterSlot(
				NativeSchema,
				MakeDefinition(TEXT("AfterSeal")),
				MakeValue(1),
				Error)));
		ASSERT_THAT(AreEqual(1, Store.Num()));
	}

	TEST(CanonicalStateValueStoreSetFailuresAreTransactional,
		"SeinARTS.Unit.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		const FName AllowedName(TEXT("AllowedStateName"));
		FSeinCanonicalStateValueSlotDefinition Definition =
			MakeDefinition(TEXT("TransactionalName"));
		Definition.AllowedNames.Add(AllowedName);
		FSeinCanonicalStateValueStoreNameTestPayload InitialPayload;
		InitialPayload.Name = AllowedName;

		FSeinCanonicalStateValueStore Store;
		ASSERT_THAT(IsTrue(
			Store.RegisterSlot(
				NativeSchema,
				Definition,
				FInstancedStruct::Make(InitialPayload),
				Error)));
		TArray<FSeinCanonicalStateValueRecord> BaselineRecords;
		ASSERT_THAT(IsTrue(
			Store.CaptureRecords(BaselineRecords, Error)));
		ASSERT_THAT(AreEqual(1, BaselineRecords.Num()));

		ASSERT_THAT(IsFalse(
			Store.SetValue(
				Definition.Key, MakeValue(42), Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("exact frozen root type"))));

		FSeinCanonicalStateValueStoreNameTestPayload ForbiddenPayload;
		ForbiddenPayload.Name = TEXT("ForbiddenStateName");
		ASSERT_THAT(IsFalse(
			Store.SetValue(
				Definition.Key,
				FInstancedStruct::Make(ForbiddenPayload),
				Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("outside the frozen catalog"))));

		FInstancedStruct Read;
		ASSERT_THAT(IsTrue(Store.GetValue(Definition.Key, Read)));
		ASSERT_THAT(AreEqual(
			AllowedName,
			Read.Get<FSeinCanonicalStateValueStoreNameTestPayload>()
				.Name));
		TArray<FSeinCanonicalStateValueRecord> FinalRecords;
		ASSERT_THAT(IsTrue(
			Store.CaptureRecords(FinalRecords, Error)));
		ASSERT_THAT(AreEqual(1, FinalRecords.Num()));
		ASSERT_THAT(IsTrue(
			SameRecord(BaselineRecords[0], FinalRecords[0])));

		FSeinCanonicalStateValueStore RejectedInitialStore;
		FSeinCanonicalStateValueSlotDefinition NoNameCatalog =
			MakeDefinition(TEXT("RejectedInitialName"));
		ASSERT_THAT(IsFalse(
			RejectedInitialStore.RegisterSlot(
				NativeSchema,
				NoNameCatalog,
				FInstancedStruct::Make(InitialPayload),
				Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("outside the frozen catalog"))));
		ASSERT_THAT(AreEqual(0, RejectedInitialStore.Num()));
	}

	TEST(CanonicalStateValueStoreRetainsCanonicalNameSpelling,
		"SeinARTS.Unit.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		FSeinCanonicalStateValueSlotDefinition Definition =
			MakeDefinition(TEXT("CanonicalNameSpelling"));
		Definition.AllowedNames.Add(
			FName(TEXT("CanonicalMixedCaseName")));

		FSeinCanonicalStateValueStoreNameTestPayload InitialPayload;
		InitialPayload.Name =
			FName(TEXT("CANONICALMIXEDCASENAME"));
		FSeinCanonicalStateValueStore Store;
		ASSERT_THAT(IsTrue(Store.RegisterSlot(
			NativeSchema,
			Definition,
			FInstancedStruct::Make(InitialPayload),
			Error)));

		FInstancedStruct Read;
		ASSERT_THAT(IsTrue(Store.GetValue(Definition.Key, Read)));
		ASSERT_THAT(AreEqual(
			FString(TEXT("canonicalmixedcasename")),
			Read.Get<FSeinCanonicalStateValueStoreNameTestPayload>()
				.Name.ToString()));

		FSeinCanonicalStateValueStoreNameTestPayload Replacement;
		Replacement.Name =
			FName(TEXT("CanonicalMixedCaseName"));
		ASSERT_THAT(IsTrue(Store.SetValue(
			Definition.Key,
			FInstancedStruct::Make(Replacement),
			Error)));
		ASSERT_THAT(IsTrue(Store.GetValue(Definition.Key, Read)));
		ASSERT_THAT(AreEqual(
			FString(TEXT("canonicalmixedcasename")),
			Read.Get<FSeinCanonicalStateValueStoreNameTestPayload>()
				.Name.ToString()));
	}

	TEST(CanonicalStateValueStorePreservesOrderedArrayMeaning,
		"SeinARTS.Unit.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		FSeinCanonicalStateValueStore Store;
		const FSeinCanonicalStateValueSlotDefinition Definition =
			MakeDefinition(TEXT("OrderedArray"));
		ASSERT_THAT(IsTrue(
			Store.RegisterSlot(
				NativeSchema,
				Definition,
				MakeValue(1, {10, 20}),
				Error)));
		TArray<FSeinCanonicalStateValueRecord> Forward;
		ASSERT_THAT(IsTrue(Store.CaptureRecords(Forward, Error)));

		ASSERT_THAT(IsTrue(
			Store.SetValue(
				Definition.Key,
				MakeValue(1, {20, 10}),
				Error)));
		TArray<FSeinCanonicalStateValueRecord> Reverse;
		ASSERT_THAT(IsTrue(Store.CaptureRecords(Reverse, Error)));
		ASSERT_THAT(AreEqual(1, Forward.Num()));
		ASSERT_THAT(AreEqual(1, Reverse.Num()));
		ASSERT_THAT(IsFalse(
			Forward[0].PayloadBytes == Reverse[0].PayloadBytes));
		ASSERT_THAT(IsFalse(
			Forward[0].LeafDigest == Reverse[0].LeafDigest));
		ASSERT_THAT(IsTrue(
			Forward[0].DescriptorDigest
				== Reverse[0].DescriptorDigest));
	}

	TEST(CanonicalStateValueStoreRejectsKeyConflicts,
		"SeinARTS.Unit.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot BaselineSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(BaselineSchema.IsValid()));

		FSeinCanonicalStateValueStore Store;
		const FSeinCanonicalStateValueSlotDefinition Duplicate =
			MakeDefinition(TEXT("DuplicateSlot"));
		ASSERT_THAT(IsTrue(
			Store.RegisterSlot(
				BaselineSchema, Duplicate, MakeValue(1), Error)));
		ASSERT_THAT(IsFalse(
			Store.RegisterSlot(
				BaselineSchema, Duplicate, MakeValue(2), Error)));
		ASSERT_THAT(AreEqual(1, Store.Num()));

		FSeinCanonicalStateDescriptor NativeDescriptor;
		NativeDescriptor.Key = MakeValueStoreKey(TEXT("NativeOwned"));
		NativeDescriptor.SchemaVersion = 1;
		NativeDescriptor.ImplementationRevision = 1;
		NativeDescriptor.PayloadStruct =
			FSeinCanonicalStateValueStoreTestPayload::StaticStruct();
		FSeinCanonicalStateRegistrationHandle NativeHandle =
			FSeinCanonicalStateRegistry::Register(
				ValueStoreOwner,
				NativeDescriptor,
				MakeNativeOps(),
				&Error);
		ASSERT_THAT(IsTrue(NativeHandle.IsValid()));
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		FSeinCanonicalStateValueStore NativeConflictStore;
		ASSERT_THAT(IsFalse(
			NativeConflictStore.RegisterSlot(
				NativeSchema,
				MakeDefinition(TEXT("NativeOwned")),
				MakeValue(1),
				Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("native contributor"))));
		ASSERT_THAT(AreEqual(0, NativeConflictStore.Num()));
	}

	TEST(CanonicalStateValueStoreManifestIgnoresRegistrationOrder,
		"SeinARTS.Unit.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));
		FSeinCanonicalStateValueSlotDefinition A =
			MakeDefinition(TEXT("ManifestOrderA"));
		A.SchemaVersion = 3;
		FSeinCanonicalStateValueSlotDefinition B =
			MakeDefinition(TEXT("ManifestOrderB"));
		B.ImplementationRevision = 4;

		FSeinCanonicalStateValueStore Forward;
		ASSERT_THAT(IsTrue(
			Forward.RegisterSlot(
				NativeSchema, A, MakeValue(11), Error)));
		ASSERT_THAT(IsTrue(
			Forward.RegisterSlot(
				NativeSchema, B, MakeValue(22), Error)));
		ASSERT_THAT(IsTrue(Forward.Seal(NativeSchema, Error)));

		FSeinCanonicalStateValueStore Reverse;
		ASSERT_THAT(IsTrue(
			Reverse.RegisterSlot(
				NativeSchema, B, MakeValue(22), Error)));
		ASSERT_THAT(IsTrue(
			Reverse.RegisterSlot(
				NativeSchema, A, MakeValue(11), Error)));
		ASSERT_THAT(IsTrue(Reverse.Seal(NativeSchema, Error)));

		ASSERT_THAT(AreEqual(
			Forward.GetCanonicalManifest(),
			Reverse.GetCanonicalManifest()));
		ASSERT_THAT(IsTrue(
			Forward.GetContractDigest()
				== Reverse.GetContractDigest()));

		TArray<FSeinCanonicalStateValueRecord> ForwardRecords;
		TArray<FSeinCanonicalStateValueRecord> ReverseRecords;
		ASSERT_THAT(IsTrue(
			Forward.CaptureRecords(ForwardRecords, Error)));
		ASSERT_THAT(IsTrue(
			Reverse.CaptureRecords(ReverseRecords, Error)));
		ASSERT_THAT(AreEqual(2, ForwardRecords.Num()));
		ASSERT_THAT(AreEqual(2, ReverseRecords.Num()));
		ASSERT_THAT(IsTrue(
			SameRecord(ForwardRecords[0], ReverseRecords[0])));
		ASSERT_THAT(IsTrue(
			SameRecord(ForwardRecords[1], ReverseRecords[1])));
	}

	TEST(CanonicalStateValueStoreResourceBoundsStayRestorable,
		"SeinARTS.Determinism.CoreEntity.CanonicalState.ValueStore")
	{
		static_assert(
			FSeinCanonicalStateValueStore::MaxSlots == 4096);
		static_assert(
			FSeinCanonicalStateValueStore::MaxAggregatePayloadBytes
				== 64ull * 1024ull * 1024ull);

		constexpr int32 InitialValueBytes = 14 * 1024 * 1024;
		constexpr int32 OversizedReplacementBytes =
			24 * 1024 * 1024;
		constexpr int32 OversizedAdditionalBytes =
			10 * 1024 * 1024;

		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		TArray<FSeinCanonicalStateValueSlotDefinition> Definitions;
		FSeinCanonicalStateValueStore Source;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FName ContributorId(
				*FString::Printf(TEXT("ResourceBound%d"), Index));
			Definitions.Add(
				MakeLargeDefinition(ContributorId));
			if (!Source.RegisterSlot(
					NativeSchema,
					Definitions.Last(),
					MakeLargeValue(Index, InitialValueBytes),
					Error))
			{
				TestRunner->AddError(FString::Printf(
					TEXT("Resource-bound slot %d failed registration: %s"),
					Index,
					*Error));
				return;
			}
		}

		ASSERT_THAT(IsFalse(Source.SetValue(
			Definitions[0].Key,
			MakeLargeValue(99, OversizedReplacementBytes),
			Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("aggregate payload bound"))));
		FInstancedStruct Unchanged;
		ASSERT_THAT(IsTrue(
			Source.GetValue(Definitions[0].Key, Unchanged)));
		ASSERT_THAT(AreEqual(
			0,
			Unchanged
				.Get<
					FSeinCanonicalStateValueStoreLargeTestPayload>()
				.Marker));
		ASSERT_THAT(IsFalse(
			Unchanged
				.Get<
					FSeinCanonicalStateValueStoreLargeTestPayload>()
				.Words.IsEmpty()));
		Unchanged.Reset();

		const FSeinCanonicalStateValueSlotDefinition Additional =
			MakeLargeDefinition(TEXT("ResourceBoundAdditional"));
		ASSERT_THAT(IsFalse(Source.RegisterSlot(
			NativeSchema,
			Additional,
			MakeLargeValue(5, OversizedAdditionalBytes),
			Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("aggregate payload bound"))));
		ASSERT_THAT(AreEqual(4, Source.Num()));

		ASSERT_THAT(IsTrue(Source.Seal(NativeSchema, Error)));
		const FGuid ContractDigest = Source.GetContractDigest();
		TArray<FSeinCanonicalStateValueRecord> Records;
		ASSERT_THAT(IsTrue(
			Source.CaptureRecords(Records, Error)));
		ASSERT_THAT(AreEqual(4, Records.Num()));
		uint64 CapturedPayloadBytes = 0;
		for (const FSeinCanonicalStateValueRecord& Record : Records)
		{
			CapturedPayloadBytes +=
				static_cast<uint64>(Record.PayloadBytes.Num());
		}
		ASSERT_THAT(IsTrue(
			CapturedPayloadBytes
				<= FSeinCanonicalStateValueStore::
					MaxAggregatePayloadBytes));
		ASSERT_THAT(IsTrue(
			CapturedPayloadBytes > 55ull * 1024ull * 1024ull));

		Source.Reset();

		FSeinCanonicalStateValueStore ExpectedSchema;
		for (const FSeinCanonicalStateValueSlotDefinition& Definition :
			Definitions)
		{
			ASSERT_THAT(IsTrue(ExpectedSchema.RegisterSlot(
				NativeSchema,
				Definition,
				MakeLargeValue(0, 0),
				Error)));
		}
		ASSERT_THAT(IsTrue(ExpectedSchema.Seal(NativeSchema, Error)));
		ASSERT_THAT(IsTrue(
			ExpectedSchema.GetContractDigest() == ContractDigest));

		FSeinCanonicalStateValueStore Destination;
		TArray<uint8> SavedLastPayload =
			MoveTemp(Records.Last().PayloadBytes);
		Records.Last().PayloadBytes.SetNumUninitialized(
			OversizedReplacementBytes);
		ASSERT_THAT(IsFalse(Destination.TryRestoreRecords(
			ExpectedSchema,
			Records,
			ContractDigest,
			Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("aggregate payload bound"))));
		ASSERT_THAT(AreEqual(0, Destination.Num()));
		Records.Last().PayloadBytes = MoveTemp(SavedLastPayload);

		ASSERT_THAT(IsTrue(Destination.TryRestoreRecords(
			ExpectedSchema,
			Records,
			ContractDigest,
			Error)));
		FInstancedStruct Restored;
		ASSERT_THAT(IsTrue(
			Destination.GetValue(Definitions[3].Key, Restored)));
		ASSERT_THAT(AreEqual(
			3,
			Restored
				.Get<
					FSeinCanonicalStateValueStoreLargeTestPayload>()
				.Marker));
		ASSERT_THAT(IsFalse(
			Restored
				.Get<
					FSeinCanonicalStateValueStoreLargeTestPayload>()
				.Words.IsEmpty()));

		TArray<FSeinCanonicalStateValueRecord> TooManyRecords;
		TooManyRecords.SetNum(
			FSeinCanonicalStateValueStore::MaxSlots + 1);
		ASSERT_THAT(IsFalse(Destination.TryRestoreRecords(
			ExpectedSchema,
			TooManyRecords,
			ContractDigest,
			Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("checkpoint contract"))));
		ASSERT_THAT(IsTrue(
			Destination.GetValue(Definitions[3].Key, Restored)));
		ASSERT_THAT(AreEqual(
			3,
			Restored
				.Get<
					FSeinCanonicalStateValueStoreLargeTestPayload>()
				.Marker));
	}

	TEST(CanonicalStateValueStoreRestoreRoundTripsCanonicalRecords,
		"SeinARTS.Determinism.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		const FSeinCanonicalStateValueSlotDefinition First =
			MakeDefinition(TEXT("RestoreRoundTripA"));
		FSeinCanonicalStateValueSlotDefinition Second =
			MakeDefinition(TEXT("RestoreRoundTripB"));

		FSeinCanonicalStateValueStore Source;
		ASSERT_THAT(IsTrue(Source.RegisterSlot(
			NativeSchema, Second, MakeValue(22, {4, 6}), Error)));
		ASSERT_THAT(IsTrue(Source.RegisterSlot(
			NativeSchema, First, MakeValue(11, {1, 3, 5}), Error)));
		ASSERT_THAT(IsTrue(Source.Seal(NativeSchema, Error)));

		TArray<FSeinCanonicalStateValueRecord> SourceRecords;
		ASSERT_THAT(IsTrue(
			Source.CaptureRecords(SourceRecords, Error)));

		FSeinCanonicalStateValueStore ExpectedSchema;
		ASSERT_THAT(IsTrue(ExpectedSchema.RegisterSlot(
			NativeSchema, First, MakeValue(0), Error)));
		ASSERT_THAT(IsTrue(ExpectedSchema.RegisterSlot(
			NativeSchema, Second, MakeValue(0), Error)));
		ASSERT_THAT(IsTrue(ExpectedSchema.Seal(NativeSchema, Error)));
		ASSERT_THAT(IsTrue(
			ExpectedSchema.GetContractDigest()
				== Source.GetContractDigest()));

		int32 ForcedGCCount = 0;
		FSeinCanonicalStateValueStore::SetRestoreStagingTestHook(
			[&ForcedGCCount]()
			{
				++ForcedGCCount;
				CollectGarbage(RF_NoFlags);
			});
		ON_SCOPE_EXIT
		{
			FSeinCanonicalStateValueStore::SetRestoreStagingTestHook({});
		};
		FSeinCanonicalStateValueStore Restored;
		ASSERT_THAT(IsTrue(Restored.TryRestoreRecords(
			ExpectedSchema,
			SourceRecords,
			Source.GetContractDigest(),
			Error)));
		ASSERT_THAT(AreEqual(SourceRecords.Num(), ForcedGCCount));
		ASSERT_THAT(IsTrue(Restored.IsSealed()));
		ASSERT_THAT(IsTrue(
			Restored.GetContractDigest() == Source.GetContractDigest()));
		ASSERT_THAT(AreEqual(
			Source.GetCanonicalManifest(),
			Restored.GetCanonicalManifest()));

		TArray<FSeinCanonicalStateValueRecord> RestoredRecords;
		ASSERT_THAT(IsTrue(
			Restored.CaptureRecords(RestoredRecords, Error)));
		ASSERT_THAT(AreEqual(SourceRecords.Num(), RestoredRecords.Num()));
		for (int32 Index = 0; Index < SourceRecords.Num(); ++Index)
		{
			ASSERT_THAT(IsTrue(
				SameRecord(SourceRecords[Index], RestoredRecords[Index])));
		}

		FInstancedStruct RestoredFirst;
		ASSERT_THAT(IsTrue(Restored.GetValue(First.Key, RestoredFirst)));
		const FSeinCanonicalStateValueStoreTestPayload& FirstPayload =
			RestoredFirst.Get<FSeinCanonicalStateValueStoreTestPayload>();
		ASSERT_THAT(AreEqual(11, FirstPayload.Marker));
		ASSERT_THAT(AreEqual(3, FirstPayload.OrderedValues.Num()));
		ASSERT_THAT(AreEqual(5, FirstPayload.OrderedValues[2]));
	}

	TEST(CanonicalStateValueStoreRestoreFailuresAreAtomic,
		"SeinARTS.Determinism.CoreEntity.CanonicalState.ValueStore")
	{
		FString Error;
		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			CaptureNativeSchema(Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));

		const FSeinCanonicalStateValueSlotDefinition LiveDefinition =
			MakeDefinition(TEXT("RestoreAtomicLive"));
		FSeinCanonicalStateValueStore Live;
		ASSERT_THAT(IsTrue(Live.RegisterSlot(
			NativeSchema, LiveDefinition, MakeValue(90, {9}), Error)));
		ASSERT_THAT(IsTrue(Live.Seal(NativeSchema, Error)));
		TArray<FSeinCanonicalStateValueRecord> Baseline;
		ASSERT_THAT(IsTrue(Live.CaptureRecords(Baseline, Error)));
		ASSERT_THAT(AreEqual(1, Baseline.Num()));
		const FGuid BaselineContract = Live.GetContractDigest();

		const FSeinCanonicalStateValueSlotDefinition CheckpointDefinition =
			MakeDefinition(TEXT("RestoreAtomicCheckpoint"));
		const FInstancedStruct CheckpointValue =
			MakeValue(12, {1, 2});
		FSeinCanonicalStateValueStore Checkpoint;
		ASSERT_THAT(IsTrue(Checkpoint.RegisterSlot(
			NativeSchema,
			CheckpointDefinition,
			CheckpointValue,
			Error)));
		ASSERT_THAT(IsTrue(Checkpoint.Seal(NativeSchema, Error)));
		TArray<FSeinCanonicalStateValueRecord> Records;
		ASSERT_THAT(IsTrue(
			Checkpoint.CaptureRecords(Records, Error)));
		ASSERT_THAT(AreEqual(1, Records.Num()));
		ASSERT_THAT(IsFalse(Records[0].PayloadBytes.IsEmpty()));

		FSeinCanonicalStateValueStore ExpectedSchema;
		ASSERT_THAT(IsTrue(ExpectedSchema.RegisterSlot(
			NativeSchema,
			CheckpointDefinition,
			MakeValue(0),
			Error)));
		ASSERT_THAT(IsTrue(ExpectedSchema.Seal(NativeSchema, Error)));
		ASSERT_THAT(IsTrue(
			ExpectedSchema.GetContractDigest()
				== Checkpoint.GetContractDigest()));

		const FSeinStructWireLimits WireLimits{
			CheckpointDefinition.Limits.MaxEncodedBytes,
			CheckpointDefinition.Limits.MaxAggregateElements,
			FMath::Min(
				CheckpointDefinition.Limits.MaxEncodedBytes,
				1024 * 1024),
			CheckpointDefinition.Limits.MaxRecursionDepth,
			CheckpointDefinition.Limits.MaxEncodedBytes };
		TArray<uint8> DirectPayloadBytes;
		FSeinWireCost DirectCost;
		ASSERT_THAT(IsTrue(FSeinCanonicalStateCodec::EncodeWithCost(
			CheckpointValue.GetScriptStruct(),
			CheckpointValue.GetMemory(),
			{},
			WireLimits,
			DirectPayloadBytes,
			Error,
			DirectCost)));
		ASSERT_THAT(IsTrue(
			Records[0].PayloadBytes == DirectPayloadBytes));

		TArray<FSeinCanonicalStateValueRecord> Corrupted = Records;
		Corrupted[0].PayloadBytes.Last() ^= 0x01;
		ASSERT_THAT(IsFalse(Live.TryRestoreRecords(
			ExpectedSchema,
			Corrupted,
			Checkpoint.GetContractDigest(),
			Error)));
		ASSERT_THAT(IsFalse(Error.IsEmpty()));

		const FGuid WrongContract(1, 2, 3, 4);
		ASSERT_THAT(IsFalse(Live.TryRestoreRecords(
			ExpectedSchema, Records, WrongContract, Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("checkpoint contract"))));

		TArray<FSeinCanonicalStateValueRecord> AfterFailures;
		ASSERT_THAT(IsTrue(
			Live.CaptureRecords(AfterFailures, Error)));
		ASSERT_THAT(IsTrue(Live.IsSealed()));
		ASSERT_THAT(IsTrue(Live.GetContractDigest() == BaselineContract));
		ASSERT_THAT(AreEqual(1, AfterFailures.Num()));
		ASSERT_THAT(IsTrue(
			SameRecord(Baseline[0], AfterFailures[0])));
		FInstancedStruct LiveValue;
		ASSERT_THAT(IsTrue(Live.GetValue(LiveDefinition.Key, LiveValue)));
		ASSERT_THAT(AreEqual(
			90,
			LiveValue
				.Get<FSeinCanonicalStateValueStoreTestPayload>()
				.Marker));
	}
}
