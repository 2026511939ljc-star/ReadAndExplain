# ReadAllandExplains 4.2.0-preview.1

本版本以“不破坏当前可用 Niagara 导出”为首要原则，将 Niagara 图数据增量接入现有 Schema 2 统一 IR。

## 主要更新

1. 新增 Niagara System、Emitter、Script 源图的统一 IR 采集。
2. 导出 Graph、Node、Pin、Link、节点位置、默认值、函数名与脚本引用。
3. Parameter Map 连线标记为 `parameter-map`，普通数据连线标记为 `data`。
4. 按源图路径去重，避免 System、Emitter 与 Script 共享图被重复写入。
5. 原 `_ReadableNiagara.md` 文本导出器零改动，现有使用入口与详细内容保持兼容。

## 累积优化

1. 插件定位从资产文字翻译器升级为 UE 资产 AI 上下文编译器。
2. 使用 Compact Markdown + JSON IR 双产物，显著降低文本体量和 Token 噪声。
3. Blueprint 与 Material 已使用 Schema 2 节点、Pin、连线统一结构；本版扩展到 Niagara。
4. 材质函数短名与 Named Reroute 穿透，改善伪 HLSL 与真实依赖追踪。
5. 自动生成 Feature Tags、美术速读、参数线索、直接依赖图与 AI 提示词。
6. 支持 Unreal Engine 5.7，并保留无界面批量导出命令。

## 兼容性

- 引擎：Unreal Engine 5.7
- 平台：Win64 Editor
- 旧命令：`GetTheMeaning.ExportAssets` 继续兼容
- 新命令：`ReadAllandExplains.ExportAssets`
- 旧 Niagara Markdown：保持不变

## 验证

- UnrealHeaderTool：通过
- C++ 编译：通过
- DLL 链接：通过
- 当前 Trans 部署目录未在编辑器运行期间覆盖

## 后续计划

- Niagara 参数级 DAG
- 递归依赖导出
- Live Sync
- 反向导入与正式发布流程