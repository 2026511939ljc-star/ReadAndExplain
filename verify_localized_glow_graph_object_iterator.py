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
    return [
        obj
        for obj in unreal.ObjectIterator(unreal.MaterialExpression)
        if obj.get_outer() == asset
    ]
'''

if source.count(old) != 1:
    raise RuntimeError("Could not patch UE 5.7 ObjectIterator access")

exec(compile(source.replace(old, new), str(SOURCE), "exec"))
