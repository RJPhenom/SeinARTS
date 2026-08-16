#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinFogVisibilityComponent.h"
#include "Components/SeinVisionComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "Default/SeinFogOfWarDefault.h"
#include "Lib/SeinFogOfWarBPFL.h"
#include "SeinFogOfWarSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinFogOfWarStateCodecTestTypes.h"
#include "TestTypes/SeinLevelDataTestTypes.h"

namespace UE::SeinARTSTests
{
	struct FFogOfWarDefaultTestAccess
	{
		struct FRuntimeSnapshot
		{
			TArray<FFixedPoint> DynamicBlockerHeight;
			TArray<uint8> DynamicBlockerLayerMask;
			TMap<int32, FSeinFogDynamicBlockerLayerHeights>
				DynamicBlockerHeightExceptions;
			TArray<FSeinFogDynamicBlockerSnapshot>
				DynamicBlockerSnapshots;
			TArray<int32> LastDynamicBlockerCells;
			TMap<FSeinPlayerID, FSeinFogVisionGroup>
				VisionGroups;
			TMap<FSeinEntityHandle, FSeinFogSourceState>
				SourceStates;
		};

		static void SeedDynamicOverlay(USeinFogOfWarDefault& Fog)
		{
			for (FFixedPoint& Height : Fog.DynamicBlockerHeight)
			{
				Height = FFixedPoint::FromInt(300);
			}
			for (uint8& Mask : Fog.DynamicBlockerLayerMask)
			{
				Mask = SEIN_FOW_BIT_NORMAL;
			}
			Fog.DynamicBlockerSnapshots.AddDefaulted();
			for (int32 Index = 0; Index < Fog.DynamicBlockerHeight.Num(); ++Index)
			{
				Fog.LastDynamicBlockerCells.Add(Index);
			}
		}

		static bool IsDynamicOverlayClear(const USeinFogOfWarDefault& Fog, int32 ExpectedCells)
		{
			if (Fog.DynamicBlockerHeight.Num() != ExpectedCells
				|| Fog.DynamicBlockerLayerMask.Num() != ExpectedCells
				|| !Fog.DynamicBlockerHeightExceptions.IsEmpty()
				|| !Fog.DynamicBlockerSnapshots.IsEmpty()
				|| !Fog.LastDynamicBlockerCells.IsEmpty())
			{
				return false;
			}
			for (int32 Index = 0; Index < ExpectedCells; ++Index)
			{
				if (Fog.DynamicBlockerHeight[Index] != FFixedPoint::Zero
					|| Fog.DynamicBlockerLayerMask[Index] != 0)
				{
					return false;
				}
			}
			return true;
		}

		static void SeedOneCellSource(USeinFogOfWarDefault& Fog,
			FSeinEntityHandle Source, FSeinPlayerID Owner)
		{
			Fog.Width = 1;
			Fog.Height = 1;

			FSeinFogVisionGroup& Group = Fog.GetOrCreateGroup(Owner);
			Group.CellBitfield[0] = SEIN_FOW_BIT_EXPLORED | SEIN_FOW_BIT_NORMAL;
			Group.RefCounts[1].SetNumZeroed(1);
			Group.RefCounts[1][0] = 1;

			FSeinFogSourceState& State = Fog.SourceStates.FindOrAdd(Source);
			State.bValid = true;
			State.Owner = Owner;
			State.Footprints[1].Add(0);
		}

		static void InvalidateForBlockerChangeThenRemove(
			USeinFogOfWarDefault& Fog, FSeinEntityHandle Source)
		{
			// This is the exact ordering in TickStamps when a dynamic blocker
			// changes on the same fog tick that a cached source disappears.
			for (TPair<FSeinEntityHandle, FSeinFogSourceState>& Pair : Fog.SourceStates)
			{
				Pair.Value.bValid = false;
			}
			Fog.RemoveSourceStamp(Source);
		}

		static bool HasSource(const USeinFogOfWarDefault& Fog,
			FSeinEntityHandle Source)
		{
			return Fog.SourceStates.Contains(Source);
		}

		static uint8 GetCellBits(const USeinFogOfWarDefault& Fog,
			FSeinPlayerID Owner)
		{
			const FSeinFogVisionGroup* Group = Fog.VisionGroups.Find(Owner);
			return Group ? Group->CellBitfield[0] : 0;
		}

		static uint16 GetNormalRefCount(const USeinFogOfWarDefault& Fog,
			FSeinPlayerID Owner)
		{
			const FSeinFogVisionGroup* Group = Fog.VisionGroups.Find(Owner);
			return Group ? Group->RefCounts[1][0] : 0;
		}

		static void SeedCanonicalStaticGrid(
			USeinFogOfWarDefault& Fog,
			int32 GridVariant = 0)
		{
			constexpr int32 NumCells = 6;
			Fog.Width = 3;
			Fog.Height = 2;
			Fog.CellSize = FFixedPoint::FromInt(100);
			Fog.Origin = FFixedVector::ZeroVector;
			Fog.GroundHeight.SetNumZeroed(NumCells);
			Fog.GroundHeight[0] =
				FFixedPoint::FromInt(GridVariant);
			Fog.BlockerHeight.SetNumZeroed(NumCells);
			Fog.BlockerLayerMask.SetNumZeroed(NumCells);
			Fog.StaticGridDigest.Invalidate();
			Fog.DynamicBlockerHeight.SetNumZeroed(NumCells);
			Fog.DynamicBlockerLayerMask.SetNumZeroed(NumCells);
			Fog.DynamicBlockerHeightExceptions.Reset();
			Fog.DynamicBlockerSnapshots.Reset();
			Fog.LastDynamicBlockerCells.Reset();
			Fog.VisionGroups.Reset();
			Fog.SourceStates.Reset();
		}

		static void SeedOneCellDynamicGrid(
			USeinFogOfWarDefault& Fog)
		{
			Fog.Width = 1;
			Fog.Height = 1;
			Fog.CellSize = FFixedPoint::FromInt(100);
			Fog.Origin = FFixedVector::ZeroVector;
			Fog.GroundHeight.SetNumZeroed(1);
			Fog.BlockerHeight.SetNumZeroed(1);
			Fog.BlockerLayerMask.SetNumZeroed(1);
			Fog.DynamicBlockerHeight.SetNumZeroed(1);
			Fog.DynamicBlockerLayerMask.SetNumZeroed(1);
			Fog.DynamicBlockerHeightExceptions.Reset();
			Fog.DynamicBlockerSnapshots.Reset();
			Fog.LastDynamicBlockerCells.Reset();
		}

