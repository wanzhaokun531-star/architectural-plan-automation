"""Read actual PDF output and plan text-grip corrections in drawing units."""

import hashlib
import io
import itertools
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

import pymupdf as pdf
from fontTools.pens.boundsPen import BoundsPen
from fontTools.ttLib import TTFont, TTLibError
from PIL import Image, ImageDraw


POINTS_PER_MM = 72 / 25.4
PIXELS_PER_POINT = 5
READ_GAP = 0.12
MOVE_GAP = 0.4


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def center(rect):
    return (rect.x0 + rect.x1) / 2, (rect.y0 + rect.y1) / 2


def inflate(rect, amount):
    return rect + (-amount, -amount, amount, amount)


def projected(point, frame, page):
    factor = frame["ratio"] * POINTS_PER_MM
    target_x, target_y = frame["modelCenter"]
    paper_x, paper_y = frame["paperOffset"]
    return (page.rect.width / 2 + paper_x * POINTS_PER_MM + (point[0] - target_x) * factor,
            page.rect.height / 2 - paper_y * POINTS_PER_MM - (point[1] - target_y) * factor)


def printed_numbers(drawings, directions):
    """AutoCAD SHX digits are separate contiguous stroke paths in actual order."""
    found = defaultdict(list)
    for vertical, length in directions:
        for i in range(len(drawings) - length + 1):
            paths = drawings[i:i + length]
            layer = paths[0]["layer"]
            if any(p["type"] != "s" or p["layer"] != layer for p in paths):
                continue
            rectangles = [p["rect"] for p in paths]
            normal = [p.width if vertical else p.height for p in rectangles]
            if min(normal) < 1 or max(normal) - min(normal) > .55:
                continue
            box = pdf.Rect(rectangles[0])
            for rect in rectangles[1:]:
                box.include_rect(rect)
            height = box.width if vertical else box.height
            alongside = box.height if vertical else box.width
            if height > max(normal) + .55 or not .2 * length * height <= alongside <= length * height:
                continue
            coords = [center(rect)[1 if vertical else 0] for rect in rectangles]
            if len(coords) > 1:
                steps = [b - a for a, b in zip(coords, coords[1:])]
                if not (all(step > .25 * height for step in steps) or
                        all(step < -.25 * height for step in steps)):
                    continue
                if max(map(abs, steps)) > 1.4 * height:
                    continue
            x, y = center(box)
            bucket = (math.floor(x / 4), math.floor(y / 4))
            found[(vertical, length, layer, *bucket)].append(
                (box, {p["seqno"] for p in paths}, None))
    return found


class EmbeddedFont:
    def __init__(self, content):
        self.font = TTFont(io.BytesIO(content))
        self.glyphs = self.font.getGlyphSet()
        self.units = self.font["head"].unitsPerEm
        self.bounds = {}

    def character_box(self, character, span):
        _, glyph, origin, advance_box = character
        if glyph < 0 or glyph >= len(self.font.getGlyphOrder()):
            return None
        if glyph not in self.bounds:
            outline = self.glyphs[self.font.getGlyphName(glyph)]
            pen = BoundsPen(self.glyphs)
            outline.draw(pen)
            self.bounds[glyph] = (pen.bounds, outline.width)
        bounds, width = self.bounds[glyph]
        if bounds is None or width <= 0:
            return None
        dx, dy = span["dir"]
        normal_scale = span["size"] / self.units
        advance = max(advance_box[2]-advance_box[0], advance_box[3]-advance_box[1])
        if abs(dx) > .999:
            advance = advance_box[2]-advance_box[0]
        elif abs(dy) > .999:
            advance = advance_box[3]-advance_box[1]
        scale = advance / width if max(abs(dx), abs(dy)) > .999 else normal_scale
        ox, oy = origin
        xmin, ymin, xmax, ymax = bounds
        points = [(ox + dx*x*scale + dy*y*normal_scale, oy + dy*x*scale - dx*y*normal_scale)
                  for x, y in itertools.product((xmin, xmax), (ymin, ymax))]
        x, y = zip(*points)
        return pdf.Rect(min(x), min(y), max(x), max(y))


