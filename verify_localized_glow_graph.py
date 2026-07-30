from collections import Counter

import unreal


MASK_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MF_LocalizedGlowMask"
)
MODULE_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MM_CH_LocalizedGlow"
)
mel = unreal.MaterialEditingLibrary


def expressions_for(asset):
    return [
        obj
        for obj in unreal.get_objects_with_outer(
            asset, include_nested_objects=True
        )
        if isinstance(obj, unreal.MaterialExpression)
    ]


def names_of(expressions, expression_class, property_name):
    return [
        str(expr.get_editor_property(property_name))
        for expr in expressions
        if isinstance(expr, expression_class)
    ]


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    ["/Game/TALibrary/Developer/albert/LocalizedGlow"],
    force_rescan=True,
)
dependency_options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=True,
    include_soft_management_references=True,
    include_hard_management_references=True,
)

mask = unreal.EditorAssetLibrary.load_asset(MASK_PATH)
module = unreal.EditorAssetLibrary.load_asset(MODULE_PATH)
if mask is None or module is None:
    raise RuntimeError("Could not load Localized Glow functions")

mask_expressions = expressions_for(mask)
module_expressions = expressions_for(module)

mask_inputs = names_of(
    mask_expressions,
    unreal.MaterialExpressionFunctionInput,
    "input_name",
)
mask_outputs = names_of(
    mask_expressions,
    unreal.MaterialExpressionFunctionOutput,
    "output_name",
)
module_inputs = names_of(
    module_expressions,
    unreal.MaterialExpressionFunctionInput,
    "input_name",
)
module_outputs = names_of(
    module_expressions,
    unreal.MaterialExpressionFunctionOutput,
    "output_name",
)
parameter_names = names_of(
    module_expressions,
    unreal.MaterialExpressionParameter,
    "parameter_name",
)
function_calls = [
    expr
    for expr in module_expressions
    if isinstance(expr, unreal.MaterialExpressionMaterialFunctionCall)
]

mask_dependencies = [
    str(dependency)
    for dependency in registry.get_dependencies(
        unreal.Name(MASK_PATH), dependency_options
    )
    if str(dependency).startswith("/Game/")
]
module_dependencies = [
    str(dependency)
    for dependency in registry.get_dependencies(
        unreal.Name(MODULE_PATH), dependency_options
    )
    if str(dependency).startswith("/Game/")
]

unreal.log(
    f"[LocalizedGlowGraphVerify] mask_count="
    f"{mel.get_num_material_expressions_in_function(mask)} "
    f"inputs={mask_inputs} outputs={mask_outputs}"
)
unreal.log(
    f"[LocalizedGlowGraphVerify] mask_classes="
    f"{dict(Counter(type(expr).__name__ for expr in mask_expressions))}"
)
unreal.log(
    f"[LocalizedGlowGraphVerify] module_count="
    f"{mel.get_num_material_expressions_in_function(module)} "
    f"inputs={module_inputs} outputs={module_outputs} "
    f"parameters={len(parameter_names)} calls={len(function_calls)}"
)
unreal.log(
    f"[LocalizedGlowGraphVerify] mask_game_deps={mask_dependencies} "
    f"module_game_deps={module_dependencies}"
)

expected_mask_inputs = {
    "PositionWS",
    "CenterWS",
    "RadiusXYZ",
    "Softness",
    "Enabled",
}
if set(mask_inputs) != expected_mask_inputs or len(mask_inputs) != 5:
    raise RuntimeError(f"Unexpected mask inputs: {mask_inputs}")
if set(mask_outputs) != {"AreaMask", "EdgeMask"} or len(mask_outputs) != 2:
    raise RuntimeError(f"Unexpected mask outputs: {mask_outputs}")
if module_inputs != ["BaseEmissive"]:
    raise RuntimeError(f"Unexpected module inputs: {module_inputs}")
if module_outputs != ["Emissive"]:
    raise RuntimeError(f"Unexpected module outputs: {module_outputs}")
if len(parameter_names) != 28 or len(set(parameter_names)) != 28:
    raise RuntimeError(f"Unexpected module parameters: {parameter_names}")
if len(function_calls) != 4:
    raise RuntimeError(f"Expected 4 mask calls, got {len(function_calls)}")
if any(
    isinstance(
        expr,
        (
            unreal.MaterialExpressionGetMaterialAttributes,
            unreal.MaterialExpressionSetMaterialAttributes,
        ),
    )
    for expr in module_expressions
):
    raise RuntimeError("Dynamic Material Attributes node unexpectedly present")
if mask_dependencies:
    raise RuntimeError(f"Mask has /Game dependencies: {mask_dependencies}")
unexpected_module_dependencies = [
    dependency
    for dependency in module_dependencies
    if dependency != MASK_PATH
]
if unexpected_module_dependencies:
    raise RuntimeError(
        f"Module has unexpected /Game dependencies: "
        f"{unexpected_module_dependencies}"
    )
if any("/Game/TALibrary/MatLibrary" in dep for dep in module_dependencies):
    raise RuntimeError("Original MatLibrary dependency detected")

unreal.log("[LocalizedGlowGraphVerify] PASS")
