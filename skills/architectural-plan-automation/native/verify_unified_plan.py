"""Exercise the unified AutoCAD entry on original drawings and changed policy copies."""

import argparse
from pathlib import Path
import shutil
import sys

from verify_examples import ROOT, SKILL, read, run_examples, sha, write
from verify_indoor_stairs import LIU_FLIGHTS, SOURCES

sys.path.insert(0, str(ROOT / "插件/tools"))
from sync_plan_policy import MASTER, master_rules, unified_catalog


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("module", type=Path)
    parser.add_argument("--cases", nargs="+")
    args = parser.parse_args()
    output = args.output.resolve()
    project02 = ROOT / "阶段2/项目/02_同侧轴圈联合排版_20261112"
    fixtures = {
        "刘府": SOURCES["刘府"], "刘府600": SOURCES["刘府"],
        "刘府只读": SOURCES["刘府"], "刘府整图": SOURCES["刘府"],
        "02整图": project02 / "saved-source.dwg",
    }
    requested = args.cases or ["刘府", "刘府600", "刘府只读", "刘府整图", "02整图", "陈府", "赵总原方案", "罗朝辉原DWG"]

    def configure(name, source, digest, work, config):
        config["mode"] = "generate" if name.endswith("整图") else "audit" if name == "刘府只读" else "revise"
        if name in {"刘府", "刘府600", "刘府整图"}:
            path = work / "indoor-stairs.tsv"
            path.write_text("\n".join((f"PLANSTAIRS1 {digest}", *LIU_FLIGHTS, "END")) + "\n", encoding="ascii")
            config["indoorStairs"] = path
        if name.endswith("整图"):
            resources = work / "resources"
            shutil.copytree(SKILL / "assets", resources)
            for file in ("axes.dwg", "axes.tsv"):
                shutil.copyfile(project02 / "native-resources" / file, resources / file)
            config["resources"] = resources
        if name == "刘府600":
            resources = work / "resources"
            shutil.copytree(SKILL / "assets", resources)
            master = read(MASTER)
            row = next(row for row in master["rules"] if row["id"] == "D23")
            row["rule"] = row["rule"].replace("550 mm", "600 mm")
            for section in master["sections"]:
                for block in section["blocks"]:
                    if block["kind"] == "rule" and block["args"][0] == "D23":
                        block["args"][1] = row["rule"]
            extra = {"id": "ZZ01", "section": row["section"], "rule": "测试新增弧形梯段转换。",
                     "execution": {"action": "arc-stair"}}
            master["rules"].append(extra)
            next(section for section in master["sections"] if any(block["kind"] == "rule" and block["args"][0] == "D23"
                 for block in section["blocks"]))["blocks"].append({"kind": "rule", "args": [extra["id"], extra["rule"]]})
            master["ruleCount"] += 1
            master_rules(master)
            policy = resources / "plan-policy"
            write(policy / "all-plan-rules.json", master)
            (policy / "rule-catalog.tsv").write_text(unified_catalog(master, sha(policy / "all-plan-rules.json")),
                                                     encoding="ascii", newline="\n")
            config["resources"] = resources

    result = run_examples(output, requested, module=args.module, extra_samples=fixtures, plots=False, configure=configure)
    assert result.get("nativeQuit") is True
    for name, case in result["cases"].items():
        assert "error" not in case, (name, case)
        audit = read(Path(case["audit"]))
        folder = output / name
        ledger = read(Path(case.get("ledger", folder / "plan-rule-ledger.json")))
        assert ledger["sourceSHA256"] == case["SHA256"] and ledger["engineSHA256"] == result["moduleSHA256"]
        assert not ledger["formalReady"]
        assert len(ledger["rules"]) == read(MASTER)["ruleCount"] + (name == "刘府600")
        if name == "刘府只读":
            assert ledger["mode"] == "audit" and not ledger["outputSHA256"] and not audit["drawingWritten"]
            continue
        assert ledger["outputSHA256"] == audit["outputSHA256"] == sha(folder / "审阅候选.dwg")
        if name in {"刘府", "刘府600"}:
            rules = {row["id"]: row for row in ledger["rules"]}
            expected = 600 if name.endswith("600") else 550
            assert [round(row["measuredMM"]) for row in rules["D23"]["evidence"]] == [expected] * 5
            assert [row["result"] for row in rules["SE23"]["evidence"]] == ["plan-written"] * 5
            if name.endswith("600"):
                assert rules["ZZ01"]["status"] == "unsupported-action" and not rules["ZZ01"]["evidence"]
    write(output / "统一入口验收.json", result)


if __name__ == "__main__":
    main()
