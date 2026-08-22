/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatTargetIndex.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements the derived spatial index used to prefilter target
 *               acquisition candidates without changing query semantics.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Combat/SeinCombatTargetIndex.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "Components/SeinVitalsComponent.h"
#include "Core/SeinEntityPool.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"

int64 FSeinCombatTargetIndex::MakeCellKey(int32 CellX, int32 CellY)
{
	const uint64 Bits =
		(static_cast<uint64>(static_cast<uint32>(CellX)) << 32)
		| static_cast<uint32>(CellY);
	return BitCast<int64>(Bits);
}

int32 FSeinCombatTargetIndex::ToCell(FFixedPoint WorldCoord)
{
	const int64 RawCellSize =
		static_cast<int64>(FFixedPoint::FromInt(CellSizeUnits));
	const int64 RawCoord = WorldCoord.Value;
	int64 Cell = RawCoord / RawCellSize;
	if (RawCoord < 0 && RawCoord % RawCellSize != 0)
	{
		--Cell;
	}
	return static_cast<int32>(Cell);
}

void FSeinCombatTargetIndex::Invalidate()
{
	Entries.Reset();
	EntityTopologyRevision = 0;
	EntityMutationRevision = 0;
	VitalsTopologyRevision = 0;
	bValid = false;
}

void FSeinCombatTargetIndex::RebuildIfStale(
	const USeinWorldSubsystem& World) const
{
	const FSeinEntityPool& EntityPool = World.GetEntityPool();
	const ISeinComponentStorage* VitalsStorage =
		World.GetComponentStorageRaw(FSeinVitalsComponent::StaticStruct());
	const uint64 CurrentEntityTopology = EntityPool.GetTopologyRevision();
	const uint64 CurrentEntityMutation =
		EntityPool.GetLatestMutationRevision();
	const uint64 CurrentVitalsTopology = VitalsStorage
		? VitalsStorage->GetTopologyRevision()
		: 0;
	if (bValid
		&& EntityTopologyRevision == CurrentEntityTopology
		&& EntityMutationRevision == CurrentEntityMutation
		&& VitalsTopologyRevision == CurrentVitalsTopology)
	{
		return;
	}

	Entries.Reset();
	if (VitalsStorage)
	{
		Entries.Reserve(VitalsStorage->GetComponentCount());
		VitalsStorage->ForEachLiveComponent(
			[&](FSeinEntityHandle Handle, const void*)
			{
				const FSeinEntity* Entity = World.GetEntity(Handle);
				if (!Entity)
				{
					return;
				}
				FCellEntry& Entry = Entries.AddDefaulted_GetRef();
				Entry.CellKey = MakeCellKey(
					ToCell(Entity->Transform.GetLocation().X),
					ToCell(Entity->Transform.GetLocation().Y));
				Entry.Handle = Handle;
			});
	}

	Algo::Sort(Entries, [](const FCellEntry& A, const FCellEntry& B)
	{
		if (A.CellKey != B.CellKey)
		{
			return A.CellKey < B.CellKey;
		}
		if (A.Handle.Index != B.Handle.Index)
		{
			return A.Handle.Index < B.Handle.Index;
		}
		return A.Handle.Generation < B.Handle.Generation;
	});

	EntityTopologyRevision = CurrentEntityTopology;
	EntityMutationRevision = CurrentEntityMutation;
	VitalsTopologyRevision = CurrentVitalsTopology;
	bValid = true;
}

bool FSeinCombatTargetIndex::QueryRadius(
	const USeinWorldSubsystem& World,
	const FFixedVector& Origin,
	FFixedPoint Radius,
	FSeinEntityHandle Exclude,
	TArray<FSeinEntityHandle>& OutHandles) const
{
	RebuildIfStale(World);
	if (Entries.IsEmpty() || Radius <= FFixedPoint::Zero)
	{
		return true;
	}

	const int64 RawRadius = Radius.Value;
	const auto CanExpandWithoutWrapping = [RawRadius](int64 RawOrigin)
	{
		return RawOrigin >= MIN_int64 + RawRadius
			&& RawOrigin <= MAX_int64 - RawRadius;
	};
	if (!CanExpandWithoutWrapping(Origin.X.Value)
		|| !CanExpandWithoutWrapping(Origin.Y.Value))
	{
		return false;
	}

	const int32 MinX = ToCell(Origin.X - Radius);
	const int32 MaxX = ToCell(Origin.X + Radius);
	const int32 MinY = ToCell(Origin.Y - Radius);
	const int32 MaxY = ToCell(Origin.Y + Radius);
	const int64 Width = static_cast<int64>(MaxX) - MinX + 1;
	const int64 Height = static_cast<int64>(MaxY) - MinY + 1;
	if (Width <= 0 || Height <= 0
		|| Width > MAX_int64 / Height
		|| Width * Height > FMath::Max<int64>(Entries.Num(), 64))
	{
		return false;
	}

	const int32 AppendStart = OutHandles.Num();
	for (int32 CellY = MinY; CellY <= MaxY; ++CellY)
	{
		for (int32 CellX = MinX; CellX <= MaxX; ++CellX)
		{
			const int64 Key = MakeCellKey(CellX, CellY);
			const int32 RunStart = Algo::LowerBound(
				Entries,
				Key,
				[](const FCellEntry& Entry, int64 CellKey)
				{
					return Entry.CellKey < CellKey;
				});
			for (int32 Index = RunStart;
				Index < Entries.Num() && Entries[Index].CellKey == Key;
				++Index)
			{
				if (Entries[Index].Handle != Exclude)
				{
					OutHandles.Add(Entries[Index].Handle);
				}
			}
		}
	}

	const int32 AppendedCount = OutHandles.Num() - AppendStart;
	if (AppendedCount > 1)
	{
		TArrayView<FSeinEntityHandle> Appended(
			OutHandles.GetData() + AppendStart, AppendedCount);
		Appended.Sort([](const FSeinEntityHandle& A, const FSeinEntityHandle& B)
		{
			if (A.Index != B.Index)
			{
				return A.Index < B.Index;
			}
			return A.Generation < B.Generation;
		});
	}
	return true;
}
