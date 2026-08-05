# Golden Pack Regression

`golden_regression.py` 是一个仅依赖 Python 标准库的 Context Pack 回归工具。它从真实 ReadAllandExplains Context Pack 创建规范化 baseline，并比较后续导出中的新增、缺失和变化文件。baseline 只应保存经团队批准的测试资产导出，不要提交用户项目的完整私有导出。输入 Pack 必须是正式发布目录中的 `state=complete` Pack，目录名与 `packId` 一致，`failedCount=0`、`skippedCount=0`，Manifest 文件清单和大小与磁盘一致，Manifest/Index 资产映射一致，且每个 `exportFile` 唯一并真实存在。

## 快速使用

需要 Python 3.9 或更高版本。在仓库根目录运行：

```powershell
python Tests/Golden/golden_regression.py create "C:/Path/To/ContextPack_20260805_120000" "C:/Path/To/Baselines/water" --format json
python Tests/Golden/golden_regression.py compare "C:/Path/To/ContextPack_20260805_130000" "C:/Path/To/Baselines/water" --json-report "C:/Path/To/Reports/water.json" --human-report "C:/Path/To/Reports/water.txt"
```

现有 baseline 默认不会被覆盖。确认语义变化正确后，显式添加 `--force` 重新创建。为避免误删任意目录，`--force` 只允许替换由本工具创建、包含 `.rae-golden-baseline.json` 管理标记的 baseline；未受管路径会被拒绝：

```powershell
python Tests/Golden/golden_regression.py create "C:/Path/To/NewContextPack" "C:/Path/To/Baselines/water" --force
```

## 规范化规则

- 所有 JSON 使用 UTF-8、排序后的对象键、2 空格缩进和单个末尾换行重新序列化。
- `context-pack.json` 忽略运行态字段 `packId`、`createdUtc`、包级 `fingerprint`、补拍追踪 ID，以及 `files[]` 中的 `size` 和 `fingerprint`；文件路径、根资产、资产清单、依赖深度、导出统计和其他语义字段仍参与比较。
- 明确属于集合的数组会按其规范 JSON 值稳定排序，包括资产、文件、依赖、反向引用、标签、参数、图、节点、连接、Pin、Renderer、曲线、Binding 和 `usedBy` 等。
- 有顺序语义的数组不会排序。特别是曲线 `keys` 保留原始顺序，因此 Key 顺序变化会触发回归。
- 非 JSON 文件按 UTF-8 文本处理：移除 BOM，统一为 LF，移除每行末尾空格或制表符，并规范为至多一个文件末尾换行。
- 符号链接和非 UTF-8 文本会作为操作错误拒绝，避免悄悄读取包外文件或产生替换字符。

baseline 保留与真实 Pack 相同的相对目录结构，内容已经规范化。比较报告只输出文件路径和内容 SHA-256，不把完整资产内容复制到报告中。

## 报告与退出码

`--format human` 是默认标准输出，`--format json` 输出机器可读 JSON。`--json-report` 和 `--human-report` 可同时落盘两种报告。

- `0`：baseline 创建成功，或 actual 与 baseline 完全匹配。
- `1`：工具成功运行，但检测到新增、缺失、变化文件或 case 必需项缺失。
- `2`：参数、配置、路径、编码、JSON 或文件系统操作错误。

## 真实 Cases

复制 `cases.example.json` 到不含私有数据的本地配置文件，填写真实 Context Pack、baseline、必须出现的 `/Game/` 资产路径和内容 marker：

```powershell
Copy-Item Tests/Golden/cases.example.json Tests/Golden/cases.local.json
python Tests/Golden/golden_regression.py cases Tests/Golden/cases.local.json --format json
```

首次建立或明确更新所有 case baseline 时，使用显式开关：

```powershell
python Tests/Golden/golden_regression.py cases Tests/Golden/cases.local.json --update-baselines
python Tests/Golden/golden_regression.py cases Tests/Golden/cases.local.json --update-baselines --force
```

