"""Verify the Liu axis, perimeter, opening and dimension repair regression."""

from collections import Counter
import csv
from math import dist
from pathlib import Path
import sys

from verify_examples import ROOT, read, sha, write


MAPPINGS = {
    "A02": "axis-label-readback", "A03": "axis-label-readback", "A07": "axis-label-readback",
    "A09": "axis-line-readback", "A10": "axis-line-readback", "A11": "axis-line-readback",
    "A12": "axis-label-readback", "A28": "axis-label-readback",
    "D01": "perimeter-chain-readback", "D02": "perimeter-chain-readback", "D03": "perimeter-chain-readback",
    "D05": "perimeter-chain-readback", "D08": "perimeter-chain-readback", "D10": "perimeter-chain-readback",
    "D11": "perimeter-chain-readback",
    "O06": "opening-caption-readback", "TL02": "opening-caption-readback", "L04": "opening-caption-readback",
    "TL04": "opening-caption-readback",
}


def verify(result):
    prior = ROOT / "outputs/刘府_PLAN_DRAW规则台账复验_20261113/整图生成"
    ledger = read(result / "plan-rule-ledger.json")
    job = read(result / "product-result.json")
    session = read(result.parent / "验证摘要.json")
    source = Path(session["cases"]["整图生成"]["source"])
    master = ROOT / "docs/规则汇编_20261105/建筑施工图平面自动化通用规则_20261105.json"
    assert session["nativeQuit"] and job["completed"] and job["sourceUnchanged"] and job["sourceMemoryObserved"]
    assert sha(source) == ledger["sourceSHA256"] == job["sourceSHA256"]
    assert sha(result / "审阅候选.dwg") == ledger["outputSHA256"] == job["outputSHA256"]
    assert sha(master) == ledger["masterSHA256"] and session["moduleSHA256"] == ledger["engineSHA256"]
    rules = {row["id"]: row for row in ledger["rules"]}
    assert len(rules) == 251
    for key, action in MAPPINGS.items():
        row = rules[key]
        assert row["action"] == action and row["status"] == "drawing-evidence-recorded", key
        assert all(e["savedReopened"] and e["object"] for e in row["evidence"]), key
    assert len(rules["A07"]["evidence"]) == 30
    assert len(rules["D11"]["evidence"]) == 8
    assert all(e["result"] == "verified-axis-chain-closure" for e in rules["D11"]["evidence"])
    assert len(rules["O06"]["evidence"]) == 34
    assert all(e["result"] == "verified-opening-number" for e in rules["O06"]["evidence"])
    assert len(rules["A11"]["evidence"]) == 32
    assert len(rules["D10"]["evidence"]) == 24
    assert len(rules["TL04"]["evidence"]) == 34
    style = read(result / "尺寸样式字高核验.json")
    assert style["failed"] == 0, [row["handle"] for row in style["dimensions"] if not row["passed"]]
    current = read(result / "interior-joint-readback.json")
    before = read(prior / "interior-joint-readback.json")
    assert not current["bcpLoaded"] and current["reopened"]
    assert all(not row["unreadable"] and not row["sideIssues"] for row in current["groups"]), current["groups"]
    assert len(current["chains"]) == len(before["chains"]) == 31
    for a, b in zip(current["chains"], before["chains"]):
        assert (a["panel"], a["source"], a["added"]) == (b["panel"], b["source"], b["added"])
        assert a["measurementVerified"] and a["witnessVerified"]
        assert len(a["values"]) == len(b["values"])
        assert len(a["stationCoordinates"]) == len(b["stationCoordinates"])
        assert all(abs(x-y) < 1e-5 for x, y in zip(a["values"], b["values"]))
        assert all(dist(x,y) < 1e-5 for x, y in zip(a["stationCoordinates"], b["stationCoordinates"]))
    pending = {(e["object"], e["segment"]) for e in rules["D18"]["evidence"] if e["result"] == "pending"}
    assert not ({("45BC", 1), ("45D1", 3), ("4609", 1)} & pending), pending
    return rules, current, ledger, session


