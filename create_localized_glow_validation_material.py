import unreal


TEST_FOLDER = "/Game/TALibrary/Developer/albert/LocalizedGlow/Test"
MATERIAL_PATH = f"{TEST_FOLDER}/M_LocalizedGlow_Validation"
MODULE_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MM_CH_LocalizedGlow"
)

mel = unreal.MaterialEditingLibrary
module = unreal.EditorAssetLibrary.load_asset(MODULE_PATH)
if module is None:
    raise RuntimeError(f"Could not load {MODULE_PATH}")

if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL_PATH):
    raise RuntimeError(f"Refusing to overwrite existing test asset: {MATERIAL_PATH}")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
material = asset_tools.create_asset(
    "M_LocalizedGlow_Validation",
    TEST_FOLDER,
    unreal.Material,
    unreal.MaterialFactoryNew(),
)
if material is None:
    raise RuntimeError(f"Could not create {MATERIAL_PATH}")

base_emissive = mel.create_material_expression(
    material,
    unreal.MaterialExpressionConstant3Vector,
    -500,
    0,
)
base_emissive.set_editor_property(
    "constant",
    unreal.LinearColor(0.0, 0.0, 0.0, 1.0),
)

localized_glow = mel.create_material_expression(
    material,
    unreal.MaterialExpressionMaterialFunctionCall,
    -240,
    0,
)
if not localized_glow.set_material_function(module):
    raise RuntimeError(f"Could not assign {MODULE_PATH} to function call")

if not mel.connect_material_expressions(
    base_emissive,
    "",
    localized_glow,
    "BaseEmissive",
):
    raise RuntimeError("Could not connect BaseEmissive")

if not mel.connect_material_property(
    localized_glow,
    "Emissive",
    unreal.MaterialProperty.MP_EMISSIVE_COLOR,
):
    raise RuntimeError("Could not connect Localized Glow to Emissive Color")

mel.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(
    material,
    only_if_is_dirty=False,
):
    raise RuntimeError(f"Could not save {MATERIAL_PATH}")

unreal.log(
    f"[LocalizedGlowValidationMaterial] expressions="
    f"{mel.get_num_material_expressions(material)}"
)
unreal.log("[LocalizedGlowValidationMaterial] PASS")
