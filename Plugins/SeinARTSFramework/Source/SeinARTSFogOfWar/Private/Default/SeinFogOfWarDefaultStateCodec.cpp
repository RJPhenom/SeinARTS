/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefaultStateCodec.cpp
 */

#include "Serialization/SeinFogOfWarCanonicalStateProvider.h"

#include "Default/SeinFogOfWarDefault.h"
#include "Default/SeinFogOfWarDefaultCanonicalState.h"
#include "Serialization/SeinCanonicalDigestTree.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalReflectedStateDigest.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Hash/Blake3.h"
#include "UObject/Package.h"

struct FSeinFogOfWarDefaultRoutineRootCache
{
	struct FObserverCache
	{
		FSeinCanonicalDigestTree ExploredCells;
		FSeinCanonicalDigestTree SeenEntities;
		FGuid Digest;
	};

	FSeinCanonicalDigestTree Observers;
	FSeinCanonicalDigestTree Sources;
	FSeinCanonicalDigestTree DynamicBlockers;
	TMap<FSeinPlayerID, FObserverCache> ObserverCaches;
	TMap<FSeinPlayerID, TSet<int32>> DirtyExploredCells;
	TMap<FSeinPlayerID, TSet<FSeinEntityHandle>> DirtySeenEntities;
	TSet<FSeinEntityHandle> DirtySources;
	FGuid ExploredCellDigest;
	FGuid SourceSchemaDigest;
	FGuid DynamicBlockerSchemaDigest;
	FGuid PayloadDigest;
	int32 NumCells = INDEX_NONE;
	int32 EntitySlotCount = INDEX_NONE;
	int32 DynamicBlockerCount = INDEX_NONE;
	uint64 EntityTopologyRevision = 0;
	uint64 MutationRevision = 1;
	bool bDynamicBlockersDirty = true;
};

namespace
{
	const FName OwnerModuleId(TEXT("SeinARTSFogOfWar"));
	constexpr int32 MaxPayloadBytes = 32 * 1024 * 1024;
	constexpr int32 MaxPayloadElements = 16 * 1024 * 1024;

	uint32 ReadDigestWord(const uint8* Bytes)
	{
		return (static_cast<uint32>(Bytes[0]) << 24)
			| (static_cast<uint32>(Bytes[1]) << 16)
			| (static_cast<uint32>(Bytes[2]) << 8)
			| static_cast<uint32>(Bytes[3]);
	}

	class FFogStaticGridDigestBuilder
	{
	public:
		FFogStaticGridDigestBuilder()
		{
			constexpr uint8 Domain[] = {
				'S', 'E', 'I', 'N', 'F', 'O', 'G', 'G', 'R', 'I', 'D', 1};
			Hasher.Update(Domain, UE_ARRAY_COUNT(Domain));
		}

		void WriteUInt32(uint32 Value)
		{
			const uint8 Bytes[4] = {
				static_cast<uint8>(Value >> 24),
				static_cast<uint8>(Value >> 16),
				static_cast<uint8>(Value >> 8),
				static_cast<uint8>(Value)};
			Hasher.Update(Bytes, UE_ARRAY_COUNT(Bytes));
		}

		void WriteUInt64(uint64 Value)
		{
			WriteUInt32(static_cast<uint32>(Value >> 32));
			WriteUInt32(static_cast<uint32>(Value));
		}

		void WriteInt32(int32 Value)
		{
			WriteUInt32(BitCast<uint32>(Value));
		}

		void WriteInt64(int64 Value)
		{
			WriteUInt64(static_cast<uint64>(Value));
		}

		void WriteBytes(TConstArrayView<uint8> Bytes)
		{
			WriteUInt64(static_cast<uint64>(Bytes.Num()));
			if (!Bytes.IsEmpty())
			{
				Hasher.Update(Bytes.GetData(), Bytes.Num());
			}
		}

		FGuid Finalize() const
		{
			const FBlake3Hash Hash = Hasher.Finalize();
			const uint8* Bytes = Hash.GetBytes();
			return FGuid(
				ReadDigestWord(Bytes),
				ReadDigestWord(Bytes + 4),
				ReadDigestWord(Bytes + 8),
				ReadDigestWord(Bytes + 12));
		}

	private:
		FBlake3 Hasher;
	};

	bool ValidateShape(const FSeinStampShape& Shape, FString& OutError)
	{
		switch (Shape.Shape)
		{
		case ESeinStampShape::Radial:
			if (Shape.Radius < FFixedPoint::Zero)
			{
				OutError = TEXT("Fog state contains a negative radial stamp radius.");
				return false;
			}
			break;

		case ESeinStampShape::Rect:
			if (Shape.HalfExtentX < FFixedPoint::Zero
				|| Shape.HalfExtentY < FFixedPoint::Zero)
			{
				OutError = TEXT("Fog state contains a negative rectangular stamp extent.");
				return false;
			}
			break;

		case ESeinStampShape::Conical:
			if (Shape.ConeLength < FFixedPoint::Zero
				|| Shape.ConeAngleDegrees <= FFixedPoint::Zero
				|| Shape.ConeAngleDegrees > FFixedPoint::FromInt(180))
			{
				OutError = TEXT("Fog state contains invalid conical stamp geometry.");
				return false;
			}
			break;

		default:
			OutError = TEXT("Fog state contains an unknown stamp shape.");
			return false;
		}
		return true;
	}

	bool IsStrictlySorted(
		TConstArrayView<FSeinEntityHandle> Handles,
		bool bRequireValid,
		FString& OutError)
	{
		for (int32 Index = 0; Index < Handles.Num(); ++Index)
		{
			if ((bRequireValid && !Handles[Index].IsValid())
				|| (Index > 0
					&& !(Handles[Index - 1] < Handles[Index])))
			{
				OutError =
					TEXT("Fog entity handles must be valid, unique, and strictly sorted.");
				return false;
			}
		}
		return true;
	}

	void AdvanceRevision(uint64& Revision)
	{
		Revision = Revision == MAX_uint64 ? 1 : Revision + 1;
	}

