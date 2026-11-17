"""Build the template PDF/CTB resources without changing installed CAD profiles."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import zlib

from ezdxf.addons import acadctb


def build(source_pc3: Path, output: Path, base_ctb: Path):
    output.mkdir(parents=True, exist_ok=True)
    original = source_pc3.read_bytes()
    if not original.startswith(b'PIAFILEVERSION_2.0,PC3VER1,compress'):
        raise ValueError('Not an Autodesk compressed PC3')
    content = zlib.decompress(original[60:])
    for key, value in ((b'All_As_Geometry', b'TRUE'), (b'View_New_File', b'FALSE')):
        content, count = re.subn(rb'(name="' + key + rb'\s+value=)(TRUE|FALSE)', rb'\g<1>' + value, content)
        if count != 1:
            raise ValueError(f'Missing or repeated PDF option: {key!r}')
    content = content.replace(b'truetype_as_text=TRUE', b'truetype_as_text=FALSE')
    compressed = zlib.compress(content)
    pc3 = output / 'FY-XS-2026.9-PDF.pc3'
    pc3.write_bytes(original[:48] + struct.pack('<III', zlib.adler32(compressed), len(content), len(compressed)) + compressed)
    assert zlib.decompress(pc3.read_bytes()[60:]) == content
    base_bytes = base_ctb.read_bytes()
    ctb = acadctb.load(base_ctb)
    content_ctb = zlib.decompress(base_bytes[60:]).decode('ascii').rstrip('\0')
    standard = json.loads((output.parent / 'annotation-template/standard.json').read_text('utf-8'))
    indices = {}
    for layer in standard['layers']:
        width = layer.get('lineweightA3A4MM')
        if width is not None and layer['aci'] != 254:
            indices[layer['aci']] = next(i + 1 for i, value in enumerate(ctb.lineweights) if abs(value - width) < 1e-6)
    # Autodesk native CTB pen indices are 1-based; zero means object weight.
    # Retain Autodesk's proven CTB serialization, palette and 27-entry pen table.
    # float32 equality must not append one new lineweight for every layer.
    def pen(match):
        index, body = int(match[1]), match[2]
        body = re.sub(r'(?m)^  lineweight=\d+', '  lineweight=' + str(indices.get(index + 1, 0)), body)
        body = re.sub(r'(?m)^  screen=\d+', '  screen=' + ('70' if index == 253 else '100'), body)
        return f' {index}{{\n{body}\n }}'
    content_ctb, changed = re.subn(r'(?m)^ (\d+)\{\n(.*?)\n \}', pen, content_ctb, flags=re.S)
    if changed != 255:
        raise ValueError('Expected all 255 Autodesk color-dependent pens')
    ctb_path = output / 'FY-XS-2026.9-A3 A4.ctb'
    compressed_ctb = zlib.compress(content_ctb.encode('ascii'))
    ctb_path.write_bytes(base_bytes[:48] + struct.pack('<III', zlib.adler32(compressed_ctb), len(content_ctb), len(compressed_ctb)) + compressed_ctb)
    check = acadctb.load(ctb_path)
    assert check[254].screen == 70 and check[7].screen == 100
    indices = {}
    for layer in standard['layers']:
        width = layer.get('lineweightA1MM')
        if width is not None and layer['aci'] not in (7, 254):
            indices[layer['aci']] = next(i + 1 for i, value in enumerate(ctb.lineweights) if abs(value - width) < 1e-6)
    # ACI7 has different A1 weights by purpose: use actual layer/viewport weights.
    a1_content, changed = re.subn(r'(?m)^ (\d+)\{\n(.*?)\n \}', pen,
                                 zlib.decompress(base_bytes[60:]).decode('ascii').rstrip('\0'), flags=re.S)
    if changed != 255:
        raise ValueError('Incomplete A1 pen table')
    a1_compressed = zlib.compress(a1_content.encode('ascii'))
    a1_path = output / 'FY-XS-2026.9-A1.ctb'
    a1_path.write_bytes(base_bytes[:48] + struct.pack('<III', zlib.adler32(a1_compressed), len(a1_content), len(a1_compressed)) + a1_compressed)
    manifest = {
        'schema': 'plan.template-plotters.v1',
        'allFontsAsGeometry': True,
        'lineweight': 'template exact CTB widths; unspecified colors use object/layer',
        'furniture': 'ACI254, black, 70 percent screening',
        'otherACI254Objects': 'explicit ACI7 to retain black table/frame printing',
        'files': {p.name: hashlib.sha256(p.read_bytes()).hexdigest().upper() for p in (pc3, ctb_path, a1_path)},
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    rows = ['PLANTEMPLATELAYERS1']
    for layer in standard['layers']:
        widths = [layer.get(key) for key in ('lineweightA3A4MM', 'lineweightA1MM')]
        rows.append(f"{layer['role']} {json.dumps(layer['name'], ensure_ascii=False)} {layer['aci']} " +
                    ' '.join(str(round(w * 100)) if w is not None else '-1' for w in widths))
    (output / 'layers.tsv').write_text('\n'.join(rows) + '\nEND\n', 'utf-8')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source_pc3', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('base_ctb', type=Path)
    args = parser.parse_args()
    print(json.dumps(build(args.source_pc3, args.output, args.base_ctb), ensure_ascii=False))
