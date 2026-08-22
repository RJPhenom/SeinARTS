// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSOnlineServices : ModuleRules
{
	public SeinARTSOnlineServices(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"SeinARTSCoreEntity",
			"SeinARTSNet"
		});

		PrivateDependencyModuleNames.Add("CoreOnline");
	}
}
