"""Run one native PLAN_DRAW attempt in a dedicated AutoCAD host."""

import importlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


PLAN_JOB_TIMEOUT_SECONDS = 1200


class JobError(RuntimeError):
    pass


class RetryableHostError(JobError):
    pass


def file_hash(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_json(path, data):
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(data, stream, ensure_ascii=False, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def require(condition, message):
    if not condition:
        raise JobError(message)


def verify_owned_host_closure(attempt, *, module=None, workspace=None):
    """Require an identity-bound normal terminal state before using any receipt."""
    attempt = Path(attempt)
    try:
        binding = read_json(attempt / 'host.json')
        session = Path(binding['session']).resolve()
        nonce = binding['nonce']
        require(isinstance(nonce, str) and len(nonce) == 32 and
                all(c in '0123456789abcdef' for c in nonce) and
                session.name == 'isolated-plan-portable-' + nonce[:8], '宿主终态身份无效')
        if workspace is not None:
            require(session.parent == (host_location(Path(workspace)).parent / 'runs').resolve(),
                    '宿主终态不属于本工作区')
        if module is not None:
            require(binding.get('moduleSHA256') == file_hash(module), '宿主终态模块指纹不符')
        launched = read_json(session / 'isolation-host-receipt.json')
        terminal = read_json(session / 'isolation-final.json')
        require(launched.get('contract') == 'stage1.isolated-host-launch.v1' and
                terminal.get('contract') == 'stage1.isolated-host-final.v1', '宿主终态契约不符')
        for record in (launched, terminal):
            require(record.get('nonce') == nonce and Path(record['session']).resolve() == session and
                    record.get('desktopName') == 'CodexS1_' + nonce, '宿主终态nonce/session/桌面身份不一致')
        require(type(launched.get('processId')) is int and launched['processId'] > 0 and
                terminal.get('processId') == launched['processId'], '宿主启动与退出进程身份不一致')
        true_fields = ('workerCreated', 'workerExited', 'readyIdentityConfirmed', 'nativeQuitConfirmed',
                       'monitorStartedBeforeWorker', 'monitorStarted', 'monitorStopped',
                       'eventFileWritten', 'desktopClosed', 'processHandlesClosed')
        false_fields = ('forcedTermination', 'timedOut', 'inputDesktopChanged',
                        'switchDesktopCalled', 'userCadTouched', 'stillRunningOwnedWorker')
        zero_fields = ('workerExitCode', 'inputDesktopReadErrors', 'droppedEvents',
                       'workerForegroundEvents', 'workerUserDesktopShowEvents', 'workerForegroundSamples')
        failures = [key for key in true_fields if terminal.get(key) is not True]
        failures += [key for key in false_fields if terminal.get(key) is not False]
        failures += [key for key in zero_fields if type(terminal.get(key)) is not int or terminal[key] != 0]
        if terminal.get('monitorError') not in (None, '') or terminal.get('supervisorErrors') != []:
            failures.append('monitor/supervisorErrors')
        # Events belonging to existing CAD processes are retained for attribution;
        # they are not attributed to this worker by this terminal-state check.
        require(not failures, '宿主未正常隔离退出：' + ', '.join(failures) +
                '；workerExitCode=' + str(terminal.get('workerExitCode')))
        return terminal
    except (JobError, OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        raise RetryableHostError(f'自有宿主终态不能复用业务回执：{error}') from error


def finish_owned_attempt(attempt, actions, *, primary_error=None, module=None, workspace=None):
    """Complete every cleanup action without replacing the original business error."""
    failures = []
    for action in actions:
        try:
            action()
        except Exception as error:
            failures.append(str(error))
    try:
        verify_owned_host_closure(attempt, module=module, workspace=workspace)
    except RetryableHostError as error:
        failures.append(str(error))
    if not failures:
        return
    message = '；'.join(failures)
    if primary_error is not None:
        if hasattr(primary_error, 'add_note'):
            primary_error.add_note('自有宿主清理/终态另有问题，原业务错误保持：' + message)
        try:
            write_json(Path(attempt) / 'host-cleanup-errors.json',
                       {'primaryError': str(primary_error), 'cleanupErrors': failures})
        except (OSError, ValueError):
            pass
        return
    raise RetryableHostError(message)


def ps_command(script, *arguments):
    return ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script), *map(str, arguments)]


def host_location(workspace):
    packaged = workspace / 'host/controller'
    if (packaged / 'run_quiet_job.py').is_file():
        return packaged
    return workspace / '阶段1/原生接口探测'


def packaged_host_inputs(workspace):
    if host_location(workspace) != workspace / 'host/controller':
        return ()
    host = workspace / 'host'
    setup = host / 'machine.json'
    require(setup.is_file(), '隔离宿主缺少 host/machine.json。请使用实际宿主发行包的配置入口；本Skill不附安装器。')
    return (setup, host / 'bridge/PlanBridge.arx', host / 'bridge/bridge.sha256',
            *(host / 'controller' / name for name in
              ('run_quiet_job.py', '启动隔离桌面宿主.ps1', 'IsolatedCadHostNative.cs')))


def bridge_ini_text(host, session, nonce, configuration, tool):
    # The bridge's public entry requires its diagnostic input paths under this
    # name. PLAN_DRAW reads the source copy and packaged drawing resources.
    snapshot = host.parent / '阶段0/输入快照'
    return (f'[probe]\nroot={host}\nscheme={snapshot / "scheme.dwg"}\n'
            f'reference={snapshot / "reference.dwg"}\noutput={session}\n'
            f'[worker]\nnonce={nonce}\nsession={session}\nexpectedProfile={configuration["profile"]}\n'
            f'supportComponent={configuration["supportComponent"]}\nearlyNativeAutoload=1\n'
            f'[stage2]\nmodule={tool}\n')


def trusted_only_worker_profile(configuration):
    """Use only the installed trusted paths in a separate CAD worker profile."""
    import winreg
    root = configuration['profileRegistry']
    original = configuration['profile']
    require(original.startswith('CodexS1Native_') and '\\' not in original and '/' not in original,
            '隔离配置名称无效')
    profile = original + '_PlanTrusted'
    parent = winreg.HKEY_CURRENT_USER
    source = root + '\\' + original
    target = root + '\\' + profile
    original_values = source + r'\Variables'
    with winreg.OpenKey(parent, original_values) as values:
        trusted = winreg.QueryValueEx(values, 'TRUSTEDPATHS')[0]
        security, kind = winreg.QueryValueEx(values, 'SECURELOAD')
        require(kind in (winreg.REG_SZ, winreg.REG_DWORD) and str(security) in ('1', '2'),
                '安装配置的安全加载策略无效')
    def copy_settings(old, new):
        with winreg.OpenKey(parent, old) as src, winreg.CreateKeyEx(parent, new, 0, winreg.KEY_WRITE) as dst:
            children, value_count, _ = winreg.QueryInfoKey(src)
            for i in range(value_count):
                name, value, kind = winreg.EnumValue(src, i)
                winreg.SetValueEx(dst, name, 0, kind, value)
            for i in range(children):
                name = winreg.EnumKey(src, i)
                copy_settings(old + '\\' + name, new + '\\' + name)
    # This profile belongs to the installed worker. Never broaden TRUSTEDPATHS
    # or approve unsigned applications registered by unrelated CAD tools.
    try:
        with winreg.OpenKey(parent, target) as dest:
            require(winreg.QueryValueEx(dest, 'PlanDrawSourceProfile')[0] == original,
                    '同名 CAD 配置已有其他所有者')
    except FileNotFoundError:
        try:
            with winreg.OpenKey(parent, target):
                raise JobError('专用 CAD 配置已存在且缺少来源标记')
        except FileNotFoundError:
            copy_settings(source, target)
            with winreg.OpenKey(parent, target, 0, winreg.KEY_SET_VALUE) as dest:
                winreg.SetValueEx(dest, 'PlanDrawSourceProfile', 0, winreg.REG_SZ, original)
    with winreg.OpenKey(parent, target + r'\Variables', 0, winreg.KEY_QUERY_VALUE | winreg.KEY_SET_VALUE) as values:
        require(winreg.QueryValueEx(values, 'TRUSTEDPATHS')[0] == trusted,
                '测试配置的受信目录与安装配置不同，请核对专用宿主')
        winreg.SetValueEx(values, 'SECURELOAD', 0, kind, '2' if kind == winreg.REG_SZ else 2)
        actual, actual_kind = winreg.QueryValueEx(values, 'SECURELOAD')
        require(str(actual) == '2' and actual_kind == kind, '专用宿主的安全加载策略未生效')
    return profile


def restore_default_profile(session):
    configuration = read_json(session / 'worker-config.json')
    registry = configuration.get('profileRegistry')
    previous = configuration.get('previousDefaultProfile')
    if not registry or not previous or not (session / 'isolation-final.json').is_file():
        return
    import winreg

    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry, 0, winreg.KEY_QUERY_VALUE | winreg.KEY_SET_VALUE) as key:
        current, kind = winreg.QueryValueEx(key, '')
        if current == configuration['profile']:
            winreg.SetValueEx(key, '', 0, kind, previous)