		static void SeedOneCellObserverState(
			USeinFogOfWarDefault& Fog,
			FSeinPlayerID Observer,
			uint8 CellBits,
			FSeinEntityHandle SeenEntity = FSeinEntityHandle())
		{
			check(Fog.Width == 1 && Fog.Height == 1);
			FSeinFogVisionGroup& Group = Fog.GetOrCreateGroup(Observer);
			Group.CellBitfield[0] = CellBits;
			Group.SeenEntities.Reset();
			if (SeenEntity.IsValid())
			{
				Group.SeenEntities.Add(SeenEntity);
			}
		}

		static void ClearRuntimeGridDimensions(
			USeinFogOfWarDefault& Fog)
		{
			Fog.Width = 0;
			Fog.Height = 0;
		}

		static void StampOneCellDynamicBlocker(
			USeinFogOfWarDefault& Fog,
			FFixedPoint ShapeBaseZ,
			FFixedPoint VerticalExtent,
			uint8 LayerMask)
		{
			FSeinStampShape Shape;
			Shape.Shape = ESeinStampShape::Radial;
			Shape.Radius = FFixedPoint::FromInt(60);
			Fog.StampDynamicBlockerShape(
				Shape,
				FFixedVector(
					FFixedPoint::FromInt(50),
					FFixedPoint::FromInt(50),
					ShapeBaseZ),
				FFixedQuaternion::Identity,
				VerticalExtent,
				LayerMask);
		}

		static bool IsOneCellOpaque(
			const USeinFogOfWarDefault& Fog,
			FFixedPoint RayZ,
			uint8 LayerMask)
		{
			return Fog.IsCellOpaqueToEye(
				0, 0, RayZ, LayerMask);
		}

		static FFixedPoint GetOneCellDynamicTop(
			const USeinFogOfWarDefault& Fog,
			uint8 LayerMask)
		{
			return Fog.GetDynamicBlockerTopForMask(
				0, LayerMask);
		}

		static void ScaleStampRangeForTerrain(
			FSeinStampShape& Shape,
			FFixedPoint Multiplier)
		{
			USeinFogOfWarDefault::ScaleStampRangeForTerrain(
				Shape, Multiplier);
		}

		static bool RebuildDynamicBlockers(
			USeinFogOfWarDefault& Fog,
			UWorld& World)
		{
			return Fog.RebuildDynamicBlockers(&World);
		}

		static const FSeinFogDynamicBlockerSnapshot*
			GetFirstDynamicBlocker(
				const USeinFogOfWarDefault& Fog)
		{
			return Fog.DynamicBlockerSnapshots.IsEmpty()
				? nullptr
				: &Fog.DynamicBlockerSnapshots[0];
		}

		static bool MatchesCanonicalStaticGrid(
			const USeinFogOfWarDefault& Fog,
			int32 GridVariant)
		{
			if (Fog.Width != 3
				|| Fog.Height != 2
				|| Fog.CellSize != FFixedPoint::FromInt(100)
				|| Fog.Origin != FFixedVector::ZeroVector
				|| Fog.GroundHeight.Num() != 6
				|| Fog.BlockerHeight.Num() != 6
				|| Fog.BlockerLayerMask.Num() != 6
				|| Fog.GroundHeight[0]
					!= FFixedPoint::FromInt(GridVariant))
			{
				return false;
			}
			for (int32 Index = 1; Index < Fog.GroundHeight.Num(); ++Index)
			{
				if (Fog.GroundHeight[Index] != FFixedPoint::Zero)
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < Fog.BlockerHeight.Num(); ++Index)
			{
				if (Fog.BlockerHeight[Index] != FFixedPoint::Zero
					|| Fog.BlockerLayerMask[Index] != 0)
				{
					return false;
				}
			}
			return true;
		}

		static void SetCanonicalStaticGridVariantPreservingRuntime(
			USeinFogOfWarDefault& Fog,
			int32 GridVariant)
		{
			check(Fog.GroundHeight.Num() == 6);
			Fog.GroundHeight[0] =
				FFixedPoint::FromInt(GridVariant);
			Fog.StaticGridDigest.Invalidate();
		}

		static FRuntimeSnapshot CaptureRuntime(
			const USeinFogOfWarDefault& Fog)
		{
			FRuntimeSnapshot Snapshot;
			Snapshot.DynamicBlockerHeight =
				Fog.DynamicBlockerHeight;
			Snapshot.DynamicBlockerLayerMask =
				Fog.DynamicBlockerLayerMask;
			Snapshot.DynamicBlockerHeightExceptions =
				Fog.DynamicBlockerHeightExceptions;
			Snapshot.DynamicBlockerSnapshots =
				Fog.DynamicBlockerSnapshots;
			Snapshot.LastDynamicBlockerCells =
				Fog.LastDynamicBlockerCells;
			Snapshot.VisionGroups = Fog.VisionGroups;
			Snapshot.SourceStates = Fog.SourceStates;
			return Snapshot;
		}

		static bool MatchesRuntime(
			const USeinFogOfWarDefault& Fog,
			const FRuntimeSnapshot& Expected)
		{
			if (Fog.DynamicBlockerHeight
					!= Expected.DynamicBlockerHeight
				|| Fog.DynamicBlockerLayerMask
					!= Expected.DynamicBlockerLayerMask
				|| !Fog.DynamicBlockerHeightExceptions
					.OrderIndependentCompareEqual(
						Expected.DynamicBlockerHeightExceptions)
				|| Fog.DynamicBlockerSnapshots
					!= Expected.DynamicBlockerSnapshots
				|| Fog.LastDynamicBlockerCells
					!= Expected.LastDynamicBlockerCells
				|| Fog.VisionGroups.Num()
					!= Expected.VisionGroups.Num()
				|| Fog.SourceStates.Num()
					!= Expected.SourceStates.Num())
			{
				return false;
			}

			for (const TPair<
				FSeinPlayerID,
				FSeinFogVisionGroup>& Pair :
				Expected.VisionGroups)
			{
				const FSeinFogVisionGroup* Actual =
					Fog.VisionGroups.Find(Pair.Key);
				if (!Actual
					|| !VisionGroupsEqual(
						*Actual, Pair.Value))
				{
					return false;
				}
			}

			for (const TPair<
				FSeinEntityHandle,
				FSeinFogSourceState>& Pair :
				Expected.SourceStates)
			{
				const FSeinFogSourceState* Actual =
					Fog.SourceStates.Find(Pair.Key);
				if (!Actual
					|| !SourceStatesEqual(
						*Actual, Pair.Value))
				{
					return false;
				}
			}
			return true;
		}

