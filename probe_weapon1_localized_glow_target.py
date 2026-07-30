import unreal


MATERIAL_PATH = "/Game/VFX/Mesh/Weapon_Mesh/Weapon_Material/M_Weapon1"

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    ["/Game/VFX/Mesh/Weapon_Mesh", "/Game/TALibrary/Developer/albert/LocalizedGlow"],
    force_rescan=True,
)
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=True,
    include_soft_management_references=True,
    include_hard_management_references=True,
)

material = unreal.EditorAssetLibrary.load_asset(MATERIAL_PATH)
if material is None:
    raise RuntimeError(f"Could not load {MATERIAL_PATH}")

expressions = [
    obj
    for obj in unreal.ObjectIterator(unreal.MaterialExpression)
    if obj.get_outer() == material
]
unreal.log(f"[Weapon1Probe] material_expressions={len(expressions)}")

for expression in expressions:
    if isinstance(expression, unreal.MaterialExpressionSetMaterialAttributes):
        input_names = []
        for input_value in expression.get_editor_property("inputs"):
            input_names.append(str(input_value.get_editor_property("input_name")))
        unreal.log(
            f"[Weapon1Probe] set_node={expression.get_name()} "
            f"inputs={input_names}"
        )

referencers = [
    str(package)
    for package in registry.get_referencers(
        unreal.Name(MATERIAL_PATH),
        options,
    )
    if str(package).startswith("/Game/")
]
unreal.log(f"[Weapon1Probe] referencers={referencers}")

for package_name in referencers:
    assets = registry.get_assets_by_package_name(
        unreal.Name(package_name),
        include_only_on_disk_assets=False,
        skip_ar_filtered_assets=False,
    )
    for asset_data in assets:
        unreal.log(
            f"[Weapon1Probe] ref_asset={asset_data.get_soft_object_path()} "
            f"class={asset_data.asset_class_path}"
        )

unreal.log("[Weapon1Probe] PASS")