def install_template_plotters(resources, configuration, session):
    """Install named resources before CAD caches its printer/style lists."""
    import winreg
    manifest = read_json(resources / 'plotters/manifest.json')
    profile = configuration['profileRegistry'] + '\\' + configuration['profile'] + '\\General'
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, profile) as key:
        directories = {suffix: Path(os.path.expandvars(winreg.QueryValueEx(key, setting)[0])) for suffix, setting in
                       (('.pc3', 'PrinterConfigDir'), ('.ctb', 'PrinterStyleSheetDir'))}
    installed = []
    for name, digest in manifest['files'].items():
        source = resources / 'plotters' / name
        require(source.name == name and source.suffix in directories and file_hash(source) == digest,
                '打印资源名称、类型或指纹不符合随包清单')
        folder = directories[source.suffix].resolve(strict=True)
        target = folder / name
        require(target.parent == folder, '打印资源目标超出指定配置目录')
        state_path = folder / '.plan-template-managed.json'
        managed = read_json(state_path) if state_path.exists() else {}
        existed = target.exists()
        if existed and file_hash(target) != digest:
            previous = file_hash(target)
            require(managed.get(name) == previous, f'同名打印配置内容不同且非本工具登记，保留原件：{target}')
            shutil.copy2(target, session / (name + '.before'))
            with target.open('wb') as stream:
                stream.write(source.read_bytes())
        elif not existed:
            # Exclusive creation protects a concurrently installed configuration.
            with target.open('xb') as stream:
                stream.write(source.read_bytes())
        require(file_hash(target) == digest, '打印资源落盘指纹不一致')
        managed[name] = digest
        write_json(state_path, managed)
        installed.append({'file': str(target), 'sha256': digest, 'alreadyPresent': existed})
    write_json(session / 'template-plot-resources.json', {'files': installed, 'unmanagedFilesOverwritten': False})