		static bool IsCanonicalRuntimeEmpty(
			const USeinFogOfWarDefault& Fog)
		{
			return IsDynamicOverlayClear(Fog, 6)
				&& Fog.VisionGroups.IsEmpty()
				&& Fog.SourceStates.IsEmpty();
		}

		static FSeinEntityHandle SeedCanonicalRuntime(
			USeinFogOfWarDefault& Fog,
			FSeinEntityHandle SeenEntity)
		{
			FSeinFogDynamicBlockerSnapshot& Blocker =
				Fog.DynamicBlockerSnapshots.AddDefaulted_GetRef();
			Blocker.WorldPos = FFixedVector(
				FFixedPoint::FromInt(250),
				FFixedPoint::FromInt(150),
				FFixedPoint::Zero);
			Blocker.Rotation = FFixedQuaternion::Identity;
			Blocker.Shape.Shape = ESeinStampShape::Radial;
			Blocker.Shape.Radius = FFixedPoint::FromInt(40);
			Blocker.Height = FFixedPoint::FromInt(300);
			Blocker.LayerMask = SEIN_FOW_BIT_NORMAL;
			Fog.StampDynamicBlockerShape(
				Blocker.Shape,
				Blocker.WorldPos,
				Blocker.Rotation,
				Blocker.Height,
				Blocker.LayerMask);

			const FSeinPlayerID Owner(1);
			FSeinFogVisionGroup& Group = Fog.GetOrCreateGroup(Owner);
			Group.CellBitfield.Last() |= SEIN_FOW_BIT_EXPLORED;
			Group.SeenEntities.Add(SeenEntity);

			// This intentionally does not exist in the current entity pool.
			// Between configured fog-cadence ticks, a destroyed source's last
			// sampled footprint remains authoritative until the next fog pass.
			const FSeinEntityHandle StaleSource(91, 7);
			FSeinFogSourceState& State =
				Fog.SourceStates.Add(StaleSource);
			State.bValid = true;
			State.Owner = Owner;
			State.WorldPos = FFixedVector(
				FFixedPoint::FromInt(50),
				FFixedPoint::FromInt(50),
				FFixedPoint::Zero);
			State.Rotation = FFixedQuaternion::Identity;
			State.EyeHeight = FFixedPoint::FromInt(180);
			FSeinVisionStamp& Stamp =
				State.Stamps.AddDefaulted_GetRef();
			Stamp.Shape.Shape = ESeinStampShape::Radial;
			Stamp.Shape.Radius = FFixedPoint::FromInt(120);
			Stamp.LayerMask = static_cast<uint8>(
				SEIN_FOW_BIT_NORMAL
				| static_cast<uint8>(ESeinFogOfWarLayerBit::N0));

			const TArray<int32> Empty;
			for (uint8 Bit = 1; Bit <= 7; ++Bit)
			{
				if ((Stamp.LayerMask & (1u << Bit)) == 0)
				{
					continue;
				}
				Fog.GenerateLayerFootprintCells(
					Stamp.Shape,
					State.WorldPos,
					State.Rotation,
					State.EyeHeight,
					Bit,
					State.Footprints[Bit]);
				State.Footprints[Bit].Sort();
				Fog.ApplyFootprintDiff(
					Owner, Group, Bit, Empty, State.Footprints[Bit]);
			}
			return StaleSource;
		}

		static bool MatchesCanonicalRuntime(
			const USeinFogOfWarDefault& Fog,
			FSeinEntityHandle SeenEntity,
			FSeinEntityHandle StaleSource)
		{
			const FSeinFogVisionGroup* Group =
				Fog.VisionGroups.Find(FSeinPlayerID(1));
			const FSeinFogSourceState* Source =
				Fog.SourceStates.Find(StaleSource);
			return Fog.Width == 3
				&& Fog.Height == 2
				&& Fog.DynamicBlockerSnapshots.Num() == 1
				&& !Fog.LastDynamicBlockerCells.IsEmpty()
				&& Group
				&& Group->CellBitfield.Num() == 6
				&& (Group->CellBitfield.Last()
					& SEIN_FOW_BIT_EXPLORED) != 0
				&& Group->SeenEntities.Contains(SeenEntity)
				&& Source
				&& Source->bValid
				&& Source->Owner == FSeinPlayerID(1)
				&& !Source->Footprints[1].IsEmpty()
				&& !Source->Footprints[2].IsEmpty();
		}

	private:
		static bool VisionGroupsEqual(
			const FSeinFogVisionGroup& A,
			const FSeinFogVisionGroup& B)
		{
			if (A.CellBitfield != B.CellBitfield
				|| A.SeenEntities.Num()
					!= B.SeenEntities.Num())
			{
				return false;
			}
			for (int32 Bit = 0; Bit <= 7; ++Bit)
			{
				if (A.RefCounts[Bit] != B.RefCounts[Bit])
				{
					return false;
				}
			}
			for (const FSeinEntityHandle Seen :
				A.SeenEntities)
			{
				if (!B.SeenEntities.Contains(Seen))
				{
					return false;
				}
			}
			return true;
		}

		static bool SourceStatesEqual(
			const FSeinFogSourceState& A,
			const FSeinFogSourceState& B)
		{
			if (A.bValid != B.bValid
				|| A.Owner != B.Owner
				|| A.WorldPos != B.WorldPos
				|| A.Rotation != B.Rotation
				|| A.EyeHeight != B.EyeHeight
				|| A.Stamps != B.Stamps)
			{
				return false;
			}
			for (int32 Bit = 0; Bit <= 7; ++Bit)
			{
				if (A.Footprints[Bit]
					!= B.Footprints[Bit])
				{
					return false;
				}
			}
			return true;
		}
	};

	namespace
	{
		uint32 LegacyVisionHash(const TArray<FSeinVisionStamp>& Stamps)
		{
			uint32 Hash = 0;
			for (const FSeinVisionStamp& Stamp : Stamps)
			{
				Hash ^= GetTypeHash(Stamp.Shape);
				Hash ^= static_cast<uint32>(Stamp.LayerMask);
			}
			return Hash;
		}

		void BuildEmptyFogChannel(int32 Width, int32 Height, TArray<uint8>& OutChannel)
		{
			const int32 NumCells = Width * Height;
			const int32 HeaderBytes = 2 * sizeof(int32) + 3 * sizeof(int64);
			OutChannel.SetNumZeroed(HeaderBytes + 3 * NumCells);
			uint8* Out = OutChannel.GetData();
			auto Write = [&Out](const auto& Value)
			{
				FMemory::Memcpy(Out, &Value, sizeof(Value));
				Out += sizeof(Value);
			};
			Write(Width);
			Write(Height);
			const int64 CellSize = FFixedPoint::FromInt(400).Value;
			const int64 MinHeight = 0;
			const int64 Quantum = FFixedPoint::One.Value;
			Write(CellSize);
			Write(MinHeight);
			Write(Quantum);
		}

