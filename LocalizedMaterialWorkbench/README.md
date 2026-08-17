# Localized Material Workbench 0.1.1-test

This editor-only integration plugin embeds the real Slate controls from
`LocalizedMapBaker` and `LocalizedGlowPainter` inside one workflow window.
It does not copy, modify, or replace either source plugin.

## Safety contract

- The workbench does not duplicate bake or paint algorithms.
- The original modules still own bake, paint, save, and restore state.
- Closing the workbench closes the embedded source hosts; an active Glow Painter
  session follows its existing cancel-and-restore safety path.
- Map Baker receives the current mesh through Content Browser selection sync.
- Glow Painter receives the existing Level Editor actor selection unchanged.
- Missing source plugins produce a status message instead of a hard load error.
- The source plugins remain independently accessible after the workbench closes.

## Test flow

1. Enable all three plugins and restart Unreal Editor.
2. Select one Static Mesh or Skeletal Mesh actor in the level.
3. Open **Tools > Localized Material Workbench**.
4. Confirm the asset type, material-slot count, and UV-channel count.
5. Open **辅助贴图烘焙** and confirm the complete Map Baker controls are visible.
6. Confirm Map Baker pre-fills the same mesh and can bake normally.
7. Open **发光遮罩绘制** and confirm the complete Glow Painter controls are visible.
8. Confirm Glow Painter sees the same actor and can prepare a paint session.
9. Cancel the paint session and verify the original material is restored.
10. Run `LocalizedMaterialWorkbench.SelfTest` in the editor console and check
    for `[LocalizedMaterialWorkbench][SelfTest] PASS` in Output Log.