def add_font_numbers(page, index):
    """Trace visible embedded-font digits and retain the other PDF text as obstacles."""
    spans = [s for s in page.get_texttrace()
             if s["type"] != 3 and s["opacity"] > 0 and min(s["color"], default=0) < .98]
    fonts = defaultdict(set)
    for xref, _, _, name, *_ in page.get_fonts(full=True):
        fonts[name.split("+")[-1]].add(xref)
    embedded = {}
    text_ink = {}
    for span in spans:
        name = span["font"]
        chars = span["chars"]
        numeric = bool(chars) and all(48 <= char[0] <= 57 for char in chars)
        dx, dy = span["dir"]
        straight = max(abs(dx), abs(dy)) > .999
        vertical = abs(dy) > abs(dx)
        boxes = []
        if numeric and straight:
            if name not in embedded:
                embedded[name] = None
                if len(fonts[name]) == 1:
                    _, _, _, content = page.parent.extract_font(next(iter(fonts[name])))
                    if content:
                        try:
                            embedded[name] = EmbeddedFont(content)
                        except (TTLibError, KeyError):
                            pass
            font = embedded[name]
            if font:
                boxes = [font.character_box(char, span) for char in chars]
                if all(box and not box.is_empty for box in boxes):
                    box = pdf.Rect(boxes[0])
                    for char_box in boxes[1:]:
                        box.include_rect(char_box)
                    x, y = center(box)
                    width = (span["linewidth"] or 0) / 2 if span["type"] == 1 else 0
                    bucket = math.floor(x/4), math.floor(y/4)
                    number = ''.join(chr(char[0]) for char in chars)
                    index[(vertical, len(chars), span["layer"], *bucket)].append(
                        (inflate(box, width), {span["seqno"]}, number))
        outlines = boxes if boxes and all(boxes) else [pdf.Rect(char[3]) for char in chars if char[0] != 32]
        text_ink[span["seqno"]] = outlines
    return text_ink


def match_number(index, target, frame, page):
    at = projected(target["anchor"], frame, page)
    family = target["vertical"], target["digits"], target["layer"]
    size = target["height"] * frame["ratio"] * POINTS_PER_MM
    near = []
    bx, by = (math.floor(value / 4) for value in at)
    for i, j in itertools.product(range(bx-1, bx+2), range(by-1, by+2)):
        for box, ids, number in index.get((*family, i, j), ()):
            exact_number = number is not None and number == target.get("number")
            if number is not None and "number" in target and not exact_number:
                continue
            height = box.width if family[0] else box.height
            # Native DIMTXT describes nominal height. A plotted SHX numeral's
            # contour is its cap height, which need not occupy the full nominal height.
            # Embedded fonts can have smaller cap heights. Use the measured outline
            # with the wider bound only when both the value and its layer match.
            # The approved native _TCH_DIM_T3 plots simplex cap contours at about
            # 0.81 of DIMTXT (measured at 600 dpi). This is contour identification,
            # not a font-size waiver: actual DIMTXT/font are audited in the DWG.
            # The same cap-height bound supports SHX and embedded-font outlines;
            # layer, glyph count, position, uniqueness and clearance stay unchanged.
            tolerance = .25
            if abs(height-size) > max(.55, tolerance * size):
                continue
            error = math.dist(at, center(box))
            if error < (2.6 if exact_number else 2.2):
                near.append((error, box, ids))
    if not near:
        return None
    near.sort(key=lambda item: item[0])
    if len(near) >= 2 and near[1][0] - near[0][0] < .18:
        return None
    return near[0][1:]


