/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    CombatSubstrateTests.cpp
 * @brief   Combat substrate contracts: vitals seed/damage/death, weapon
 *          cycling + instant delivery, deterministic target queries,
 *          projectile flight/impact/interception, and the starter attack
 *          ability's fire loop.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinAbility_Attack.h"
#include "Combat/SeinCombatDamage.h"
#include "Combat/SeinTargetQueryService.h"
#include "Combat/SeinWeaponFire.h"
#include "Components/SeinProjectileComponent.h"
#include "Components/SeinVitalsComponent.h"
#include "Components/SeinWeaponComponent.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinCombatTestTypes.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool TickSimulation(USeinWorldSubsystem& World, float DeltaTime)
	{
		return World.TickSimulation(DeltaTime);
	}
};

namespace UE::SeinARTSTests
{
	namespace CombatSubstrateTestLocal
	{
		FFixedVector At(int32 X, int32 Y = 0)
		{
			return FFixedVector(
				FFixedPoint::FromInt(X), FFixedPoint::FromInt(Y),
				FFixedPoint::Zero);
		}

		FSeinVitalsComponent MakeVitals(int32 MaxHealth)
		{
			FSeinVitalsComponent Vitals;
			Vitals.MaxHealth = FFixedPoint::FromInt(MaxHealth);
			Vitals.Health = FFixedPoint::FromInt(MaxHealth);
			return Vitals;
		}

		FSeinDamagePayload MakePayload(int32 BaseDamage)
		{
			FSeinDamagePayload Payload;
			Payload.BaseDamage = FFixedPoint::FromInt(BaseDamage);
			return Payload;
		}

		FSeinWeaponSlot MakeWeapon(
			int32 Range, int32 Damage, int32 CooldownSeconds = 1)
		{
			FSeinWeaponSlot Slot;
			Slot.Range = FFixedPoint::FromInt(Range);
			Slot.CooldownSeconds = FFixedPoint::FromInt(CooldownSeconds);
			Slot.Payload = MakePayload(Damage);
			return Slot;
		}

		struct FCombatFixture
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World = nullptr;
			FSeinPlayerID Attacker = FSeinPlayerID(1);
			FSeinPlayerID Defender = FSeinPlayerID(2);

			bool Initialize(
				TFunctionRef<void()> AuthorEntities, uint32 Seed,
				const TCHAR* FixtureId)
			{
				World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
				if (!World) return false;
				FString Error;
				return SeinTestMatchBootstrap::Materialize(
						*World,
						[&]()
						{
							World->RegisterPlayer(Attacker, FSeinFactionID(1));
							World->RegisterPlayer(Defender, FSeinFactionID(2));
							AuthorEntities();
						},
						FSeinMatchSettings(),
						Seed,
						FixtureId,
						&Error)
					&& SeinTestMatchBootstrap::Start(*World, &Error);
			}

			void Tick(int32 Count = 1)
			{
				for (int32 Index = 0; Index < Count; ++Index)
				{
					FSeinWorldSubsystemTestAccess::TickSimulation(
						*World, World->GetFixedDeltaTimeSeconds());
				}
			}

