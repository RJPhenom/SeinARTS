using UnrealBuildTool;

public class SeinARTSCoreEntity: ModuleRules
{
    public SeinARTSCoreEntity(ReadOnlyTargetRules Target) : base(Target)
    {
        // This module intentionally uses file-local helper names across many
        // implementation files. Unity merging collapses those translation
        // units and makes otherwise-valid anonymous namespaces collide.
        bUseUnity = false;

        // Public headers expose UObject/Engine types, fixed-point types,
        // developer settings, and gameplay tags. Their include paths must
        // propagate to downstream modules that include CoreEntity headers.
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "SeinARTSCore",
            "DeveloperSettings", "GameplayTags"
        });

        PrivateDependencyModuleNames.Add("AssetRegistry");

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
