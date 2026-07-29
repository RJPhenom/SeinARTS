using UnrealBuildTool;

public class SeinARTSCoreEntity: ModuleRules
{
    public SeinARTSCoreEntity(ReadOnlyTargetRules Target) : base(Target)
    {
        // This module intentionally uses file-local helper names across many
        // implementation files. Unity merging collapses those translation
        // units and makes otherwise-valid anonymous namespaces collide.
        bUseUnity = false;

        PrivateDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "SeinARTSCore", "DeveloperSettings", "GameplayTags", "AssetRegistry"});

        if (Target.bBuildEditor)
        {
            // BP preview-actor refresh when ComponentData mutates — without
            // this, adding a new FInstancedStruct entry leaves the preview
            // actor's bridge component on its pre-add state until the BP is
            // closed + reopened. UnrealEd brings AssetEditorSubsystem,
            // Kismet brings FBlueprintEditor + FBlueprintEditorUtils.
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd",
                "Kismet"
            });
        }
    }
}
