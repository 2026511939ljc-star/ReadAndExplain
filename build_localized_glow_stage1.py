from pathlib import Path


SOURCE = Path(r"C:\tmp\build_localized_glow_material_functions.py")
source = SOURCE.read_text(encoding="utf-8")


def replace_once(old, new):
    global source
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"Expected one source patch match, got {count}: {old[:80]!r}"
        )
    source = source.replace(old, new)


source = source.replace(', "Input")', ', "None")')
replace_once(
    'connect(area_mask, "", area_output, "")',
    'connect(area_mask, "", area_output, "None")',
)
replace_once(
    'connect(edge_mask, "", edge_output, "")',
    'connect(edge_mask, "", edge_output, "None")',
)
replace_once(
    '''for function in (mask_function, module_function):
    expression_count = mel.get_num_material_expressions_in_function(function)
    if expression_count != 0:
        raise RuntimeError(
            f"Refusing to overwrite non-empty function "
            f"{function.get_path_name()} ({expression_count} expressions)"
        )
''',
    '''for function in (mask_function, module_function):
    mel.delete_all_material_expressions_in_function(function)
''',
)
replace_once(
    '''emissive_guid = unreal.Guid(
    0xB769B54D,
    0xD08D4440,
    0xABC21BA6,
    0xCD27D0E2,
)
''',
    '''emissive_guid = unreal.Guid()
for guid_field, guid_value in (
    ("a", 0xB769B54D),
    ("b", 0xD08D4440),
    ("c", 0xABC21BA6),
    ("d", 0xCD27D0E2),
):
    emissive_guid.set_editor_property(guid_field, guid_value)
''',
)

tail_start = source.index("get_attributes = create(")
source = source[:tail_start] + '''get_attributes = create(
    module_function,
    unreal.MaterialExpressionGetMaterialAttributes,
    -700,
    -600,
)
set_property(get_attributes, "attribute_get_types", [emissive_guid])
connect(attributes_input, "", get_attributes, "None")

set_attributes = create(
    module_function,
    unreal.MaterialExpressionSetMaterialAttributes,
    -120,
    -520,
)
set_property(set_attributes, "attribute_set_types", [emissive_guid])
connect(attributes_input, "", set_attributes, "MaterialAttributes")

mel.update_material_function(module_function)
if not unreal.EditorAssetLibrary.save_loaded_asset(
    module_function,
    only_if_is_dirty=False,
):
    raise RuntimeError(f"Failed to save {MODULE_PATH}")

unreal.log(
    f"[LocalizedGlowStage1] mask_expressions="
    f"{mel.get_num_material_expressions_in_function(mask_function)}"
)
unreal.log(
    f"[LocalizedGlowStage1] module_expressions="
    f"{mel.get_num_material_expressions_in_function(module_function)}"
)
unreal.log("[LocalizedGlowStage1] PASS")
'''

exec(compile(source, str(SOURCE), "exec"))
