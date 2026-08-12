# CP7 双端确定性验证 — 操作说明

## 这次要证明什么

两个互不相通的客户端，面对同一份不可变 Pack、同一套问题，返回的**事实字段逐字相同**。

之前那轮双端一致跑的是 4.7 Pack，图索引由 MCP 在内存里派生。CP7 把索引搬到了导出端，所以旧结论对原生 Pack **不自动成立**，必须重跑一轮。

关键点：如果返回的是大模型即兴生成的解释，两个客户端不可能给出完全相同的字符数和 JSON 指针。**确定性能被第三方复核，解释不能。**

## 基准信息

```text
Pack        ContextPack_20260811_210312_55EAC3E3
指纹        sha1:10533df69cb81b6ea0da67dad92ac0c40ee4c88b
状态        complete
插件版本    4.8.0-preview.3
MCP 版本    0.7.0
```

## 操作要求

- 两个客户端**各自新建会话**，不要复用已有上下文。
- **不要**把一边的答案贴给另一边看。
- 每题都必须显式带上 `pack_path`，否则服务可能自选 Pack，结果就失去比对意义。
- 五道题按顺序问完，把两边的完整回复各自保存成文本文件。

## 问题集（两个客户端各跑一遍）

### Q1 — Pack 身份

> 请用 `list_context_packs` 确认 `ContextPack_20260811_210312_55EAC3E3` 的状态和指纹，原样给出这两个值。

### Q2 — 蓝图规模与索引来源

> 请对 `BP_FluxAllOne` 调用 `get_asset_outline`，`pack_path` 用 `ContextPack_20260811_210312_55EAC3E3`。告诉我：图的数量、所有图的节点总数/引脚总数/连线总数、`graph_index_source` 的值、出现的 warning code、以及第一个图的 `graph_id`。

### Q3 — Niagara 规模与索引来源

> 同样对 `NS_InfiniteSurfaceMesh` 调用 `get_asset_outline`（同一个 `pack_path`）。告诉我：图的数量、节点/引脚/连线总数、`graph_index_source`、warning code、以及全部 `json_pointer` 列表。

### Q4 — Custom HLSL 节点定位

> 请用 `locate_graph_target` 在 `NS_InfiniteSurfaceMesh` 里筛选 `kind_filter` 为 `NiagaraNodeCustomHlsl` 的节点（同一个 `pack_path`）。告诉我：`resolution`、候选数量、每个候选的 `node_id` 和 `json_pointer`。

### Q5 — 章节索引

> 请用 `get_readable_sections` 列出 `NS_InfiniteSurfaceMesh` 的章节（同一个 `pack_path`）。按顺序给出全部 `section_id` 和各自的 `character_count`。

## 跑完之后

把两边输出保存为两个文件，然后执行：

```powershell
python Tests\Tools\compare_dual_client.py --a <客户端A输出.txt> --b <客户端B输出.txt>
```

脚本会从两份文本里抽取事实字段，与基准逐项比对，输出一致/不一致清单。

## 预期结果

两边应在以下字段完全一致（这些是基准值，比对脚本会自动核对）：

```text
指纹              sha1:10533df69cb81b6ea0da67dad92ac0c40ee4c88b
BP 图数           7
BP 节点/引脚/连线  256 / 780 / 329
NS 图数           20
NS 节点/引脚/连线  767 / 2951 / 1087
索引来源          native_metadata_graph_index（两个资产都是）
warning           仅 READABLE_INDEX_DERIVED，不应出现 GRAPH_INDEX_DERIVED
HLSL 节点 id      54CF7B53-4C9C-1BB0-BB8F-F592DC86CC57
HLSL 指针         /graphs/16/nodes/20
章节数            7
technical 字符数   176732
```

## 需要注意的边界

一致**不等于**正确。这轮证明的是同一份 Pack 上的读取确定性，不证明 UE 导出本身没有遗漏。如果导出时漏了一个节点，两个客户端会一致地漏掉同一个。

答辩或对外表述时这一点要主动说清楚，比等人问更有分量。