	FSeinCanonicalReflectedStateLimits RoutineReflectedLimits()
	{
		FSeinCanonicalReflectedStateLimits Limits;
		Limits.MaxAggregateElements = 64 * 1024;
		Limits.MaxStringCharacters = 16 * 1024;
		Limits.MaxTotalStringCharacters = 64 * 1024;
		Limits.MaxRecursionDepth = 32;
		Limits.MaxInstancedObjects = 0;
		return Limits;
	}

	bool ComputeSeenEntityDigest(
		FSeinEntityHandle Handle,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.Fog.Default.Routine.SeenEntity"), 1);
		return Writer.WriteInt32(Handle.Index)
			&& Writer.WriteInt32(Handle.Generation)
			&& Writer.Finalize(OutDigest, OutError);
	}

	bool ComputeObserverDigest(
		FSeinPlayerID Observer,
		const FGuid& ExploredRoot,
		const FGuid& SeenRoot,
		FGuid& OutDigest,
		FString& OutError)
	{
		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.Fog.Default.Routine.Observer"), 1);
		return Writer.WriteUInt8(Observer.Value)
			&& Writer.WriteGuid(ExploredRoot)
			&& Writer.WriteGuid(SeenRoot)
			&& Writer.Finalize(OutDigest, OutError);
	}
}

void USeinFogOfWarDefault::MarkRoutineExploredCellDirty(
	FSeinPlayerID Observer, int32 CellIndex)
{
	if (!RoutineRootCache.IsValid())
	{
		return;
	}
	RoutineRootCache->DirtyExploredCells.FindOrAdd(Observer).Add(CellIndex);
	AdvanceRevision(RoutineRootCache->MutationRevision);
}

void USeinFogOfWarDefault::MarkRoutineSeenEntityDirty(
	FSeinPlayerID Observer, FSeinEntityHandle Handle)
{
	if (!RoutineRootCache.IsValid())
	{
		return;
	}
	RoutineRootCache->DirtySeenEntities.FindOrAdd(Observer).Add(Handle);
	AdvanceRevision(RoutineRootCache->MutationRevision);
}

void USeinFogOfWarDefault::MarkRoutineSourceDirty(FSeinEntityHandle Handle)
{
	if (!RoutineRootCache.IsValid())
	{
		return;
	}
	RoutineRootCache->DirtySources.Add(Handle);
	AdvanceRevision(RoutineRootCache->MutationRevision);
}

void USeinFogOfWarDefault::MarkRoutineDynamicBlockersDirty()
{
	if (!RoutineRootCache.IsValid())
	{
		return;
	}
	RoutineRootCache->bDynamicBlockersDirty = true;
	AdvanceRevision(RoutineRootCache->MutationRevision);
}

void USeinFogOfWarDefault::ResetRoutineRootCache()
{
	RoutineRootCache.Reset();
}

struct FSeinFogOfWarDefaultStateCodec
{
	struct FRestoreStage final : ISeinFogOfWarStateRestoreStage
	{
		USeinFogOfWarDefault* Candidate = nullptr;

		virtual void GatherReferencedObjects(
			TArray<UObject*>& OutObjects) const override
		{
			if (Candidate)
			{
				OutObjects.Add(Candidate);
			}
		}
	};

	static bool ValidateStaticGrid(
		const USeinFogOfWarDefault& Fog,
		int32& OutNumCells,
		FString& OutError)
	{
		OutNumCells = 0;
		const int64 NumCells64 =
			static_cast<int64>(Fog.Width) * static_cast<int64>(Fog.Height);
		const bool bEmptyGrid = Fog.Width == 0 && Fog.Height == 0;
		if ((!bEmptyGrid && (Fog.Width <= 0 || Fog.Height <= 0))
			|| NumCells64 < 0
			|| NumCells64 > MAX_int32
			|| Fog.CellSize <= FFixedPoint::Zero)
		{
			OutError =
				TEXT("Default fog static grid dimensions or cell size are invalid.");
			return false;
		}

		OutNumCells = static_cast<int32>(NumCells64);
		if (Fog.GroundHeight.Num() != OutNumCells
			|| Fog.BlockerHeight.Num() != OutNumCells
			|| Fog.BlockerLayerMask.Num() != OutNumCells)
		{
			OutError =
				TEXT("Default fog static grid arrays do not match its dimensions.");
			return false;
		}
		return true;
	}

	static bool ComputeStaticEnvironmentDigest(
		const USeinFogOfWar& BaseFog,
		FGuid& OutDigest,
		FString& OutError)
	{
		OutDigest.Invalidate();
		OutError.Reset();
		const USeinFogOfWarDefault* Fog =
			Cast<USeinFogOfWarDefault>(&BaseFog);
		int32 NumCells = 0;
		if (!Fog || !ValidateStaticGrid(*Fog, NumCells, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Default fog codec received an incompatible implementation.");
			}
			return false;
		}

		if (Fog->StaticGridDigest.IsValid())
		{
			OutDigest = Fog->StaticGridDigest;
			return true;
		}

		FFogStaticGridDigestBuilder Writer;
		Writer.WriteInt32(Fog->Width);
		Writer.WriteInt32(Fog->Height);
		Writer.WriteInt64(Fog->CellSize.Value);
		Writer.WriteInt64(Fog->Origin.X.Value);
		Writer.WriteInt64(Fog->Origin.Y.Value);
		Writer.WriteInt64(Fog->Origin.Z.Value);
		Writer.WriteUInt64(static_cast<uint64>(NumCells));
		for (const FFixedPoint Value : Fog->GroundHeight)
		{
			Writer.WriteInt64(Value.Value);
		}
		for (const FFixedPoint Value : Fog->BlockerHeight)
		{
			Writer.WriteInt64(Value.Value);
		}
		Writer.WriteBytes(Fog->BlockerLayerMask);
		Fog->StaticGridDigest = Writer.Finalize();
		if (!Fog->StaticGridDigest.IsValid())
		{
			OutError =
				TEXT("Default fog static-grid digest was invalid.");
			return false;
		}
		OutDigest = Fog->StaticGridDigest;
		return true;
	}

