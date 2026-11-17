"""Export project rule dispositions and their outstanding object evidence."""

import argparse
import csv
from pathlib import Path

from check_source_reconciliation import read, verify


def open_result(result):
    return result.startswith(("pending", "failed", "unreadable", "missing"))


def export(report):
    verify(report)
    ledger = read(report / "plan-rule-ledger.json")
    inventory = read(report / "plan-delivery-reconciliation.json")
    path = report / "逐条规则执行与未决原因.csv"
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("规则", "程序动作", "本次状态", "应有项", "未闭合", "局部失败", "原因和定位", "处理方向"))
        for rule in ledger["rules"]:
            evidence = set()
            for row in inventory["items"]:
                if rule["id"] in row["rules"] and row["status"] in {"pending", "failed"}:
                    evidence.add(f"幅{row['panel']} {row['scope']} {row['source']} {row['reason']}")
            for row in rule["evidence"]:
                result = row.get("result", "")
                if open_result(result):
                    panel = row.get("component", row.get("sourcePanel", row.get("panel", 0)))
                    target = row.get("object") or row.get("source") or row.get("key") or ""
                    location = f"幅{panel} {target}"
                    if "side" in row:
                        location += f" 侧{row['side']}"
                    detail = row.get("detail") or row.get("reason") or " / ".join(row.get("reasons", [])) or result
                    report_name = row.get("report", "")
                    evidence.add(f"{location} {detail} {report_name}".strip())
            status = rule["status"]
            action = rule["action"]
            if status in {"needs-rule-mapping", "unsupported-action"}:
                next_step = "实现本规则适用判定、写入动作和独立重开核验"
            elif status == "awaiting-drawing-evidence":
                next_step = "补查触发对象。触发时补动作或读回，未触发时登记排除依据"
            elif status == "drawing-evidence-with-open-items":
                next_step = "按右侧清单定位缺项，补项目依据、生成或专项复验后再对账"
                if action == "floor-datum-readback":
                    next_step = "核冲突楼层、未落图的标高或个别墙写入失败，按实际依据补值并重开"
                elif action == "roof-plan-native":
                    next_step = "补实际檐边或已确认屋面资料，核异形分区、檐口和逐侧尺寸"
                elif action in {"stair-flight", "vertical-core-native"}:
                    next_step = "逐跑核踏步、方向、平台和上下端标高，扩展异形跑及原生参数核验"
                elif action == "balcony-edge-readback":
                    next_step = "补临空边、对应墙厚、内侧方向和收口，复核栏杆反坎四线"
                elif rule["id"] in {"P02", "T07"}:
                    next_step = "对照图名、墙体范围及原布局确认孤立组件归属，确认后补对应主体或局部范围"
                elif rule["id"] in {"D01", "D02", "D03", "D04", "D06", "QA10"}:
                    next_step = "按侧核轴线、三道测站、保留链和省标依据，处理等测站但定义点不同的旧链后重开"
                elif rule["id"] in {"D22", "Q03", "G06", "Y06"}:
                    next_step = "按旧对象句柄核定义点、测值、用途和对应成图链，补核不可读源链和块内尺寸"
            else:
                next_step = ""
            writer.writerow((rule["id"], action, status, rule["sourceExpected"], rule["sourceUnresolved"],
                             rule["sourceFailed"], "\n".join(sorted(evidence)) or rule["reason"], next_step))
    return path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    for report in parser.parse_args().reports:
        print(export(report))