class Ink:
    def __init__(self, viewport):
        self.bounds = viewport
        self.image = Image.new("L", (math.ceil(viewport.width * PIXELS_PER_POINT)+1,
                                     math.ceil(viewport.height * PIXELS_PER_POINT)+1))
        self.drawer = ImageDraw.Draw(self.image)

    def point(self, point):
        return ((point[0]-self.bounds.x0)*PIXELS_PER_POINT,
                (point[1]-self.bounds.y0)*PIXELS_PER_POINT)

    def has_ink(self, rect):
        if not self.bounds.contains(rect):
            return True
        start, end = self.point(rect.tl), self.point(rect.br)
        bounds = math.floor(start[0]), math.floor(start[1]), math.ceil(end[0]), math.ceil(end[1])
        return self.image.crop(bounds).getbbox() is not None

    def add_path(self, path):
        if not self.bounds.intersects(inflate(path["rect"], 1)):
            return
        stroke = path["type"] in ("s", "fs") and path["color"] is not None and min(path["color"]) < .98
        fill = path["type"] in ("f", "fs") and path["fill"] is not None and min(path["fill"]) < .98
        thickness = max(1, round((path["width"] or .16) * PIXELS_PER_POINT)) if stroke else 0
        contours = []
        points = []

        def append_segment(segment):
            if points and points[-1] != segment[0]:
                contours.append(points[:])
                points.clear()
            points.extend(segment if not points else segment[1:])

        for item in path["items"]:
            kind = item[0]
            if kind == "l":
                append_segment(item[1:])
            elif kind == "c":
                a, b, c, d = item[1:]
                append_segment([a*(1-t)**3+b*(3*(1-t)**2*t)+c*(3*(1-t)*t*t)+d*t**3
                                for t in (i/16 for i in range(17))])
            elif kind == "re":
                rect = item[1]
                append_segment((rect.tl, rect.tr, rect.br, rect.bl, rect.tl))
            elif kind == "qu":
                quad = item[1]
                append_segment((quad.ul, quad.ur, quad.lr, quad.ll, quad.ul))
        if points:
            contours.append(points)
        for contour in contours:
            projected_contour = [self.point(p) for p in contour]
            if fill and len(projected_contour) > 2:
                self.drawer.polygon(projected_contour, fill=255)
            if thickness and len(projected_contour) > 1:
                if path["closePath"] and contour[-1] != contour[0]:
                    projected_contour.append(projected_contour[0])
                self.drawer.line(projected_contour, fill=255, width=thickness)


OFFSETS = sorted(
    ((normal * .45, along * .45)
     for normal in range(12, 120) for along in range(-55, 56)),
    key=lambda p: 1.15*(p[0]-7)**2+1.8*p[1]**2,
)


def group_positions(indices, choices, stations, vertical):
    """Pack a few affected labels together while keeping the chain's station order."""
    selected = {}
    remaining = iter(range(24000))

    def search(options):
        if not options:
            return True
        index = min(options, key=lambda i: len(options[i]))
        for box in options[index]:
            if next(remaining, None) is None:
                return False
            expanded = inflate(box, MOVE_GAP)
            at = center(box)[vertical]
            adjacent = {j: [other for other in available if not expanded.intersects(other)
                            and (stations[index] - stations[j]) * (at - center(other)[vertical]) > 0]
                        for j, available in options.items() if j != index}
            if any(not available for available in adjacent.values()):
                continue
            selected[index] = box
            if search(adjacent):
                return True
            del selected[index]
        return False

    return selected if search({i: choices[i] for i in indices}) else {}