			FFixedPoint Health(FSeinEntityHandle Handle) const
			{
				const FSeinVitalsComponent* Vitals =
					World->GetComponent<FSeinVitalsComponent>(Handle);
				return Vitals ? Vitals->Health : FFixedPoint::Zero;
			}
		};
	}

	TEST(VitalsDamageDeathAndEventsAreDeterministic,
		"SeinARTS.Sim.Combat.Vitals")
	{
		using namespace CombatSubstrateTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Victim;
		FSeinEntityHandle Shooter;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Shooter = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(0)), Fixture.Attacker);
				Victim = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(500)), Fixture.Defender);
				Fixture.World->AddComponent(Victim, MakeVitals(100));
			},
			0x434D4231, TEXT("SeinARTS.Combat.Vitals"))));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(
				FSeinCombatDamage::ApplyDamage(
					*Fixture.World, Victim, Shooter, MakePayload(30))
				== FFixedPoint::FromInt(30)));
		}
		ASSERT_THAT(IsTrue(
			Fixture.Health(Victim) == FFixedPoint::FromInt(70)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			// Overkill clamps to remaining health and kills.
			ASSERT_THAT(IsTrue(
				FSeinCombatDamage::ApplyDamage(
					*Fixture.World, Victim, Shooter, MakePayload(500))
				== FFixedPoint::FromInt(70)));
		}
		ASSERT_THAT(IsFalse(Fixture.World->IsEntityAlive(Victim)));
		// Dead targets absorb nothing.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(
				FSeinCombatDamage::ApplyDamage(
					*Fixture.World, Victim, Shooter, MakePayload(10))
				== FFixedPoint::Zero));
		}
		Fixture.World->StopSimulation();
	}

	TEST(WeaponCyclingGatesFireAndReloadsMagazines,
		"SeinARTS.Sim.Combat.Weapons")
	{
		using namespace CombatSubstrateTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Shooter;
		FSeinEntityHandle Victim;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Shooter = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(0)), Fixture.Attacker);
				FSeinWeaponComponent Weapons;
				FSeinWeaponSlot Slot = MakeWeapon(2000, 10);
				Slot.MagazineSize = 2;
				Slot.ReloadSeconds = FFixedPoint::One;
				Slot.CooldownSeconds = FFixedPoint::Zero;
				Weapons.Weapons.Add(Slot);
				Fixture.World->AddComponent(Shooter, Weapons);
				Victim = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(500)), Fixture.Defender);
				Fixture.World->AddComponent(Victim, MakeVitals(1000));
			},
			0x434D4232, TEXT("SeinARTS.Combat.Weapons"))));

		// First sim tick seeds magazines.
		Fixture.Tick();
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(
				FSeinWeaponFire::TryFireWeaponAt(
					*Fixture.World, Shooter, 0, Victim)
				== ESeinWeaponFireResult::Fired));
			ASSERT_THAT(IsTrue(
				FSeinWeaponFire::TryFireWeaponAt(
					*Fixture.World, Shooter, 0, Victim)
				== ESeinWeaponFireResult::Fired));
			// Magazine spent — reloading refuses.
			ASSERT_THAT(IsTrue(
				FSeinWeaponFire::TryFireWeaponAt(
					*Fixture.World, Shooter, 0, Victim)
				== ESeinWeaponFireResult::NotReady));
		}
		ASSERT_THAT(IsTrue(
			Fixture.Health(Victim) == FFixedPoint::FromInt(980)));

		// A generous 1.5 s of ticks completes the reload.
		Fixture.Tick(45);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(
				FSeinWeaponFire::TryFireWeaponAt(
					*Fixture.World, Shooter, 0, Victim)
				== ESeinWeaponFireResult::Fired));
		}
		// Range gate: beyond weapon range refuses without firing.
		FSeinEntityHandle FarVictim;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FarVictim = Fixture.World->SpawnAbstractEntity(
				FFixedTransform(At(5000)), Fixture.Defender);
			Fixture.World->AddComponent(FarVictim, MakeVitals(100));
			ASSERT_THAT(IsTrue(
				FSeinWeaponFire::TryFireWeaponAt(
					*Fixture.World, Shooter, 0, FarVictim)
				== ESeinWeaponFireResult::OutOfRange));
		}
		Fixture.World->StopSimulation();
	}

	TEST(TargetQueriesFilterDeterministicallyAndScoreNearest,
		"SeinARTS.Sim.Combat.Acquisition")
	{
		using namespace CombatSubstrateTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Instigator;
		FSeinEntityHandle OwnUnit;
		FSeinEntityHandle NearEnemy;
		FSeinEntityHandle FarEnemy;
		FSeinEntityHandle OutOfRangeEnemy;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Instigator = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(0)), Fixture.Attacker);
				Fixture.World->AddComponent(
					Instigator, MakeVitals(100));
				OwnUnit = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(200)), Fixture.Attacker);
				Fixture.World->AddComponent(OwnUnit, MakeVitals(100));
				NearEnemy = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(400)), Fixture.Defender);
				Fixture.World->AddComponent(NearEnemy, MakeVitals(100));
				FarEnemy = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(900)), Fixture.Defender);
				Fixture.World->AddComponent(FarEnemy, MakeVitals(100));
				OutOfRangeEnemy = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(5000)), Fixture.Defender);
				Fixture.World->AddComponent(
					OutOfRangeEnemy, MakeVitals(100));
			},
			0x434D4233, TEXT("SeinARTS.Combat.Acquisition"))));

		FSeinTargetQuery Query;
		Query.Instigator = Instigator;
		Query.Range = FFixedPoint::FromInt(1500);
		Query.MaxResults = 8;
		TArray<FSeinTargetCandidate> Candidates;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		// Own unit excluded by the neutral scorer; out-of-range excluded by
		// the mechanical gate; nearest enemy scores first.
		ASSERT_THAT(AreEqual(2, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));
		ASSERT_THAT(IsTrue(Candidates[1].Target == FarEnemy));

		Query.MaxResults = 1;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(1, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));

		// A custom scorer still receives every mechanically valid candidate and
		// the bounded result set honors its score rather than the built-in fast path.
		Query.ScorerClass = FSoftClassPath(
			USeinFarthestTargetScorerTestDouble::StaticClass()->GetPathName());
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(1, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == FarEnemy));
		Query.ScorerClass.Reset();

		// A range spanning more index cells than entities deliberately falls
		// back to the canonical full sweep without changing result semantics.
		Query.Range = FFixedPoint::FromInt(10000);
		Query.MaxResults = 8;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(3, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));
		ASSERT_THAT(IsTrue(Candidates[1].Target == FarEnemy));
		ASSERT_THAT(IsTrue(Candidates[2].Target == OutOfRangeEnemy));
		Query.Range = FFixedPoint::FromInt(1500);

		// Arc gate: a 45° half-angle facing +X keeps +X targets and drops a
		// flanker at +Y.
		FSeinEntityHandle Flanker;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Flanker = Fixture.World->SpawnAbstractEntity(
				FFixedTransform(At(0, 400)), Fixture.Defender);
			Fixture.World->AddComponent(Flanker, MakeVitals(100));
		}
		Query.MaxResults = 8;
		Query.ArcHalfAngleDegrees = FFixedPoint::FromInt(45);
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(2, Candidates.Num()));
		ASSERT_THAT(IsFalse(Candidates.ContainsByPredicate(
			[&](const FSeinTargetCandidate& Candidate)
			{
				return Candidate.Target == Flanker;
			})));

		// A warmed spatial prefilter must follow canonical position mutation.
		// Moving the formerly-far target nearest changes the first result.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinEntity* FarEntity =
				Fixture.World->GetEntityMutable(FarEnemy);
			ASSERT_THAT(IsNotNull(FarEntity));
			FarEntity->Transform.SetLocation(At(100));
		}
		Query.ArcHalfAngleDegrees = FFixedPoint::FromInt(180);
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(IsTrue(Candidates[0].Target == FarEnemy));

		// Health does not affect index membership, so zero-health exclusion must
		// remain a live narrow-phase gate even while the position cache is warm.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinVitalsComponent* Vitals =
				Fixture.World->GetComponentMutable<FSeinVitalsComponent>(FarEnemy);
			ASSERT_THAT(IsNotNull(Vitals));
			Vitals->Health = FFixedPoint::Zero;
		}
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(IsTrue(Candidates[0].Target == NearEnemy));
		ASSERT_THAT(IsFalse(Candidates.ContainsByPredicate(
			[&](const FSeinTargetCandidate& Candidate)
			{
				return Candidate.Target == FarEnemy;
			})));
		Fixture.World->StopSimulation();
	}

	TEST(TargetQueriesFallbackExactlyAtFixedPointBounds,
		"SeinARTS.Sim.Combat.Acquisition.FixedPointBounds")
	{
		using namespace CombatSubstrateTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Instigator;
		FSeinEntityHandle Enemy;
		const FFixedPoint OriginX =
			FFixedPoint::MaxValue - FFixedPoint::FromInt(1000);
		const FFixedPoint EnemyX =
			FFixedPoint::MaxValue - FFixedPoint::FromInt(500);
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Instigator = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(FFixedVector(
						OriginX, FFixedPoint::Zero, FFixedPoint::Zero)),
					Fixture.Attacker);
				Fixture.World->AddComponent(
					Instigator, MakeVitals(100));
				Enemy = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(FFixedVector(
						EnemyX, FFixedPoint::Zero, FFixedPoint::Zero)),
					Fixture.Defender);
				Fixture.World->AddComponent(Enemy, MakeVitals(100));
			},
			0x434D4237, TEXT("SeinARTS.Combat.Acquisition.Bounds"))));

		FSeinTargetQuery Query;
		Query.Instigator = Instigator;
		Query.Range = FFixedPoint::FromInt(1500);
		Query.MaxResults = 1;
		TArray<FSeinTargetCandidate> Candidates;
		FSeinTargetQueryService::FindTargets(
			*Fixture.World, Query, Candidates);
		ASSERT_THAT(AreEqual(1, Candidates.Num()));
		ASSERT_THAT(IsTrue(Candidates[0].Target == Enemy));
		Fixture.World->StopSimulation();
	}

	TEST(ProjectilesFlyImpactAndCanBeIntercepted,
		"SeinARTS.Sim.Combat.Projectiles")
	{
		using namespace CombatSubstrateTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Shooter;
		FSeinEntityHandle Victim;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Shooter = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(0)), Fixture.Attacker);
				FSeinWeaponComponent Weapons;
				FSeinWeaponSlot Slot = MakeWeapon(5000, 25);
				Slot.Delivery = ESeinWeaponDelivery::Projectile;
				Slot.ProjectileSpeed = FFixedPoint::FromInt(1000);
				Weapons.Weapons.Add(Slot);
				Fixture.World->AddComponent(Shooter, Weapons);
				Victim = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(3000)), Fixture.Defender);
				Fixture.World->AddComponent(Victim, MakeVitals(100));
			},
			0x434D4234, TEXT("SeinARTS.Combat.Projectiles"))));

		Fixture.Tick();
		FSeinEntityHandle Projectile;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(
				FSeinWeaponFire::TryFireWeaponAt(
					*Fixture.World, Shooter, 0, Victim)
				== ESeinWeaponFireResult::Fired));
		}
		// The shell exists as a real entity while in flight.
		Fixture.World->GetEntityPool().ForEachEntity(
			[&](FSeinEntityHandle Handle, const FSeinEntity&)
			{
				if (Fixture.World->GetComponent<FSeinProjectileComponent>(
						Handle))
				{
					Projectile = Handle;
				}
			});
		ASSERT_THAT(IsTrue(Projectile.IsValid()));

		// 3000 units at 1000/s ≈ 3 s; 4 s of ticks guarantees arrival.
		Fixture.Tick(120);
		ASSERT_THAT(IsTrue(
			Fixture.Health(Victim) == FFixedPoint::FromInt(75)));
		ASSERT_THAT(IsFalse(Fixture.World->IsEntityAlive(Projectile)));

		// Interception: destroy the next shell mid-flight; the target must
		// never take its damage.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinWeaponComponent* Weapons =
				Fixture.World->GetComponentMutable<FSeinWeaponComponent>(
					Shooter);
			ASSERT_THAT(IsNotNull(Weapons));
			Weapons->Weapons[0].CooldownRemaining = FFixedPoint::Zero;
			ASSERT_THAT(IsTrue(
				FSeinWeaponFire::TryFireWeaponAt(
					*Fixture.World, Shooter, 0, Victim)
				== ESeinWeaponFireResult::Fired));
		}
		Fixture.Tick(1);
		FSeinEntityHandle SecondShell;
		Fixture.World->GetEntityPool().ForEachEntity(
			[&](FSeinEntityHandle Handle, const FSeinEntity&)
			{
				if (Fixture.World->GetComponent<FSeinProjectileComponent>(
						Handle))
				{
					SecondShell = Handle;
				}
			});
		ASSERT_THAT(IsTrue(SecondShell.IsValid()));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->DestroyEntity(SecondShell);
		}
		Fixture.Tick(120);
		ASSERT_THAT(IsTrue(
			Fixture.Health(Victim) == FFixedPoint::FromInt(75)));
		Fixture.World->StopSimulation();
	}

	TEST(StarterAttackAbilityFiresUntilTheTargetDies,
		"SeinARTS.Sim.Combat.Attack")
	{
		using namespace CombatSubstrateTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Shooter;
		FSeinEntityHandle Victim;
		int32 AbilityID = INDEX_NONE;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Shooter = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(0)), Fixture.Attacker);
				FSeinWeaponComponent Weapons;
				// 10 damage every 1/10 s kills a 100 HP target in ~1 s.
				FSeinWeaponSlot Slot = MakeWeapon(2000, 10);
				Slot.CooldownSeconds =
					FFixedPoint::One / FFixedPoint::FromInt(10);
				Weapons.Weapons.Add(Slot);
				Fixture.World->AddComponent(Shooter, Weapons);
				Fixture.World->AddComponent(
					Shooter, FSeinAbilityComponent());
				AbilityID = USeinAbilityBPFL::SeinGrantAbility(
					Fixture.World, Shooter,
					USeinAbility_Attack::StaticClass());
				Victim = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(500)), Fixture.Defender);
				Fixture.World->AddComponent(Victim, MakeVitals(100));
			},
			0x434D4235, TEXT("SeinARTS.Combat.Attack"))));

		Fixture.Tick();
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			USeinAbility* Ability =
				Fixture.World->GetAbilityInstance(AbilityID);
			ASSERT_THAT(IsNotNull(Ability));
			// The pipeline's CanActivate escape hatch runs BEFORE the
			// activation target is assigned — the starter must pass it on
			// arming alone (red-team finding: gating on the stale
			// TargetEntity rejected every pipeline activation).
			ASSERT_THAT(IsTrue(Ability->CanActivate()));
			ASSERT_THAT(IsTrue(Ability->ActivateAbility(
				Victim, FFixedVector::ZeroVector)));
		}
		// Two seconds of ticks is ample for eleven 0.1 s cycles.
		Fixture.Tick(60);
		ASSERT_THAT(IsFalse(Fixture.World->IsEntityAlive(Victim)));
		Fixture.World->StopSimulation();
	}

	TEST(ZeroedVitalsDieInsteadOfReseedingToFull,
		"SeinARTS.Sim.Combat.Vitals")
	{
		using namespace CombatSubstrateTestLocal;
		FCombatFixture Fixture;
		FSeinEntityHandle Unit;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			[&]()
			{
				Unit = Fixture.World->SpawnAbstractEntity(
					FFixedTransform(At(0)), Fixture.Attacker);
				Fixture.World->AddComponent(Unit, MakeVitals(100));
			},
			0x434D4236, TEXT("SeinARTS.Combat.Revive"))));

		// First tick seeds. A scripted whole-struct write then zeroes health
		// WITHOUT the damage path — the next tick must deliver a real death,
		// never a silent re-seed back to full (red-team finding).
		Fixture.Tick();
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			FSeinVitalsComponent* Vitals =
				Fixture.World->GetComponentMutable<FSeinVitalsComponent>(
					Unit);
			ASSERT_THAT(IsNotNull(Vitals));
			ASSERT_THAT(IsTrue(Vitals->bHealthSeeded));
			Vitals->Health = FFixedPoint::Zero;
		}
		Fixture.Tick();
		ASSERT_THAT(IsFalse(Fixture.World->IsEntityAlive(Unit)));
		Fixture.World->StopSimulation();
	}
}
