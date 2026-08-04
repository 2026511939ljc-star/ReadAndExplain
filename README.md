# ReadAllandExplains 开发仓库

本仓库是 ReadAllandExplains 的唯一开发源码仓库。当前开发线为 `iteration/vnext`，插件版本为 `4.6.0-preview.3`。外层目录名 `ReadAllandExplains_4.1.0_dev` 是历史工作目录名，不代表当前产品版本；版本只以插件清单为准。

## 权威来源

- 插件源码根：[ReadAllandExplains](ReadAllandExplains/)
- UE 插件版本：[ReadAllandExplains.uplugin](ReadAllandExplains/ReadAllandExplains.uplugin)
- CodeBuddy Skill 源：[SKILL.md](ReadAllandExplains/skills/readallandexplains/SKILL.md)
- MCP 源：[readallandexplains_mcp.py](ReadAllandExplains/Integrations/MCP/readallandexplains_mcp.py)
- 当前能力说明：[README.md](ReadAllandExplains/README.md)
- 当前版本说明：[RELEASE_NOTES_4.6.0-preview.3.md](ReadAllandExplains/RELEASE_NOTES_4.6.0-preview.3.md)
- 资产与发布管理规则：[REPOSITORY_MANAGEMENT.md](ReadAllandExplains/docs/REPOSITORY_MANAGEMENT.md)

仓库外的 `.agent/skills/readallandexplains`、UE 项目 `Plugins/ReadAllandExplains`、备份目录和导出的 Context Pack 都是安装、部署、备份或数据副本，不是源码来源，不应反向覆盖仓库。

## 日常流程

```powershell
python -m unittest discover -s ReadAllandExplains/Tests/MCP -p "test_*.py" -v
powershell -ExecutionPolicy Bypass -File ReadAllandExplains/Scripts/SyncWorkspaceSkill.ps1 -WorkspaceRoot <workspace-root>
codebuddy plugin validate ReadAllandExplains
```

提交前确认 `git status --short` 只包含预期源码、测试或文档。`Binaries`、`Intermediate`、`Saved`、缓存、压缩包和本地安装副本均不进入 Git。
