// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SeinARTS_Framework_Runtime : ModuleRules {
	public SeinARTS_Framework_Runtime(ReadOnlyTargetRules Target) : base(Target) {
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
			
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"AIModule",
			"NavigationSystem",
			"GameplayAbilities",
			"GameplayTasks",
			"GameplayTags",
			"UMG",
		});
			
		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore",
		});
	}
}
