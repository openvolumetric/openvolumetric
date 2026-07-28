"""Create or update the material used by OpenVol's textured dynamic mesh."""

import unreal


ASSET_PATH = "/OpenVol/Materials/M_OpenVolTexture"


def create_material():
    material_exists = unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH)
    if material_exists:
        material = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        material = tools.create_asset(
            "M_OpenVolTexture",
            "/OpenVol/Materials",
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    if material is None:
        raise RuntimeError("Could not create or load the OpenVol texture material.")

    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT
    )
    # Volumetric reconstructions can contain open or inconsistently oriented
    # surfaces. Rendering both sides prevents valid capture triangles from
    # disappearing when viewed from their back face.
    material.set_editor_property("two_sided", True)

    # The existing asset already contains this graph. UE 5.8 protects direct
    # Python access to Material.Expressions, so only construct the graph when
    # creating a new material.
    if not material_exists:
        sample = unreal.MaterialEditingLibrary.create_material_expression(
            material,
            unreal.MaterialExpressionTextureSampleParameter2D,
            -300,
            0,
        )
        sample.set_editor_property("parameter_name", "OpenVolTexture")
        unreal.MaterialEditingLibrary.connect_material_property(
            sample,
            "RGB",
            unreal.MaterialProperty.MP_EMISSIVE_COLOR,
        )
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log("Created or updated two-sided OpenVol texture material.")


create_material()