def await_file(path, launcher, seconds, idle=None):
    deadline = time.monotonic() + seconds
    while True:
        if path.is_file():
            try:
                return read_json(path)
            except (OSError, ValueError):
                pass  # The writer may still be flushing the report.
        if launcher.poll() is not None:
            raise RetryableHostError(f'自有 CAD 宿主提前退出：{path.parent}')
        if time.monotonic() >= deadline:
            raise RetryableHostError(f'等待 {path.name} 超时：{path.parent}')
        if idle is not None:
            idle()
        time.sleep(.25)


def retire_staging(session, staged_files, nonce):
    final = session / 'isolation-final.json'
    if not final.is_file():
        return
    closure = read_json(final)
    if (closure.get('nonce') != nonce or str(session) != closure.get('session') or
            (closure.get('workerExited') is not True and closure.get('workerCreated') is not False)):
        return
    staging = staged_files[0].parent
    archive = session / 'staging-final-archive'
    archive.mkdir()
    for item in staged_files:
        require(item.resolve().parent == staging.resolve(), '暂存路径离开自有宿主范围')
        shutil.move(item, archive / item.name)
    staging.rmdir()


def close_worker(paths, launcher, native, ready, clean_material, nonce):
    session, supervisor = paths
    if launcher.poll() is not None:
        return
    if ready is not None:
        try:
            clean = session / 'clean'
            clean.mkdir()
            shutil.copyfile(clean_material, clean / 'control.dwg')
            native.dispatch(session, ready, 'S1OPENPROBECONTROL', clean, 'native-open.done', 45)
            if (session / 'template-plot-resources.json').is_file():
                config = '[plan]\nmode=owned-cleanup\nsource=' + str(clean / 'control.dwg') + '\nresources=' + str(clean_material.parent) + '\nreport=' + str(clean) + '\n'
                (clean / 'plan.ini').write_text(config, 'utf-16')
                native.dispatch(session, ready, 'S1PHASE2EXECUTE', clean, 'owned-documents-closed.json', 60)
                require(read_json(clean / 'owned-documents-closed.json').get('completed') is True,
                        '原生模块卸载前未能关闭自有业务文档')
            unload = session / 'unload'
            unload.mkdir()
            (unload / 'phase2-product-unload.only').touch()
            native.dispatch(session, ready, 'S1PHASE2EXECUTE', unload, 'native-product-unload.json', 30)
            require(read_json(unload / 'native-product-unload.json').get('unloaded') is True, '精确原生卸载失败')
            native.request_quit(session, ready, 35, 50)
            launcher.wait(12)
            return
        except (JobError, OSError, TimeoutError, RuntimeError, subprocess.SubprocessError) as error:
            write_json(session / 'owned-quit-recovery.json', {'nonce': nonce, 'error': str(error)})
    # The existing supervisor's explicit recovery verifies the original PID, creation time,
    # executable and desktop immediately before stopping any process.
    receipt = session / 'isolation-host-receipt.json'
    if not receipt.is_file() or launcher.poll() is not None:
        return
    try:
        stop = ps_command(supervisor, '-RunDirectory', session, '-ExpectedNonce', nonce, '-StopOwnedWorker')
        with (session / 'owned-stop.log').open('wb') as log:
            subprocess.run(stop, check=True, timeout=35, stdout=log, stderr=subprocess.STDOUT,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        launcher.wait(12)
    except (OSError, subprocess.SubprocessError) as error:
        write_json(session / 'owned-stop-error.json', {'nonce': nonce, 'error': str(error)})


class AttachedWorker:
    def __init__(self, native, pid):
        self.native, self.pid = native, pid

    def poll(self):
        return None if self.native.process_alive(self.pid) else 0

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while self.poll() is None:
            if time.monotonic() > deadline:
                raise subprocess.TimeoutExpired(f'owned CAD PID {self.pid}', timeout)
            time.sleep(.2)


def resume_owned_plan(workspace, module, attempt, resources):
    binding = attempt / 'host.json'
    if not binding.is_file():
        raise RetryableHostError('续跑缺少自有宿主身份记录')
    prior = read_json(binding)
    session = Path(prior['session']).resolve()
    nonce = prior['nonce']
    host = host_location(workspace)
    runs = (host.parent / 'runs').resolve()
    require(session.parent == runs and session.name == f'isolated-plan-portable-{nonce[:8]}' and
            prior['moduleSHA256'] == file_hash(module), '保存点中的自有宿主来源与模块不一致')
    receipt = session / 'isolation-host-receipt.json'
    if not receipt.is_file() or (session / 'isolation-final.json').is_file():
        verify_owned_host_closure(attempt, module=module, workspace=workspace)
        return
    ownership = read_json(receipt)
    require(ownership.get('contract') == 'stage1.isolated-host-launch.v1' and ownership['nonce'] == nonce and
            ownership['session'] == str(session) and ownership['desktopName'] == f'CodexS1_{nonce}', '保存点的私有桌面身份不一致')
    pid = int(ownership['processId'])
    identity_probe = (f'$p=Get-Process -Id {pid} -ErrorAction SilentlyContinue; if(!$p){{exit 0}}; '
                      '[pscustomobject]@{executable=$p.Path; creationFileTime=$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}'
                      '|ConvertTo-Json -Compress')
    reply = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', identity_probe],
                                    creationflags=subprocess.CREATE_NO_WINDOW, encoding='utf-8')
    if not reply.strip():
        verify_owned_host_closure(attempt, module=module, workspace=workspace)
        return
    identity = json.loads(reply)
    if identity != {key: ownership[key] for key in ('executable', 'creationFileTime')}:
        verify_owned_host_closure(attempt, module=module, workspace=workspace)
        return  # PID has since been reused. This process is outside the saved attempt.
    sys.path.insert(0, str(host))
    native = importlib.import_module('run_quiet_job')
    launcher = AttachedWorker(native, pid)
    marker = session / 'quiet-worker-ready.json'
    ready = read_json(marker) if marker.is_file() else None
    if ready is not None:
        require(ready.get('nonce') == nonce and ready.get('processId') == pid and ready.get('session') == str(session) and
                ready.get('desktop') == ownership['desktopName'], '续跑时自有 CAD 就绪身份不一致')
    try:
        if (session / 'native-dispatch.intent.json').is_file():
            await_file(attempt / 'job/product-result.json', launcher, PLAN_JOB_TIMEOUT_SECONDS)
    finally:
        def retire():
            cfg = read_json(session / 'worker-config.json')
            bridge = Path(cfg['stagedARX'])
            tool = bridge.parent / f'PlanNative_{nonce[:8]}.arx'
            retire_staging(session, (bridge, tool, bridge.with_suffix('.ini'), tool.with_suffix('.ini')), nonce)
        finish_owned_attempt(attempt, (
            lambda: close_worker((session, host / '启动隔离桌面宿主.ps1'), launcher, native, ready,
                                 resources / 'reference.dwg', nonce),
            retire, lambda: restore_default_profile(session)),
            primary_error=sys.exc_info()[1], module=module, workspace=workspace)


