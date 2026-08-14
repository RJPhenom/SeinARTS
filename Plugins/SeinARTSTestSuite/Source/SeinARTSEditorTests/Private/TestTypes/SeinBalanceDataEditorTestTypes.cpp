#include "TestTypes/SeinBalanceDataEditorTestTypes.h"

#include "Actor/SeinEntityComponent.h"

namespace
{
	FSeinBalanceEditorTestComponent MakeComponent(
		FFixedPoint Speed,
		int32 Armor,
		FFixedPoint TurnRate,
		int32 GearCount)
	{
		FSeinBalanceEditorTestNestedData Nested;
		Nested.TurnRate = TurnRate;
		Nested.GearCount = GearCount;

		FSeinBalanceEditorTestComponent Component;
		Component.Speed = Speed;
		Component.Armor = Armor;
		Component.ModeData = FInstancedStruct::Make(Nested);
		return Component;
	}
}

ASeinBalanceEditorTestEntityA::ASeinBalanceEditorTestEntityA()
{
	GetEntityBridge()->ComponentData.Add(FInstancedStruct::Make(MakeComponent(
		FFixedPoint::FromInt(3),
		12,
		FFixedPoint::FromInt(2),
		5)));
}

ASeinBalanceEditorTestEntityB::ASeinBalanceEditorTestEntityB()
{
	GetEntityBridge()->ComponentData.Add(FInstancedStruct::Make(MakeComponent(
		FFixedPoint::FromInt(6),
		24,
		FFixedPoint::FromInt(4),
		7)));
}

USeinBalanceEditorTestAbilityA::USeinBalanceEditorTestAbilityA()
{
	Cooldown = FFixedPoint::FromInt(2);
	MaxRange = FFixedPoint::FromInt(8);
	AreaRadius = FFixedPoint::FromInt(1);
}

USeinBalanceEditorTestAbilityB::USeinBalanceEditorTestAbilityB()
{
	Cooldown = FFixedPoint::FromInt(5);
	MaxRange = FFixedPoint::FromInt(12);
	AreaRadius = FFixedPoint::FromInt(3);
}

USeinBalanceEditorSiblingAbilityA::USeinBalanceEditorSiblingAbilityA()
{
	SiblingTuning = FFixedPoint::FromInt(101);
}

USeinBalanceEditorSiblingAbilityB::USeinBalanceEditorSiblingAbilityB()
{
	SiblingTuning = FFixedPoint::FromInt(202);
}
