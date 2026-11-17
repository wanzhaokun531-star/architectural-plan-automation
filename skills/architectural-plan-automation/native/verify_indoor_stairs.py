"""Check saved native stair flights on unchanged source copies."""

import argparse
from pathlib import Path

from verify_examples import ROOT, read, run_examples, write


SOURCES = {
    "刘府": ROOT / "阶段2/项目/刘府_施工图平面生成_20261113/saved-source.dwg",
    "刘府成图": next((ROOT / "阶段2/交付/刘府_建筑施工图平面_尺寸修订_20261113").glob("*.dwg")),
}
LIU_FLIGHTS = (
    "STRAIGHT F1-north -1 40AD 40AC 40AB 40AA 40A9",
    "STRAIGHT F1-east -1 40A8 40A7 40A4 40A5 40A6",
    "STRAIGHT F2-south -1 4002 4001 4000 3FFF 3FFE 3FFD 3FFC 3FFB 3FFA",
    "STRAIGHT F2-east 1 4007 4006 4003 4004 4005",
    "STRAIGHT F2-north 1 40A3 40A2 40A1 40A0 409F 409E 409D 409C 4008",
)


def verify(folder, name):
    native = read(folder / "plan-native-result.json")
    assert native["reopened"] and not native["bcpLoadedBefore"] and not native["bcpLoadedAfter"]
    receipt = read(folder / "indoor-stairs.json")
    assert receipt["savedReopened"]
    rows = {row["key"]: row for row in receipt["flights"]}
    styles = {row["handle"]: row for row in read(folder / "尺寸样式字高核验.json")["dimensions"]}
    assert set(rows) == {line.split()[1] for line in LIU_FLIGHTS}, rows
    for line in LIU_FLIGHTS:
        parts = line.split()
        row = rows[parts[1]]
        assert row["state"] == "plan-written" and row["native"] and row["pending"], row
        assert row["visibleTreads"] == len(parts) - 3 and abs(row["pitchMM"] - 280) < .01, row
        assert abs(row["widthMM"] - 1200) < .01, row
        dimension, = row["dimensions"]
        assert abs(dimension["measuredMM"] - 1200) < .01 and dimension["visibleExtensionVerified"], row
        dimension["stylePassed"] = styles.get(dimension["handle"], {}).get("passed")
        if name == "刘府成图":
            assert dimension["stylePassed"], dimension
    return receipt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--case", choices=list(SOURCES), action="append")
    parser.add_argument("--plots", action="store_true")
    args = parser.parse_args()
    cases = args.case or list(SOURCES)

    def configure(name, source, digest, work, config):
        path = work / "indoor-stairs.tsv"
        path.write_text("\n".join((f"PLANSTAIRS1 {digest}", *LIU_FLIGHTS, "END")) + "\n", encoding="ascii")
        config["indoorStairs"] = path

    summary = run_examples(args.output, cases, module=args.module, extra_samples=SOURCES,
                           plots=args.plots, configure=configure)
    assert summary.get("nativeQuit") is True, summary
    summary["stairs"] = {name: verify(args.output / name, name) for name in cases}
    write(args.output / "楼梯逐跑复验.json", summary)


if __name__ == "__main__":
    main()
