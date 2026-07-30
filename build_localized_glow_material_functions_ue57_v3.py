from pathlib import Path


v2_path = Path(__file__).with_name(
    "build_localized_glow_material_functions_ue57_v2.py"
)
v2_source = v2_path.read_text(encoding="utf-8")

marker = '\nexec(compile(source, str(SOURCE), "exec"))\'\'\''
replacement = '''
replace_once(
    'connect(get_attributes, "EmissiveColor", add_emissive, "A")',
    'connect(get_attributes, "Emissive Color", add_emissive, "A")',
)

exec(compile(source, str(SOURCE), "exec"))\'''\'\''''

if v2_source.count(marker) != 1:
    raise RuntimeError("Could not add UE 5.7 Emissive Color pin patch")

exec(
    compile(
        v2_source.replace(marker, replacement),
        str(v2_path),
        "exec",
    )
)