配置中的相对路径以 cases JSON 所在目录为基准。`requiredAssetPaths` 会在规范化 JSON 字符串值中做精确匹配；`requiredMarkers` 需要指定 Pack 内相对文件和必须包含的文本；`forbiddenMarkers` 使用相同格式声明不得出现的文本，例如禁止 Custom HLSL 回退为 `UnknownExpr`。示例仅含占位符，不含任何用户资产内容。

## 真实 UE 自动导出

复制 `cases.ue.example.json` 为已被 Git 忽略的本地配置，填写当前项目中的 `/Game/...Asset.Asset`：

```powershell
Copy-Item Tests/Golden/cases.ue.example.json Tests/Golden/cases.trans.local.json
```

关闭 UE 编辑器后，首次建立本地基线：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunGoldenPackRegression.ps1 -Project "D:/UE5/Trans/Trans.uproject" -Cases "Tests/Golden/cases.trans.local.json" -UpdateBaseline
```

后续回归或双运行确定性检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunGoldenPackRegression.ps1 -Project "D:/UE5/Trans/Trans.uproject" -Cases "Tests/Golden/cases.trans.local.json"
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunGoldenPackRegression.ps1 -Project "D:/UE5/Trans/Trans.uproject" -Cases "Tests/Golden/cases.trans.local.json" -Repeat 2
```

Runner 会为每个 case 单独启动 `UnrealEditor-Cmd`、定位新生成的正式完整 Pack、执行正负向 Marker 与 baseline 对比，并把报告写到本地 `.reports/`。case 名仅允许字母、数字、点、下划线和连字符；baseline 必须是 cases 配置目录下 `.baselines/` 的相对子路径。为了避免把第二次输出误批准为标准，`-UpdateBaseline` 只能与 `-Repeat 1` 一起使用。

## 运行追溯

PowerShell Runner 会在每份临时 runtime config 的顶层写入 `provenance`，Python 工具验证其为标准 JSON 对象后，将其原样透传到对应的 JSON 和人类可读 cases 报告。Python 不执行 Git、引擎或文件哈希探测；直接运行 `golden_regression.py cases` 时，原有不含 `provenance` 的 cases 配置仍完全兼容，报告也不会凭空增加该字段。

```json
{
  "schemaVersion": 1,
  "provenance": {
    "collectedUtc": "2026-08-05T12:34:56.0000000Z",
    "project": { "path": "D:\\UE5\\Trans\\Trans.uproject" },
    "engine": {
      "commandPath": "C:\\Program Files\\Epic Games\\UE_5.7\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe",
      "version": "5.7.0.0"
    },
    "git": {
      "repositoryPath": "C:\\src\\ReadAllandExplains",
      "head": "0123456789abcdef0123456789abcdef01234567",
      "dirty": true
    },
    "pluginDescriptor": {
      "path": "D:\\UE5\\Trans\\Plugins\\ReadAllandExplains\\ReadAllandExplains.uplugin",
      "sha256": "...",
      "source": "loadedModule"
    },
    "pluginModule": {
      "path": "D:\\UE5\\Trans\\Plugins\\ReadAllandExplains\\Binaries\\Win64\\UnrealEditor-ReadAllandExplains.dll",
      "sha256": "..."
    }
  },
  "cases": []
}
```

`pluginModule` 只在 Runner 从本次 `UnrealEditor-Cmd` 进程中观察到实际加载的 `UnrealEditor-ReadAllandExplains.dll` 且文件仍存在时写入，因此其中路径和 SHA-256 代表项目实际加载的二进制，而不是按目录猜测的候选文件。观察到 DLL 时，`pluginDescriptor` 也优先取该 DLL 所属插件目录中的 `.uplugin`，并标记 `source=loadedModule`；否则记录当前源码仓库描述文件并标记 `source=repository`。Git `dirty` 基于包含未跟踪文件的 `git status --porcelain`，用于说明报告是否严格对应记录的 HEAD。

手写 `provenance` 时必须是 JSON 对象；数组、字符串以及 `NaN`/`Infinity` 等非标准 JSON 数值会被拒绝。该对象只用于运行证据，不参与 Pack 规范化或 baseline 内容比较。

## 运行测试

```powershell
python -m unittest discover -s Tests/Golden -p "test_*.py" -v
```
