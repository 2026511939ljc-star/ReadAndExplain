import unreal


MASK_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MF_LocalizedGlowMask"
)
MODULE_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MM_CH_LocalizedGlow"
)
MATERIAL_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Test/"
    "M_LocalizedGlow_Validation"
)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    ["/Game/TALibrary/Developer/albert/LocalizedGlow"],
    force_rescan=True,
)
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=True,
    include_soft_management_references=True,
    include_hard_management_references=True,
)


def game_dependencies(asset_path):
    return [
        str(dependency)
        for dependency in registry.get_dependencies(
            unreal.Name(asset_path),
            options,
        )
        if str(dependency).startswith("/Game/")
    ]


mask_dependencies = game_dependencies(MASK_PATH)
module_dependencies = game_dependencies(MODULE_PATH)
material_dependencies = game_dependencies(MATERIAL_PATH)

material = unreal.EditorAssetLibrary.load_asset(MATERIAL_PATH)
if material is None:
    raise RuntimeError(f"Could not load {MATERIAL_PATH}")

expressions = [
    obj
    for obj in unreal.ObjectIterator(unreal.MaterialExpression)
    if obj.get_outer() == material
]
function_calls = [
    expression
    for expression in expressions
    if isinstance(
        expression,
        unreal.MaterialExpressionMaterialFunctionCall,
    )
]

unreal.log(
    f"[LocalizedGlowFinalVerify] material_expressions={len(expressions)} "
    f"function_calls={len(function_calls)}"
)
unreal.log(
    f"[LocalizedGlowFinalVerify] mask_game_deps={mask_dependencies} "
    f"module_game_deps={module_dependencies} "
    f"material_game_deps={material_dependencies}"
)

if mask_dependencies:
    raise RuntimeError(f"Mask has unexpected dependencies: {mask_dependencies}")
if module_dependencies != [MASK_PATH]:
    raise RuntimeError(
        f"Module dependency chain is not isolated: {module_dependencies}"
    )
if material_dependencies != [MODULE_PATH]:
    raise RuntimeError(
        f"Validation material dependency chain is not isolated: "
        f"{material_dependencies}"
    )
if len(expressions) != 2 or len(function_calls) != 1:
    raise RuntimeError("Unexpected validation material graph")

all_dependencies = (
    mask_dependencies
    + module_dependencies
    + material_dependencies
)
if any(
    dependency.startswith("/Game/TALibrary/MatLibrary")
    for dependency in all_dependencies
):
    raise RuntimeError("Original MatLibrary dependency detected")

unreal.log("[LocalizedGlowFinalVerify] PASS")
