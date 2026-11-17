"""Verify the native evidence and update the Liu per-rule review from the prior audit."""

from collections import Counter
import csv
from math import dist
from pathlib import Path
import sys

from verify_examples import ROOT, read, sha, write


def verify(root):
    report = root / "整图生成"
    ledger = read(report / "plan-rule-ledger.json")
    result = read(report / "product-result.json")
    session = read(root / "验证摘要.json")
    master = ROOT / "docs/规则汇编_20261105/建筑施工图平面自动化通用规则_20261105.json"
    drawing = report / "审阅候选.dwg"
    source = Path(session["cases"]["整图生成"]["source"])
    assert session["nativeQuit"] and result["completed"] and result["sourceMemoryObserved"]
    assert sha(source) == ledger["sourceSHA256"] == result["sourceSHA256"]
    assert sha(drawing) == ledger["outputSHA256"] == result["outputSHA256"]
    assert sha(master) == ledger["masterSHA256"] and session["moduleSHA256"] == ledger["engineSHA256"]
    prior = ROOT / "outputs/刘府_PLAN_DRAW室内联合排版交付_20261113/整图生成"
    actual = {row["id"]: row for row in ledger["rules"]}
    original = {row["id"]: row for row in read(prior / "plan-rule-ledger.json")["rules"]}
    assert len(actual) == len(ledger["rules"]) == 251 and actual.keys() == original.keys()

    mapped = {"A14", "D09", "D17", "D18", "D24", "Y04", "Y07", "Y08", "QA09"}
    assert {key for key in actual if actual[key]["status"] != original[key]["status"]} == mapped
    for key in mapped:
        row = actual[key]
        assert row["status"] == "drawing-evidence-recorded" and row["evidence"]
        for entry in row["evidence"]:
            assert entry["savedReopened"] is True
            assert entry.get("object") or (key == "QA09" and entry.get("group"))
            if "report" in entry:
                assert (report / entry["report"]).is_file(), entry

    chains = read(report / "外围间距核验.json")["dimensions"]
    by_handle = {row["handle"]: row for row in chains}
    d17 = actual["D17"]["evidence"]
    perimeter = [row for row in d17 if row["scope"] == "perimeter-segment"]
    assert len(perimeter) == 127
    assert len(d17) - len(perimeter) == 55
    for row in perimeter:
        chain = by_handle[row["object"]]
        index = row["segment"] - 1
        assert row["sameSide"] and row["noLeader"] and row["result"] == "verified-perimeter-text"
        assert row["stations"] == chain["stations"][index:index + 2]
        assert abs(row["value"] - chain["values"][index]) < 1e-5
        assert abs(row["baseline"] - chain["baseline"]) < 1e-5
    short = sorted([row for row in perimeter if row["panel"] == 1 and row["side"] == 0 and
                    row["row"] == 3 and round(row["value"]) in {180, 220}], key=lambda row: row["value"])
    assert len(short) == 2 and short[0]["object"] == short[1]["object"]
    gap = short[1]["textBox"][0][1] - short[0]["textBox"][1][1]
    assert abs(gap - 82) < 0.01

    assert {row["object"] for row in actual["D09"]["evidence"]} == by_handle.keys()
    opening_chains = {row["handle"] for row in chains if row.get("sourceStationsChecked")}
    assert opening_chains and {row["object"] for row in actual["D24"]["evidence"]} == opening_chains
    frames = actual["Y07"]["evidence"]
    assert Counter(row["panel"] for row in frames) == {1: 4, 2: 4}
    assert all(row["result"] == "verified-frame-appearance" for row in frames)
    assert all(not row["plotEnabled"] for row in actual["Y08"]["evidence"])

    review = read(report / "interior-joint-readback.json")
    prior_review = read(prior / "interior-joint-readback.json")
    assert review["reopened"] and not review["bcpLoaded"]
    fields = ("panel", "group", "before", "after", "overlaps", "unreadable", "sideIssues", "result")
    assert [{k: row[k] for k in fields} for row in review["groups"]] == prior_review["groups"]
    assert len(review["chains"]) == len(prior_review["chains"]) == 31
    for row, before in zip(review["chains"], prior_review["chains"]):
        assert row["handle"] == before["handle"] and row["measurementVerified"] and row["witnessVerified"]
        assert len(row["stationCoordinates"]) == len(before["stationCoordinates"])
        assert all(dist(a, b) < 1e-5 for a, b in zip(row["stationCoordinates"], before["stationCoordinates"]))
    pending = [row for row in actual["D18"]["evidence"] if row["result"] == "pending"]
    assert len(pending) == 3 and all(row["panel"] == 2 and row["obstacles"] for row in pending)
    assert {row["object"] for row in actual["Y04"]["evidence"] if row["result"] == "pending"} == {"43E4", "43E5"}
    assert Counter(row["scope"] for row in actual["QA09"]["evidence"]) == {
        "name": 26, "area-label": 10, "interior-chain": 31, "interior-group": 2}
    return actual, read(master)["rules"], ledger, session, gap