def run_owned_plan(workspace, module, source, attempt, configuration):
    from runtime_preflight import inspect_runtime
    runtime = inspect_runtime(Path(__file__).resolve().parents[1], workspace, configuration.get('mode'))
    require(runtime['ready'], '隔离宿主依赖不完整：' + '，'.join(runtime['missing']))
    host = host_location(workspace)
    sys.path.insert(0, str(host))
    native = importlib.import_module('run_quiet_job')
    supervisor = host / '启动隔离桌面宿主.ps1'
    portable = bool(packaged_host_inputs(workspace))
    if portable:
        import winreg

        frozen = read_json(host.parent / 'machine.json')
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, frozen['profileRegistry']) as key:
            frozen['previousDefaultProfile'] = winreg.QueryValue(key, None)
        if frozen.get('planTrustedLocationsOnly') is True:
            frozen['profile'] = trusted_only_worker_profile(frozen)
        existing_bridge = host.parent / 'bridge/PlanBridge.arx'
        frozen = dict(frozen, binarySHA256=(existing_bridge.parent / 'bridge.sha256').read_text('ascii').strip(),
                      earlyNativeAutoload=True)
        require(all(Path(frozen[key]).is_file() for key in ('acadPath', 'template', 'supportComponent')),
                '本机 CAD、模板或天正组件路径已失效，请重新运行安装脚本')
    else:
        configuration_path = Path(runtime['workerConfig'])
        template = configuration_path.parent
        frozen = read_json(configuration_path)
        retired = template / 'staging-final-archive'
        existing_bridge = retired / Path(frozen['stagedARX']).name
    require(file_hash(existing_bridge) == frozen['binarySHA256'], '隔离宿主桥接模块指纹不一致')

    nonce = uuid.uuid4().hex
    session = host.parent / 'runs' / f'isolated-plan-portable-{nonce[:8]}'
    session.mkdir(parents=True)
    (session / 'queue').mkdir()
    if configuration.get('mode') in ('template-finalize', 'control-frames'):
        install_template_plotters(Path(configuration['resources']), frozen, session)
    attempt.mkdir()
    report = attempt / 'job'
    report.mkdir()
    work = session / 'case-1'
    work.mkdir()
    control = work / 'control.dwg'
    original = file_hash(source)
    shutil.copyfile(source, control)
    require(file_hash(control) == original, '隔离副本与源 DWG 不一致')
    settings = {**configuration, 'source': str(control), 'output': str(report / 'candidate.dwg'), 'report': str(report)}
    require(all('\r' not in value and '\n' not in value for value in settings.values()), '作业参数不能包含换行')
    (work / 'plan.ini').write_text('[plan]\n'+''.join(f'{k}={v}\n' for k,v in settings.items()), encoding='utf-16')
    write_json(attempt / 'host.json', {'session': str(session), 'nonce': nonce, 'moduleSHA256': file_hash(module)})

    staging = Path(frozen.get('stagingRoot', 'C:/Program Files/Autodesk/ApplicationPlugins')) / f'CodexStage1Probe_{nonce}'
    staging.mkdir(parents=True)
    staged_bridge = staging / f'S1Acad_{nonce[:8]}.arx'
    staged_tool = staging / f'PlanNative_{nonce[:8]}.arx'
    bridge_ini = staged_bridge.with_suffix('.ini')
    tool_ini = staged_tool.with_suffix('.ini')
    shutil.copyfile(existing_bridge, staged_bridge)
    shutil.copyfile(module, staged_tool)
    if portable:
        ini = bridge_ini_text(host.parent, session, nonce, frozen, staged_tool)
    else:
        old_nonce = frozen['nonce']
        ini = (retired / Path(frozen['stagedINI']).name).read_text(encoding='utf-16')
        ini = ini.replace(str(template), str(session)).replace(old_nonce, nonce).replace(f'BCP2_{old_nonce[:8]}.arx', staged_tool.name)
    bridge_ini.write_text(ini, encoding='utf-16')
    tool_ini.write_text(f'[stage2]\nnonce={nonce}\n', encoding='utf-16')
    config = {**frozen, 'nonce': nonce, 'session': str(session), 'stagingDirectory': str(staging),
              'stagedARX': str(staged_bridge), 'stagedINI': str(bridge_ini)}
    write_json(session / 'worker-config.json', config)
    boot = session / 'boot.scr'
    boot.write_bytes(f'_.ARX\r\n_L\r\n"{staged_bridge.as_posix()}"\r\n_.ARX\r\n_L\r\n"{staged_tool.as_posix()}"\r\nS1QUIETSTART\r\n'.encode('mbcs'))
    marker = session / 'quiet-worker-ready.json'
    launch = ps_command(supervisor, '-RunDirectory', session, '-BootScriptPath', boot, '-Template', config['template'],
                        '-Profile', config['profile'], '-EarlyNativeModule', staged_bridge, '-Nonce', nonce,
                        '-ReadyMarkerName', marker.name, '-TimeoutSeconds', PLAN_JOB_TIMEOUT_SECONDS + 180,
                        '-CloseOwnStartupTips')
    if portable:
        launch += ['-AcadPath', config['acadPath']]
    flag = subprocess.CREATE_NO_WINDOW
    ready, font_prompt = None, None
    update_tip = Path(__file__).resolve().parents[1] / 'tools/ack-owned-update-failure.ps1'
    if not update_tip.is_file():
        update_tip = workspace / '阶段2/项目/罗朝辉_按规则整理_20261110/ack-update-failure.ps1'
    next_update_probe = time.monotonic() + 10

    def acknowledge_own_update_tip():
        nonlocal next_update_probe
        if (time.monotonic() >= next_update_probe and update_tip.is_file() and
                (session / 'isolation-host-receipt.json').is_file()):
            next_update_probe = time.monotonic() + 15
            with (session / 'owned-startup-update-tip.log').open('ab') as log:
                try:
                    subprocess.run(ps_command(update_tip, '-Session', session), timeout=10, creationflags=flag,
                                   stdout=log, stderr=subprocess.STDOUT)
                except subprocess.TimeoutExpired:
                    log.write(b'Private startup tip check timed out.\n')

    with (session / 'launcher.log').open('wb') as log:
        launcher = subprocess.Popen(launch, stdout=log, stderr=subprocess.STDOUT, creationflags=flag)
        try:
            marker_data = await_file(marker, launcher, 145, acknowledge_own_update_tip)
            require(marker_data.get('nonce') == nonce and marker_data.get('session') == str(session) and
                    marker_data.get('desktop') == f'CodexS1_{nonce}' and marker_data.get('inputDesktop') and
                    marker_data['inputDesktop'] != marker_data['desktop'] and not marker_data.get('error'),
                    '自有 CAD 就绪身份未通过核对')
            ready = marker_data
            policy = f"(Get-Process -Id {int(ready['processId'])}).Modules | ForEach-Object {{$_.ModuleName}}"
            modules = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', policy],
                                              creationflags=flag).lower().split()
            require(staged_tool.name.lower().encode('ascii') in modules, '预期的 PLAN_DRAW 模块尚未加载')
            require(not any(name.endswith(b'.arx') and name.startswith((b'bcp', b'buildingplanplugin2020'))
                            for name in modules), '自有宿主加载了额外的 BCP 模块')
            watcher = Path(__file__).resolve().parents[1] / 'tools/continue-owned-font-prompt.ps1'
            if not watcher.is_file():
                watcher = workspace / '阶段2/项目/02_同侧轴圈联合排版_20261112/continue-own-source-font-prompt.ps1'
            if watcher.is_file():
                font_prompt = subprocess.Popen(ps_command(watcher, '-Session', session, '-Watch'),
                                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flag)
            native.dispatch(session, ready, 'S1OPENPROBECONTROL', work, 'native-open.done', 85)
            write_json(session / 'native-dispatch.intent.json', {'nonce': nonce, 'sourceSHA256': original})
            native.dispatch(session, ready, 'S1PHASE2EXECUTE', work, 'stage2-execute.done', PLAN_JOB_TIMEOUT_SECONDS)
            native_error = report / 'plan-native-error.json'
            if native_error.is_file():
                raise JobError(f'PLAN_DRAW：{read_json(native_error).get("error", "原生执行失败")}')
            require(file_hash(control) == file_hash(source) == original, '运行前后的源 DWG 指纹不一致')
            if configuration.get('mode') == 'audit':
                audit = await_file(report / 'plan-native-audit.json', launcher, PLAN_JOB_TIMEOUT_SECONDS)
                require(audit.get('sourceSHA256') == original and audit.get('drawingWritten') is False and
                        not (report / 'candidate.dwg').exists(), '只读复核源指纹或写盘状态异常')
            else:
                receipt = await_file(report / 'product-result.json', launcher, PLAN_JOB_TIMEOUT_SECONDS)
                if receipt.get('completed') is not True:
                    raise JobError(f'PLAN_DRAW：{receipt.get("error", "原生执行失败")}')
        except TimeoutError as error:
            raise RetryableHostError(str(error)) from error
        except RuntimeError as error:
            if str(error).startswith('only the owned worker exited'):
                raise RetryableHostError(str(error)) from error
            if isinstance(error, JobError):
                raise
            raise JobError(str(error)) from error
        finally:
            def stop_font_helper():
                if font_prompt is not None and font_prompt.poll() is None:
                    font_prompt.terminate()
                    try:
                        font_prompt.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        write_json(session / 'owned-font-helper-end-timeout.json', {'nonce': nonce})
                        raise
            finish_owned_attempt(attempt, (
                lambda: close_worker((session, supervisor), launcher, native, ready,
                                     Path(configuration['resources']) / 'reference.dwg', nonce),
                stop_font_helper,
                lambda: retire_staging(session, (staged_bridge, staged_tool, bridge_ini, tool_ini), nonce),
                lambda: restore_default_profile(session)),
                primary_error=sys.exc_info()[1], module=module, workspace=workspace)
