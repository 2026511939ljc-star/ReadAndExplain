from pathlib import Path


SOURCE = Path(r"C:\tmp\build_localized_glow_material_functions.py")
source = SOURCE.read_text(encoding="utf-8")

helpers_end = source.index("mask_function = ")
helpers = source[:helpers_end]

module_body_start = source.index("world_position = create(")
module_body_end = source.index("emissive_guid = unreal.Guid(")
module_body = source[module_body_start:module_body_end]

generated = helpers + '''
mask_function = unreal.EditorAssetLibrary.load_asset(MASK_PATH)
module_function = unreal.EditorAssetLibrary.load_asset(MODULE_PATH)
if mask_function is None or module_function is None:
    raise RuntimeError("Localized Glow material functions could not be loaded")

mel.delete_all_material_expressions_in_function(module_function)

base_emissive = make_function_input(
    module_function,
    "BaseEmissive",
    unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3,
    0,
    -3300,
    -700,
    "Existing material Emissive Color. Connect zero when unused.",
)
''' + module_body + '''
final_emissive = create(
    module_function,
    unreal.MaterialExpressionAdd,
    -420,
    -300,
)
connect(base_emissive, "", final_emissive, "A")
connect(combined_emission, "", final_emissive, "B")

emissive_output = make_function_output(
    module_function,
    "Emissive",
    0,
    -120,
    -300,
    "Base Emissive plus four localized soft Fresnel glow regions.",
)
connect(final_emissive, "", emissive_output, "None")

mel.update_material_function(module_function)
if not unreal.EditorAssetLibrary.save_loaded_asset(
    module_function,
    only_if_is_dirty=False,
):
    raise RuntimeError(f"Failed to save {MODULE_PATH}")

unreal.log(
    f"[LocalizedGlowEmissiveBuild] expressions="
    f"{mel.get_num_material_expressions_in_function(module_function)}"
)
unreal.log("[LocalizedGlowEmissiveBuild] PASS")
'''

exec(compile(generated, str(SOURCE), "exec"))
