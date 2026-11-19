# 平面自动化 Skill

本次源码与 Skill 更新至 **2026-11-19**。尺寸、轴号继续以指定模板原生对象为来源；字体、墙高及最终检查采用现行统一参数，防止旧终核或原图同名样式覆盖新规则。

| 用途与样式 | 主字体＋大字体 | 纸面字高 | 宽度 |
| --- | --- | --- | --- |
| 普通文字 `FY_HZ` | `tssdeng.shx`＋`hztxt.shx` | 4.0 | 0.7 |
| 尺寸文字 `_TCH_DIM` | `simplex.shx`＋`gbcbig.shx` | 2.8 | 1.0 |
| 门窗自身号 `_TCH_WINDOW` | `simplex.shx`＋`gbcbig.shx` | **3.0** | 0.65 |

参数按实际单位和出图比例换算，并读回实际对象引用、字形、字高和宽度。每幅保留四道 `Defpoints` 实体辅助框：模型可见、ACI 8 灰色虚线、不打印。墙高在全部后处理结束后独立重开逐墙复核；字体、禁用标注、轴号、图签纸边和同版实打分别记录证据。

A28 的单次折线、斜段直接接轴圈已接入共用轴号布局。替换采用候选先验证、再退出旧对象的流程，失败保留原件并回收未采用候选。最终回滚专项已核两种退出顺序、原对象恢复、保存重开及宿主正常退出；早期失败记录仍保留，不据此声称所有异常场景均已覆盖。

本轮两个既有输入完成实际生成或模板终核、独立重开及整套实打：共 **161 面墙、6 幅图**；最终项目宿主正常退出。**97 项 Python 测试、29 项规则同步测试通过**，四种入口的本机依赖预检通过。验证模块 SHA-256 为 `EB4F605407F190516F3D0C29C770410A39293D89AFDCABD3508B5D6C975EEAFC`。范围及限制见[精简验证摘要](docs/runtime-validation-20261119.json)和[运行登记](skills/architectural-plan-automation/assets/template-runtime.json)。

**整图 `allRulesPassed` 仍为 `false`。** 已知例外包括关联引注字体、图签越纸边及内部文字重叠、窗号与标高文字相撞，以及轴号应有范围、部分实例标注和图签的覆盖未知。未解决项按 P06 随同版图纸列清单；打印成功、流程结束或预检兼容均不代表所有规则通过。本轮未独立实跑 `revise`、`audit` 两种入口；BCP 仅完成兼容编译，未安装或实跑。不据此承诺任意项目一键全部通过。

仓库包含平面自动化源代码、构建工程和规则。本次只更新源码与 Skill，**不发布新的换机 ZIP**。[2026-11-18 换机包](https://github.com/wanzhaokun531-star/architectural-plan-automation/releases/tag/portable-20261118)仍为历史包，不包含本次修复，不能用于验收本次版本。

| 目录 | 内容 |
| --- | --- |
| [skills/architectural-plan-automation](skills/architectural-plan-automation) | Skill 规则、Python 出图入口、PowerShell 安装工具、模板、CTB 和 PC3 |
| [PlanNative.cpp](skills/architectural-plan-automation/native/PlanNative.cpp) | 原生 `PLAN_DRAW` 命令及模板终核入口 |
| [插件/src](插件/src) 和 [阶段2/src](阶段2/src) | 原生平面生成、轴网、尺寸、建筑构件和验收实现 |
| [阶段1/原生接口探测](阶段1/原生接口探测) | 隔离 CAD 宿主、命令桥接器和桥接源码 |
| [docs](docs) | 平面规则主汇编与执行规则 |

## 构建原生模块

使用 Windows x64、Visual Studio 的 C++ v141 工具集、Windows SDK 10.0.19041 及 AutoCAD 2020 ObjectARX SDK。SDK 路径可在命令中指定；运行还需要已安装的 AutoCAD 2020 和兼容天正。

在 Developer PowerShell 从仓库根目录运行：

```powershell
$ArxSdk = 'C:\Autodesk\ObjectARX_for_AutoCAD_2020_Win_64_bit'
$BuildRoot = Join-Path (Get-Location) 'build/source-20261119'
msbuild 'skills/architectural-plan-automation/native/PlanNative.vcxproj' /p:Configuration=Release /p:Platform=x64 "/p:ArxSdkRoot=$ArxSdk" "/p:OutDir=$BuildRoot/native/" "/p:IntDir=$BuildRoot/obj/native/"
msbuild '阶段1/原生接口探测/src/NativeAccessProbe.vcxproj' /p:Configuration=Release /p:Platform=x64 "/p:ArxSdkRoot=$ArxSdk" "/p:OutDir=$BuildRoot/bridge/" "/p:IntDir=$BuildRoot/obj/bridge/"
```

上述命令将主模块生成到 `build/source-20261119/native/PlanNative.arx`，桥接模块生成到 `build/source-20261119/bridge/NativeAccessProbe.arx`，不会覆盖已登记的运行模块。新 SHA 的构建须取得自身的原生验证证据并更新指纹登记，不能沿用 EB4 模块的通过记录。

换机包内桥接模块名为 `host/bridge/PlanBridge.arx`。`插件/tools/build_plan_portable.py` 从显式指定的主模块和桥接模块构建 ZIP；发布模板包前，`assets/template-runtime.json` 须通过验证绑定实际打包的主模块指纹。本次更新不执行 ZIP 发布。

## 本机安装与版本说明

已编译版本的安装和运行步骤见 [换机安装说明](skills/architectural-plan-automation/references/portable-install.md)。已发布 ZIP 的校验值见 [SHA256SUMS.txt](SHA256SUMS.txt)。

源码、已编译模块与 Releases 安装包分别按指纹和实际验证范围管理。Releases 中的 ZIP 保持原发布版本；更新仓库不会自动更新安装包。
