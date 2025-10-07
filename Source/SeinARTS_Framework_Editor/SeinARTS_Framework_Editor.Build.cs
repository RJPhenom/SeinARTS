// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SeinARTS_Framework_Editor : ModuleRules
{
	public SeinARTS_Framework_Editor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(new string[] {

		});
				
		
		PrivateIncludePaths.AddRange(new string[] {

		});
			
		PublicDependencyModuleNames.AddRange(new string[] {
				"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
		});
		
		DynamicallyLoadedModuleNames.AddRange(new string[] {

		});
	}
}
