# 平面自动化 Skill 换机安装

使用 Windows x64、已激活的 AutoCAD 2020（R23.1）、兼容的天正及 Python 3.11 或以上 x64。先正常启动一次 AutoCAD 和天正，完成激活及个人配置初始化。

1. 解压到本用户可写的目录。用 Codex 聊天时，可以打开解压的 `architectural-plan-automation` 目录作为工作区，使用其中的 `SKILL.md`。
2. 在该目录运行安装脚本。脚本检查文件指纹、CAD、天正、字体和 Python 打印依赖，并建立专用 CAD 配置。若提示插件目录权限不足，用同一个 Windows 账户以管理员身份安装一次。Python 已有依赖时可以省略 `-InstallPythonDependencies`。安装资料有多个版本或位于自定义目录时，按提示提供 `-AcadPath`、`-TianzhengKernel`、`-Template`、`-BaseProfile` 或 `-PythonPath`。

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Install-PlanDraw.ps1 -InstallPythonDependencies
   ```

3. 用已保存的定稿方案运行出图。成图使用 1:100、毫米制，最终复验按随包模板选择 PC3 和 CTB。输出路径选新文件，用同样的参数重试可接续该作业。可定位的未解决事项随同版遗留清单交付。对已有布局的平面执行模板修订时，可按 `references/native-tools.md` 的“模板原生终核”一节传入 `-TemplateSheets`。

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Invoke-PlanDraw.ps1 -Source 'D:\项目\方案.dwg' -Output 'D:\项目\施工图平面.dwg'
   ```

作业资料、独立重开记录及实打 PDF 保存在内部作业目录。已配置另一台电脑时，在该电脑重新运行安装脚本，生成它自己的 CAD 路径和专用配置。

轴号规则 A28 要求避让时只在圈外折一次，斜段直接接圆周。当前安装包已更新规则和验收说明，自动生成这类引线的原生实现尚待适配。验收时按 A28 量取重开后的引线几何。