def write_review(result, rules, current, ledger, session):
    old_csv = ROOT / "outputs/刘府_PLAN_DRAW规则台账复验_20261113/251条规则逐条复核.csv"
    with old_csv.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        headers = reader.fieldnames
    assert len(rows) == len(rules) and {row["规则号"] for row in rows} == rules.keys()
    pending_text = [e for e in rules["D18"]["evidence"] if e["result"] == "pending"]
    pending_links = [e for e in rules["A28"]["evidence"] if e["result"] == "pending-line-connection"]
    layout = "、".join(f'{row["before"]}→{row["after"]}' for row in current["groups"])
    updates = {}

    def note(ids, observation, cause, remedy, source):
        for key in ids.split():
            updates[key] = observation, cause, remedy, source

    note("A02 A03 A07 A12", "30 组天正轴号和 40 个实际引线端有读回，核了编号、分侧显隐及真实轴位锚点。",
         "已生成对象通过相应核验，逐层统一编号、E/J 型适用依据和业务编辑试拖仍缺独立对账。",
         "按本条补应有编号、实际构件关系和分侧清单，逐根对照重开对象并对可编辑性试改后还原。",
         "整图生成/plan-rule-ledger.json；外围间距核验.json")
    note("A09 A10 A11", "32 根独立 DOTE 轴线已读回实际端点、线型和所属模型空间。",
         "当前证据比较生成范围，仍需逐轴补源构件和跨度依据，并核业务编辑能力。",
         "按真实墙柱、主梁和交接建立应有起止，逐轴核多余延伸、断口、线型空档和试拖。",
         "整图生成/plan-rule-ledger.json；平面构件数据.json")
    note("A28", f"40 个引线端已核真实轴位，{len(pending_links)} 个端点未与当前独立轴线几何相接。",
         "需按每侧服务对象判定哪些端点需要独立轴线投影接合，并复查对应测站。",
         "按台账圈号、侧别和锚点，逐端确定应接或局部终止，必要时补实际投影接头后重开。",
         "整图生成/plan-rule-ledger.json；外围间距核验.json")
    note("D01 D02 D03 D08 D10 D11", "24 条外围链已读回原生结构和测站，8 侧总尺寸与轴间尺寸同首末且分段和闭合。",
         "本次证据覆盖已生成链，独立应有轴站、未识别的轴或洞口及业务试改仍需逐侧复验。",
         "从真实墙柱轴、洞口和局部边界建立应有链、可省依据和漏标对账，复查整体链节点试改。",
         "整图生成/plan-rule-ledger.json；外围间距核验.json")
    note("D05 D24", "8 条第三道链与已登记的原始洞口测站及测值一致。",
         "现有对账覆盖已识别洞口链，仍需独立检查各侧应有开间、退折和屋面局部链。",
         "按真实洞口边、墙段交接、有效定位轴和实际檐口建立逐侧应有清单并重开对照。",
         "整图生成/plan-rule-ledger.json；外围间距核验.json")
    note("O06 TL02 L04 TL04", "34 个原生门窗编号已重开核自身字段和显示，另读位置、方向、适用室内净距及展开字高。",
         "45 个源洞口仍有 11 个未成号待核。编号显示层、完整净空及名义字高字段本体仍须相应证据。",
         "按本条逐樘核门型、宿主、编号层与有效颜色、室内门编号的实际净距和字高本体字段。",
         "整图生成/plan-rule-ledger.json；门窗编号重开公开属性")
    note("D12 D13 D15 D16 D18 D22 D23 D25 QA09 Q03 TL06 TL08 TL09 TL10",
         f"31 条链、64 段的测点、测值及 550 mm 界线已重开，图幅净空冲突为 {layout}。",
         f"本轮入组剩余 {len(pending_text)} 个文字碰撞。应有尺寸、门垛省标、源线文、门窗与净域仍按本条分项核验。",
         "按规则原文补独立的应有对象清单，并以实际字框和测量对象关闭待项。",
         "整图生成/interior-joint-readback.json；plan-rule-ledger.json；待确认清单.csv")
    note("Y04", "原 43E4、43E5 已以实际测点重建为正确比例的原生链；55 条可见尺寸样式读回通过。",
         "本项目可读可见尺寸已通过。其他比例、非默认覆盖及共享样式仍按各项目复验。",
         "保持逐链样式、模型字高、源与成果句柄和几何保真对账。",
         "整图生成/尺寸样式字高核验.json；interior-joint-readback.json")
    for row in rows:
        key = row["规则号"]
        rule = rules[key]
        if key in updates:
            row["现场或历史依据"], row["未关闭原因"], row["解决方案"], row["证据位置"] = updates[key]
        if key in MAPPINGS:
            row["程序台账状态"] = "统一台账已有对象证据"
            row["刘府判定"] = "已复验所列对象，适用范围见逐条说明"
        if key == "Y04":
            row["刘府判定"] = "本轮所列对象读回通过"
        results = Counter(e["result"] for e in rule["evidence"])
        row["本次对象证据结果"] = "，".join(f"{key} {count}" for key, count in sorted(results.items()))
    assert all(row["未关闭原因"] and row["解决方案"] for row in rows)
    with (result.parent / "251条规则逐条复核.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, headers)
        writer.writeheader()
        writer.writerows(rows)
    counts = Counter(r["status"] for r in rules.values())
    write(result.parent / "规则台账复验摘要.json", {
        "counts": counts, "newMappings": MAPPINGS, "session": session,
        "sourceSHA256": ledger["sourceSHA256"], "outputSHA256": ledger["outputSHA256"],
        "masterSHA256": ledger["masterSHA256"], "engineSHA256": ledger["engineSHA256"],
        "interiorGroups": current["groups"], "pendingText": pending_text, "pendingAxisJoins": pending_links,
    })
    print(counts)
    print("Remaining interior clashes:", [(e["object"], e["segment"], e["obstacles"]) for e in pending_text])


if __name__ == "__main__":
    result = Path(sys.argv[1])
    details = verify(result)
    write_review(result, *details)
    assert all(not row["after"] for row in details[1]["groups"]), details[1]["groups"]
