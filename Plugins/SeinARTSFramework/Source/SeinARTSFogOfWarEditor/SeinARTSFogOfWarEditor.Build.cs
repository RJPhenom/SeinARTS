// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSFogOfWarEditor : ModuleRules
{
	public SeinARTSFogOfWarEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"PropertyEditor",
			"GameplayTags",
			"SeinARTSCore",
			"SeinARTSCoreEntity",           // FInstancedStruct walk in the bridge draw delegate
			"SeinARTSFogOfWar",              // FSeinVisionComponent + FSeinStampShape, ASeinFogOfWarVolume, USeinFogOfWarSubsystem
			"SeinARTSEditor",                // RegisterComponentDataDraw for vision-stamp viz on the entity bridge
		});
	}
}
