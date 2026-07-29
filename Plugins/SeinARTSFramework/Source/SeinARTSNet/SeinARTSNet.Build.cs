using UnrealBuildTool;

public class SeinARTSNet : ModuleRules
{
    public SeinARTSNet(ReadOnlyTargetRules Target) : base(Target)
    {
        // Preserve file-local helper isolation; unity merging can combine
        // anonymous namespaces from the replay reader and writer.
        bUseUnity = false;

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
