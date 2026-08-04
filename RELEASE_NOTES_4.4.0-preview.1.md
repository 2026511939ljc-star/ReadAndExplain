# ReadAllandExplains 4.4.0-preview.1

本版本把单资产导出进一步升级为可直接交给 AI 的 Context Pack，并完善 Niagara 曲线的去重、追踪与可视化。

## 主要更新

1. 新增“生成 AI Context Pack（含项目依赖）”入口，把内容浏览器选择作为根资产，递归收集 `/Game/` 下受支持的项目依赖。
2. Context Pack 生成独立时间戳目录，包含 `README.md`、`context-pack.json`、`index.md`、`index.json` 及分类后的资产文档。
3. 新增 `ContextPackDependencyDepth`，默认递归 2 层，可在 Editor Preferences > Plugins > ReadAllandExplains 调整，范围 0 到 4。
4. Niagara 曲线按完整 Key、插值、切线、外推和曲线配置生成稳定 MD5 指纹；同形副本合并为一条记录。
5. 曲线 JSON 新增 `fingerprint`、`usedBy` 和 `usageCount`，去重后仍保留全部实际来源位置。
6. 含曲线的 Niagara 资产新增 `_NiagaraCurves.svg`，可直接在浏览器中查看各通道形状、Key 点、时间和值范围。
7. 新增无界面命令 `ReadAllandExplains.ExportContextPack`，原 `ReadAllandExplains.ExportAssets` 与旧兼容命令保持不变。

## 输出示例

```text
Saved/ReadAllandExplainsExports/ContextPacks/ContextPack_YYYYMMDD_HHMMSS/
├── README.md
├── context-pack.json
├── index.md
├── index.json
├── Niagara/
│   ├── Asset_ReadableNiagara.md
│   ├── Asset.meta.json
│   └── Asset_NiagaraCurves.svg
├── Materials/
└── Blueprints/
```

## 兼容性

- Unreal Engine 5.7
- Win64 Editor
- Schema 2 单资产元数据保持兼容，只对 `niagaraCurves` 增加字段并将重复曲线合并
- 普通单资产/批量导出行为保持不变

## 建议验证

1. 在内容浏览器选中一个 Niagara System。
2. 右键选择 ReadAllandExplains > 生成 AI Context Pack（含项目依赖）。
3. 检查 Context Pack 是否包含根资产、项目自定义 Niagara 脚本、材质和其他受支持依赖。
4. 打开根 Niagara 资产的 `_NiagaraCurves.svg`，核对 `ScaleSpriteSize` 等曲线形状。
5. 检查 `.meta.json` 中相同 `fingerprint` 是否只保留一条，并通过 `usedBy` 列出多个来源。
