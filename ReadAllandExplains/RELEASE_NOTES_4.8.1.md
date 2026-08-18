# ReadAllandExplains 4.8.1

ReadAllandExplains 4.8.1 是 4.8.0 的确定性修复版本。它修复 Texture 可读文档分辨率和 Blueprint Pin 身份在重复导出、跨 UE 进程时可能漂移的问题，不增加新的资产类型或 MCP 接口。

## 运行环境

```text
Unreal Engine   5.7
操作系统        Windows 64 位
插件模块类型    Editor
Python          3.9+（仅 CodeBuddy / MCP 需要）
```

发布包包含 UE 5.7 / Win64 编辑器二进制。其他 UE 5.x 版本需要针对对应引擎重新编译，并重新验证导出结果。

## 修复内容

### Texture Resolution 使用导入源尺寸

Texture 可读文档不再使用异步平台数据编译期间可能返回占位尺寸的 `UTexture2D::GetSizeX()` / `GetSizeY()`，改为读取 `GetImportedSize()`。同一纹理不会再因加载时机不同而在 `32 x 32` 与真实 `16 x 16` 之间漂移。

### Blueprint Pin 使用稳定身份

Blueprint 导出不再直接把可能随图重建变化的运行态 `UEdGraphPin::PinId` 当作稳定身份：

- 有效 `PersistentGuid` 优先作为 Pin 身份。
- 无持久 GUID 时使用 `NodeId:pin:PinIndex` 结构身份。
- Link 端点与 Pin 使用同一规则，保持引用闭包。
- Niagara 保留原有 Pin ID 路径，本次不扩大到已验证稳定的 Niagara 行为。

## 验证结果

- MCP 契约测试：58 项通过，1 项预期跳过。
- Golden 回归：22/22 通过。
- UE 5.7 CompileHost 与 UE 5.8 BuildPlugin 均编译通过。
- 两个独立 UE 5.7 进程各连续导出两次同条件 Waterfall Pack，共 4 份。
- 四次均为 `complete`、依赖深度 2、资产 39/39、失败 0、清单完整性错误 0。
- 同进程和跨进程规范化 Golden 均为 82/82。
- 跨进程除 `context-pack.json` 的 Pack 身份字段外，其余 81 个内容文件字节一致。
- 两张问题纹理四次均稳定为 `16 x 16`，文件 SHA1 一致。
- Blueprint 节点内重复 Pin ID 为 0，所有 Link 端点均可解析。
- 修复前 Pack 对新基线的阴性对照正确失败，证明检测门禁能够识别原缺陷。

## 升级与兼容性

从 4.8.0 升级时，请关闭 Unreal Editor 和 CodeBuddy，备份并完整替换 `Plugins/ReadAllandExplains`，不要把新文件覆盖进旧目录后保留陈旧文件。

本版会改变部分 Blueprint `pin_id` 的表示。旧 Pack 仍可读取，但不要在 4.8.0 与 4.8.1 Pack 之间复用 Pin ID 缓存；建议为关键资产重新生成 Context Pack。节点、参数与连线语义经归一化审计保持一致。

## 明确边界

- 本次 82/82 结论绑定所测 Waterfall 39 资产样本，不能外推为所有 UE 资产在所有引擎版本上绝对确定。
- 静态 Context Pack 不能证明 GPU 逐帧结果、PIE 时序或运行时参数最终值。
- UE 5.8 已通过编译，但本版正式二进制与真实导出验证仍以 UE 5.7 / Win64 为准。
- UE 5.8 的 `GetObjectsWithOuter` 弃用警告仍是独立技术债，须在 UE 5.9 前处理。

---

## English

ReadAllandExplains 4.8.1 is a determinism patch for 4.8.0. It fixes Texture readable-resolution drift and Blueprint Pin identity drift across repeated exports and fresh UE processes. It adds no new asset type or MCP interface.

### Fixes

- Texture readable documents now use `GetImportedSize()` instead of platform-data dimensions that may temporarily report a placeholder while asynchronous compilation is in progress.
- Blueprint Pins prefer UE's persistent `PersistentGuid`; Pins without one use deterministic `NodeId:pin:PinIndex` identities.
- Graph links use the same identity rule as their Pin endpoints.
- Niagara keeps its existing Pin ID path to avoid widening this behavioral change.

### Verification

The MCP contract suite and 22 Golden tests pass. Two independent UE 5.7 processes produced four complete depth-2 Waterfall Packs at 39/39 assets. All normalized comparisons passed at 82/82; outside the manifest's per-Pack identity fields, all 81 content files were byte-identical across processes. The old implementation correctly fails the new baseline as a negative control.

### Upgrade note

Blueprint `pin_id` representation changes for affected Pins. Existing Packs remain readable, but do not reuse Pin ID caches across 4.8.0 and 4.8.1 Packs. Regenerate important Context Packs after upgrading.

The published binary targets Unreal Engine 5.7 on Windows 64-bit. UE 5.8 compilation passes, but UE 5.7 remains the validated runtime-export target for this release.