		struct FScopedUnclaimedNativeFogClass
		{
			FScopedUnclaimedNativeFogClass()
				: Settings(
					GetMutableDefault<USeinARTSCoreSettings>())
				, FogClass(
					USeinFogOfWarNativeSubclassWithoutCodecTest::
						StaticClass())
				, SavedFogOfWarClass(Settings->FogOfWarClass)
				, SavedClassFlags(FogClass->ClassFlags)
			{
				// Adversarially give the native class the Blueprint flag too.
				// Native must remain the stronger, fail-closed classification.
				FogClass->ClassFlags |=
					CLASS_CompiledFromBlueprint;
				Settings->FogOfWarClass =
					FSoftClassPath(FogClass);
			}

			~FScopedUnclaimedNativeFogClass()
			{
				Settings->FogOfWarClass =
					SavedFogOfWarClass;
				FogClass->ClassFlags =
					SavedClassFlags;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			UClass* FogClass = nullptr;
			FSoftClassPath SavedFogOfWarClass;
			EClassFlags SavedClassFlags = CLASS_None;
		};

		struct FScopedFogDisabled
		{
			FScopedFogDisabled()
				: Settings(
					GetMutableDefault<USeinARTSCoreSettings>())
				, SavedFogOfWarClass(Settings->FogOfWarClass)
			{
				Settings->FogOfWarClass = FSoftClassPath();
			}

			~FScopedFogDisabled()
			{
				Settings->FogOfWarClass = SavedFogOfWarClass;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			FSoftClassPath SavedFogOfWarClass;
		};
	}

	TEST(VisionStampIdentityIsExact, "SeinARTS.Unit.FogOfWar")
	{
		FSeinVisionStamp Left;
		Left.Shape.LocalOffset.X = FFixedPoint::FromInt(-100);
		Left.LayerMask = SEIN_FOW_BIT_NORMAL;

		FSeinVisionStamp Right;
		Right.Shape.LocalOffset.X = FFixedPoint::FromInt(100);
		Right.LayerMask = static_cast<uint8>(ESeinFogOfWarLayerBit::N0);

		TArray<FSeinVisionStamp> Before;
		Before.Add(Left);
		Before.Add(Right);

		TArray<FSeinVisionStamp> After = Before;
		Swap(After[0].LayerMask, After[1].LayerMask);

		// The former XOR key cannot see which geometry owns each layer.
		ASSERT_THAT(AreEqual(LegacyVisionHash(Before), LegacyVisionHash(After)));
		ASSERT_THAT(IsFalse(Before == After));
	}

	TEST(BlueprintEntityVisibilityUsesAuthoritativePolicy,
		"SeinARTS.Unit.FogOfWar")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* FogSubsystem =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		USeinFogOfWarDefault* Fog = FogSubsystem
			? Cast<USeinFogOfWarDefault>(
				FogSubsystem->GetFogOfWar())
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Fog));
		FFogOfWarDefaultTestAccess::SeedOneCellDynamicGrid(*Fog);

