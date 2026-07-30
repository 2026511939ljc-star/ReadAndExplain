from pathlib import Path


SOURCE = Path(r"C:\tmp\build_localized_glow_material_functions.py")
source = SOURCE.read_text(encoding="utf-8")

helpers_end = source.index("mask_function = ")
helpers = source[:helpers_end]

mask_body_start = source.index(
    "# ---------------------------------------------------------------------------\n"
    "# MF_LocalizedGlowMask"
)
mask_body_end = source.index(
    "# ---------------------------------------------------------------------------\n"
    "# MM_CH_LocalizedGlow"
)
mask_body = source[mask_body_start:mask_body_end]

# UE 5.7 exposes the sole input pin of unary nodes and Function Output as
# "None" through MaterialEditingLibrary.
mask_body = mask_body.replace(
    ', "Input")',
    ', "None")',
).replace(
    'connect(area_mask, "", area_output, "")',
    'connect(area_mask, "", area_output, "None")',
).replace(
    'connect(edge_mask, "", edge_output, "")',
    'connect(edge_mask, "", edge_output, "None")',
)

generated = helpers + '''
mask_function = unreal.EditorAssetLibrary.load_asset(MASK_PATH)
if mask_function is None:
    raise RuntimeError("Localized Glow mask function could not be loaded")

expression_count = mel.get_num_material_expressions_in_function(mask_function)
if expression_count != 0:
    raise RuntimeError(
        f"Refusing to overwrite non-empty mask "
        f"{mask_function.get_path_name()} ({expression_count} expressions)"
    )

''' + mask_body + '''
expression_count = mel.get_num_material_expressions_in_function(mask_function)
if expression_count != 25:
    raise RuntimeError(
        f"Expected 25 mask expressions, got {expression_count}"
    )

unreal.log(f"[LocalizedGlowMaskBuild] expressions={expression_count}")
unreal.log("[LocalizedGlowMaskBuild] PASS")
'''

exec(compile(generated, str(SOURCE), "exec"))
