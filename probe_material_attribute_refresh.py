import unreal


MODULE_PATH = (
    "/Game/TALibrary/Developer/albert/LocalizedGlow/Materials/Functions/"
    "MM_CH_LocalizedGlow"
)
mel = unreal.MaterialEditingLibrary
function = unreal.EditorAssetLibrary.load_asset(MODULE_PATH)
if function is None:
    raise RuntimeError(f"Could not load {MODULE_PATH}")

guid = unreal.Guid()
for field, value in (
    ("a", 0xB769B54D),
    ("b", 0xD08D4440),
    ("c", 0xABC21BA6),
    ("d", 0xCD27D0E2),
):
    guid.set_editor_property(field, value)

try:
    mel.delete_all_material_expressions_in_function(function)
    get_node = mel.create_material_expression_in_function(
        function, unreal.MaterialExpressionGetMaterialAttributes, -300, 0
    )
    add_node = mel.create_material_expression_in_function(
        function, unreal.MaterialExpressionAdd, 0, 0
    )
    get_node.set_editor_property("attribute_get_types", [guid])

    unreal.log(
        "[LocalizedGlowRefreshProbe] callable_post_edit_change="
        f"{callable(getattr(get_node, 'post_edit_change', None))}"
    )
    if callable(getattr(get_node, "post_edit_change", None)):
        get_node.post_edit_change()

    connected = mel.connect_material_expressions(
        get_node, "Emissive Color", add_node, "A"
    )
    unreal.log(
        f"[LocalizedGlowRefreshProbe] connection_after_refresh={connected}"
    )
finally:
    mel.delete_all_material_expressions_in_function(function)
    mel.update_material_function(function)
    unreal.EditorAssetLibrary.save_loaded_asset(
        function, only_if_is_dirty=False
    )

unreal.log("[LocalizedGlowRefreshProbe] PASS")
