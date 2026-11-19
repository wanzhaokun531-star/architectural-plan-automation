# O09 / D31 的实际来源采用入口

此入口把已核定的资料对应落实为原生洞口宽高、定位和自身号。它不把任意 SU 外包框当洞口，也不把任意 PDF/截图中的数字直接写入平面。助手先读取资料及实际对象，形成源绑定采用表；尚未建立对应的资料、冲突、不可读或未支持对象逐件进入 P06，不冒称已经采用。

## 已支持资料

- CSV：UTF-8，列 `id,width,height,unit,dimensionKind`；`unit` 为 `mm/cm/m`，`dimensionKind` 必须是 `rough-opening`。同 ID 多行不自动选择。
- JSON：`schema=plan.opening-schedule.v1`，`openings` 内使用上述字段。
- SKP：使用随技能的 `native/skp_readback.py` 和该电脑实际安装且兼容源版本的 `SketchUpAPI.dll`。读取真实实例路径、变换和可见边，按明确的局部宽/高轴测量；支持旋转、缩放、镜像，拒绝把剪切或边框外包尺寸默认为洞口尺寸。源文件保持不变。DLL 不随本技能复制。
- 非结构化资料先转成有原文件页码/对象定位及依据的采用资料；不能读取或无法确定尺寸用途时明确列清单，不伪造数值。

## 源绑定采用 JSON

`schema=plan.design-adoptions.v1`，`sourceSHA256` 必须是当前保存的源 DWG。下面的尺寸是格式示例，不是项目默认值。

```json
{
  "schema": "plan.design-adoptions.v1",
  "sourceSHA256": "实际源DWG的SHA256",
  "sources": {
    "schedule": {"path": "门窗表.csv", "sha256": "实际资料SHA256"}
  },
  "openings": [{
    "handle": "实际洞口句柄",
    "decision": "adopt",
    "basis": "已核门窗表第W1行，按洞口尺寸采用",
    "dimensionKind": "rough-opening",
    "source": "schedule",
    "record": "W1",
    "anchor": "CENTER",
    "prefix": "LC"
  }],
  "walls": [{"handle": "实际围墙句柄", "role": "BOUNDARY", "basis": "本项目源图围墙用途及几何核定"}],
  "pending": []
}
```

`anchor` 是已采用的 `CENTER/START/END`，不会被后续默认门垛平移覆盖。`prefix` 是核定后的 `M/LM/LC/FMD/DK`，必须符合真实原生类别。普通原生单洞口已接公开特性改宽高及自身号；组合洞口子对象尚未有经验证适配时保留并列清单。墙的 `MAIN/BOUNDARY` 是逐构件用途，不把整连通组件都排除。

SKP 的 `openings` 用 `instancePath`、`widthAxis`、`heightAxis`、`geometryRole="opening-envelope"` 替代 `record`。`sources` 可提供已核定的完整 `openingInventory` 实例路径数组和 `inventoryBasis`；没有完整清单时，即使部分实例采用成功，仍保留全模型语义覆盖未证实事项。未采用的表格行和 SU 清单实例均记录，不因其他对象成功而消失。

## 编译采用表并进入无人值守入口

```powershell
python native/design_sources.py source.dwg design-adoptions.json design-sources.tsv --skp-api "实际安装目录/SketchUpAPI.dll" --skp-reader native/skp_readback.py
```

无 SKP 时可省略两个 `--skp-*` 参数。输出 `PLANDESIGN1` TSV 和同名 JSON，包含源 DWG、采用 JSON、各资料文件的指纹以及逐对象未解决项。现有 `PLANPROJECT1` 项目参数增加：

```text
FILE designSources "design-sources.tsv"
```

无人值守入口发现该项后统一走完整生成链，已有图修订与模板终核请求也先重建受影响标注，再执行模板终核。仅模板换样式不能算门窗尺寸已联动。输入资料指纹纳入断点身份，变化后不得复用旧结果。

## 原生落实与核对

1. 原生单洞口逐樘读取宽高、底标高、类别和真实测点，在自有隔离 CAD 内按公开特性改写。编号按采用宽高重新生成。保护其他洞口、墙基线和根对象数量。
2. 写入失败回滚后，再读原宽高、底标高、类型、号和测点；仅在回滚证实后跳过该项。回滚失败不能把受污染副本交付。
3. 主体三道尺寸、轴网及四框使用剔除已确认围墙后的实际墙轴范围；围墙门保留在自身号任务，围墙长度独立成原生尺寸，不进入主体洞口链。未明确围墙用途、不支持斜/曲围墙长度等仍列清单。围墙柱等未建立逐构件用途时不能宣称全部围墙构件已分离。
4. 保存独立重开核实际宽高、两端定位、号和独立围墙尺寸。模板及墙高最终处理后，再独立只读核最终 DWG 的门窗值；绑定最终 SHA256，不另打印。
5. 最终只读宿主失败可恢复或最多尝试三次，安全退出仍未完成时列 `design-source-execution`，继续交付其他已完成项；资料/文件身份变化不得当作普通超时跳过。最终整套实打仍按原标准执行。

验证范围必须写实：源码编译或文字规则通过不等于任意 SU、任意表格、任意组合洞口及围墙类型全部通过。专项原生实验的合成尺寸不得作为正式项目采用值。
