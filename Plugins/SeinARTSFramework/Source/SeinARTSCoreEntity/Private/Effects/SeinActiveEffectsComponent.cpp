/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffectsComponent.cpp
 * @brief   Read/query helpers for instance-scope active effect data. Lifecycle
 *          mutation stays on USeinWorldSubsystem so teardown cannot bypass hooks.
 */

#include "Components/SeinActiveEffectsPayload.h"
#include "Templates/SubclassOf.h"
#include "Effects/SeinEffect.h"

namespace SeinActiveEffectsInternal
{
	FORCEINLINE const USeinEffect* CDO(const FSeinActiveEffect& Effect)
	{
		return Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
	}
}

bool FSeinActiveEffectsPayload::HasEffectWithTag(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid())
	{
		return false;
	}

	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		const USeinEffect* Def = SeinActiveEffectsInternal::CDO(Effect);
		if (Def && Def->EffectTag.MatchesTag(Tag))
		{
			return true;
		}
	}
	return false;
}

int32 FSeinActiveEffectsPayload::GetStackCountForTag(const FGameplayTag& Tag) const
{
	int64 Total = 0;
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		const USeinEffect* Def = SeinActiveEffectsInternal::CDO(Effect);
		if (Def && Def->EffectTag.MatchesTag(Tag))
		{
			Total += FMath::Max(0, Effect.CurrentStacks);
			if (Total >= MAX_int32) return MAX_int32;
		}
	}
	return static_cast<int32>(Total);
}

int32 FSeinActiveEffectsPayload::GetStackCountForClass(TSubclassOf<USeinEffect> EffectClass) const
{
	int64 Total = 0;
	if (!EffectClass) return 0;
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		if (Effect.EffectClass == EffectClass)
		{
			Total += FMath::Max(0, Effect.CurrentStacks);
			if (Total >= MAX_int32) return MAX_int32;
		}
	}
	return static_cast<int32>(Total);
}

void FSeinActiveEffectsPayload::CollectModifiers(TArray<FSeinModifier>& OutModifiers) const
{
	for (const FSeinActiveEffect& Effect : ActiveEffects)
	{
		const USeinEffect* Def = SeinActiveEffectsInternal::CDO(Effect);
		if (!Def) continue;

		for (int32 Stack = 0; Stack < Effect.CurrentStacks; ++Stack)
		{
			for (const FSeinModifier& Mod : Def->Modifiers)
			{
				FSeinModifier& Added = OutModifiers.Add_GetRef(Mod);
				Added.SourceEntity = Effect.Source;
				Added.SourceEffectID = Effect.EffectInstanceID;
			}
		}
	}
}