		const FSeinPlayerID Observer(1);
		const FSeinPlayerID OtherOwner(2);
		const FFixedTransform TargetTransform(FFixedVector(
			FFixedPoint::FromInt(50),
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero));
		FSeinEntityHandle AlwaysVisible;
		FSeinEntityHandle VisibleOnceExplored;
		FSeinEntityHandle VisibleOnceSeen;
		FSeinEntityHandle VisionLayersOnly;
		FSeinEntityHandle Owned;
		FString Error;
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Materialize(
				*World,
				[&]()
				{
					auto SpawnWithPolicy = [&](FSeinPlayerID Owner,
						ESeinFogVisibilityPolicy Policy)
					{
						const FSeinEntityHandle Entity =
							World->SpawnAbstractEntity(
								TargetTransform, Owner);
						FSeinFogVisibilityComponent Visibility;
						Visibility.FogVisibilityPolicy = Policy;
						World->AddComponent(Entity, Visibility);
						return Entity;
					};

					AlwaysVisible = SpawnWithPolicy(
						OtherOwner,
						ESeinFogVisibilityPolicy::AlwaysVisible);
					VisibleOnceExplored = SpawnWithPolicy(
						OtherOwner,
						ESeinFogVisibilityPolicy::VisibleOnceExplored);
					VisibleOnceSeen = SpawnWithPolicy(
						OtherOwner,
						ESeinFogVisibilityPolicy::VisibleOnceSeen);
					VisionLayersOnly = SpawnWithPolicy(
						OtherOwner,
						ESeinFogVisibilityPolicy::VisionLayersOnly);
					Owned = SpawnWithPolicy(
						Observer,
						ESeinFogVisibilityPolicy::VisionLayersOnly);
				},
				FSeinMatchSettings(),
				0x464F5751,
				TEXT("Fog.BlueprintVisibility.Policy"),
				&Error)));

		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, AlwaysVisible)));
		ASSERT_THAT(IsFalse(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, VisibleOnceExplored)));
		ASSERT_THAT(IsFalse(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, VisibleOnceSeen)));
		ASSERT_THAT(IsFalse(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, VisionLayersOnly)));

		FFogOfWarDefaultTestAccess::SeedOneCellObserverState(
			*Fog,
			Observer,
			SEIN_FOW_BIT_EXPLORED,
			VisibleOnceSeen);

		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, VisibleOnceExplored)));
		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, VisibleOnceSeen)));
		ASSERT_THAT(IsFalse(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, VisionLayersOnly)));
		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, Owned)));
		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld,
				FSeinPlayerID::Neutral(),
				VisionLayersOnly)));

		FFogOfWarDefaultTestAccess::ClearRuntimeGridDimensions(*Fog);
		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, Observer, VisionLayersOnly)));
	}

	TEST(BlueprintEntityVisibilityPermitsFogDisabledProjects,
		"SeinARTS.Unit.FogOfWar")
	{
		FScopedFogDisabled ScopedFogDisabled;
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* FogSubsystem =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(FogSubsystem));
		ASSERT_THAT(IsNull(FogSubsystem->GetFogOfWar()));

		FSeinEntityHandle Target;
		FString Error;
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Materialize(
				*World,
				[&]()
				{
					Target = World->SpawnAbstractEntity(
						FFixedTransform(),
						FSeinPlayerID(2));
				},
				FSeinMatchSettings(),
				0x464F5744,
				TEXT("Fog.BlueprintVisibility.Disabled"),
				&Error)));

		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld, FSeinPlayerID(1), Target)));
		ASSERT_THAT(IsTrue(
			USeinFogOfWarBPFL::SeinIsCellVisible(
				&UnrealWorld,
				FSeinPlayerID(1),
				FVector::ZeroVector,
				TEXT("Normal"))));
		ASSERT_THAT(IsFalse(
			USeinFogOfWarBPFL::SeinIsEntityVisible(
				&UnrealWorld,
				FSeinPlayerID(1),
				FSeinEntityHandle())));
		ASSERT_THAT(IsFalse(
			USeinFogOfWarBPFL::SeinIsCellVisible(
				nullptr,
				FSeinPlayerID(1),
				FVector::ZeroVector,
				TEXT("Normal"))));
		ASSERT_THAT(IsFalse(
			USeinFogOfWarBPFL::SeinIsCellVisible(
				NewObject<USeinFogOfWarDefault>(),
				FSeinPlayerID(1),
				FVector::ZeroVector,
				TEXT("Normal"))));
	}

	TEST(DynamicFogBlockerIdentityIncludesPose, "SeinARTS.Unit.FogOfWar")
	{
		FSeinFogDynamicBlockerSnapshot Base;
		Base.WorldPos = FFixedVector(
			FFixedPoint::FromInt(10), FFixedPoint::FromInt(20), FFixedPoint::Zero);
		Base.Rotation = FFixedQuaternion::Identity;
		Base.Shape.Shape = ESeinStampShape::Rect;
		Base.Height = FFixedPoint::FromInt(300);
		Base.LayerMask = SEIN_FOW_BIT_NORMAL;

		FSeinFogDynamicBlockerSnapshot Rotated = Base;
		Rotated.Rotation.Z = FFixedPoint::SmallNumber;
		ASSERT_THAT(IsFalse(Base == Rotated));

		FSeinFogDynamicBlockerSnapshot SwappedPosition = Base;
		Swap(SwappedPosition.WorldPos.X, SwappedPosition.WorldPos.Y);
		ASSERT_THAT(IsFalse(Base == SwappedPosition));

		TArray<FSeinFogDynamicBlockerSnapshot> Stable;
		Stable.Add(Base);
		ASSERT_THAT(IsTrue(Stable == Stable));
	}

	TEST(DynamicFogOverlapRetainsExactPerLayerHeights,
		"SeinARTS.Unit.FogOfWar")
	{
		const uint8 Thermal = static_cast<uint8>(
			ESeinFogOfWarLayerBit::N0);
		const FFixedPoint RayBelowTall =
			FFixedPoint::FromInt(200);
		const FFixedPoint RayBelowBoth =
			FFixedPoint::FromInt(50);

		for (const bool bTallFirst : { true, false })
		{
			USeinFogOfWarDefault* Fog =
				NewObject<USeinFogOfWarDefault>();
			ASSERT_THAT(IsNotNull(Fog));
			FFogOfWarDefaultTestAccess::
				SeedOneCellDynamicGrid(*Fog);

			auto StampTallNormal = [&]()
			{
				FFogOfWarDefaultTestAccess::
					StampOneCellDynamicBlocker(
						*Fog,
						FFixedPoint::Zero,
						FFixedPoint::FromInt(300),
						SEIN_FOW_BIT_NORMAL);
			};
			auto StampShortThermal = [&]()
			{
				FFogOfWarDefaultTestAccess::
					StampOneCellDynamicBlocker(
						*Fog,
						FFixedPoint::Zero,
						FFixedPoint::FromInt(100),
						Thermal);
			};
			if (bTallFirst)
			{
				StampTallNormal();
				StampShortThermal();
			}
			else
			{
				StampShortThermal();
				StampTallNormal();
			}

			ASSERT_THAT(IsTrue(
				FFogOfWarDefaultTestAccess::IsOneCellOpaque(
					*Fog, RayBelowTall, SEIN_FOW_BIT_NORMAL)));
			ASSERT_THAT(IsFalse(
				FFogOfWarDefaultTestAccess::IsOneCellOpaque(
					*Fog, RayBelowTall, Thermal)));
			ASSERT_THAT(IsTrue(
				FFogOfWarDefaultTestAccess::IsOneCellOpaque(
					*Fog, RayBelowBoth, Thermal)));
			ASSERT_THAT(IsTrue(
				FFogOfWarDefaultTestAccess::
					GetOneCellDynamicTop(
						*Fog, SEIN_FOW_BIT_NORMAL)
					== FFixedPoint::FromInt(300)));
			ASSERT_THAT(IsTrue(
				FFogOfWarDefaultTestAccess::
					GetOneCellDynamicTop(*Fog, Thermal)
					== FFixedPoint::FromInt(100)));
		}
	}

	TEST(DynamicFogBlockerHonorsExtentsLocalZOffset,
		"SeinARTS.Unit.FogOfWar")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* FogSubsystem =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		USeinFogOfWarDefault* Fog = FogSubsystem
			? Cast<USeinFogOfWarDefault>(
				FogSubsystem->GetFogOfWar())
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Fog));
		FFogOfWarDefaultTestAccess::SeedOneCellDynamicGrid(*Fog);

		FSeinEntityHandle Blocker;
		FString Error;
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Materialize(
				*World,
				[&]()
				{
					Blocker = World->SpawnAbstractEntity(
						FFixedTransform(FFixedVector(
							FFixedPoint::FromInt(50),
							FFixedPoint::FromInt(50),
							FFixedPoint::FromInt(100))),
						FSeinPlayerID::Neutral());
					FSeinExtentsComponent Extents;
					Extents.bBlocksFogOfWar = true;
					Extents.BlockedFogOfWarLayerMask =
						SEIN_FOW_BIT_NORMAL;
					FSeinExtentsShape& Shape =
						Extents.Shapes.AddDefaulted_GetRef();
					Shape.Shape = ESeinExtentsShape::Capsule;
					Shape.Radius = FFixedPoint::FromInt(60);
					Shape.LocalOffset.Z =
						FFixedPoint::FromInt(200);
					Shape.Height = FFixedPoint::FromInt(100);
					World->AddComponent(Blocker, Extents);
				},
				FSeinMatchSettings(),
				0x464F575A,
				TEXT("Fog.DynamicBlocker.LocalZ"),
				&Error)));
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Start(*World, &Error)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::RebuildDynamicBlockers(
				*Fog, UnrealWorld)));

		const FSeinFogDynamicBlockerSnapshot* Snapshot =
			FFogOfWarDefaultTestAccess::GetFirstDynamicBlocker(
				*Fog);
		ASSERT_THAT(IsNotNull(Snapshot));
		ASSERT_THAT(IsTrue(
			Snapshot->WorldPos.Z == FFixedPoint::FromInt(300)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::
				GetOneCellDynamicTop(*Fog, SEIN_FOW_BIT_NORMAL)
				== FFixedPoint::FromInt(400)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::IsOneCellOpaque(
				*Fog,
				FFixedPoint::FromInt(350),
				SEIN_FOW_BIT_NORMAL)));
		ASSERT_THAT(IsFalse(
			FFogOfWarDefaultTestAccess::IsOneCellOpaque(
				*Fog,
				FFixedPoint::FromInt(450),
				SEIN_FOW_BIT_NORMAL)));
		World->StopSimulation();
	}

	TEST(TerrainVisionMultiplierScalesEveryStampShapeRange,
		"SeinARTS.Unit.FogOfWar")
	{
		const FFixedPoint Half = FFixedPoint::One
			/ FFixedPoint::FromInt(2);

		FSeinStampShape Radial;
		Radial.Shape = ESeinStampShape::Radial;
		Radial.Radius = FFixedPoint::FromInt(100);
		FFogOfWarDefaultTestAccess::ScaleStampRangeForTerrain(
			Radial, Half);
		ASSERT_THAT(IsTrue(
			Radial.Radius == FFixedPoint::FromInt(50)));

		FSeinStampShape Rect;
		Rect.Shape = ESeinStampShape::Rect;
		Rect.HalfExtentX = FFixedPoint::FromInt(150);
		Rect.HalfExtentY = FFixedPoint::FromInt(100);
		FFogOfWarDefaultTestAccess::ScaleStampRangeForTerrain(
			Rect, Half);
		ASSERT_THAT(IsTrue(
			Rect.HalfExtentX == FFixedPoint::FromInt(75)));
		ASSERT_THAT(IsTrue(
			Rect.HalfExtentY == FFixedPoint::FromInt(50)));

		FSeinStampShape Cone;
		Cone.Shape = ESeinStampShape::Conical;
		Cone.ConeLength = FFixedPoint::FromInt(500);
		Cone.ConeAngleDegrees = FFixedPoint::FromInt(60);
		FFogOfWarDefaultTestAccess::ScaleStampRangeForTerrain(
			Cone, Half);
		ASSERT_THAT(IsTrue(
			Cone.ConeLength == FFixedPoint::FromInt(250)));
		ASSERT_THAT(IsTrue(
			Cone.ConeAngleDegrees == FFixedPoint::FromInt(60)));
	}

	TEST(SameDimensionGridReloadClearsDynamicOverlay, "SeinARTS.Unit.FogOfWar")
	{
		constexpr int32 Width = 2;
		constexpr int32 Height = 2;
		constexpr int32 NumCells = Width * Height;

		USeinFogOfWarDefault* Fog = NewObject<USeinFogOfWarDefault>();
		USeinLevelDataTestDouble* LevelData = NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Fog));
		ASSERT_THAT(IsNotNull(LevelData));
		BuildEmptyFogChannel(Width, Height,
			LevelData->LayerChannels.FindOrAdd(TEXT("FogOfWar")));

		ASSERT_THAT(IsTrue(
			Fog->LoadFromSubstrate(*LevelData).IsAdopted()));
		FFogOfWarDefaultTestAccess::SeedDynamicOverlay(*Fog);

		// Reloading the same dimensions must clear retained TArray storage before
		// the exact empty-snapshot fast path can observe it on the next tick.
		ASSERT_THAT(IsTrue(
			Fog->LoadFromSubstrate(*LevelData).IsAdopted()));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::IsDynamicOverlayClear(*Fog, NumCells)));
	}

	TEST(RejectedFogAdoptionPreservesPriorStaticGrid,
		"SeinARTS.Unit.FogOfWar")
	{
		USeinFogOfWarDefault* Fog =
			NewObject<USeinFogOfWarDefault>();
		ASSERT_THAT(IsNotNull(Fog));
		FFogOfWarDefaultTestAccess::SeedCanonicalStaticGrid(
			*Fog, 19);

		USeinLevelDataTestDouble* Missing =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Missing));
		Missing->TestDimensions = FIntPoint(3, 2);
		const FSeinStaticEnvironmentAdoptionResult MissingResult =
			Fog->LoadFromSubstrate(*Missing);
		ASSERT_THAT(IsTrue(MissingResult.IsRejected()));
		ASSERT_THAT(IsTrue(
			MissingResult.Detail.Contains(
				TEXT("missing the required FogOfWar channel"))));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::
				MatchesCanonicalStaticGrid(*Fog, 19)));

		USeinLevelDataTestDouble* Malformed =
			NewObject<USeinLevelDataTestDouble>();
		ASSERT_THAT(IsNotNull(Malformed));
		Malformed->TestDimensions = FIntPoint(3, 2);
		Malformed->LayerChannels.Add(
			TEXT("FogOfWar"), { 0x01 });
		const FSeinStaticEnvironmentAdoptionResult
			MalformedResult =
				Fog->LoadFromSubstrate(*Malformed);
		ASSERT_THAT(IsTrue(MalformedResult.IsRejected()));
		ASSERT_THAT(IsTrue(
			MalformedResult.Detail.Contains(
				TEXT("FogOfWar channel is malformed"))));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::
				MatchesCanonicalStaticGrid(*Fog, 19)));
	}

	TEST(DynamicBlockerChangeAndSourceDestroyClearsLiveVision,
		"SeinARTS.Unit.FogOfWar")
	{
		USeinFogOfWarDefault* Fog = NewObject<USeinFogOfWarDefault>();
		ASSERT_THAT(IsNotNull(Fog));

		const FSeinEntityHandle Source(1, 1);
		const FSeinPlayerID Owner(1);
		FFogOfWarDefaultTestAccess::SeedOneCellSource(*Fog, Source, Owner);

		FFogOfWarDefaultTestAccess::InvalidateForBlockerChangeThenRemove(
			*Fog, Source);

		ASSERT_THAT(IsFalse(FFogOfWarDefaultTestAccess::HasSource(*Fog, Source)));
		ASSERT_THAT(AreEqual(
			static_cast<uint16>(0),
			FFogOfWarDefaultTestAccess::GetNormalRefCount(*Fog, Owner)));

		const uint8 CellBits = FFogOfWarDefaultTestAccess::GetCellBits(*Fog, Owner);
		ASSERT_THAT(IsFalse((CellBits & SEIN_FOW_BIT_NORMAL) != 0));
		ASSERT_THAT(IsTrue((CellBits & SEIN_FOW_BIT_EXPLORED) != 0));
	}

	TEST(DefaultFogCanonicalStateRoundTripsDerivedCachesAndCadence,
		"SeinARTS.Determinism.FogOfWar.CanonicalState")
	{
		FActorTestSpawner SourceSpawner;
		UWorld& SourceUnrealWorld = SourceSpawner.GetWorld();
		USeinWorldSubsystem* Source =
			SourceUnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* SourceSubsystem =
			SourceUnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		USeinFogOfWarDefault* SourceFog = SourceSubsystem
			? Cast<USeinFogOfWarDefault>(
				SourceSubsystem->GetFogOfWar())
			: nullptr;
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsNotNull(SourceFog));
		FFogOfWarDefaultTestAccess::SeedCanonicalStaticGrid(
			*SourceFog);

		FSeinEntityHandle SeenEntity;
		FString Error;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*Source,
			[&]()
			{
				SeenEntity = Source->SpawnAbstractEntity(
					FFixedTransform(),
					FSeinPlayerID::Neutral());
			},
			FSeinMatchSettings(),
			0x464F4753,
			TEXT("FogCanonicalState.RoundTrip"),
			&Error)));
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Start(*Source, &Error)));

		FGuid EmptyFogRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			EmptyFogRoot, Error)));
		const FSeinEntityHandle StaleSource =
			FFogOfWarDefaultTestAccess::SeedCanonicalRuntime(
				*SourceFog, SeenEntity);

		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot != EmptyFogRoot));

		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		Source->StopSimulation();

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* DestinationSubsystem =
			DestinationUnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		USeinFogOfWarDefault* DestinationFog =
			DestinationSubsystem
				? Cast<USeinFogOfWarDefault>(
					DestinationSubsystem->GetFogOfWar())
				: nullptr;
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationFog));
		FFogOfWarDefaultTestAccess::SeedCanonicalStaticGrid(
			*DestinationFog);

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesCanonicalRuntime(
				*DestinationFog, SeenEntity, StaleSource)));

		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(DestinationRoot == SourceRoot));
		Destination->StopSimulation();
	}

	TEST(FogStaticDriftFailStopsBeforeSameTickLineOfSightRead,
		"SeinARTS.Unit.FogOfWar.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* FogSubsystem =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		USeinFogOfWarDefault* Fog = FogSubsystem
			? Cast<USeinFogOfWarDefault>(
				FogSubsystem->GetFogOfWar())
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Fog));
		FFogOfWarDefaultTestAccess::SeedCanonicalStaticGrid(
			*Fog, 11);
		FogSubsystem->OnWorldBeginPlay(UnrealWorld);
		ASSERT_THAT(IsTrue(
			World->LineOfSightResolver.IsBound()));

		FString Error;
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Materialize(
				*World,
				FSeinMatchSettings(),
				0x464F4744,
				TEXT("FogCanonicalState.SameTickDrift"),
				&Error)));
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Start(*World, &Error)));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));

		FFogOfWarDefaultTestAccess::
			SetCanonicalStaticGridVariantPreservingRuntime(
				*Fog, 12);
		TestRunner->AddExpectedError(
			TEXT("Fog implementation or static environment changed after the match StateContract froze"),
			EAutomationExpectedErrorFlags::Contains,
			2,
			false);
		ASSERT_THAT(IsFalse(
			World->LineOfSightResolver.Execute(
				FSeinPlayerID(0),
				FFixedVector::ZeroVector)));
		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(
			World->IsExecutionTopologyValid()));
	}

	TEST(NativeFogSubclassCannotInheritDefaultCodec,
		"SeinARTS.Unit.FogOfWar.CanonicalState")
	{
		FScopedUnclaimedNativeFogClass ScopedFogClass;
		ASSERT_THAT(IsTrue(
			ScopedFogClass.FogClass->HasAnyClassFlags(
				CLASS_Native)));
		ASSERT_THAT(IsTrue(
			ScopedFogClass.FogClass->HasAnyClassFlags(
				CLASS_CompiledFromBlueprint)));

		TestRunner->AddExpectedError(
			TEXT("has no exact reload-safe state codec claim"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinFogOfWarSubsystem* FogSubsystem =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		ASSERT_THAT(IsNotNull(FogSubsystem));
		ASSERT_THAT(IsNull(FogSubsystem->GetFogOfWar()));
	}

	TEST(DefaultFogRejectedStaticGridRestoreRemainsRetryable,
		"SeinARTS.Determinism.FogOfWar.CanonicalState")
	{
		constexpr int32 GridA = 101;
		constexpr int32 GridB = 202;
		FSeinWorldSnapshot Snapshot;
		FGuid SourceRoot;
		{
			FActorTestSpawner SourceSpawner;
			UWorld& SourceUnrealWorld =
				SourceSpawner.GetWorld();
			USeinWorldSubsystem* Source =
				SourceUnrealWorld.GetSubsystem<
					USeinWorldSubsystem>();
			USeinFogOfWarSubsystem* SourceSubsystem =
				SourceUnrealWorld.GetSubsystem<
					USeinFogOfWarSubsystem>();
			USeinFogOfWarDefault* SourceFog =
				SourceSubsystem
					? Cast<USeinFogOfWarDefault>(
						SourceSubsystem->GetFogOfWar())
					: nullptr;
			ASSERT_THAT(IsNotNull(Source));
			ASSERT_THAT(IsNotNull(SourceFog));
			FFogOfWarDefaultTestAccess::SeedCanonicalStaticGrid(
				*SourceFog, GridA);

			FString Error;
			ASSERT_THAT(IsTrue(
				SeinTestMatchBootstrap::Materialize(
					*Source,
					FSeinMatchSettings(),
					0x464F4754,
					TEXT("FogCanonicalState.GridMismatch"),
					&Error)));
			ASSERT_THAT(IsTrue(
				SeinTestMatchBootstrap::Start(
					*Source, &Error)));
			ASSERT_THAT(IsTrue(
				Source->ComputeCanonicalStateRoot(
					SourceRoot, Error)));
			Source->CaptureSnapshot(Snapshot);
			ASSERT_THAT(AreEqual(
				FSeinWorldSnapshot::CurrentVersion,
				Snapshot.SnapshotVersion));
			Source->StopSimulation();
		}

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld =
			DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<
				USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* DestinationSubsystem =
			DestinationUnrealWorld.GetSubsystem<
				USeinFogOfWarSubsystem>();
		USeinFogOfWarDefault* DestinationFog =
			DestinationSubsystem
				? Cast<USeinFogOfWarDefault>(
					DestinationSubsystem->GetFogOfWar())
				: nullptr;
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationFog));
		FFogOfWarDefaultTestAccess::SeedCanonicalStaticGrid(
			*DestinationFog, GridB);
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesCanonicalStaticGrid(
				*DestinationFog, GridB)));
		const FSeinEntityHandle DestinationSeenSentinel(73, 9);
		FFogOfWarDefaultTestAccess::SeedCanonicalRuntime(
			*DestinationFog, DestinationSeenSentinel);
		const FFogOfWarDefaultTestAccess::FRuntimeSnapshot
			DestinationRuntimeSentinel =
				FFogOfWarDefaultTestAccess::CaptureRuntime(
					*DestinationFog);
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesRuntime(
				*DestinationFog,
				DestinationRuntimeSentinel)));

		TestRunner->AddExpectedError(
			TEXT("checkpoint contract is invalid"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));

		ASSERT_THAT(IsFalse(
			Destination->IsSimulationRunning()));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Awaiting),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(AreEqual(
			0, Destination->GetCurrentTick()));
		ASSERT_THAT(IsFalse(
			Destination->GetCanonicalStateContractDigest()
				.IsValid()));
		ASSERT_THAT(IsTrue(
			Destination->IsExecutionTopologyValid()));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesCanonicalStaticGrid(
				*DestinationFog, GridB)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesRuntime(
				*DestinationFog,
				DestinationRuntimeSentinel)));

		FFogOfWarDefaultTestAccess::
			SetCanonicalStaticGridVariantPreservingRuntime(
			*DestinationFog, GridA);
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesCanonicalStaticGrid(
				*DestinationFog, GridA)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesRuntime(
				*DestinationFog,
				DestinationRuntimeSentinel)));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(
				ESeinMatchBootstrapState::Consumed),
			static_cast<uint8>(
				Destination->GetMatchBootstrapState())));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::MatchesCanonicalStaticGrid(
				*DestinationFog, GridA)));
		ASSERT_THAT(IsTrue(
			FFogOfWarDefaultTestAccess::
				IsCanonicalRuntimeEmpty(*DestinationFog)));
		ASSERT_THAT(IsFalse(
			FFogOfWarDefaultTestAccess::MatchesRuntime(
				*DestinationFog,
				DestinationRuntimeSentinel)));

		FString Error;
		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(
			DestinationRoot == SourceRoot));
		Destination->StopSimulation();
	}

	TEST(ModulePreUnloadReleaseSeversLiveFogState,
		"SeinARTS.Unit.FogOfWar.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* Fog =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Fog));
		ASSERT_THAT(IsNotNull(Fog->GetFogOfWar()));

		FString Error;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0x464F4755,
			TEXT("FogCanonicalState.ModulePreUnload"),
			&Error)));
		ASSERT_THAT(IsTrue(
			SeinTestMatchBootstrap::Start(*World, &Error)));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));

		TestRunner->AddExpectedError(
			TEXT("Execution topology invalid"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		Fog->ReleaseModuleOwnedStateForModuleUnload();

		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(
			World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsNull(Fog->GetFogOfWar()));

		// Ordinary teardown may repeat after ModuleManager's pre-unload pass.
		Fog->ReleaseModuleOwnedStateForModuleUnload();
	}

	TEST(ShareVisionCapabilityUnionsAllyVisionDirectionally,
		"SeinARTS.Unit.FogOfWar")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinFogOfWarSubsystem* FogSubsystem =
			UnrealWorld.GetSubsystem<USeinFogOfWarSubsystem>();
		USeinFogOfWarDefault* Fog = FogSubsystem
			? Cast<USeinFogOfWarDefault>(FogSubsystem->GetFogOfWar())
			: nullptr;
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Fog));

		const FSeinPlayerID Sharer(1);
		const FSeinPlayerID Consumer(2);
		FSeinEntityHandle Target;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Sharer, FSeinFactionID(1));
			World->RegisterPlayer(Consumer, FSeinFactionID(1));
			Target = World->SpawnAbstractEntity(
				FFixedTransform(), Sharer);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0x53485256,
			TEXT("SeinARTS.FogOfWar.ShareVision"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		// Seed live Sharer vision covering the target's cell. The test never
		// advances a fixed tick, so the direct queries below run against this
		// exact state.
		FFogOfWarDefaultTestAccess::SeedOneCellSource(*Fog, Target, Sharer);

		const FFixedVector CellPos;
		ASSERT_THAT(IsTrue(
			Fog->IsEntityVisibleToObserver(Sharer, *World, Target)));
		ASSERT_THAT(IsFalse(
			Fog->IsEntityVisibleToObserver(Consumer, *World, Target)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(0),
			Fog->GetEffectiveCellBitfield(*World, Consumer, CellPos)));

		// Sharer -> Consumer means the Consumer may consume the Sharer's
		// vision: entity spotting AND cell bits union in.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				Sharer,
				Consumer,
				SeinARTSTags::Relationship_Capability_ShareVision,
				SeinARTSTags::Relationship_Source_MatchAdministration,
				1)));
		}
		ASSERT_THAT(IsTrue(
			Fog->IsEntityVisibleToObserver(Consumer, *World, Target)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(SEIN_FOW_BIT_EXPLORED | SEIN_FOW_BIT_NORMAL),
			Fog->GetEffectiveCellBitfield(*World, Consumer, CellPos)));

		// Exact revocation restores the original blindness.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->RevokePairCapability(
				Sharer,
				Consumer,
				SeinARTSTags::Relationship_Capability_ShareVision,
				SeinARTSTags::Relationship_Source_MatchAdministration,
				1)));
		}
		ASSERT_THAT(IsFalse(
			Fog->IsEntityVisibleToObserver(Consumer, *World, Target)));

		// Direction matters: a reversed Consumer -> Sharer grant must not
		// leak the Sharer's vision to the Consumer.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->GrantPairCapability(
				Consumer,
				Sharer,
				SeinARTSTags::Relationship_Capability_ShareVision,
				SeinARTSTags::Relationship_Source_MatchAdministration,
				2)));
		}
		ASSERT_THAT(IsFalse(
			Fog->IsEntityVisibleToObserver(Consumer, *World, Target)));
		ASSERT_THAT(AreEqual(
			static_cast<uint8>(0),
			Fog->GetEffectiveCellBitfield(*World, Consumer, CellPos)));

		World->StopSimulation();
	}
}
