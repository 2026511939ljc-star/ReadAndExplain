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


# UE 5.7 exposes single unnamed destination inputs as the literal name "None".
source = source.replace(', "Input")', ', "None")')
replace_once(
    'connect(attributes_input, "", get_attributes, "")',
    'connect(attributes_input, "", get_attributes, "None")',
)
replace_once(
    'connect(area_mask, "", area_output, "")',
    'connect(area_mask, "", area_output, "None")',
)
replace_once(
    'connect(edge_mask, "", edge_output, "")',
    'connect(edge_mask, "", edge_output, "None")',
)
replace_once(
    'connect(set_attributes, "", result_output, "")',
    'connect(set_attributes, "", result_output, "None")',
)

# These two assets belong solely to this feature. Clearing them makes a failed
# build safely repeatable without touching any source MatLibrary asset.
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

exec(compile(source, str(SOURCE), "exec"))
