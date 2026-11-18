"""Read-only geometry survey. Every proposed geometry is a candidate, never a write request."""
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
from hashlib import file_digest
from math import atan2, cos, floor, isfinite, radians, sin
from pathlib import Path
import json
import re
import shutil
import subprocess
from time import perf_counter

import ezdxf
from ezdxf.math import Matrix44, Vec3
from ezdxf.units import conversion_factor


@dataclass(frozen=True)
class Stroke:
    source: str
    start: Vec3
    end: Vec3


@dataclass(frozen=True)
class Label:
    source: str
    anchor: Vec3
    direction: Vec3
    height: float
    value: float
    tolerance: float


def fingerprint(path):
    with path.open("rb") as handle:
        return file_digest(handle, "sha256").hexdigest()


def walk(doc):
    objects, strokes, numbers = [], [], []
    layers = {layer.dxf.name.casefold(): layer for layer in doc.layers}

    def visit(entity, frame, parent, matrix, layer, hidden, ancestry, suffix=""):
        kind = entity.dxftype()
        path = "/".join(filter(None, (parent, (entity.dxf.handle or suffix) + (suffix if entity.dxf.handle else ""))))
        local_layer = entity.dxf.get("layer", "0")
        layer = layer if local_layer == "0" else local_layer
        settings = layers.get(layer.casefold())
        hidden = hidden or bool(entity.dxf.get("invisible", 0)) or (
            settings is not None and (settings.is_off() or settings.is_frozen()))
        row = {"layout": frame, "source": path, "type": kind, "layer": layer,
               "status": "hidden" if hidden else "unclassified"}
        objects.append(row)
        if hidden:
            return
        if kind == "INSERT":
            name = entity.dxf.name
            block = doc.blocks.get(name)
            if block is None:
                row["status"] = "missing-block-definition"
                return
            if block.block.is_xref or block.block.is_xref_overlay:
                row.update(status="external-reference", reference=block.block.dxf.get("xref_path", ""))
                return
            if len(ancestry) >= 20 or name.casefold() in ancestry:
                row["status"] = "unreadable-block-ancestry"
                return
            row["status"] = "block-instance"
            instances = entity.multi_insert() if entity.mcount > 1 else (entity,)
            for ordinal, instance in enumerate(instances):
                instance_path = path if entity.mcount == 1 else f"{path}[{ordinal}]"
                for i, attribute in enumerate(instance.attribs):
                    visit(attribute, frame, instance_path, matrix, layer, hidden, ancestry, f"@{i}" if attribute.dxf.handle is None else "")
                child_matrix = instance.matrix44() @ matrix
                for member in block:
                    if member.dxftype() != "ATTDEF":
                        visit(member, frame, instance_path, child_matrix, layer, hidden, ancestry | {name.casefold()})
            return
        if kind == "ACAD_PROXY_ENTITY":
            row["status"] = "proxy-unreadable"
            return
        # Retain every layout in the inventory. Drawing geometry tests use model coordinates.
        if frame != "Model":
            return
        if kind == "LINE":
            pairs = [(entity.dxf.start, entity.dxf.end)]
        elif kind == "LWPOLYLINE":
            coordinates = list(entity.vertices_in_wcs())
            bulges = list(entity.get_points("b"))
            pairs = [(a, b) for index, (a, b) in enumerate(zip(coordinates, coordinates[1:]))
                     if abs(bulges[index][0]) <= 1e-12]
            if entity.closed and len(coordinates) > 1 and abs(bulges[-1][0]) <= 1e-12:
                pairs.append((coordinates[-1], coordinates[0]))
        else:
            pairs = []
        for index, (start, end) in enumerate(pairs):
            a, b = matrix.transform(Vec3(start)), matrix.transform(Vec3(end))
            if all(isfinite(value) for value in (*a, *b)) and a.distance(b) > .00001:
                strokes.append(Stroke(f"{path}:{index}" if kind != "LINE" else path, a, b))
                row["status"] = "linework-read"
        if kind in {"TEXT", "ATTRIB"}:
            ocs = entity.ocs()
            text = entity.dxf.text.strip()
            if re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", text):
                direction = radians(entity.dxf.get("rotation", 0))
                anchor = matrix.transform(ocs.to_wcs(entity.get_placement()[1]))
                along = matrix.transform_direction(ocs.to_wcs(Vec3(cos(direction), sin(direction), 0)))
                normal = matrix.transform_direction(ocs.to_wcs(Vec3(-sin(direction), cos(direction), 0)))
                height = abs(entity.dxf.height) * normal.magnitude
                if all(isfinite(value) for value in (*anchor, *along, height)) and along.magnitude > 0 and height > 0:
                    precision = len(text.partition(".")[2])
                    value = float(text)
                    if isfinite(value) and value > 0:
                        numbers.append(Label(path, anchor, along.normalize(), height, value, .5 * 10**-precision))
                        row["status"] = "numeric-text-read"

    for layout in doc.layouts:
        for entity in layout:
            visit(entity, layout.name, "", Matrix44(), "0", False, set())
    return objects, strokes, numbers


