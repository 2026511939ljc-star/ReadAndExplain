from pathlib import Path


wrapper_path = Path(__file__).with_name(
    "build_localized_glow_material_functions_ue57.py"
)
wrapper_source = wrapper_path.read_text(encoding="utf-8")

old_tail = 'exec(compile(source, str(SOURCE), "exec"))'
new_tail = '''replace_once(
    """emissive_guid = unreal.Guid(
    0xB769B54D,
    0xD08D4440,
    0xABC21BA6,
    0xCD27D0E2,
)
""",
    """emissive_guid = unreal.Guid()
for guid_field, guid_value in (
    ("a", 0xB769B54D),
    ("b", 0xD08D4440),
    ("c", 0xABC21BA6),
    ("d", 0xCD27D0E2),
):
    emissive_guid.set_editor_property(guid_field, guid_value)
""",
)

exec(compile(source, str(SOURCE), "exec"))'''

if wrapper_source.count(old_tail) != 1:
    raise RuntimeError("Could not patch UE 5.7 builder tail")

exec(
    compile(
        wrapper_source.replace(old_tail, new_tail),
        str(wrapper_path),
        "exec",
    )
)
