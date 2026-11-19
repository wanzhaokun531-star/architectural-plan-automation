# 平面自动化 Skill 换机安装

仓库源码与 Skill 已更新至 **2026-11-19**：普通文字 FY_HZ 为 4.0／宽 0.7，尺寸 _TCH_DIM 为 2.8／宽 1，门窗自身号 _TCH_WINDOW 为 **3.0／宽 0.65**，按实际对象恢复并独立读回复核。每幅保留四道模型可见、ACI 8 灰色虚线、不打印的 Defpoints 实体辅助框；尺寸与轴号使用模板原生对象。

Releases 中 `portable-20261118` 仍为历史换机包，未包含本次修复。本次只更新源码与 Skill，未发布新的 ZIP。安装时按所用运行目录或安装包自身的文件指纹检查，不能把最新源码的验证结论套用到旧包。源码构建应使用仓库 README 中的独立输出目录，避免覆盖已登记的运行模块。

使用 Windows x64、已激活的 AutoCAD 2020（R23.1）、兼容的天正及 Python 3.11 或以上 x64。先正常启动一次 AutoCAD 和天正，完成激活及个人配置初始化。

1. 解压到本用户可写的目录。用 Codex 聊天时，可以打开解压的 `architectural-plan-automation` 目录作为工作区，使用其中的 `SKILL.md`。
2. 在该目录运行安装脚本。脚本检查文件指纹、CAD、天正、字体和 Python 打印依赖，并建立专用 CAD 配置。若提示插件目录权限不足，用同一个 Windows 账户以管理员身份安装一次。Python 已有依赖时可以省略 `-InstallPythonDependencies`。安装资料有多个版本或位于自定义目录时，按提示提供 `-AcadPath`、`-TianzhengKernel`、`-Template`、`-BaseProfile` 、`-PythonPath` 或 `-FontDirectory`。

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Install-PlanDraw.ps1 -InstallPythonDependencies
   ```

3. 用已保存的定稿方案运行出图。成图使用 1:100、毫米制，最终复验按随包模板选择 PC3 和 CTB。输出路径选新文件，用同样的参数重试可接续该作业。可定位的未解决事项随同版遗留清单交付。对已有布局的平面执行模板修订时，可按 `references/native-tools.md` 的“模板原生终核”一节传入 `-TemplateSheets`。

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Invoke-PlanDraw.ps1 -Source 'D:\项目\方案.dwg' -Output 'D:\项目\施工图平面.dwg'
   ```

作业资料、独立重开记录及实打 PDF 保存在内部作业目录。已配置另一台电脑时，在该电脑重新运行安装脚本，生成它自己的 CAD 路径和专用配置。

轴号规则 A28 要求避让时只在圈外折一次，斜段直接接圆周。2026-11-19 源码已接入共用布局，并有模板轴号、分数轴及旧表达替换的原生验证。候选验证成功后才退出旧对象，失败保留原件并回收未采用候选；最终专项已验证两种退出顺序、原对象恢复、保存重开及正常退出，早期失败记录保留，不能宣称所有失败场景已覆盖。每个项目仍须核重开后的实际引线与源应有轴号覆盖，旧 ZIP 不据此升级为已适配。

本轮验证范围为两个既有输入、161 面墙及 6 幅同版实打；整图 `allRulesPassed` 仍为 `false`，未解决项随同版清单交付。`revise` 和 `audit` 入口只完成本轮依赖预检，未分别实跑；BCP 仅兼容编译，未安装。详见仓库的 `docs/runtime-validation-20261119.json`。

安装会核齐simplex.shx、gbcbig.shx、tssdeng.shx、hztxt.shx。字体须来自本机已安装的CAD、天正或用户字体目录。自定义位置可传入 `-FontDirectory 'D:\CAD字体'`，安装器会将已找到的字体复制到本包的专用本机支持目录。
