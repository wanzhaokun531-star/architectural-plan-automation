"""Read-only discovery and rule compatibility checks; never launches CAD."""
import argparse
import hashlib
import json
import os
import math
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def typography_contract(skill):
    folder = Path(skill) / 'assets/annotation-template'
    raw = (folder / 'standard.json').read_bytes()
    digest = hashlib.sha256(raw).hexdigest().upper()
    standard = json.loads(raw.decode('utf-8-sig'))
    rows = (folder / 'typography.tsv').read_text('ascii').splitlines()
    if not rows or rows[0].split() != ['PLANFONTS1', digest] or rows[-1] != 'END':
        raise ValueError('原生文字执行参数与现行规则指纹不一致，先同步资源；不回退旧常数。')
    found = {}
    for row in rows[1:-1]:
        fields = row.split()
        if len(fields) != 7 or fields[0] != 'STYLE' or fields[1] in found:
            raise ValueError('原生文字执行参数重复或格式不符')
        values = tuple(map(float, fields[4:]))
        if not all(math.isfinite(x) for x in values):
            raise ValueError('文字参数非有限数')
        found[fields[1]] = (fields[2], fields[3], *values)
    expected = {s['name']: (s['font'], s['bigFont'], float(s['heightMM']),
                          float(s['width']), float(s['fixedHeight']))
                for s in standard['finalTypographyPolicy']['styles']}
    if found != expected:
        raise ValueError('文字执行参数与当前强规则值不一致')
    return {'policySHA256': digest, 'styles': sorted(found), 'verified': True}


def module_compatibility(skill, module, mode):
    contract = typography_contract(skill)
    policy = read(skill / 'assets/runtime-capabilities.json')
    digest = hashlib.sha256(Path(module).read_bytes()).hexdigest().upper()
    entry = policy.get('modules', {}).get(digest)
    if not entry:
        return {'compatible': False, 'moduleSHA256': digest, 'mode': mode,
                'reason': '当前模块未有与现行规则对应的能力记录；不能沿用另一模块的通过结论。'}
    state = entry.get('modes', {}).get(mode, {})
    return {'compatible': state.get('compatible') is True, 'moduleSHA256': digest,
            'typographyContract': contract,
            'mode': mode, 'reason': state.get('reason', '仅按下列已核范围接用，仍须项目验收' if state.get('compatible') is True else '此模式尚未核实'),
            'scope': state.get('scope', ''), 'pending': entry.get('pending', [])}


def discover_workspace(explicit, cwd, skill):
    def supported(root):
        return any((root / name).is_file() for name in
                   ('host/controller/run_quiet_job.py', '阶段1/原生接口探测/run_quiet_job.py'))
    configured = explicit or os.environ.get('PLAN_DRAW_WORKSPACE')
    if configured:
        root = Path(configured).resolve()
        if supported(root):
            return root
        raise ValueError('指定的隔离宿主工作区无效，不切换其他项目：' + str(root))
    skill, cwd = Path(skill).resolve(), Path(cwd).resolve()
    candidates = (skill, cwd, *cwd.parents)
    for root in candidates:
        if (root / 'host/controller/run_quiet_job.py').is_file():
            return root
    location = skill / 'host/location.json'
    if location.is_file():
        value = read(location).get('workspaceRoot')
        if value and supported(Path(value)):
            return Path(value).resolve()
    for root in candidates:
        if supported(root):
            return root
    raise ValueError('未找到隔离宿主。使用实际发行包提供宿主，并通过--workspace-root或PLAN_DRAW_WORKSPACE指定；不能只复制Skill宣称安装完成。')


def legacy_worker_config(skill, workspace):
    location_file = skill / 'host/location.json'
    location = read(location_file) if location_file.is_file() else {}
    root = location.get('legacyWorkspaceRoot') or location.get('workspaceRoot')
    worker = location.get('legacyWorkerConfig')
    if root and Path(root).resolve() == workspace.resolve() and worker:
        return Path(worker).resolve()
    return None


def inspect_runtime(skill, workspace, mode='audit'):
    portable = (workspace / 'host/controller/run_quiet_job.py').is_file()
    controller = workspace / ('host/controller' if portable else '阶段1/原生接口探测')
    machine = workspace / 'host/machine.json' if portable else legacy_worker_config(skill, workspace)
    files = [controller / name for name in ('run_quiet_job.py', '启动隔离桌面宿主.ps1', 'IsolatedCadHostNative.cs')]
    missing = []
    if machine is None:
        missing.append('与当前工作区匹配的 host/location.json 中的 legacyWorkerConfig')
    else:
        files.append(machine)
    configured = machine is not None and machine.is_file()
    bridge = None
    expected_hash = None
    if portable:
        bridge = workspace / 'host/bridge/PlanBridge.arx'
        receipt = bridge.with_name('bridge.sha256')
        files.extend((bridge, receipt))
        if receipt.is_file():
            expected_hash = receipt.read_text(encoding='ascii').strip()
    if configured:
        config = read(machine)
        path_keys = ('template', 'acadPath', 'supportComponent') if portable else ('template',)
        keys = ['profile', *path_keys]
        if portable or mode in ('template-finalize', 'control-frames'):
            keys.append('profileRegistry')
        if not portable:
            keys.extend(('nonce', 'stagedARX', 'stagedINI', 'binarySHA256'))
        missing.extend(f'{machine}#{key}' for key in keys if not config.get(key))
        files.extend(Path(config[key]) for key in path_keys if config.get(key))
        if not portable:
            retired = machine.parent / 'staging-final-archive'
            if config.get('stagedARX'):
                bridge = retired / Path(config['stagedARX']).name
                files.append(bridge)
            if config.get('stagedINI'):
                files.append(retired / Path(config['stagedINI']).name)
            expected_hash = config.get('binarySHA256')
    missing.extend(str(path) for path in files if not path.is_file())
    if bridge and bridge.is_file() and expected_hash is not None:
        if hashlib.sha256(bridge.read_bytes()).hexdigest().lower() != expected_hash.lower():
            missing.append('桥接模块指纹不符：' + str(bridge))
    return {'ready': not missing, 'workspaceRoot': str(workspace), 'portableHost': portable,
            'workerConfig': str(machine) if configured else None, 'missing': missing,
            'scope': '文件与依赖路径检查；不证明CAD已初始化、原生命令执行或图纸验收通过'}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--workspace-root', type=Path)
    p.add_argument('--module', type=Path)
    p.add_argument('--mode', choices=('generate', 'revise', 'audit', 'template-finalize'), default='generate')
    p.add_argument('--module-only', action='store_true')
    args = p.parse_args(argv)
    skill = Path(__file__).resolve().parents[1]
    try:
        result = module_compatibility(skill, args.module or skill / 'tools/PlanNative.arx', args.mode)
        if not args.module_only:
            result['runtime'] = inspect_runtime(skill, discover_workspace(args.workspace_root, Path.cwd(), skill), args.mode)
        result['ready'] = result['compatible'] and result.get('runtime', {}).get('ready', True)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {'ready': False, 'error': str(error)}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result['ready'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