def solve_page(page, frame, *, extended=False, propose_repairs=True):
    drawings = page.get_drawings()
    targets = frame["targets"]
    index = printed_numbers(drawings, {(t["vertical"], t["digits"]) for t in targets})
    printed_text = add_font_numbers(page, index)
    matched, misses = [], []
    path_use = defaultdict(int)
    for row in targets:
        pair = match_number(index, row, frame, page)
        if pair is None:
            misses.append(row)
            continue
        box, paths = pair
        matched.append((row, box, paths))
        for identifier in paths:
            path_use[identifier] += 1
    unique = []
    for row, box, paths in matched:
        if any(path_use[i] != 1 for i in paths):
            misses.append(row)
        else:
            unique.append((row, box, paths))
    if not unique:
        return [], misses, [], 0
    excluded = set().union(*(part[2] for part in unique))
    ink = Ink(page.rect)
    for stroke in drawings:
        if stroke["seqno"] not in excluded:
            ink.add_path(stroke)
    for seqno, boxes in printed_text.items():
        if seqno not in excluded:
            for box in boxes:
                ink.drawer.rectangle((ink.point(box.tl), ink.point(box.br)), fill=255)

    boxes = [item[1] for item in unique]
    viewport = page.rect
    if "modelSize" in frame:
        cx, cy = frame["modelCenter"]
        half_width, half_height = (value/2 for value in frame["modelSize"])
        left, top = projected((cx-half_width, cy+half_height), frame, page)
        right, bottom = projected((cx+half_width, cy-half_height), frame, page)
        viewport &= pdf.Rect(left, top, right, bottom)
    normals, stations, segments = [], [], []
    for row, _, _ in unique:
        along = int(row["vertical"])
        start, end = (projected(row[key], frame, page)[along] for key in ("start", "end"))
        normals.append(projected(row["baseline"], frame, page)[1-along])
        stations.append((start + end) / 2)
        segments.append(tuple(sorted((start, end))))

    interior_limit = 3.5 if extended else 2

    def near_measurement(index, box):
        row = unique[index][0]
        if not row.get("interior"):
            return True
        vertical = row["vertical"]
        low, high = (box.y0, box.y1) if vertical else (box.x0, box.x1)
        start, end = segments[index]
        tolerance = .35 * POINTS_PER_MM
        edge = box.x1 if vertical else box.y1
        nominal = row["height"] * frame["ratio"] * POINTS_PER_MM
        return (high >= start - tolerance and low <= end + tolerance and
                normals[index] - edge <= interior_limit * nominal + tolerance)

    def clear(index, box, gap, fixed):
        expanded = inflate(box, gap)
        vertical = unique[index][0]["vertical"]
        side = expanded.x1 < normals[index] if vertical else expanded.y1 < normals[index]
        return (side and near_measurement(index, box) and viewport.contains(expanded) and not ink.has_ink(expanded)
                and not any(expanded.intersects(other) for other in fixed))

    def alternatives(index, fixed):
        row, original, _ = unique[index]
        vertical = row["vertical"]
        x, y = center(original)
        if row.get("interior"):
            width, height = (original.height, original.width) if vertical else (original.width, original.height)
            start, end = segments[index]
            tolerance = .35 * POINTS_PER_MM
            left, right = start - width / 2 - tolerance, end + width / 2 + tolerance
            nominal = row["height"] * frame["ratio"] * POINTS_PER_MM
            near = MOVE_GAP + height / 2
            old_limit = 2 * nominal + tolerance + height / 2
            far = interior_limit * nominal + tolerance + height / 2
            step = .38
            alongs = [left + i * step for i in range(int((right - left) / step) + 1)] + [right, stations[index]]
            across = [near + i * step for i in range(int((far - near) / step) + 1)] + [far]
            if extended:
                across.append(old_limit)
            ideal = min(max(normals[index] - (x if vertical else y), near), old_limit)
            positions = sorted(itertools.product(across, alongs), key=lambda at:
                               (at[0] > old_limit,
                                (at[0] - ideal) ** 2 + .7 * (at[1] - stations[index]) ** 2))
            offsets = ((cross, along - stations[index]) for cross, along in positions)
        else:
            offsets = OFFSETS
        for cross, offset in offsets:
            normal, along = normals[index] - cross, stations[index] + offset
            new_x, new_y = (normal, along) if vertical else (along, normal)
            dx, dy = new_x-x, new_y-y
            box = original + (dx, dy, dx, dy)
            if clear(index, box, MOVE_GAP, fixed):
                yield box

    def others(index):
        return boxes[:index] + boxes[index+1:]

    ordered = sorted(range(len(unique)), key=lambda i: not unique[i][0]["short"])
    collisions = [i for i in ordered if not clear(i, boxes[i], READ_GAP, others(i))]
    if not propose_repairs:
        return [], misses, [unique[i][0] for i in collisions], len(unique)
    for i in collisions:
        fixed = others(i)
        if unique[i][0]["movable"] and not clear(i, boxes[i], READ_GAP, fixed):
            boxes[i] = next(alternatives(i, fixed), boxes[i])

    # Nearby labels on the same chain can occupy each other's only clear area.
    # Repack those labels together once individual moves have been exhausted.
    for i in collisions:
        row = unique[i][0]
        if not row["movable"] or clear(i, boxes[i], READ_GAP, others(i)):
            continue
        neighbors = [j for j, (other, _, _) in enumerate(unique)
                     if other["handle"] == row["handle"] and other["movable"]
                     and abs(other["segment"] - row["segment"]) <= 2]
        fixed = [b for j, b in enumerate(boxes) if j not in neighbors]
        choices = {}
        for j in neighbors:
            current = [boxes[j]] if clear(j, boxes[j], MOVE_GAP, fixed) else []
            choices[j] = current + list(alternatives(j, fixed))
        packed = group_positions(neighbors, choices, stations, row["vertical"])
        for j, box in packed.items():
            boxes[j] = box

    scale = frame["ratio"] * POINTS_PER_MM
    changes = []
    for i, (row, original, _) in enumerate(unique):
        if boxes[i] != original:
            before, after = center(original), center(boxes[i])
            changes.append((row, (after[0]-before[0])/scale, (before[1]-after[1])/scale))
    return changes, misses, [unique[i][0] for i in collisions], len(unique)


