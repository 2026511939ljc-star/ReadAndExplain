# Localized Material Workbench 0.1.0-test

This editor-only integration plugin coordinates the existing
`LocalizedMapBaker` and `LocalizedGlowPainter` plugins without modifying,
copying, or replacing either one.

## Safety contract

- The workbench owns no paint or bake session.
- Closing the workbench does not cancel, save, or mutate either source tool.
- Map Baker receives the current mesh through Content Browser selection sync.
- Glow Painter receives the existing Level Editor actor selection unchanged.
- Missing source plugins produce a status message instead of a hard load error.
- The source plugins remain independently accessible from the Tools menu.

## Test flow

1. Enable all three plugins and restart Unreal Editor.
2. Select one Static Mesh or Skeletal Mesh actor in the level.
3. Open **Tools > Localized Material Workbench**.
4. Confirm the asset type, material-slot count, and UV-channel count.
5. Open **辅助贴图烘焙**, then click **同步选择并打开 Map Baker**.
6. Confirm Map Baker pre-fills the same mesh and can bake normally.
7. Return to the workbench, open **发光遮罩绘制**, then click
   **保留 Actor 并打开 Glow Painter**.
8. Confirm Glow Painter sees the same actor and can prepare a paint session.
9. Cancel the paint session and verify the original material is restored.
10. Run `LocalizedMaterialWorkbench.SelfTest` in the editor console and check
    for `[LocalizedMaterialWorkbench][SelfTest] PASS` in Output Log.
