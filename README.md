# 平面自动化 Skill

仓库主分支包含平面自动化的源代码、构建工程和规则。直接安装可使用 [2026-11-17 换机包](https://github.com/wanzhaokun531-star/architectural-plan-automation/releases/tag/portable-20261117)，包内有编译好的 ARX 和安装脚本。

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
msbuild 'skills/architectural-plan-automation/native/PlanNative.vcxproj' /p:Configuration=Release /p:Platform=x64 "/p:ArxSdkRoot=$ArxSdk"
msbuild '阶段1/原生接口探测/src/NativeAccessProbe.vcxproj' /p:Configuration=Release /p:Platform=x64 "/p:ArxSdkRoot=$ArxSdk"
```

主模块生成在 `skills/architectural-plan-automation/tools/PlanNative.arx`。桥接工程生成 `阶段1/原生接口探测/bin/NativeAccessProbe.arx`，换机包内桥接模块名为 `host/bridge/PlanBridge.arx`。`插件/tools/build_plan_portable.py` 从显式指定的主模块和桥接模块构建 ZIP；发布模板包前，`assets/template-runtime.json` 须通过验证绑定实际打包的主模块指纹。

## 本机安装与版本说明

已编译版本的安装和运行步骤见 [换机安装说明](skills/architectural-plan-automation/references/portable-install.md)。已发布 ZIP 的校验值见 [SHA256SUMS.txt](SHA256SUMS.txt)。

本仓库的规则随源码更新；Releases 中的 ZIP 保持原发布版本。A28 要求轴圈避让时只折一次，斜段直接接圆周，当前原生自动生成仍待适配。
