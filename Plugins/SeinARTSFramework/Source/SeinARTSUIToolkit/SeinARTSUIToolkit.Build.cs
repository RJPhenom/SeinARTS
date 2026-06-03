// Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSUIToolkit : ModuleRules
{
	public SeinARTSUIToolkit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"SlateCore",
				"Slate",
				"SeinARTSCore",
				"SeinARTSCoreEntity",
				"SeinARTSFramework",
				"GameplayTags",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"RenderCore",
				"SeinARTSNet",
			}
		);
	}
}
