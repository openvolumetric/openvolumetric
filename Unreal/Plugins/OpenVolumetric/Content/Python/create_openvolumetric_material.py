"""Create or update the material used by OpenVolumetric's textured dynamic mesh."""

import unreal


ASSET_PATH = "/OpenVolumetric/Materials/M_OpenVolumetricTexture"


def create_material():
    material_exists = unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH)
    if material_exists:
        material = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        material = tools.create_asset(
            "M_OpenVolumetricTexture",
            "/OpenVolumetric/Materials",
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    if material is None:
        raise RuntimeError("Could not create or load the OpenVolumetric texture material.")

    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT
    )
    # Volumetric reconstructions can contain open or inconsistently oriented
    # surfaces. Rendering both sides prevents valid capture triangles from
    # disappearing when viewed from their back face.
    material.set_editor_property("two_sided", True)

    # Find the existing texture sample. A previous version placed an
    # EyeAdaptationInverse node between it and Emissive; reconnecting the
    # sample directly is safer than deleting rooted graph nodes in UE 5.8.
    emissive_input = unreal.MaterialEditingLibrary.get_material_property_input_node(
        material,
        unreal.MaterialProperty.MP_EMISSIVE_COLOR,
    )
    sample = emissive_input
    if isinstance(
        emissive_input, unreal.MaterialExpressionEyeAdaptationInverse
    ):
        inputs = unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
            material,
            emissive_input,
        )
        sample = next(
            (
                node
                for node in inputs
                if isinstance(
                    node,
                    unreal.MaterialExpressionTextureSampleParameter2D,
                )
            ),
            None,
        )
    if not isinstance(
        sample, unreal.MaterialExpressionTextureSampleParameter2D
    ):
        sample = unreal.MaterialEditingLibrary.create_material_expression(
            material,
            unreal.MaterialExpressionTextureSampleParameter2D,
            -300,
            0,
        )
        sample.set_editor_property("parameter_name", "OpenVolumetricTexture")
    unreal.MaterialEditingLibrary.connect_material_property(
        sample,
        "RGB",
        unreal.MaterialProperty.MP_EMISSIVE_COLOR,
    )
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log("Created or updated two-sided OpenVolumetric texture material.")


create_material()