class StrokeIndex:
    def __init__(self, strokes, cell):
        self.cell = cell
        self.buckets = defaultdict(list)
        self.long_strokes = []
        self.strokes = strokes
        for index, stroke in enumerate(strokes):
            xs, ys = self.cells(stroke.start, stroke.end)
            if len(xs) * len(ys) > 128:
                self.long_strokes.append(index)
                continue
            for x in xs:
                for y in ys:
                    self.buckets[x, y].append(index)

    def cells(self, a, b):
        return tuple(range(floor(min(v, w) / self.cell), floor(max(v, w) / self.cell) + 1)
                     for v, w in zip((a.x, a.y), (b.x, b.y)))

    def near(self, a, b):
        xs, ys = self.cells(a, b)
        if len(xs) * len(ys) > 256:
            return self.strokes
        ids = set(self.long_strokes)
        for x in xs:
            for y in ys:
                ids.update(self.buckets.get((x, y), ()))
        return [self.strokes[index] for index in sorted(ids)]


def wall_candidates(strokes, index, unit):
    candidates = []
    minimum_length = 800 / unit
    half_window = Vec3(250 / unit, 250 / unit, 0)
    for a in strokes:
        vec = a.end - a.start
        length = vec.magnitude
        if length < minimum_length or abs(vec.z) > .05 / unit:
            continue
        along = vec.normalize()
        normal = Vec3(-along.y, along.x, 0)
        low = Vec3(min(a.start.x, a.end.x), min(a.start.y, a.end.y))
        high = Vec3(max(a.start.x, a.end.x), max(a.start.y, a.end.y))
        for b in index.near(low - half_window, high + half_window):
            if a.source >= b.source:
                continue
            t1, t2 = b.start - a.start, b.end - a.start
            n1, n2 = t1.dot(normal), t2.dot(normal)
            separation = abs((n1 + n2) / 2)
            if abs(t1.z) > .05 / unit or abs(t2.z) > .05 / unit:
                continue
            if abs(n1 - n2) > 1 / unit or not any(abs(separation * unit - width) <= 2 for width in (100, 120, 200, 240)):
                continue
            x1, x2 = sorted((t1.dot(along), t2.dot(along)))
            lo, hi = max(0, x1), min(length, x2)
            if hi - lo < max(minimum_length, 4 * separation):
                continue
            center = normal * ((n1 + n2) / 4) + a.start
            ends = sorted((center + along * lo, center + along * hi), key=lambda p: (round(p.x, 6), round(p.y, 6)))
            candidates.append({"edges": [a.source, b.source], "centerline": [list(p) for p in ends],
                               "thicknessMM": round(separation * unit, 4), "allowReplacement": False})
    return candidates


