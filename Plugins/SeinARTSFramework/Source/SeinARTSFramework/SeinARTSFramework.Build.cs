// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SeinARTSFramework : ModuleRules
{
	public SeinARTSFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		// Bootstrap and game-mode implementations use file-local helpers whose
		// anonymous namespaces must remain separate translation units.
		bUseUnity = false;

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"SeinARTSCore",
				"SeinARTSCoreEntity",
				"GameplayTags",
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"EnhancedInput",
				"UMG",
				"SeinARTSNet",
				"SeinARTSLevelData",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