def joint_proposals(page, frame, blocked, unmatched, changes):
    import time
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    sys.modules.setdefault("check_plan_print_ink", sys.modules[__name__])
    from joint_annotation_search import plan_sheet

    affected = defaultdict(list)
    for row in blocked:
        affected[row["handle"]].append(row["segment"])
    incomplete = {row["handle"] for row in unmatched}
    eligible = [g for g in frame["jointChains"] if g["handle"] in affected and
                g["handle"] not in incomplete and g["values"] and g["ink"]]
    if not eligible:
        return changes, []
    deadline = time.monotonic() + 12
    targets = {(r["handle"], r["segment"]): r for r in frame["targets"]}
    for index, geometry in enumerate(eligible):
        # A fully blocked chain must leave time to try other independent chains.
        limit = time.monotonic() + max(0., deadline-time.monotonic()) / (len(eligible)-index)
        blocked_segments = {(geometry["handle"], part) for part in affected[geometry["handle"]]}
        plans, _ = plan_sheet(page, frame, [geometry], blocked_segments=blocked_segments, deadline=limit)
        if not plans:
            continue
        plan = plans[0][0]
        handle = plan["handle"]
        edits = []
        for segment, (x, y) in enumerate(plan["text"]):
            target = targets[handle, segment]
            dx, dy = x - target["anchor"][0], y - target["anchor"][1]
            if abs(dx) + abs(dy) > 1e-5 or plan["normal"]:
                edits.append((target, dx, dy))
        if edits:
            keep = [entry for entry in changes if entry[0]["handle"] != handle]
            baseline = [(handle, affected[handle][0], plan["normal"])] if plan["normal"] else []
            return keep + edits, baseline
    return changes, []


def main(request_path, reply_path):
    request = json.loads(request_path.read_text(encoding="utf-8"))
    if request["contract"] != "plan.printed-ink-input.v1":
        raise ValueError("unexpected print review request")
    propose_repairs = request.get("proposeRepairs", True) is True
    sheets = []
    for frame in request["frames"]:
        filename = request_path.parent / frame["file"]
        if filename.parent != request_path.parent or sha(filename) != frame["sha256"]:
            raise ValueError("printed page does not match the owned plot receipt")
        with pdf.open(filename) as document:
            if len(document) != 1:
                raise ValueError("expected one drawing sheet per PDF")
            changes, unmatched, blocked, seen = solve_page(
                document[0], frame, extended=frame.get("extendedInteriorSearch", request.get("extendedInteriorSearch")) is True,
                propose_repairs=propose_repairs)
        sheets.append((frame, filename, changes, unmatched, blocked, seen))
    # A speculative baseline move on one sheet must not roll back a simpler
    # text placement on an independent sheet in the same verified trial.
    use_joint = propose_repairs and not any(sheet[2] for sheet in sheets)
    rows = []
    checked = expected = misses = conflicts = edits = 0
    for frame, filename, changes, unmatched, blocked, seen in sheets:
        baselines = []
        if use_joint and frame.get("jointChains") and blocked:
            with pdf.open(filename) as document:
                changes, baselines = joint_proposals(document[0], frame, blocked, unmatched, changes)
        checked += seen
        expected += len(frame["targets"])
        misses += len(unmatched)
        conflicts += len(blocked)
        edits += len(changes)
        panel = frame["panel"]
        for kind, collection in (("MISSING", unmatched), ("COLLISION", blocked)):
            rows.extend(f"{kind} {panel} {row['handle']} {row['segment']}" for row in collection)
        rows.extend(f"MOVE {panel} {row['handle']} {row['segment']} {dx:.9g} {dy:.9g}"
                    for row, dx, dy in changes)
        rows.extend(f"BASELINE {panel} {handle} {segment} {normal:.9g}"
                    for handle, segment, normal in baselines)
    with reply_path.open("x", encoding="ascii", newline="\n") as output:
        output.write(f"PLANINKRESULT1 {request['drawingSHA256']} {len(request['frames'])} "
                     f"{expected} {checked} {misses} {conflicts} {edits}\n")
        output.write("\n".join(rows) + "\nEND\n")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
