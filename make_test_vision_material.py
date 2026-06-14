"""
Quick throwaway: generates a simple Post Process "vision style" material for
testing the SeinARTS fog render actor's vision-layer switching. It just remaps
scene luminance to a purple->orange ramp — not meant to be a real thermal look,
only an obviously-different full-screen effect to confirm Sein.Vision.Layer works.

Requires the "Python Editor Script Plugin" (enable in Edit > Plugins if needed).
Run it in-editor:
    Tools > Execute Python Script...   (pick this file)
  or in the Output Log's Cmd dropdown -> Python:
    py "D:/Projects/Unreal Engine/SeinARTS/make_test_vision_material.py"
"""
import unreal

PKG_PATH   = "/Game/SeinARTS"
ASSET_NAME = "M_Vision_TestStyle"
FULL       = PKG_PATH + "/" + ASSET_NAME

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.log_warning("[SeinTest] {} already exists - delete it to regenerate.".format(FULL))
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(ASSET_NAME, PKG_PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)

    mel = unreal.MaterialEditingLibrary

    # Scene color (the post-process input).
    scene = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -900, 0)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)

    # Luminance = dot(sceneRGBA, (0.299, 0.587, 0.114, 0)).
    weights = mel.create_material_expression(mat, unreal.MaterialExpressionConstant4Vector, -700, 200)
    weights.set_editor_property("constant", unreal.LinearColor(0.299, 0.587, 0.114, 0.0))
    dot = mel.create_material_expression(mat, unreal.MaterialExpressionDotProduct, -480, 40)
    mel.connect_material_expressions(scene, "Color", dot, "A")
    mel.connect_material_expressions(weights, "", dot, "B")

    # Cold purple -> hot orange ramp by luminance.
    cold = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -480, 220)
    cold.set_editor_property("constant", unreal.LinearColor(0.15, 0.0, 0.40, 1.0))
    hot = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -480, 340)
    hot.set_editor_property("constant", unreal.LinearColor(1.0, 0.75, 0.10, 1.0))
    lerp = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, 120)
    mel.connect_material_expressions(cold, "", lerp, "A")
    mel.connect_material_expressions(hot,  "", lerp, "B")
    mel.connect_material_expressions(dot,  "", lerp, "Alpha")

    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(FULL)
    unreal.log("[SeinTest] Created {} (Post Process). Assign it to a Vision Layer slot.".format(FULL))
