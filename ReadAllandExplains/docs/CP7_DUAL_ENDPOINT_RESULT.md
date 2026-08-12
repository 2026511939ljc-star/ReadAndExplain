# CP7 双端确定性验证 — 结果

## 结论

两个独立部署、独立进程、独立工作目录，对同一份不可变 Pack 的 **63 个叶字段全部逐字相同**，零分歧。

```text
Pack        ContextPack_20260811_210312_55EAC3E3
指纹        sha1:10533df69cb81b6ea0da67dad92ac0c40ee4c88b
日期        2026-08-12
结果        PASS - 63 leaf values identical across both endpoints
```

## 两个端点

```text
A  CodeBuddy 工作区部署
   C:\Users\albertinsli\CodeBuddy\FluidFlux_Fork\work\
   ReadAllandExplains_4.8.0_mvp\ReadAllandExplains\Integrations\MCP\

B  UE 项目安装部署
   D:\UE5\Trans\plugins\ReadAllandExplains\Integrations\MCP\
```

两端脚本 SHA256 相同（`B47F0182270A7960…`），由不同磁盘、不同目录、不同工作路径分别启动。

## 逐题结果

```text
Q1  Pack 身份       state=complete，指纹一致
Q2  蓝图规模        7 图 / 256 节点 / 780 引脚 / 329 连线
Q3  Niagara 规模    20 图 / 767 节点 / 2951 引脚 / 1087 连线
Q4  HLSL 定位       listed / 1 候选 / GUID 54CF7B53… / /graphs/16/nodes/20
Q5  章节索引        7 章节，technical=176732 字符
```

两端 `graph_index_source` 在 Q2、Q3、Q4 全部为 `native_metadata_graph_index`，`GRAPH_INDEX_DERIVED` 未出现。

## 比对器可信度

一个永远报 PASS 的比对器没有价值，因此做了阴性对照：篡改 B 端两个字段后重跑，比对器精确命中两处并返回 FAIL。

```text
DIFF q3_niagara.total_nodes: A=767 B=768
DIFF q4_custom_hlsl.graph_index_source: A='native_…' B='derived_…'
RESULT: FAIL
```

证据文件 [dual_endpoint_a.json](evidence/dual_endpoint_a.json) 与 [dual_endpoint_b.json](evidence/dual_endpoint_b.json) 字节数不同（2614 / 2549），差异仅来自记录各自脚本路径的 `script` 字段，与被比对的 `q` 数据无关。

## 这轮验证了什么

同一份代码在两个独立部署下，对同一份 Pack 的读取是可复现的。这排除了三类风险：部署漂移、进程状态残留、配置相关行为差异。

## 这轮没有验证什么

**这不是两套独立实现的交叉印证。** 两端运行同一份源码，哈希已确认相同。真正的交叉验证需要第二套独立实现，本项目没有。

**一致不等于正确。** 若导出阶段漏采了某节点，两端会一致地漏掉同一个。本轮证明读取确定性，不证明导出完整性。

**不能证明运行时行为。** 静态 Pack 无法说明 GPU 逐帧结果、动态参数最终值或 PIE 时序。

对外表述时应写为"双部署读取确定性已验证"，不应写成"双端交叉验证通过"。

## 本轮的真实收获

最有价值的不是那 63 个对上的字段，而是首次执行 Q4 时暴露的矛盾：`get_asset_outline` 报原生索引，`locate_graph_target` 报派生索引，同一份 Pack 上两个工具自相矛盾。

根因是 `locate_graph_target` 硬编码了 `derived_metadata_graphs`，从未做过实际判断。契约 54 项、Golden 22 项、parity 1,619 资产全绿都未发现它，因为没有任何一条测试断言过该字段。

已修复并补充 3 条防回归测试锁定该契约。这印证了一条既有教训：**测试未覆盖的契约变更是静默回归的常见来源。**

## 复现方式

```powershell
python Tests\Tools\collect_endpoint_answers.py --script <A端脚本> --root <导出根> --label A --out a.json
python Tests\Tools\collect_endpoint_answers.py --script <B端脚本> --root <导出根> --label B --out b.json
python Tests\Tools\compare_endpoint_answers.py --a a.json --b b.json
```
