using UnrealBuildTool;

public class SeinARTSNet : ModuleRules
{
    public SeinARTSNet(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "GameplayTags"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "NetCore"
        });
    }
}
