"""Run the standalone ARX on owned DWG copies using the existing private host."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid


ROOT = Path(__file__).resolve().parents[3]
SKILL = Path(__file__).resolve().parents[1]
HOST = ROOT / "阶段1/原生接口探测"
sys.path.insert(0, str(HOST))
from run_quiet_job import dispatch


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def run_examples(results, requested=(), *, module=None, extra_samples=None, plots=True, configure=None,
                 preview_generated=None):
    results = Path(results).resolve()
    results.mkdir(parents=True, exist_ok=False)
    materials = SKILL / "assets"
    archives = ROOT / "阶段1/runs"
    template = archives / "isolated-autocad-20261110-153955"
    config = read(template / "worker-config.json")
    retired = template / "staging-final-archive"
    bridge = retired / Path(config["stagedARX"]).name
    assert sha(bridge) == config["binarySHA256"]
    old_nonce = config["nonce"]
    nonce = uuid.uuid4().hex
    session = archives / f"isolated-plan-portable-{nonce[:8]}"
    session.mkdir()
    (session / "queue").mkdir()
    staging = Path("C:/Program Files/Autodesk/ApplicationPlugins") / f"CodexStage1Probe_{nonce}"
    staging.mkdir()
    staged_bridge = staging / f"S1Acad_{nonce[:8]}.arx"
    staged_tool = staging / f"PlanNative_{nonce[:8]}.arx"
    shutil.copyfile(bridge, staged_bridge)
    shutil.copyfile(module or SKILL / "tools/PlanNative.arx", staged_tool)
    bridge_ini = staged_bridge.with_suffix(".ini")
    text = (retired / Path(config["stagedINI"]).name).read_text(encoding="utf-16")
    text = text.replace(str(template), str(session)).replace(old_nonce, nonce)
    text = text.replace(f"BCP2_{old_nonce[:8]}.arx", staged_tool.name)
    bridge_ini.write_text(text, encoding="utf-16")
    tool_ini = staged_tool.with_suffix(".ini")
    tool_ini.write_text(f"[stage2]\nnonce={nonce}\n", encoding="utf-16")
    config.update(nonce=nonce, session=str(session), stagingDirectory=str(staging),
                  stagedARX=str(staged_bridge), stagedINI=str(bridge_ini))
    write(session / "worker-config.json", config)
    boot = session / "boot.scr"
    boot.write_bytes(f'_.ARX\r\n_L\r\n"{staged_bridge.as_posix()}"\r\n_.ARX\r\n_L\r\n"{staged_tool.as_posix()}"\r\nS1QUIETSTART\r\n'.encode("ascii"))
    marker = session / "quiet-worker-ready.json"
    launch = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
              str(HOST / "启动隔离桌面宿主.ps1"), "-RunDirectory", str(session),
              "-BootScriptPath", str(boot), "-Template", config["template"], "-Profile", config["profile"],
              "-EarlyNativeModule", str(staged_bridge), "-Nonce", nonce,
              "-ReadyMarkerName", marker.name, "-TimeoutSeconds", "600", "-CloseOwnStartupTips"]
    samples = {
        "陈府": ROOT / "阶段2/项目/1号DWG_施工图平面生成_20261111/saved-source.dwg",
        "赵总原方案": ROOT / "阶段2/项目/赵总_施工图平面生成_20261111/定稿输入快照.dwg",
        "罗朝辉原DWG": Path(read(ROOT / "阶段2/项目/罗朝辉_现行Skill生成_20261111/交付验收.json")["source"]),
        "赵总": Path(read(ROOT / "阶段2/项目/赵总_一层房间净面积修订_20261111/交付验收.json")["source"]),
        "罗朝辉": next((ROOT / "阶段2/交付/罗朝辉_建筑施工图平面_20261111").glob("*.dwg")),
        "陈府既有尺寸": next((ROOT / "阶段2/交付/陈府_建筑施工图平面_20261111").glob("*.dwg")),
    }
    if extra_samples:
        samples.update(extra_samples)
    if requested:
        assert set(requested) <= samples.keys()
        samples = {name: samples[name] for name in requested}
    summary = {"session": str(session), "moduleSHA256": sha(staged_tool), "cases": {}}
    flags = subprocess.CREATE_NO_WINDOW
    with (session / "launcher.log").open("wb") as log:
        host = subprocess.Popen(launch, stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            deadline = time.monotonic() + 145
            last_ack = 0
            while not marker.exists():
                if host.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"Private CAD startup did not become ready: {session}")
                if time.monotonic() - last_ack > 15 and (session / "isolation-host-receipt.json").exists():
                    helper = ROOT / "阶段2/项目/罗朝辉_按规则整理_20261110/ack-update-failure.ps1"
                    subprocess.run(["powershell.exe", "-NoProfile", "-File", str(helper), "-Session", str(session)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
                    last_ack = time.monotonic()
                time.sleep(.25)
            ready = read(marker)
            assert ready["nonce"] == nonce and ready["inputDesktop"] != ready["desktop"] and not ready["error"]
            # Query the exact worker process before the first native operation.
            query = f"(Get-Process -Id {ready['processId']}).Modules | ForEach-Object {{$_.ModuleName}}"
            module_inventory = subprocess.check_output(
                ["powershell.exe", "-NoProfile", "-Command", query], creationflags=flags)
            assert_no_bcp(module_inventory, staged_tool.name)
            (results / "worker-modules.txt").write_bytes(module_inventory)
            for index, (name, source) in enumerate(samples.items()):
                work = session / f"case-{index + 1}"
                work.mkdir()
                final = results / name
                final.mkdir()
                control = work / "control.dwg"
                original = sha(source)
                shutil.copyfile(source, control)
                result = final / "审阅候选.dwg"
                settings = {"source": control, "output": result, "report": final,
                            "resources": materials, "scale": 100, "unitMM": 1}
                if name == "赵总":
                    measured = read(ROOT / "阶段2/项目/赵总_一层房间净面积修订_20261111/净面积测量.json")
                    assert measured["sourceSHA256"] == original
                    x, y = measured["originMM"]
                    guest = next(room for room in measured["rooms"] if room["name"] == "客房")
                    left = min(p[0] for p in guest["verticesMM"])
                    right = max(p[0] for p in guest["verticesMM"])
                    top = max(p[1] for p in guest["verticesMM"])
                    added = work / "new-dimensions.tsv"
                    added.write_text(f"1 {x+left:.12f} {y+top:.12f} {x+right:.12f} {y+top:.12f} -450\n", encoding="ascii")
                    settings["newDimensions"] = added
                    confirmed = work / "confirmed-rooms.tsv"
                    records = [f"PLANROOMS1 {original}"]
                    for room in measured["rooms"]:
                        points = " ".join(f"{x+px:.12f} {y+py:.12f}" for px, py in room["verticesMM"])
                        prefix = "三厅合计 " if room["name"] == "三厅合计" else ""
                        records.append(f'1 {room["nameHandle"]} {room["netM2"]} {len(room["verticesMM"])} "{prefix}" {points}')
                    confirmed.write_text("\n".join(records) + "\n", encoding="utf-8")
                    settings["confirmedRooms"] = confirmed
                if configure:
                    configure(name, source, original, work, settings)
                (work / "plan.ini").write_text("[plan]\n" + "".join(
                    f"{key}={value}\n" for key, value in settings.items()), encoding="utf-16")
                if name == "02整图":
                    helper = ROOT / "阶段2/项目/02_同侧轴圈联合排版_20261112/continue-own-source-font-prompt.ps1"
                    font_worker = subprocess.Popen(["powershell.exe", "-NoProfile", "-File", str(helper), "-Session", str(session), "-Watch"],
                                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
                    dispatch(session, ready, "S1OPENPROBECONTROL", work, "native-open.done", 85)
                else:
                    dispatch(session, ready, "S1OPENPROBECONTROL", work, "native-open.done", 50)
                dispatch(session, ready, "S1PHASE2EXECUTE", work, "stage2-execute.done", 200)
                evidence = final / "plan-native-result.json"
                assert sha(source) == sha(control) == original
                error = final / "plan-native-error.json"
                if error.exists():
                    summary["cases"][name] = {"error": read(error)}
                    assert not result.exists(), error
                    write(results / "验证摘要.json", summary)
                    continue
                if settings.get("mode") == "axis-edit-trial":
                    evidence = final / "axis-edit-readback.json"
                    trial = read(evidence)
                    assert trial["savedReopened"] and trial["savedInputUnchanged"] and not result.exists()
                    summary["cases"][name] = {"source": str(source), "SHA256": original, "audit": str(evidence)}
                    write(results / "验证摘要.json", summary)
                    continue
                if settings.get("mode") in {"generate", "audit"} and not (final / "plan-native-error.json").exists():
                    if settings["mode"] == "generate":
                        evidence = final / "product-result.json"
                        until = time.monotonic() + 180
                        while not evidence.exists() and time.monotonic() < until:
                            time.sleep(.25)
                        native = read(evidence)
                        assert native["completed"] and native["finalDocumentOpened"], native
                        result = Path(native["output"])
                        assert native["published"] and sha(result) == native["outputSHA256"]
                        ledger = Path(native["records"]) / "plan-rule-ledger.json"
                    else:
                        evidence = final / "plan-native-audit.json"
                        assert not result.exists() and not read(evidence)["drawingWritten"]
                        ledger = final / "plan-rule-ledger.json"
                    summary["cases"][name] = {"source": str(source), "SHA256": original,
                                              "audit": str(evidence), "ledger": str(ledger)}
                    if settings["mode"] == "generate" and preview_generated:
                        preview_generated(session, ready, work, final, result, native["finalRoots"])
                    write(results / "验证摘要.json", summary)
                    continue
                if not evidence.exists():
                    summary["cases"][name] = {"error": read(final / "plan-native-error.json")}
                else:
                    audit = read(evidence)
                    assert audit["completed"] and audit["reopened"] and sha(result) == audit["outputSHA256"]
                    assert audit["bcpLoadedBefore"] is False and audit["bcpLoadedAfter"] is False
                    summary["cases"][name] = {"source": str(source), "SHA256": original, "audit": str(evidence)}
                    case_plots = []
                    for panel in audit["panels"] if plots else ():
                        if not panel["processed"]:
                            continue
                        preview = work / f"plot-{panel['panel']}"
                        preview.mkdir()
                        shutil.copyfile(result, preview / "control.dwg")
                        x0, y0, x1, y1 = panel["bounds"]
                        window = f"{x0-5500} {y0-6800} {x1+5500} {y1+5500}"
                        (preview / "annotation-index.tsv").write_text(
                            f"S2DIMPREVIEW3 0 {audit['modelRoots']} {window} 594 420\n", encoding="ascii")
                        dispatch(session, ready, "S1OPENPROBECONTROL", preview, "native-open.done", 45)
                        dispatch(session, ready, "S1PHASE2PREVIEW", preview, "stage2-preview-native.json", 70)
                        plot = read(preview / "stage2-preview-native.json")
                        pdf = preview / "stage2-A2-1-100.pdf"
                        if not pdf.is_file() or plot["error"]:
                            case_plots.append({"panel": panel["panel"], "error": plot["error"]})
                            continue
                        picture = final / f"图幅{panel['panel']}"
                        subprocess.run(["pdftoppm", "-scale-to", "2200", "-png", "-singlefile",
                                        str(pdf), str(picture)], check=True, creationflags=flags)
                        case_plots.append({"panel": panel["panel"], "image": str(picture.with_suffix(".png"))})
                    summary["cases"][name]["plots"] = case_plots
                write(results / "验证摘要.json", summary)
                print(name, summary["cases"][name], flush=True)
            modules_after = subprocess.check_output(
                ["powershell.exe", "-NoProfile", "-Command", query], creationflags=flags)
            assert_no_bcp(modules_after, staged_tool.name)
            (results / "worker-modules-after.txt").write_bytes(modules_after)
        finally:
            if "font_worker" in locals() and font_worker.poll() is None:
                font_worker.terminate()
                font_worker.wait(5)
            if marker.exists() and host.poll() is None:
                ready = read(marker)
                clean = session / "clean"
                clean.mkdir()
                # Final shutdown needs only a clean owned document. Reopening a full generated
                # project here needlessly re-instantiates third-party project reactors.
                shutil.copyfile(materials / "reference.dwg", clean / "control.dwg")
                dispatch(session, ready, "S1OPENPROBECONTROL", clean, "native-open.done", 45)
                unload = session / "unload"
                unload.mkdir()
                (unload / "phase2-product-unload.only").touch()
                dispatch(session, ready, "S1PHASE2EXECUTE", unload, "native-product-unload.json", 30)
                assert read(unload / "native-product-unload.json")["unloaded"]
                subprocess.run([sys.executable, str(HOST / "run_quiet_job.py"), str(session), "Exit",
                                "--timeout", "35", "--exit-timeout", "50"], check=True, creationflags=flags)
                host.wait(12)
            terminal = session / "isolation-final.json"
            if terminal.exists() and read(terminal).get("workerExited"):
                summary["nativeQuit"] = read(terminal)["nativeQuitConfirmed"]
                archive = session / "staging-final-archive"
                archive.mkdir()
                for file in (staged_bridge, staged_tool, bridge_ini, tool_ini):
                    assert file.resolve().parent == staging.resolve()
                    shutil.move(file, archive / file.name)
                staging.rmdir()
            write(results / "验证摘要.json", summary)
    return summary


def assert_no_bcp(inventory, required_module):
    modules = inventory.lower().split()
    assert required_module.lower().encode("ascii") in modules
    assert not any(name.endswith(b".arx") and name.startswith((b"bcp", b"buildingplanplugin2020"))
                   for name in modules)


def main():
    run_examples(sys.argv[1], sys.argv[2:])


if __name__ == "__main__":
    main()
