import unreal


ASSET_FOLDER = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions"
)
ASSET_NAME = "MM_CH_LocalizedGlow"
ASSET_PATH = f"{ASSET_FOLDER}/{ASSET_NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
    raise RuntimeError(f"Refusing to overwrite existing asset: {ASSET_PATH}")

asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    ASSET_NAME,
    ASSET_FOLDER,
    unreal.MaterialFunction,
    unreal.MaterialFunctionFactoryNew(),
)
if asset is None:
    raise RuntimeError(f"Failed to create material function: {ASSET_PATH}")

if unreal.MaterialEditingLibrary.get_num_material_expressions_in_function(asset) != 0:
    raise RuntimeError(f"New material function was not empty: {ASSET_PATH}")

if not unreal.EditorAssetLibrary.save_loaded_asset(
    asset, only_if_is_dirty=False
):
    raise RuntimeError(f"Failed to save material function: {ASSET_PATH}")

unreal.log(f"[LocalizedGlowCreateModule] Created clean asset: {ASSET_PATH}")
unreal.log("[LocalizedGlowCreateModule] PASS")
