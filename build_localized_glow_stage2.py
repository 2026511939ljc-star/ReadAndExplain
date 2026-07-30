import unreal


MODULE_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MM_CH_LocalizedGlow"
)
mel = unreal.MaterialEditingLibrary
function = unreal.EditorAssetLibrary.load_asset(MODULE_PATH)
if function is None:
    raise RuntimeError(f"Could not load {MODULE_PATH}")


def create(expression_class, x, y):
    expression = mel.create_material_expression_in_function(
        function, expression_class, x, y
    )
    if expression is None:
        raise RuntimeError(f"Could not create {expression_class.__name__}")
    return expression


def connect(source, output_name, destination, input_name):
    if not mel.connect_material_expressions(
        source, output_name, destination, input_name
    ):
        raise RuntimeError(
            f"Failed connection: {source.get_name()}[{output_name}] -> "
            f"{destination.get_name()}[{input_name}]"
        )


expressions = [
    obj
    for obj in unreal.get_objects_with_outer(
        function, include_nested_objects=True
    )
    if isinstance(obj, unreal.MaterialExpression)
]

attributes_inputs = [
    expr
    for expr in expressions
    if isinstance(expr, unreal.MaterialExpressionFunctionInput)
    and str(expr.get_editor_property("input_name")) == "In"
]
get_nodes = [
    expr
    for expr in expressions
    if isinstance(expr, unreal.MaterialExpressionGetMaterialAttributes)
]
set_nodes = [
    expr
    for expr in expressions
    if isinstance(expr, unreal.MaterialExpressionSetMaterialAttributes)
]

if len(attributes_inputs) != 1 or len(get_nodes) != 1 or len(set_nodes) != 1:
    raise RuntimeError(
        "Stage 1 graph markers are missing or ambiguous: "
        f"In={len(attributes_inputs)}, Get={len(get_nodes)}, Set={len(set_nodes)}"
    )

combined_candidates = []
for expr in expressions:
    if not isinstance(expr, unreal.MaterialExpressionAdd):
        continue
    position = mel.get_material_expression_node_position(expr)
    if tuple(position) == (-820, 600):
        combined_candidates.append(expr)

if len(combined_candidates) != 1:
    raise RuntimeError(
        f"Combined emission marker count was {len(combined_candidates)}"
    )

attributes_input = attributes_inputs[0]
get_attributes = get_nodes[0]
set_attributes = set_nodes[0]
combined_emission = combined_candidates[0]

get_inputs = list(mel.get_material_expression_input_names(get_attributes))
set_inputs = list(mel.get_material_expression_input_names(set_attributes))
unreal.log(
    f"[LocalizedGlowStage2] get_inputs={get_inputs} set_inputs={set_inputs}"
)
if "Emissive Color" not in set_inputs:
    raise RuntimeError("Set Material Attributes did not rebuild Emissive Color")

add_emissive = create(unreal.MaterialExpressionAdd, -420, -300)
connect(get_attributes, "Emissive Color", add_emissive, "A")
connect(combined_emission, "", add_emissive, "B")
connect(add_emissive, "", set_attributes, "Emissive Color")

result_output = create(unreal.MaterialExpressionFunctionOutput, 180, -500)
result_output.set_editor_property("output_name", "Result")
result_output.set_editor_property("sort_priority", 0)
result_output.set_editor_property(
    "description",
    "Material Attributes with four localized glow regions added to Emissive.",
)
connect(set_attributes, "", result_output, "None")

mel.update_material_function(function)
if not unreal.EditorAssetLibrary.save_loaded_asset(
    function, only_if_is_dirty=False
):
    raise RuntimeError(f"Could not save {MODULE_PATH}")

unreal.log(
    f"[LocalizedGlowStage2] expressions="
    f"{mel.get_num_material_expressions_in_function(function)}"
)
unreal.log("[LocalizedGlowStage2] PASS")