def review_rows(actual, rules):
    historical = ROOT / "outputs/刘府_统一入口全规则实测_20261113/workbook-data.json"
    previous = {row[0]: row for row in read(historical)["rows"]}
    assert previous.keys() == actual.keys()
    overrides = {}

    def update(ids, finding, reason, resolution, reference):
        for rule_id in ids.split():
            overrides[rule_id] = (finding, reason, resolution, reference)

    update("G04 G05 G06 Q02 TL07", "本轮 12 处名称、10 处净面积与 31 条室内链已重开读回，建筑主体及楼梯专项有对应记录。",
           "现有报告缺全构件及用户保留标注的前后逐件清单。",
           "按该条要求匹配原句柄、新句柄及公开几何，分别登记保留、隐藏、替换和未识别对象。",
           "整图生成/plan-rule-ledger.json")
    update("N01 N02 N03 N04 N05 TL05", "已确认 12 处 FY_HZ 名称通过；14 处源短文字或部位名称仍待核。",
           "剩余源文字用途、空间归属及嵌套成员覆盖尚未逐项核实。",
           "按本条条件补查原文字和锚点，含普通文字、块属性及室外投影归属，重开核样式、原文及位置。",
           "整图生成/平面名称文字终核.json")
    update("S01 S02 S03 S04 S05 S08 S09 S10 Q05", "两幅共 11 个净域候选，10 处已落字并读回。",
           "第 11 个净域的实际用途、边界或扣除条件未确认；建筑面积另按 S06。",
           "逐房对账确认净域、旧面积及楼梯井道等扣除项，再按条文复算和读回字样。",
           "整图生成/room-area-readback.json；通用房间核验_1.json、通用房间核验_2.json")
    update("D12 D13 D15 D16 D22 Q03 TL06 TL08 TL09 TL10",
           "整图已核 31 条室内链和 64 段，两端共 128 处 550 mm；第二幅仍有三处字框冲突。",
           "现有入组结果尚未覆盖本条所要求的应有测量用途、全部墙内边、门垛省标、源线文或业务编辑试验。",
           "按本条建立逐房应有测量及省标清单，复核真实端点、链结构、受影响相邻标注及实际净空。",
           "整图生成/interior-joint-readback.json")
    update("D23 D25", "整图已读回 31 条链、64 段和 128 处 550 mm；两幅冲突 37→0、51→3。",
           "D23 本轮入组界线通过。D25 第二幅三处字框仍与家具、门窗对象或楼梯尺寸符号相撞。",
           "按已记录的对象与分段处理残余冲突，继续保持实际测点、测值及可见界线。",
           "整图生成/interior-joint-readback.json；待确认清单.csv")
    update("D26", "刘府本轮入组没有可触发转换的普通旋转／对齐投影尺寸。",
           "动作已登记，本项目未取得触发对象的转换读回。",
           "以含普通投影尺寸的原图副本验证两测点、投影方向、原值、类型及必要的保留回退。",
           "整图生成/interior-joint-readback.json")
    update("A14 D09 Y07 Y08", "两幅 24 条外围链和 8 道辅助框已重开，逐对象记录基线、可见起笔、圈位或框属性。",
           "本轮验证生成对象；全侧应有基准、其他屋面情形及正式打印仍需相应依据。",
           "逐幅核独立应有基准和屋面影响，正式打印时用项目图签及 CTB 验证 Defpoints 不出纸。",
           "整图生成/外围间距核验.json；plan-rule-ledger.json")
    update("A13", "本轮生成外围尺寸按现行基准及纸面换算重开读回。",
           "尚未为历史偏距条款覆盖关系和全部既有轴引线接入独立记录。",
           "按本条列旧口径影响的引线，查成果实际长度及现行近边距离，禁止套旧固定长度。",
           "整图生成/外围间距核验.json")
    update("D17", "127 个外围字框及 55 条链的同侧无引线核验已回填，西侧 180／220 净距 82 mm。",
           "本轮已核输出对象及尺寸保真，其他户型及未识别的源线文尺寸另作覆盖验证。",
           "继续按保存重开的链、分段及实际字框核短尺寸，拥挤组保留原测站和基线。",
           "整图生成/外围尺寸文字重开核验.json；尺寸同侧无引线核验.json")
    update("D18", "外围 127 个及室内 64 个字框已有重开对象级记录。",
           "第二幅仍有三处文字碰撞；现有记录覆盖入组文字，完整端部符号对构件的检查仍待接入。",
           "按待项中的相撞对象处理三处并重开，另加端部符号与构件净空对账。",
           "整图生成/plan-rule-ledger.json；待确认清单.csv")
    update("D24", "已生成且已登记源测站的第三道链完成测站、实测值及显示覆盖校验。",
           "现有对账覆盖已识别洞口链，独立应有开间和屋面局部链清单仍待补。",
           "先从源洞口、实际墙交接和第二道有效轴建立应有测站，逐侧查漏链及必要墙段。",
           "整图生成/外围间距核验.json")
    update("Y01 Y02 Y03 Y04", "重开尺寸字高已逐对象验收，43E4、43E5 派生样式比例仍待核。",
           "既有尺寸样式覆盖、线型与嵌套成员仍须按该条核实际生效属性。",
           "逐链重读有效字高与比例，按条文补字体、层色、线型或实际原生图面检查。",
           "整图生成/尺寸样式字高核验.json")
    update("QA09", "已回填名称、净面积及室内尺寸链和分组的对象级证据。",
           "仍有名称用途、一个净域、三处碰撞，以及门窗应有项、组合编号与门垛省标覆盖未齐。",
           "逐幅补应有名称、净域、门窗与尺寸清单，按原句柄和实际对象重开关闭对应待项。",
           "整图生成/plan-rule-ledger.json")
    update("Q06 QA02 QA06", "整图已独立重开，251 条规则均有状态，已有执行证据按规则回填。",
           "应有项完整性、其他规则的对象证据和范围外专项仍需分别验收。",
           "逐条绑定规则号、图幅、对象、重开证据及结果，分别记录程序接入和项目验收。",
           "整图生成/plan-rule-ledger.json；251条规则逐条复核.csv")
    update("Q07 Q08 Q10 QA01 QA03", "统一台账保留本次程序识别的待项，逐条复核表补充原因及解决办法。",
           "其他专项仍缺本项目资料或相应程序动作。",
           "按本条逐项补齐资料、实现动作或复验实际对象，保留未关闭记录和源图关联。",
           "整图生成/plan-rule-ledger.json；待确认清单.csv")
    update("D20 SE01 SE03 SE06 SE07 SE08 SE10 SE13 SE16 SE18 SE23",
           "本次仍有五跑按原图踏步关联生成并重开，五条梯宽链均入组，界线达到 550 mm。",
           "整跑级数、上下完成面、踢面高、平台净深、梯井和楼板洞口资料未全部确认。",
           "按本条补楼梯剖面和上下端完成面，逐跑核梯型、剖断、平台与构件关系，补实测定位。",
           "整图生成/indoor-stairs.json")
    state_labels = {"drawing-evidence-recorded": "统一台账已有对象证据",
                    "awaiting-drawing-evidence": "已映射，当前项目缺证据",
                    "needs-rule-mapping": "当前统一入口尚未映射",
                    "paused": "用户暂停", "retired": "已停用"}
    for rule in rules:
        key = rule["id"]
        row = actual[key]
        old = previous[key]
        finding, reason, solution, ref = overrides.get(key, (old[5], old[6], old[7], "历史刘府审计；" + old[9]))
        disposition = old[2]
        if key in {"D17", "D09", "D23", "Y07", "Y08"}:
            disposition = "本轮所列对象读回通过"
        elif key in overrides and key not in {"D26"}:
            disposition = "已有执行或运行证据，仍需按本条核覆盖"
        results = Counter(item["result"] for item in row["evidence"])
        yield [key, rule["section"], state_labels.get(row["status"], row["status"]), disposition,
               finding, reason, solution, ref, "，".join(f"{k} {n}" for k, n in sorted(results.items())),
               rule["rule"]]


def main(root):
    rules, master, ledger, session, gap = verify(root)
    rows = list(review_rows(rules, master))
    assert len(rows) == 251 and all(row[5] and row[6] for row in rows)
    with (root / "251条规则逐条复核.csv").open("w", encoding="utf-8-sig", newline="") as target:
        writer = csv.writer(target)
        writer.writerow(["规则号", "章节", "程序台账状态", "刘府判定", "现场或历史依据", "未关闭原因",
                         "解决方案", "证据位置", "本次对象证据结果", "现行原文"])
        writer.writerows(rows)
    counts = Counter(row["status"] for row in rules.values())
    write(root / "规则台账复验摘要.json", {"counts": counts, "westTextClearanceMM": gap, "session": session,
          "sourceSHA256": ledger["sourceSHA256"], "outputSHA256": ledger["outputSHA256"],
          "masterSHA256": ledger["masterSHA256"], "engineSHA256": ledger["engineSHA256"]})
    print(dict(counts))


if __name__ == "__main__":
    main(Path(sys.argv[1]))
