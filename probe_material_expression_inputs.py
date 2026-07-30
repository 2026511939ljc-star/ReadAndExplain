import unreal


MASK_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MF_LocalizedGlowMask"
)


mel = unreal.MaterialEditingLibrary
function = unreal.EditorAssetLibrary.load_asset(MASK_PATH)
if function is None:
    raise RuntimeError(f"Could not load {MASK_PATH}")

expression_classes = (
    unreal.MaterialExpressionAbs,
    unreal.MaterialExpressionMax,
    unreal.MaterialExpressionDivide,
    unreal.MaterialExpressionDistance,
    unreal.MaterialExpressionMin,
    unreal.MaterialExpressionOneMinus,
    unreal.MaterialExpressionSmoothStep,
    unreal.MaterialExpressionSaturate,
    unreal.MaterialExpressionMultiply,
    unreal.MaterialExpressionAdd,
    unreal.MaterialExpressionFresnel,
    unreal.MaterialExpressionGetMaterialAttributes,
    unreal.MaterialExpressionSetMaterialAttributes,
    unreal.MaterialExpressionFunctionOutput,
)

try:
    mel.delete_all_material_expressions_in_function(function)

    for index, expression_class in enumerate(expression_classes):
        class_name = expression_class.__name__
        expression = mel.create_material_expression_in_function(
            function,
            expression_class,
            index * 100,
            0,
        )
        if expression is None:
            raise RuntimeError(f"Could not create {class_name}")

        input_names = mel.get_material_expression_input_names(expression)
        unreal.log(
            f"[LocalizedGlowProbe] {class_name} inputs={list(input_names)}"
        )
finally:
    mel.delete_all_material_expressions_in_function(function)
    mel.update_material_function(function)

    if not unreal.EditorAssetLibrary.save_loaded_asset(
        function,
        only_if_is_dirty=False,
    ):
        raise RuntimeError(f"Could not save clean {MASK_PATH}")

unreal.log("[LocalizedGlowProbe] PASS")