def dimension_candidates(numbers, index, unit):
    candidates = []
    tolerance = 4 / unit
    for label in numbers:
        direction = label.direction
        if abs(direction.z) > 1e-5:
            continue
        normal = Vec3(-direction.y, direction.x)
        height = label.height
        if not 35 <= height * unit <= 1000:
            continue
        # Bound lookups to nearby ink and never equate a numeric string with a dimension.
        reach = min(max(label.value / unit * 1.2, height * 12), 50000 / unit)
        span = Vec3(abs(direction.x) * reach + abs(normal.x) * height * 8,
                    abs(direction.y) * reach + abs(normal.y) * height * 8)
        baselines, witnesses = [], []
        for s in index.near(label.anchor - span, label.anchor + span):
            a, b = s.start - label.anchor, s.end - label.anchor
            if max(abs(a.z), abs(b.z)) > tolerance:
                continue
            x1, x2 = a.dot(direction), b.dot(direction)
            y1, y2 = a.dot(normal), b.dot(normal)
            if abs(y1-y2) <= tolerance and abs(x1-x2) > tolerance and abs((y1+y2)/2) < height * 2:
                baselines.append((s.source, min(x1, x2), max(x1, x2), (y1+y2)/2))
            if abs(x1-x2) <= tolerance and abs(y1-y2) >= height * .55:
                witnesses.append((s.source, (x1+x2)/2, y1, y2))
        options = {}
        for _, low, high, level in baselines:
            if low > height * 2.5 or high < -height * 2.5:
                continue
            ends = sorted([(x, key, a, b) for key, x, a, b in witnesses
                           if min(abs(a-level), abs(b-level)) <= tolerance], key=lambda w: w[0])
            left = [w for w in ends if w[0] < -tolerance]
            right = [w for w in ends if w[0] > tolerance]
            if not left or not right:
                continue
            lo, lh, la, lb = left[-1]
            hi, rh, ra, rb = right[0]
            if (la+lb-2*level) * (ra+rb-2*level) <= 0 or hi-lo < height:
                continue
            occupied = sorted((max(lo,a), min(hi,b), key) for key,a,b,y in baselines
                              if abs(y-level)<=tolerance and b>lo and a<hi)
            if not occupied or occupied[0][0] > lo+tolerance:
                continue
            cursor = lo
            member_ids = []
            for start, end, key in occupied:
                if start > cursor+tolerance and not (-height*2.5 <= cursor < start <= height*3.5):
                    break
                cursor = max(cursor,end)
                member_ids.append(key)
            if cursor < hi-tolerance:
                continue
            measured = (hi-lo)*unit
            options[lh,rh,round(level*unit)] = {
                "source": label.source, "labelMM": label.value, "geometryMM": round(measured, 4),
                "witnesses": [lh,rh], "baselineStrokes": member_ids, "allowReplacement": False,
                "status": "measured-candidate" if abs(measured-label.value) <= label.tolerance+.02
                else "text-geometry-mismatch"}
        if len(options) == 1:
            candidates.append(next(iter(options.values())))
        elif options:
            candidates.append({"source": label.source, "labelMM": label.value,
                               "status": "ambiguous-dimension", "candidateCount": len(options),
                               "allowReplacement": False})
    return candidates


def inspect(path, unit_mm=None):
    path = Path(path)
    started = perf_counter()
    doc = ezdxf.readfile(path)
    if unit_mm is None:
        if not doc.units:
            raise ValueError("DXF unit is unspecified. Supply the verified mm per drawing unit.")
        unit_mm = conversion_factor(doc.units, 4)
    if not isfinite(unit_mm) or unit_mm <= 0:
        raise ValueError("The drawing unit conversion must be positive and finite.")
    objects, strokes, numbers = walk(doc)
    index = StrokeIndex(strokes, 2500/unit_mm)
    walls = wall_candidates(strokes, index, unit_mm)
    dimensions = dimension_candidates(numbers, index, unit_mm)
    return {"schema": "plan.source-preflight.v1", "readOnly": True,
            "sourceSHA256": fingerprint(path), "unitMM": unit_mm,
            "objectCounts": dict(Counter(row["status"] for row in objects)),
            "objects": objects, "wallCandidates": walls, "dimensionCandidates": dimensions,
            "scope": "Model-space candidate geometry. No building semantics, floor identity or write permission inferred.",
            "seconds": round(perf_counter()-started, 3)}


def inspect_dwg(source, console, job, unit_mm):
    job = job.resolve()
    job.mkdir(parents=True, exist_ok=False)
    original = fingerprint(source)
    copy = job / "control.dwg"
    shutil.copyfile(source, copy)
    if fingerprint(copy) != original:
        raise ValueError("The DWG copy does not match the source.")
    script = job / "read-only-export.scr"
    script.write_text('(setvar "FILEDIA" 0)\n(command "_.DXFOUT" (strcat (getvar "DWGPREFIX") "readback.dxf") "16")\n',
                      encoding="ascii")
    dxf = job / "readback.dxf"
    result = subprocess.run([str(console), "/i", str(copy), "/s", str(script), "/l", "en-US"],
                            cwd=job, capture_output=True, timeout=120,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    (job/"console.log").write_bytes(result.stdout+result.stderr)
    if result.returncode != 0 or not dxf.is_file() or not dxf.stat().st_size:
        raise ValueError("CAD DXF export failed. See the job console log.")
    report = inspect(dxf, unit_mm)
    if fingerprint(source) != original or fingerprint(copy) != original:
        raise ValueError("The source or copied DWG changed during inspection.")
    report["dxfReadbackSHA256"] = report["sourceSHA256"]
    report["sourceSHA256"] = original
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--unit-mm", type=float)
    parser.add_argument("--cad-console", type=Path)
    parser.add_argument("--job", type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Choose a new report path.")
    if args.source.suffix.lower() == ".dwg":
        if args.cad_console is None or args.job is None:
            parser.error("A DWG needs --cad-console and a new --job directory.")
        report = inspect_dwg(args.source.resolve(strict=True), args.cad_console.resolve(strict=True), args.job, args.unit_mm)
    elif args.source.suffix.lower() == ".dxf":
        report = inspect(args.source, args.unit_mm)
    else:
        parser.error("Choose a DWG or DXF.")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
