using UnrealBuildTool;

public class SeinARTSCore: ModuleRules
{
    public SeinARTSCore(ReadOnlyTargetRules Target) : base(Target)
    {
        // Fixed-point public headers contain reflected structs, so consumers
        // require CoreUObject's generated-type support as part of this module's
        // public include closure.
        PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject"});
    }
}
