# ReadAllandExplains 4.3.0-preview.1

本版本继续完善 Niagara Schema 2 IR，重点补齐 Renderer、源曲线 Key 与项目自定义模块内部图，让 AI 从“看见模块结构”进一步升级到“解释特效如何呈现”。

## 主要更新

1. 新增 `niagaraRenderers`，导出 Renderer 类型、启用状态、Source Mode、材质、属性绑定及可编辑配置。
2. 新增 `niagaraCurves`，保留 Float、Vector、Color 曲线各通道的原始 Key、插值、切线、权重与前后外推模式。
3. 函数调用节点新增 `referencePath`、`calleeGraphId`、`selectedVersion` 与 `enabled` 字段。
4. 递归展开 `/Game/` 下项目自定义 Niagara 模块，使用图路径去重、循环检测与最大深度 4 控制体量。
5. Markdown 新增 Renderer 与曲线概览，完整曲线原始数据写入 `.meta.json`。
6. 保留原有 `graphs` 结构和 `_ReadableNiagara.md` 详细文本导出，兼容 4.2 消费方式。

## 实测结果

使用 Trans 中真实 Niagara System `FX_Syst_Waterfall_FXTex` 验证：

- 4 张图，88 个节点，98 条连线。
- 1 个 Sprite Renderer，读取 27 个绑定与 36 个可编辑属性。
- 21 条曲线记录（含不同脚本上下文副本），能够还原 `ScaleSpriteSize` 等曲线的关键帧与切线。
- 成功递归展开 `SampleFXTex` 与 `KillFloor` 两个项目自定义模块。
- UE 5.7 / Win64 完整 C++ 编译与 DLL 链接通过。

## 兼容性

- 引擎：Unreal Engine 5.7
- 平台：Win64 Editor
- 新命令：`ReadAllandExplains.ExportAssets`
- 旧命令：`GetTheMeaning.ExportAssets` 继续兼容
- 旧 Niagara Markdown 与 Schema 2 `graphs` 保持兼容

## 当前边界

- 同一源曲线可能因 GPU、Spawn、Update 与源图实例产生重复记录，后续将增加曲线指纹去重。
- Markdown 当前展示曲线概览，完整 Key 数据请读取 `.meta.json`。
- 仅递归项目自定义模块，不展开全部引擎内置模块。
- 完整参数级 DAG、Live Sync 与反向导入仍在后续计划中。
