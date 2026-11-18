"""Build a portable PLAN_DRAW ZIP from explicit native binaries and current rules."""

import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[2]
SKILL = ROOT / 'skills/architectural-plan-automation'
CONTROLLER = ROOT / '阶段1/原生接口探测'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(output, native_module, bridge, prepared_assets=None):
    revision_manifest = json.loads((SKILL / 'package-manifest.json').read_text('utf-8-sig'))
    members = {name: (SKILL / name).read_bytes() for name in revision_manifest
               if name != 'tools/PlanNative.arx'}
    members['tools/PlanNative.arx'] = native_module.read_bytes()
    if prepared_assets is not None:
        for name in ('reference.dwg', 'axes.dwg', 'floor-datum-style.dwg'):
            members[f'assets/{name}'] = (prepared_assets / name).read_bytes()
        axes_digest = sha(members['assets/axes.dwg']).upper()
        axes_index = (prepared_assets / 'axes-index-body.tsv').read_bytes().replace(b'\r\n', b'\n')
        members['assets/axes.tsv'] = f'S2AXISTEMPLATES1 {axes_digest}\n'.encode('ascii') + axes_index
    members['tools/Install-PlanDraw.ps1'] = (SKILL / 'tools/Install-PlanDraw.ps1').read_bytes()
    members['README.md'] = (SKILL / 'references/portable-install.md').read_bytes()
    members['host/bridge/PlanBridge.arx'] = bridge.read_bytes()
    members['host/bridge/bridge.sha256'] = (sha(members['host/bridge/PlanBridge.arx']).upper() + '\n').encode('ascii')
    for name in ('run_quiet_job.py', '启动隔离桌面宿主.ps1', 'IsolatedCadHostNative.cs'):
        members[f'host/controller/{name}'] = (CONTROLLER / name).read_bytes()
    for name in ('tools/PlanNative.arx', 'host/bridge/PlanBridge.arx'):
        if not members[name].startswith(b'MZ'):
            raise ValueError(f'Invalid native Windows binary: {name}')
    master = json.loads(members['assets/plan-policy/all-plan-rules.json'].decode('utf-8-sig'))
    policy = master['dimensionStylePolicy']
    template_mode = policy.get('mode') == 'native-template-clone-stretch-only'
    if (policy['textStyleDefinition']['fontFile'] != 'simplex.shx' or
            policy['textStyleDefinition']['bigFontFile'] != 'gbcbig.shx' or
            not policy['verifyActualSegmentTextStyle'] or
            (not template_mode and not policy.get('restoreAlteredSourceTextStyle'))):
        raise ValueError('Missing current mandatory dimension typography')
    if template_mode:
        standard = json.loads(members['assets/annotation-template/standard.json'].decode('utf-8-sig'))
        operations = standard['operations']
        if (standard != master['annotationTemplatePolicy'] or
                operations.get('allowSourceStyleOverwrite') is not False or
                operations.get('allowExplodedReplacement') is not False or
                not {'clone-native-object', 'stretch-native-grips'} <= set(operations['dimensionAndAxis']) or
                sha(members['assets/' + policy['templateAsset']]) != policy['templateSHA256'].lower()):
            raise ValueError('Missing or inconsistent native annotation template')
        plotters = json.loads(members['assets/plotters/manifest.json'].decode('utf-8-sig'))
        if (plotters.get('allFontsAsGeometry') is not True or
                set(plotters['files']) != {'FY-XS-2026.9-PDF.pc3', 'FY-XS-2026.9-A3 A4.ctb', 'FY-XS-2026.9-A1.ctb'} or
                any(sha(members['assets/plotters/' + name]) != digest.lower()
                    for name, digest in plotters['files'].items())):
            raise ValueError('Missing or inconsistent template print resources')
        runtime = json.loads(members['assets/template-runtime.json'].decode('utf-8-sig'))
        module_hash = sha(members['tools/PlanNative.arx'])
        records = [runtime, runtime.get('controlFrameRevision', {}),
                   runtime.get('mandatoryTypographyRevision', {})]
        binding = next((record for record in records
                        if record.get('moduleSHA256', '').lower() == module_hash), None)
        if binding is None:
            raise ValueError('Runtime record does not match the packaged ARX')
        runtime['packagedModuleSHA256'] = module_hash.upper()
        runtime['packagedValidationScope'] = binding.get('scope', runtime['scope'])
        for project in runtime['projects']:
            project.pop('nativeAudit', None)  # Machine-local verification path is not portable.
        members['assets/template-runtime.json'] = (json.dumps(runtime, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    manifest = {name: sha(data) for name, data in sorted(members.items())}
    members['package-manifest.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, 'x', compression=ZIP_DEFLATED) as archive:
        for name, data in sorted(members.items()):
            archive.writestr('architectural-plan-automation/' + name, data)
    with ZipFile(output) as archive:
        for name, expected in manifest.items():
            if sha(archive.read('architectural-plan-automation/' + name)) != expected:
                raise ValueError(f'Archive readback mismatch: {name}')
    return sha(output.read_bytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--native-module', required=True, type=Path)
    parser.add_argument('--bridge', required=True, type=Path)
    parser.add_argument('--prepared-assets', type=Path,
                        help='native style-only exports for reference, axes and floor datums')
    args = parser.parse_args()
    print(build(args.output, args.native_module, args.bridge, args.prepared_assets))
