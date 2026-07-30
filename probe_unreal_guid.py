import unreal


values = (0xB769B54D, 0xD08D4440, 0xABC21BA6, 0xCD27D0E2)

tests = (
    ("keyword", lambda: unreal.Guid(a=values[0], b=values[1], c=values[2], d=values[3])),
    ("default_set", lambda: unreal.Guid()),
)

for name, factory in tests:
    try:
        guid = factory()
        if name == "default_set":
            for field, value in zip(("a", "b", "c", "d"), values):
                guid.set_editor_property(field, value)
        unreal.log(f"[LocalizedGlowGuidProbe] {name}=PASS value={guid}")
    except Exception as exc:
        unreal.log_warning(f"[LocalizedGlowGuidProbe] {name}=FAIL error={exc}")

unreal.log("[LocalizedGlowGuidProbe] DONE")
