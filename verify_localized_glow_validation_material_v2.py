from pathlib import Path


source_path = Path(__file__).with_name(
    "verify_localized_glow_validation_material.py"
)
source = source_path.read_text(encoding="utf-8")
old = "if material_dependencies != [MODULE_PATH]:"
new = "if set(material_dependencies) != {MODULE_PATH, MASK_PATH}:"
if source.count(old) != 1:
    raise RuntimeError("Could not patch transitive dependency expectation")

exec(compile(source.replace(old, new), str(source_path), "exec"))