	static void PackExplored(
		const FSeinFogVisionGroup& Group,
		int32 NumCells,
		TArray<uint8>& OutPacked)
	{
		OutPacked.SetNumZeroed((NumCells + 7) / 8);
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if ((Group.CellBitfield[Cell] & SEIN_FOW_BIT_EXPLORED) != 0)
			{
				OutPacked[Cell >> 3] |=
					static_cast<uint8>(1u << (Cell & 7));
			}
		}
	}

	static bool UnpackExplored(
		const TArray<uint8>& Packed,
		int32 NumCells,
		FSeinFogVisionGroup& OutGroup,
		FString& OutError)
	{
		const int32 ExpectedBytes = (NumCells + 7) / 8;
		if (Packed.Num() != ExpectedBytes)
		{
			OutError =
				TEXT("Fog explored-cell payload does not match the static grid.");
			return false;
		}
		if (ExpectedBytes > 0 && (NumCells & 7) != 0)
		{
			const uint8 UsedMask =
				static_cast<uint8>((1u << (NumCells & 7)) - 1u);
			if ((Packed.Last() & ~UsedMask) != 0)
			{
				OutError =
					TEXT("Fog explored-cell payload sets non-canonical padding bits.");
				return false;
			}
		}

		OutGroup.CellBitfield.SetNumZeroed(NumCells);
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			if ((Packed[Cell >> 3]
				& static_cast<uint8>(1u << (Cell & 7))) != 0)
			{
				OutGroup.CellBitfield[Cell] |= SEIN_FOW_BIT_EXPLORED;
			}
		}
		return true;
	}

	static void CopyStaticGrid(
		const USeinFogOfWarDefault& Source,
		USeinFogOfWarDefault& Target,
		int32 NumCells)
	{
		Target.Width = Source.Width;
		Target.Height = Source.Height;
		Target.CellSize = Source.CellSize;
		Target.Origin = Source.Origin;
		Target.GroundHeight = Source.GroundHeight;
		Target.BlockerHeight = Source.BlockerHeight;
		Target.BlockerLayerMask = Source.BlockerLayerMask;
		Target.DynamicBlockerHeight.SetNumZeroed(NumCells);
		Target.DynamicBlockerLayerMask.SetNumZeroed(NumCells);
		Target.DynamicBlockerHeightExceptions.Reset();
		Target.DynamicBlockerSnapshots.Reset();
		Target.LastDynamicBlockerCells.Reset();
		Target.VisionGroups.Reset();
		Target.SourceStates.Reset();
	}

	static bool CanApplyFootprint(
		FSeinFogVisionGroup& Group,
		uint8 Bit,
		const TArray<int32>& Cells,
		int32 NumCells,
		FString& OutError)
	{
		if (Cells.IsEmpty())
		{
			return true;
		}
		TArray<uint16>& RefCounts = Group.RefCounts[Bit];
		if (RefCounts.Num() != NumCells)
		{
			RefCounts.SetNumZeroed(NumCells);
		}

		for (int32 Cursor = 0; Cursor < Cells.Num();)
		{
			const int32 Cell = Cells[Cursor];
			if (Cell < 0 || Cell >= NumCells)
			{
				OutError =
					TEXT("Default fog reconstructed an out-of-grid footprint.");
				return false;
			}
			int32 End = Cursor + 1;
			while (End < Cells.Num() && Cells[End] == Cell)
			{
				++End;
			}
			const int32 RunLength = End - Cursor;
			if (RunLength > MAX_uint16 - RefCounts[Cell])
			{
				OutError =
					TEXT("Default fog source overlap exceeds its exact uint16 refcount bound.");
				return false;
			}
			Cursor = End;
		}
		return true;
	}

	static bool BuildCandidate(
		const USeinFogOfWarDefault& Live,
		const FSeinFogOfWarDefaultCanonicalState& Payload,
		TFunctionRef<bool(FSeinEntityHandle)> IsEntityValid,
		USeinFogOfWarDefault*& OutCandidate,
		FString& OutError)
	{
		OutCandidate = nullptr;
		int32 NumCells = 0;
		FGuid StaticDigest;
		if (!ValidateStaticGrid(Live, NumCells, OutError)
			|| !ComputeStaticEnvironmentDigest(
				Live, StaticDigest, OutError)
			|| Payload.StaticEnvironmentDigest != StaticDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Default fog payload targets a different static grid.");
			}
			return false;
		}

		USeinFogOfWarDefault* Candidate =
			NewObject<USeinFogOfWarDefault>(
				GetTransientPackage(),
				Live.GetClass(),
				NAME_None,
				RF_Transient);
		if (!Candidate)
		{
			OutError =
				TEXT("Default fog restore could not allocate isolated candidate state.");
			return false;
		}
		CopyStaticGrid(Live, *Candidate, NumCells);

		TSet<uint8> ObserverIds;
		uint8 PreviousObserver = 0;
		bool bHasPreviousObserver = false;
		for (const FSeinFogDefaultObserverState& Observer :
			Payload.Observers)
		{
			if ((bHasPreviousObserver
					&& PreviousObserver >= Observer.Observer.Value)
				|| !IsStrictlySorted(
					Observer.SeenEntities, true, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Fog observers must be unique and strictly sorted.");
				}
				return false;
			}
			PreviousObserver = Observer.Observer.Value;
			bHasPreviousObserver = true;
			ObserverIds.Add(Observer.Observer.Value);

			FSeinFogVisionGroup& Group =
				Candidate->VisionGroups.Add(Observer.Observer);
			if (!UnpackExplored(
				Observer.ExploredCells,
				NumCells,
				Group,
				OutError))
			{
				return false;
			}
			for (const FSeinEntityHandle Seen : Observer.SeenEntities)
			{
				if (!IsEntityValid(Seen))
				{
					OutError =
						TEXT("Fog seen-entity payload references an entity absent from the staged simulation.");
					return false;
				}
				Group.SeenEntities.Add(Seen);
			}
		}

		Candidate->DynamicBlockerSnapshots.Reserve(
			Payload.DynamicBlockers.Num());
		for (const FSeinFogDefaultDynamicBlockerInput& Input :
			Payload.DynamicBlockers)
		{
			if (Input.Height <= FFixedPoint::Zero
				|| Input.LayerMask == 0
				|| !ValidateShape(Input.Shape, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError =
						TEXT("Fog dynamic blocker payload is malformed.");
				}
				return false;
			}

			FSeinFogDynamicBlockerSnapshot& Snapshot =
				Candidate->DynamicBlockerSnapshots.AddDefaulted_GetRef();
			Snapshot.WorldPos = Input.WorldPos;
			Snapshot.Rotation = Input.Rotation;
			Snapshot.Shape = Input.Shape;
			Snapshot.Height = Input.Height;
			Snapshot.LayerMask = Input.LayerMask;
			Candidate->StampDynamicBlockerShape(
				Input.Shape,
				Input.WorldPos,
				Input.Rotation,
				Input.Height,
				Input.LayerMask);
		}

		FSeinEntityHandle PreviousSource;
		bool bHasPreviousSource = false;
		for (const FSeinFogDefaultSourceInput& Input : Payload.Sources)
		{
			if (!Input.Source.IsValid()
				|| (bHasPreviousSource
					&& !(PreviousSource < Input.Source))
				|| !ObserverIds.Contains(Input.Owner.Value))
			{
				OutError =
					TEXT("Fog sources must be valid, unique, strictly sorted, and owned by a serialized observer.");
				return false;
			}
			for (const FSeinVisionStamp& Stamp : Input.Stamps)
			{
				if (!ValidateShape(Stamp.Shape, OutError))
				{
					return false;
				}
			}
			PreviousSource = Input.Source;
			bHasPreviousSource = true;

			FSeinFogVisionGroup& Group =
				Candidate->VisionGroups.FindChecked(Input.Owner);
			FSeinFogSourceState& State =
				Candidate->SourceStates.Add(Input.Source);
			State.bValid = true;
			State.Owner = Input.Owner;
			State.WorldPos = Input.WorldPos;
			State.Rotation = Input.Rotation;
			State.EyeHeight = Input.EyeHeight;
			State.Stamps = Input.Stamps;

			for (uint8 Bit = 1; Bit <= 7; ++Bit)
			{
				const uint8 BitMask =
					static_cast<uint8>(1u << Bit);
				TArray<int32>& Cells = State.Footprints[Bit];
				for (const FSeinVisionStamp& Stamp : Input.Stamps)
				{
					if (Stamp.Shape.bEnabled
						&& Stamp.LayerMask != 0
						&& (Stamp.LayerMask & BitMask) != 0)
					{
						Candidate->GenerateLayerFootprintCells(
							Stamp.Shape,
							Input.WorldPos,
							Input.Rotation,
							Input.EyeHeight,
							Bit,
							Cells);
					}
				}
				Cells.Sort();
				if (!CanApplyFootprint(
					Group, Bit, Cells, NumCells, OutError))
				{
					return false;
				}
				const TArray<int32> Empty;
				Candidate->ApplyFootprintDiff(
					Input.Owner, Group, Bit, Empty, Cells);
			}
		}

		for (const FSeinFogDefaultObserverState& Observer :
			Payload.Observers)
		{
			const FSeinFogVisionGroup& Group =
				Candidate->VisionGroups.FindChecked(Observer.Observer);
			TArray<uint8> Repacked;
			PackExplored(Group, NumCells, Repacked);
			if (Repacked != Observer.ExploredCells)
			{
				OutError =
					TEXT("Fog explored-cell payload omits cells covered by its serialized source samples.");
				return false;
			}
		}

		OutCandidate = Candidate;
		return true;
	}

	static bool RefCountsEqual(
		const FSeinFogVisionGroup& A,
		const FSeinFogVisionGroup& B,
		int32 NumCells)
	{
		for (int32 Bit = 1; Bit <= 7; ++Bit)
		{
			for (int32 Cell = 0; Cell < NumCells; ++Cell)
			{
				const uint16 ACount = A.RefCounts[Bit].IsValidIndex(Cell)
					? A.RefCounts[Bit][Cell]
					: 0;
				const uint16 BCount = B.RefCounts[Bit].IsValidIndex(Cell)
					? B.RefCounts[Bit][Cell]
					: 0;
				if (ACount != BCount)
				{
					return false;
				}
			}
		}
		return true;
	}

	static bool CandidateMatchesLive(
		const USeinFogOfWarDefault& Live,
		const USeinFogOfWarDefault& Candidate,
		const USeinWorldSubsystem& World,
		int32 NumCells,
		FString& OutError)
	{
		if (Live.DynamicBlockerHeight
				!= Candidate.DynamicBlockerHeight
			|| Live.DynamicBlockerLayerMask
				!= Candidate.DynamicBlockerLayerMask
			|| !Live.DynamicBlockerHeightExceptions
				.OrderIndependentCompareEqual(
					Candidate.DynamicBlockerHeightExceptions)
			|| Live.DynamicBlockerSnapshots
				!= Candidate.DynamicBlockerSnapshots
			|| Live.LastDynamicBlockerCells
				!= Candidate.LastDynamicBlockerCells
			|| Live.VisionGroups.Num()
				!= Candidate.VisionGroups.Num()
			|| Live.SourceStates.Num()
				!= Candidate.SourceStates.Num())
		{
			OutError =
				TEXT("Default fog derived caches are not exactly reconstructible from their canonical samples.");
			return false;
		}

		for (const TPair<FSeinPlayerID, FSeinFogVisionGroup>& Pair :
			Live.VisionGroups)
		{
			const FSeinFogVisionGroup* Other =
				Candidate.VisionGroups.Find(Pair.Key);
			if (!Other
				|| Pair.Value.CellBitfield != Other->CellBitfield
				|| !RefCountsEqual(
					Pair.Value, *Other, NumCells))
			{
				OutError =
					TEXT("Default fog live visibility is not reconstructible from its canonical samples.");
				return false;
			}

			int32 ValidSeenCount = 0;
			for (const FSeinEntityHandle Seen :
				Pair.Value.SeenEntities)
			{
				if (World.IsEntityAlive(Seen))
				{
					++ValidSeenCount;
					if (!Other->SeenEntities.Contains(Seen))
					{
						OutError =
							TEXT("Default fog valid seen-entity latch was lost during canonical reconstruction.");
						return false;
					}
				}
			}
			if (Other->SeenEntities.Num() != ValidSeenCount)
			{
				OutError =
					TEXT("Default fog seen-entity canonical reconstruction is inconsistent.");
				return false;
			}
		}

		for (const TPair<FSeinEntityHandle, FSeinFogSourceState>& Pair :
			Live.SourceStates)
		{
			const FSeinFogSourceState* Other =
				Candidate.SourceStates.Find(Pair.Key);
			if (!Other
				|| !Pair.Value.bValid
				|| !Other->bValid
				|| Pair.Value.Owner != Other->Owner
				|| Pair.Value.WorldPos != Other->WorldPos
				|| Pair.Value.Rotation != Other->Rotation
				|| Pair.Value.EyeHeight != Other->EyeHeight
				|| Pair.Value.Stamps != Other->Stamps)
			{
				OutError =
					TEXT("Default fog source cache is not exactly reconstructible.");
				return false;
			}
			for (int32 Bit = 1; Bit <= 7; ++Bit)
			{
				if (Pair.Value.Footprints[Bit]
					!= Other->Footprints[Bit])
				{
					OutError =
						TEXT("Default fog source footprint is not exactly reconstructible.");
					return false;
				}
			}
		}
		return true;
	}

	static bool Capture(
		const FSeinFogOfWarStateCaptureContext& Context,
		FInstancedStruct& OutPayload,
		FString& OutError)
	{
		OutPayload.Reset();
		OutError.Reset();
		const USeinFogOfWarDefault* Live =
			Cast<USeinFogOfWarDefault>(&Context.Fog);
		int32 NumCells = 0;
		if (!Live
			|| !ValidateStaticGrid(*Live, NumCells, OutError)
			|| Live->DynamicBlockerHeight.Num() != NumCells
			|| Live->DynamicBlockerLayerMask.Num() != NumCells)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Default fog runtime grid is incomplete.");
			}
			return false;
		}

		FSeinFogOfWarDefaultCanonicalState Payload;
		if (!ComputeStaticEnvironmentDigest(
			*Live, Payload.StaticEnvironmentDigest, OutError))
		{
			return false;
		}

		TArray<FSeinPlayerID> ObserverKeys;
		Live->VisionGroups.GetKeys(ObserverKeys);
		ObserverKeys.Sort();
		Payload.Observers.Reserve(ObserverKeys.Num());
		for (const FSeinPlayerID Observer : ObserverKeys)
		{
			const FSeinFogVisionGroup& Group =
				Live->VisionGroups.FindChecked(Observer);
			if (Group.CellBitfield.Num() != NumCells)
			{
				OutError =
					TEXT("Default fog observer grid is incomplete.");
				return false;
			}

			FSeinFogDefaultObserverState& State =
				Payload.Observers.AddDefaulted_GetRef();
			State.Observer = Observer;
			PackExplored(Group, NumCells, State.ExploredCells);
			for (const FSeinEntityHandle Seen : Group.SeenEntities)
			{
				if (Context.World.IsEntityAlive(Seen))
				{
					State.SeenEntities.Add(Seen);
				}
			}
			State.SeenEntities.Sort();
		}

		TArray<FSeinEntityHandle> SourceKeys;
		Live->SourceStates.GetKeys(SourceKeys);
		SourceKeys.Sort();
		Payload.Sources.Reserve(SourceKeys.Num());
		for (const FSeinEntityHandle Source : SourceKeys)
		{
			const FSeinFogSourceState& LiveState =
				Live->SourceStates.FindChecked(Source);
			if (!Source.IsValid() || !LiveState.bValid)
			{
				OutError =
					TEXT("Default fog capture encountered a non-canonical invalid source cache.");
				return false;
			}
			FSeinFogDefaultSourceInput& State =
				Payload.Sources.AddDefaulted_GetRef();
			State.Source = Source;
			State.Owner = LiveState.Owner;
			State.WorldPos = LiveState.WorldPos;
			State.Rotation = LiveState.Rotation;
			State.EyeHeight = LiveState.EyeHeight;
			State.Stamps = LiveState.Stamps;
		}

		Payload.DynamicBlockers.Reserve(
			Live->DynamicBlockerSnapshots.Num());
		for (const FSeinFogDynamicBlockerSnapshot& Snapshot :
			Live->DynamicBlockerSnapshots)
		{
			FSeinFogDefaultDynamicBlockerInput& State =
				Payload.DynamicBlockers.AddDefaulted_GetRef();
			State.WorldPos = Snapshot.WorldPos;
			State.Rotation = Snapshot.Rotation;
			State.Shape = Snapshot.Shape;
			State.Height = Snapshot.Height;
			State.LayerMask = Snapshot.LayerMask;
		}

		USeinFogOfWarDefault* Candidate = nullptr;
		if (!BuildCandidate(
				*Live,
				Payload,
				[&Context](FSeinEntityHandle Handle)
				{
					return Context.World.IsEntityAlive(Handle);
				},
				Candidate,
				OutError)
			|| !CandidateMatchesLive(
				*Live,
				*Candidate,
				Context.World,
				NumCells,
				OutError))
		{
			return false;
		}

		OutPayload = FInstancedStruct::Make(MoveTemp(Payload));
		return true;
	}

	static bool CaptureRoutineRoot(
		const FSeinFogOfWarStateCaptureContext& Context,
		bool bForceFullRebuild,
		FGuid& OutPayloadDigest,
		uint64& OutProjectedPayloadBytes,
		uint64& OutMutationRevision,
		FString& OutError)
	{
		OutPayloadDigest.Invalidate();
		OutProjectedPayloadBytes = 0;
		OutMutationRevision = 0;
		OutError.Reset();
		const USeinFogOfWarDefault* Live =
			Cast<USeinFogOfWarDefault>(&Context.Fog);
		int32 NumCells = 0;
		if (!Live
			|| !ValidateStaticGrid(*Live, NumCells, OutError)
			|| Live->DynamicBlockerHeight.Num() != NumCells
			|| Live->DynamicBlockerLayerMask.Num() != NumCells)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Default fog routine root requires a complete runtime grid.");
			}
			return false;
		}

		FGuid StaticDigest;
		if (!ComputeStaticEnvironmentDigest(*Live, StaticDigest, OutError))
		{
			return false;
		}
		const int32 EntitySlotCount =
			Context.World.GetEntityPool().GetCapacity() + 1;
		const uint64 EntityTopologyRevision =
			Context.World.GetEntityPool().GetTopologyRevision();

		const TSharedPtr<FSeinFogOfWarDefaultRoutineRootCache> Existing =
			Live->RoutineRootCache;
		const bool bNeedFreshCache = bForceFullRebuild
			|| !Existing.IsValid()
			|| Existing->NumCells != NumCells;
		TSharedPtr<FSeinFogOfWarDefaultRoutineRootCache> Working =
			bNeedFreshCache
				? MakeShared<FSeinFogOfWarDefaultRoutineRootCache>()
				: Existing;
		FSeinFogOfWarDefaultRoutineRootCache& Cache = *Working;
		if (bNeedFreshCache && Existing.IsValid())
		{
			Cache.MutationRevision = Existing->MutationRevision;
		}

		if (!Cache.ExploredCellDigest.IsValid())
		{
			FSeinCanonicalDigestWriter Writer(
				TEXT("SeinARTS.Fog.Default.Routine.ExploredCell"), 1);
			if (!Writer.WriteBool(true)
				|| !Writer.Finalize(Cache.ExploredCellDigest, OutError))
			{
				return false;
			}
		}
		const FSeinCanonicalReflectedStateLimits Limits =
			RoutineReflectedLimits();
		if ((!Cache.SourceSchemaDigest.IsValid()
				&& !FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
					FSeinFogDefaultSourceInput::StaticStruct(),
					Limits,
					Cache.SourceSchemaDigest,
					OutError))
			|| (!Cache.DynamicBlockerSchemaDigest.IsValid()
				&& !FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
					FSeinFogDefaultDynamicBlockerInput::StaticStruct(),
					Limits,
					Cache.DynamicBlockerSchemaDigest,
					OutError)))
		{
			return false;
		}

		auto ComputeSourceLeaf =
			[&](FSeinEntityHandle Handle,
				const FSeinFogSourceState& Source,
				FGuid& OutDigest) -> bool
			{
				if (!Handle.IsValid() || !Source.bValid)
				{
					OutError = TEXT("Default fog routine root encountered an invalid source cache.");
					return false;
				}
				FSeinFogDefaultSourceInput Value;
				Value.Source = Handle;
				Value.Owner = Source.Owner;
				Value.WorldPos = Source.WorldPos;
				Value.Rotation = Source.Rotation;
				Value.EyeHeight = Source.EyeHeight;
				Value.Stamps = Source.Stamps;
				return FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
					FSeinFogDefaultSourceInput::StaticStruct(),
					&Value,
					Cache.SourceSchemaDigest,
					Limits,
					OutDigest,
					OutError);
			};

		auto BuildObserver =
			[&](FSeinPlayerID Observer,
				bool bRebuildExplored,
				bool bRebuildSeen) -> bool
			{
				const FSeinFogVisionGroup* Group =
					Live->VisionGroups.Find(Observer);
				if (!Group)
				{
					Cache.ObserverCaches.Remove(Observer);
					return Cache.Observers.SetLeafDigest(
						Observer.Value, FGuid(), OutError);
				}
				if (Group->CellBitfield.Num() != NumCells)
				{
					OutError = TEXT("Default fog routine root found an incomplete observer grid.");
					return false;
				}
				FSeinFogOfWarDefaultRoutineRootCache::FObserverCache& ObserverCache =
					Cache.ObserverCaches.FindOrAdd(Observer);
				if (bRebuildExplored
					|| ObserverCache.ExploredCells.Num() != NumCells)
				{
					if (!ObserverCache.ExploredCells.Reset(
							FString::Printf(
								TEXT("fog.default.observer.%u.explored"),
								Observer.Value),
							NumCells,
							OutError))
					{
						return false;
					}
					for (int32 CellIndex = 0; CellIndex < NumCells; ++CellIndex)
					{
						if ((Group->CellBitfield[CellIndex]
							& SEIN_FOW_BIT_EXPLORED) != 0
							&& !ObserverCache.ExploredCells.SetLeafDigest(
								CellIndex,
								Cache.ExploredCellDigest,
								OutError))
						{
							return false;
						}
					}
				}
				else if (const TSet<int32>* DirtyCells =
					Cache.DirtyExploredCells.Find(Observer))
				{
					for (const int32 CellIndex : *DirtyCells)
					{
						if (CellIndex == INDEX_NONE)
						{
							continue;
						}
						if (!Group->CellBitfield.IsValidIndex(CellIndex)
							|| !ObserverCache.ExploredCells.SetLeafDigest(
								CellIndex,
								(Group->CellBitfield[CellIndex]
									& SEIN_FOW_BIT_EXPLORED) != 0
									? Cache.ExploredCellDigest
									: FGuid(),
								OutError))
						{
							return false;
						}
					}
				}

				if (bRebuildSeen
					|| ObserverCache.SeenEntities.Num() != EntitySlotCount)
				{
					if (!ObserverCache.SeenEntities.Reset(
							FString::Printf(
								TEXT("fog.default.observer.%u.seen"),
								Observer.Value),
							EntitySlotCount,
							OutError))
					{
						return false;
					}
					for (const FSeinEntityHandle Handle : Group->SeenEntities)
					{
						if (!Context.World.IsEntityAlive(Handle))
						{
							continue;
						}
						FGuid SeenDigest;
						if (!ComputeSeenEntityDigest(Handle, SeenDigest, OutError)
							|| !ObserverCache.SeenEntities.SetLeafDigest(
								Handle.Index, SeenDigest, OutError))
						{
							return false;
						}
					}
				}
				else if (const TSet<FSeinEntityHandle>* DirtySeen =
					Cache.DirtySeenEntities.Find(Observer))
				{
					for (const FSeinEntityHandle Handle : *DirtySeen)
					{
						FGuid SeenDigest;
						if (Group->SeenEntities.Contains(Handle)
							&& Context.World.IsEntityAlive(Handle)
							&& !ComputeSeenEntityDigest(
								Handle, SeenDigest, OutError))
						{
							return false;
						}
						if (!ObserverCache.SeenEntities.SetLeafDigest(
							Handle.Index, SeenDigest, OutError))
						{
							return false;
						}
					}
				}
				if (!ObserverCache.ExploredCells.FinalizeUpdates(OutError)
					|| !ObserverCache.SeenEntities.FinalizeUpdates(OutError)
					|| !ComputeObserverDigest(
						Observer,
						ObserverCache.ExploredCells.GetRoot(),
						ObserverCache.SeenEntities.GetRoot(),
						ObserverCache.Digest,
						OutError)
					|| !Cache.Observers.SetLeafDigest(
						Observer.Value,
						ObserverCache.Digest,
						OutError))
				{
					return false;
				}
				return true;
			};

		const bool bRebuildObservers = bNeedFreshCache
			|| Cache.Observers.Num() != 256;
		const bool bRebuildEntityIndexed = bNeedFreshCache
			|| Cache.EntitySlotCount != EntitySlotCount;
		const bool bRebuildSeen = bRebuildEntityIndexed
			|| Cache.EntityTopologyRevision != EntityTopologyRevision;
		if (bRebuildObservers
			&& !Cache.Observers.Reset(
				TEXT("fog.default.observers"), 256, OutError))
		{
			return false;
		}
		if (bRebuildObservers || bRebuildSeen)
		{
			TArray<FSeinPlayerID> Observers;
			Live->VisionGroups.GetKeys(Observers);
			Observers.Sort();
			for (const FSeinPlayerID Observer : Observers)
			{
				if (!BuildObserver(
					Observer,
					bRebuildObservers,
					true))
				{
					return false;
				}
			}
		}
		else
		{
			TSet<FSeinPlayerID> DirtyObservers;
			for (const TPair<FSeinPlayerID, TSet<int32>>& Pair :
				Cache.DirtyExploredCells)
			{
				DirtyObservers.Add(Pair.Key);
			}
			for (const TPair<FSeinPlayerID, TSet<FSeinEntityHandle>>& Pair :
				Cache.DirtySeenEntities)
			{
				DirtyObservers.Add(Pair.Key);
			}
			for (const FSeinPlayerID Observer : DirtyObservers)
			{
				if (!BuildObserver(Observer, false, false))
				{
					return false;
				}
			}
		}
		if (!Cache.Observers.FinalizeUpdates(OutError))
		{
			return false;
		}

		if (bRebuildEntityIndexed)
		{
			if (!Cache.Sources.Reset(
					TEXT("fog.default.sources"),
					EntitySlotCount,
					OutError))
			{
				return false;
			}
			for (const TPair<FSeinEntityHandle, FSeinFogSourceState>& Pair :
				Live->SourceStates)
			{
				FGuid SourceDigest;
				if (!ComputeSourceLeaf(Pair.Key, Pair.Value, SourceDigest)
					|| !Cache.Sources.SetLeafDigest(
						Pair.Key.Index, SourceDigest, OutError))
				{
					return false;
				}
			}
		}
		else
		{
			for (const FSeinEntityHandle Handle : Cache.DirtySources)
			{
				FGuid SourceDigest;
				if (const FSeinFogSourceState* Source =
					Live->SourceStates.Find(Handle))
				{
					if (!ComputeSourceLeaf(Handle, *Source, SourceDigest))
					{
						return false;
					}
				}
				if (!Cache.Sources.SetLeafDigest(
					Handle.Index, SourceDigest, OutError))
				{
					return false;
				}
			}
		}
		if (!Cache.Sources.FinalizeUpdates(OutError))
		{
			return false;
		}

		const bool bRebuildDynamic = bNeedFreshCache
			|| Cache.bDynamicBlockersDirty
			|| Cache.DynamicBlockerCount
				!= Live->DynamicBlockerSnapshots.Num();
		if (bRebuildDynamic)
		{
			if (!Cache.DynamicBlockers.Reset(
					TEXT("fog.default.dynamic-blockers"),
					Live->DynamicBlockerSnapshots.Num(),
					OutError))
			{
				return false;
			}
			for (int32 Index = 0;
				Index < Live->DynamicBlockerSnapshots.Num();
				++Index)
			{
				const FSeinFogDynamicBlockerSnapshot& Snapshot =
					Live->DynamicBlockerSnapshots[Index];
				FSeinFogDefaultDynamicBlockerInput Value;
				Value.WorldPos = Snapshot.WorldPos;
				Value.Rotation = Snapshot.Rotation;
				Value.Shape = Snapshot.Shape;
				Value.Height = Snapshot.Height;
				Value.LayerMask = Snapshot.LayerMask;
				FGuid Digest;
				if (!FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
						FSeinFogDefaultDynamicBlockerInput::StaticStruct(),
						&Value,
						Cache.DynamicBlockerSchemaDigest,
						Limits,
						Digest,
						OutError)
					|| !Cache.DynamicBlockers.SetLeafDigest(
						Index, Digest, OutError))
				{
					return false;
				}
			}
		}
		if (!Cache.DynamicBlockers.FinalizeUpdates(OutError))
		{
			return false;
		}

		FSeinCanonicalDigestWriter PayloadWriter(
			TEXT("SeinARTS.Fog.Default.Routine.Payload"), 1);
		if (!PayloadWriter.WriteGuid(StaticDigest)
			|| !PayloadWriter.WriteGuid(Cache.Observers.GetRoot())
			|| !PayloadWriter.WriteGuid(Cache.Sources.GetRoot())
			|| !PayloadWriter.WriteGuid(Cache.DynamicBlockers.GetRoot())
			|| !PayloadWriter.Finalize(Cache.PayloadDigest, OutError))
		{
			return false;
		}

		Cache.NumCells = NumCells;
		Cache.EntitySlotCount = EntitySlotCount;
		Cache.DynamicBlockerCount =
			Live->DynamicBlockerSnapshots.Num();
		Cache.EntityTopologyRevision = EntityTopologyRevision;
		Cache.DirtyExploredCells.Reset();
		Cache.DirtySeenEntities.Reset();
		Cache.DirtySources.Reset();
		Cache.bDynamicBlockersDirty = false;
		Live->RoutineRootCache = Working;
		OutPayloadDigest = Cache.PayloadDigest;
		OutProjectedPayloadBytes = 4 * sizeof(FGuid);
		OutMutationRevision = Cache.MutationRevision;
		return true;
	}

	static bool StageRestore(
		const FSeinFogOfWarStateStageContext& Context,
		const FInstancedStruct& Payload,
		TUniquePtr<ISeinFogOfWarStateRestoreStage>& OutStage,
		FString& OutError)
	{
		OutStage.Reset();
		OutError.Reset();
		const USeinFogOfWarDefault* Live =
			Cast<USeinFogOfWarDefault>(&Context.Fog);
		const FSeinFogOfWarDefaultCanonicalState* State =
			Payload.GetPtr<FSeinFogOfWarDefaultCanonicalState>();
		if (!Live || !State || !Context.Candidate)
		{
			OutError =
				TEXT("Default fog restore requires its exact payload and staged simulation.");
			return false;
		}

		USeinFogOfWarDefault* Candidate = nullptr;
		if (!BuildCandidate(
			*Live,
			*State,
			[&Context](FSeinEntityHandle Handle)
			{
				return Context.Candidate->IsEntityValid(Handle);
			},
			Candidate,
			OutError))
		{
			return false;
		}

		TUniquePtr<FRestoreStage> Stage = MakeUnique<FRestoreStage>();
		Stage->Candidate = Candidate;
		OutStage = MoveTemp(Stage);
		return true;
	}

	static void CommitRestore(
		FSeinFogOfWarStateCommitContext& Context,
		TUniquePtr<ISeinFogOfWarStateRestoreStage>&& OpaqueStage)
	{
		USeinFogOfWarDefault* Live =
			Cast<USeinFogOfWarDefault>(&Context.Fog);
		FRestoreStage* Stage =
			static_cast<FRestoreStage*>(OpaqueStage.Get());
		check(Live && Stage && Stage->Candidate);
		USeinFogOfWarDefault& Candidate = *Stage->Candidate;
		check(Candidate.GetClass() == Live->GetClass());

		Live->DynamicBlockerHeight =
			MoveTemp(Candidate.DynamicBlockerHeight);
		Live->DynamicBlockerLayerMask =
			MoveTemp(Candidate.DynamicBlockerLayerMask);
		Live->DynamicBlockerHeightExceptions =
			MoveTemp(Candidate.DynamicBlockerHeightExceptions);
		Live->DynamicBlockerSnapshots =
			MoveTemp(Candidate.DynamicBlockerSnapshots);
		Live->LastDynamicBlockerCells =
			MoveTemp(Candidate.LastDynamicBlockerCells);
		Live->VisionGroups = MoveTemp(Candidate.VisionGroups);
		Live->SourceStates = MoveTemp(Candidate.SourceStates);
		Live->ResetRoutineRootCache();
	}
};

