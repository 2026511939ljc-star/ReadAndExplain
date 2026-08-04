# ReadAllandExplains 4.6.0-preview.2

本版本把重心收敛到“稳定静态快照、渐进式理解、缺什么再定向补什么”，优先改善 CodeBuddy 与美术用户的实际分析流程，不扩张为实时 UE 控制平台。

## 第一批更新

1. Skill 收到大型问题时先拆为最多 3 个小任务，默认优先材质与 Niagara，蓝图只追踪视觉参数写入和触发入口。
2. 默认下钻预算限制为最多 5 个资产、直接依赖深度 1，超过预算先汇总缺口，不递归抓取整个项目。
3. `get_asset_detail` 新增 `coverage` section，在不增加 MCP 工具数量的前提下返回直接依赖的包内覆盖、缺失 `/Game/` 依赖、外部依赖和定向补拍建议。
4. Skill 将缺口分为“阻塞结论”“提高置信度”“运行时验证”，先完成不受影响的分析，再一次性生成补充计划。
5. 补充计划明确资产或字段、用途、预期答案、优先级、资产数、深度、只读范围和 Pack 指纹；用户许可前不得触发后台快照。
6. 补拍完成后要求验证新 Pack 为 `complete` 并重新检查 coverage，然后自动从中断的小任务续答。
7. 文本读取改为严格 UTF-8/UTF-8 BOM；非法编码返回稳定错误 `TEXT_ENCODING_INVALID`，不再用替换字符掩盖乱码。
8. 新增中文往返、非法编码、依赖 coverage 和 Skill 工作流契约测试。

## 当前边界

- `coverage` 只检查当前资产的直接依赖是否已存在于所选 Context Pack，不自动递归读取或触发 UE 导出。
- SyncLive Lite 的实际 UE 后台定向导出通道尚未接入；当前 Skill 会先生成补充计划并请求许可，再根据可用环境执行或指导用户导出。
- Graph Query、语义 Diff、TA Audit、运行时 GPU 观测继续后置。

## 验证

- 20 项 MCP 与 Skill 契约测试通过。
- CodeBuddy 官方插件/市场清单校验通过。
- 真实 `BP_FluxAllOne` Context Pack 可生成依赖覆盖和定向补拍候选。

```powershell
python -m unittest discover -s Tests/MCP -p "test_*.py" -v
```
