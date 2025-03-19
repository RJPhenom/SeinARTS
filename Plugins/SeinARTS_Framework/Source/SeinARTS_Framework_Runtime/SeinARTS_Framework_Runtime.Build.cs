// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SeinARTS_Framework_Runtime : ModuleRules
{
	public SeinARTS_Framework_Runtime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory + "/Public",
            ModuleDirectory + "/Public/Game",
            ModuleDirectory + "/Public/GameState",
            ModuleDirectory + "/Public/Objects",
            ModuleDirectory + "/Public/Components",
            ModuleDirectory + "/Public/Interfaces",
		});
					
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"EnhancedInput",
			"Slate",
			"SlateCore",
            "NavigationSystem",
			"UMG"
        });
	}
}
