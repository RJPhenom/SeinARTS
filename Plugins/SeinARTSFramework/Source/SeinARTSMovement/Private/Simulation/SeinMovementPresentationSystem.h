/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementPresentationSystem.h
 * @brief   Render-only final-motion sampling after authoritative PostTick work.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinMovementPayload.h"
#include "Core/SeinSystemPriority.h"
#include "Core/SeinTickPhase.h"
#include "Movement/SeinMovement.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

/**
 * Samples final transforms after every authoritative phase, then lets the
 * active movement mode publish cosmetic telemetry. Previous samples and render
 * values are deliberately non-canonical and never influence simulation.
 */
class FSeinMovementPresentationSystem final : public ISeinSystem
{
public:
	explicit FSeinMovementPresentationSystem(
		USeinMovementSubsystem* InOwnerSubsystem)
		: OwnerSubsystem(InOwnerSubsystem)
	{
	}

	virtual void Tick(
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World) override
	{
		USeinMovementSubsystem* Subsystem = OwnerSubsystem.Get();
		if (!Subsystem)
		{
			return;
		}

		ISeinComponentStorage* MovementStorage =
			World.GetComponentStorageMutable(
				FSeinMovementPayload::StaticStruct());
		if (!MovementStorage)
		{
			return;
		}

		TSet<FSeinEntityHandle> LiveHandles;
		World.GetEntityPool().ForEachEntity([&](
			FSeinEntityHandle Handle,
			const FSeinEntity& Entity)
		{
			const FSeinMovementPayload* ReadMovement =
				World.GetComponent<FSeinMovementPayload>(Handle);
			USeinMovement* Movement =
				Subsystem->FindMovementInstance(Handle);
			if (!ReadMovement)
			{
				return;
			}

			FSeinMovementPayload* MutableMovement =
				static_cast<FSeinMovementPayload*>(
					MovementStorage->GetComponentRawForDeferredMutation(
						Handle));
			if (!MutableMovement)
			{
				return;
			}
			if (!Movement)
			{
				FSeinMovementRenderStateWriter Writer(
					MutableMovement->RenderState);
				Writer.Reset();
				return;
			}

			LiveHandles.Add(Handle);
			FSeinSettledMovementRenderContext Context;
			Context.CurrentTransform = Entity.Transform;
			Context.PreviousTransform = Entity.Transform;
			Context.DriverVelocity = ReadMovement->Velocity;
			Context.DeltaTime = DeltaTime;

			FSeinMovementRenderStateWriter Writer(
				MutableMovement->RenderState);
			const FPresentationSample* Previous = Samples.Find(Handle);
			const bool bSameMovementImplementation = Previous
				&& Previous->MovementClass.Get() == Movement->GetClass();
			if (!bSameMovementImplementation)
			{
				Writer.Reset();
				Previous = nullptr;
			}
			if (Previous)
			{
				Context.bHasPreviousSample =
					DeltaTime > FFixedPoint::Zero;
				Context.PreviousTransform = Previous->Transform;
				Context.PreviousSettledVelocity =
					Previous->SettledVelocity;
				Context.PreviousDriverVelocity =
					Previous->DriverVelocity;
				if (Context.bHasPreviousSample)
				{
					FFixedVector Delta = Entity.Transform.GetLocation()
						- Previous->Transform.GetLocation();
					Delta.Z = FFixedPoint::Zero;
					Context.SettledVelocity = Delta / DeltaTime;
				}
			}

			// Deferred access deliberately remains uncommitted: the restricted
			// writer can touch only the Transient RenderState field.
			Movement->UpdateSettledRenderState(
				Context, *ReadMovement, Writer);

			FPresentationSample& Current = Samples.FindOrAdd(Handle);
			Current.Transform = Entity.Transform;
			Current.SettledVelocity = Context.SettledVelocity;
			Current.DriverVelocity = Context.DriverVelocity;
			Current.MovementClass = Movement->GetClass();
		});

		for (auto It = Samples.CreateIterator(); It; ++It)
		{
			if (!LiveHandles.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
	}

	void ResetSamples()
	{
		Samples.Reset();
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.movement.presentation")),
			3u,
			ESeinTickPhase::FinalObservation,
			SeinSystemPriority::MovementPresentation);
	}

private:
	struct FPresentationSample
	{
		FFixedTransform Transform;
		FFixedVector SettledVelocity = FFixedVector::ZeroVector;
		FFixedVector DriverVelocity = FFixedVector::ZeroVector;
		TWeakObjectPtr<UClass> MovementClass;
	};

	TWeakObjectPtr<USeinMovementSubsystem> OwnerSubsystem;
	TMap<FSeinEntityHandle, FPresentationSample> Samples;
};
