using UnrealBuildTool;

public class SeinARTSCore: ModuleRules
{
    public SeinARTSCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject"});
    }
}