FSeinFogOfWarStateCodecRegistrationHandle
SeinRegisterDefaultFogOfWarStateCodec(FString& OutError)
{
	OutError.Reset();
	FSeinFogOfWarStateCodecDescriptor Descriptor;
	Descriptor.SupportedClass = USeinFogOfWarDefault::StaticClass();
	Descriptor.SubclassPolicy =
		ESeinFogOfWarStateCodecSubclassPolicy::
			DataOnlyBlueprintGeneratedChildren;
	Descriptor.StableImplementationId =
		TEXT("seinarts.fog.default-grid");
	Descriptor.StateSchemaVersion = 1;
	Descriptor.BehaviorRevision = 2;
	Descriptor.CodecRevision = 5;
	Descriptor.PayloadStruct =
		FSeinFogOfWarDefaultCanonicalState::StaticStruct();
	Descriptor.Limits.MaxRecursionDepth = 32;
	Descriptor.Limits.MaxEncodedBytes = MaxPayloadBytes;
	Descriptor.Limits.MaxAggregateElements = MaxPayloadElements;
	if (!FSeinCanonicalStateCodec::ComputeSchemaDigest(
		Descriptor.PayloadStruct,
		Descriptor.PayloadSchemaDigest,
		OutError))
	{
		return {};
	}

	FSeinFogOfWarStateCodecOps Ops;
	Ops.ComputeStaticEnvironmentDigest =
		&FSeinFogOfWarDefaultStateCodec::
			ComputeStaticEnvironmentDigest;
	Ops.Capture = &FSeinFogOfWarDefaultStateCodec::Capture;
	Ops.CaptureRoutineRoot =
		&FSeinFogOfWarDefaultStateCodec::CaptureRoutineRoot;
	Ops.StageRestore =
		&FSeinFogOfWarDefaultStateCodec::StageRestore;
	Ops.CommitRestore =
		&FSeinFogOfWarDefaultStateCodec::CommitRestore;
	return FSeinFogOfWarStateCodecRegistry::Register(
		OwnerModuleId,
		Descriptor,
		MoveTemp(Ops),
		&OutError);
}
