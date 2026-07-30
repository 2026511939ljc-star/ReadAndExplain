from pathlib import Path


SOURCE = Path(__file__).with_name("verify_localized_glow_graph.py")
source = SOURCE.read_text(encoding="utf-8")

old = '''def expressions_for(asset):
    return [
        obj
        for obj in unreal.get_objects_with_outer(
            asset, include_nested_objects=True
        )
        if isinstance(obj, unreal.MaterialExpression)
    ]
'''
new = '''def expressions_for(asset):
    editor_data = asset.get_editor_property("editor_only_data")
    collection = editor_data.get_editor_property("expression_collection")
    return list(collection.get_editor_property("expressions"))
'''

if source.count(old) != 1:
    raise RuntimeError("Could not patch UE 5.7 expression collection access")

exec(compile(source.replace(old, new), str(SOURCE), "exec"))
