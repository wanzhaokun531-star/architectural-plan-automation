# 平面自动化仓库维护

## 唯一发布仓库

自 2026-11-20 起，平面代码、通用规则和后续发行统一维护于：
https://github.com/wanzhaokun531-star/architectural-plan-automation

`building-construction-drawing-automation` 为已迁出的历史来源，不再更新。发布前核对目标仓库与远程分支；从本仓库的干净检出构建提交，不把旧混合工作区的历史或未审查文件整体推入。旧工作区与本仓库历史不同，不以 reset、强制推送或覆盖工作区来“同步”。

## 现行规则与验证

先读取 `skills/architectural-plan-automation/SKILL.md` 和本次工作相关的 references。现行三套文字参数取 `assets/annotation-template/standard.json`，生成与终核共用规则指纹；`references/runtime-consistency.md`、`references/template-standard.md`、`references/acceptance.md` 说明统一入口与验收边界。历史资料不是当前规则来源，不能让旧硬编码或项目同名样式覆盖现行参数。

保持读取源图、逐层生成、独立重开补漏、最终实打、DWG 与同版清单的既有流程。规则已写入、程序已执行、项目已验证分别记录；未解决项如实列出，不把打印成功或任务完成写成全部规则通过。源码构建出的新模块不得沿用旧模块的验证指纹。

本仓库仅维护平面；立面、剖面及项目原图不在此次合并范围。`docs/history/` 仅供追溯，禁止从中恢复旧室内尺寸、房间面积、文字参数或排布策略。

## 后台支持测试

`tools/Test-PlanSupport.ps1` 编译并运行纯 C++ 支持测试和宿主退出策略单元测试，不启动 CAD、不控制桌面、不覆盖已登记的 ARX。运行环境需要 MSBuild、v141 C++ 工具集和 Windows SDK 10.0.19041.0；输出到每轮独立目录，结果见其中 `result.json`。

该测试不替代实际 DWG 生成、独立重开或实打验收。ApronGeometry 是已有纯几何函数的回归测试，不启用暂停的散水出图功能。现行 Python 测试与规则同步测试仍按各自入口执行。
