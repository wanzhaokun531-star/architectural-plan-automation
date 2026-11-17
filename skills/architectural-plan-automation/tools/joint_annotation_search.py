"""Find coupled local dimension layouts from an actual reopened DWG plot."""

from itertools import product
import math
import time

from check_plan_print_ink import (
    Ink, POINTS_PER_MM, READ_GAP, add_font_numbers, center, inflate, match_number, pdf,
    printed_numbers, projected,
)


def shifted(box, dx, dy):
    return box + (dx, dy, dx, dy)


def inside_line(point, segment, tolerance):
    a, b = segment
    length = math.hypot(b.x - a.x, b.y - a.y)
    if length <= 0.001:
        return False
    vx, vy = (b.x - a.x) / length, (b.y - a.y) / length
    dx, dy = point.x - a.x, point.y - a.y
    return abs(dx * vy - dy * vx) <= tolerance and -tolerance <= dx * vx + dy * vy <= length + tolerance


def crosses(segment, box):
    a, b = segment
    if box.contains(a) or box.contains(b):
        return True
    if min(a.x, b.x) > box.x1 or max(a.x, b.x) < box.x0 or min(a.y, b.y) > box.y1 or max(a.y, b.y) < box.y0:
        return False
    if abs(a.x-b.x)<.01 or abs(a.y-b.y)<.01:
        return True
    sides = [(b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x)
             for x in (box.x0,box.x1) for y in (box.y0,box.y1)]
    return min(sides) <= 0 <= max(sides)


def trace_boxes(page, excluded):
    for span in page.get_texttrace():
        if span["seqno"] in excluded or span["type"] == 3:
            continue
        for char, _, _, box in span["chars"]:
            if not chr(char).isspace():
                yield pdf.Rect(box)


def plotted_chain(page, frame, geometry, targets):
    points, values = geometry["grips"], geometry["values"]
    project = lambda p: pdf.Point(projected(p, frame, page))
    lines = [(project(a), project(b)) for a, b in geometry["ink"]]
    vertical = abs(points[0][0] - points[1][0]) < .01
    along_axis = int(vertical)
    normal_axis = 1 - along_axis
    baseline = project(points[0])[normal_axis]
    rows = []
    for i, value in enumerate(values):
        number = str(round(abs(value)))
        rows.append({
            "handle": geometry["handle"], "segment": i, "number": number, "digits": len(number),
            "vertical": vertical, "anchor": points[len(values)+3+i], "height": 280,
            "start": points[i+2], "end": points[i+3], "baseline": points[0],
            "layer": geometry["layer"],
            **targets.get((geometry["handle"], i), {}),
        })
    stations = [project(point)[along_axis] for point in points[2:len(values)+3]]
    return {"handle": geometry["handle"], "rows": rows, "lines": lines, "vertical": vertical,
            "normal": normal_axis, "along": along_axis, "baseline": baseline,
            "stations": stations, "ticks": [], "movable_baseline": geometry.get("movableBaseline", True)}


def original_chain_ink(path, chains):
    """Recognize whole PDF paths that consist of readback dimension strokes."""
    if path.get("type") != "s":
        return False
    items = path["items"]
    if not items or any(item[0] != "l" for item in items):
        return False
    for chain in chains:
        if path.get("layer") != chain["rows"][0]["layer"]:
            continue
        if all(any(inside_line(a, line, 0.75) and inside_line(b, line, 0.75) for line in chain["lines"])
               for _, a, b in items):
            return True
    return False


def plotted_tick(path, chains):
    """Tangent tick marks are plotted at the measured station on the baseline."""
    items = path['items']
    if path.get('type') != 's' or len(items) != 1 or items[0][0] != 'l':
        return None
    _, a, b = items[0]
    if min(abs(a.x-b.x),abs(a.y-b.y)) < .35 or math.hypot(a.x-b.x,a.y-b.y) > 7:
        return None
    xy = ((a.x+b.x)*.5,(a.y+b.y)*.5)
    matching = [c for c in chains if path.get('layer') == c['rows'][0]['layer']
                and abs(xy[c['normal']]-c['baseline']) <= .7
                and any(abs(xy[c['along']]-s) <= .7 for s in c['stations'])]
    return matching[0] if len(matching) == 1 else None


def move_lines(chain, shift, frame, page, reverse):
    slide = lambda p: pdf.Point(p.x + (shift if chain["vertical"] else 0),
                                 p.y + (0 if chain["vertical"] else shift))
    for index,(a,b) in enumerate(chain["lines"]):
        if abs(a[chain["normal"]] - b[chain["normal"]]) < .01:
            yield slide(a), slide(b)
        else:
            # The witness is written from the fixed visible start to the
            # extrapolated tail. Moving the baseline moves that tail.
            if reverse:
                row = chain["rows"][index//3]
                station = projected(row["start" if index%3==1 else "end"],frame,page)[chain["normal"]]
                def reflect(point, axis):
                    data = [point.x,point.y]
                    data[chain["normal"]] = 2*axis-data[chain["normal"]]
                    return pdf.Point(data)
                yield reflect(a,station), slide(reflect(b,chain["baseline"]))
            else:
                yield a, slide(b)
    for a,b in chain['ticks']:
        yield slide(a),slide(b)


def offsets(chain, frame, page, allow_reversal):
    yield 0, 0, False
    if not chain["movable_baseline"]:
        return
    normal = chain["normal"]
    values = [projected(row["start"], frame, page)[normal] - chain["baseline"] for row in chain["rows"]]
    values.append(projected(chain["rows"][-1]["end"], frame, page)[normal] - chain["baseline"])
    for mm in (step * sign for step in range(50, 1801, 50) for sign in (1,-1)):
        paper = mm * frame["ratio"] * POINTS_PER_MM * (1 if chain["vertical"] else -1)
        if all(v * (v - paper) > 0 and abs(v - paper) > 10 for v in values):
            yield mm, paper, False
        elif allow_reversal and max(values)-min(values)<.001 and all(v*(v-paper)<0 and abs(v-paper)>10 for v in values):
            yield mm, paper, True


def model_point(page, frame, point):
    denominator = frame["ratio"] * POINTS_PER_MM
    return (
        frame["modelCenter"][0] + (point[0] - page.rect.width/2 - frame["paperOffset"][0]*POINTS_PER_MM) / denominator,
        frame["modelCenter"][1] + (page.rect.height/2 - frame["paperOffset"][1]*POINTS_PER_MM - point[1]) / denominator,
    )


def edge_centers(boxes, axis, half_size, low, high, preferred):
    """Include narrow slots bounded by actual drawn edges between regular samples."""
    positions = set()
    for box in boxes:
        for value in (box[axis] - half_size - .22, box[axis+2] + half_size + .22):
            if low <= value <= high:
                positions.add(value)
    return sorted(positions, key=lambda value: (abs(value-preferred), value))[:36]


def plan_sheet(page, frame, geometries, *, opening_glyphs=(), allow_reversal=False, notes=(),
               blocked_segments=(), deadline=math.inf):
    paths = page.get_drawings()
    targets = {(r["handle"], r["segment"]): r for r in frame["targets"]}
    chains = [plotted_chain(page, frame, geometry, targets) for geometry in geometries]
    all_rows = [r for c in chains for r in c["rows"]]
    requested = {(r["handle"], r["segment"]) for r in all_rows}
    other = [r for r in frame["targets"] if (r["handle"], r["segment"]) not in requested]
    number_index = printed_numbers(paths, {(r["vertical"], r["digits"]) for r in all_rows + other})
    add_font_numbers(page, number_index)
    groups = {}
    excluded_glyphs = set()
    fixed_numbers = []
    for row in all_rows + other:
        attached = match_number(number_index, row, frame, page)
        key = row["handle"], row["segment"]
        if attached is None:
            if key in requested:
                return [], {"reason": "dimension glyph could not be uniquely matched", "object": key}
            continue
        box, ids = attached
        if key in requested:
            groups[key] = box
            excluded_glyphs.update(ids)
        else:
            fixed_numbers.append(box)
    obstacles = Ink(page.rect)
    removed_lines = set()
    ink_edges = []
    note_boxes = []
    for note, dx, dy in notes:
        a,b=[projected(p,frame,page) for p in note["bounds"]]
        box=pdf.Rect(a[0],b[1],b[0],a[1])
        unit=frame["ratio"]*POINTS_PER_MM
        note_boxes.append((note["layer"],inflate(box,.35),shifted(box,dx*unit,-dy*unit)))
    for path in paths:
        if path["seqno"] in excluded_glyphs:
            continue
        if any(path["layer"]==layer and box.contains(path["rect"]) for layer,box,_ in note_boxes):
            continue
        tick = plotted_tick(path, chains)
        if tick is not None:
            _, a, b = path['items'][0]
            tick['ticks'].append((a,b))
            removed_lines.add(path['seqno'])
        elif original_chain_ink(path, chains):
            removed_lines.add(path["seqno"])
        else:
            obstacles.add_path(path)
            ink_edges.append(path["rect"])
    fixed_letters = [box for box in trace_boxes(page, excluded_glyphs)
                     if not any(old.contains(box) for _,old,_ in note_boxes)]
    for box in fixed_letters:
        obstacles.drawer.rectangle((obstacles.point(box.tl), obstacles.point(box.br)), fill=255)
    if any(obstacles.has_ink(inflate(box,.2)) for _,_,box in note_boxes):
        return [],{"reason":"source note group would hit actual fixed ink"}
    for _,_,box in note_boxes:
        obstacles.drawer.rectangle((obstacles.point(box.tl), obstacles.point(box.br)), fill=255)
        fixed_letters.append(box)
    vicinity = pdf.Rect(chains[0]["lines"][0])
    for chain in chains:
        for a,b in chain["lines"]:
            vicinity |= pdf.Rect(min(a.x,b.x),min(a.y,b.y),max(a.x,b.x),max(a.y,b.y))
    vicinity = inflate(vicinity, 95)
    all_fixed = [box for box in fixed_letters + fixed_numbers if vicinity.intersects(box)]
    for glyph in opening_glyphs:
        a,b=[projected(p,frame,page) for p in glyph["bounds"]]
        box=pdf.Rect(a[0],b[1],b[0],a[1])
        if vicinity.intersects(box):
            all_fixed.append(box)
    chain_options = []
    # Leave time to pack candidates even when a long chain uses the search budget.
    option_deadline = deadline - min(1., max(0., (deadline - time.monotonic()) * .12))
    partial = set(blocked_segments)
    for chain in chains:
        original_lines = chain['lines'] + chain['ticks']
        choices = []
        for mm, displacement, reverse in offsets(chain, frame, page, allow_reversal):
            if time.monotonic() >= option_deadline:
                break
            active_lines = list(move_lines(chain, displacement,frame,page,reverse))
            if displacement and any(crosses(line, box) and not any(crosses(old, box) for old in original_lines)
                for box in all_fixed for line in active_lines):
                continue
            labels = []
            for row in chain["rows"]:
                key = row["handle"], row["segment"]
                box = groups[key]
                current = center(box)
                normal, along = chain["normal"], chain["along"]
                limits = sorted(projected(row[k], frame, page)[along] for k in ("start","end"))
                height = box.width if chain["vertical"] else box.height
                length = box.height if chain["vertical"] else box.width
                # Keep measurable overlap with this segment and leave room for
                # glyph rounding during native PDF export.
                margin = length / 2 + .35 * POINTS_PER_MM
                min_along, max_along = limits[0] - margin, limits[1] + margin
                even = lambda a,b: [a+i*.4 for i in range(max(0, int((b-a)/.4))+1)]
                base = chain["baseline"] + displacement
                normal_max = base - height/2 - .27
                nominal = row["height"] * frame["ratio"] * POINTS_PER_MM
                reach = (3.5 if frame.get("extendedInteriorSearch") else 2) * nominal + .35 * POINTS_PER_MM
                normal_min = base - height/2 - reach if row.get("interior") else base - 4.1*height
                if chain['vertical']:
                    region = pdf.Rect(normal_min-height/2, min_along-length/2, normal_max+height/2, max_along+length/2)
                else:
                    region = pdf.Rect(min_along-length/2, normal_min-height/2, max_along+length/2, normal_max+height/2)
                edges = [item for item in ink_edges if inflate(region,.5).intersects(inflate(item,.01))] if key in partial else ()
                alongs = list(dict.fromkeys([current[along], sum(limits)/2, min_along, max_along]
                    + even(min_along, max_along) + edge_centers(edges, along, length/2, min_along, max_along, current[along])))
                normals = list(dict.fromkeys([current[normal], min(normal_max,current[normal]+displacement), normal_min, normal_max]
                    + even(normal_min,normal_max) + edge_centers(edges, normal, height/2, normal_min, normal_max, current[normal])))
                anchor = projected(row["anchor"],frame,page)
                positions = ((run, across) for run, across in product(alongs, normals)
                             if min_along <= run <= max_along and normal_min <= across <= normal_max)
                def displacement_cost(at):
                    return (at[0]-current[along])**2 + (at[1]-current[normal])**2
                candidates = []
                for run, across in sorted(positions, key=displacement_cost):
                    xy = (across, run) if chain["vertical"] else (run, across)
                    dx, dy = xy[0]-current[0], xy[1]-current[1]
                    placed = shifted(box, dx, dy)
                    padded = inflate(placed, READ_GAP if dx == dy == 0 else .20)
                    if not page.rect.contains(padded) or obstacles.has_ink(padded) or any(crosses(line,padded) for line in active_lines):
                        continue
                    candidates.append((displacement_cost((run,across)), placed,
                                       model_point(page,frame,(anchor[0]+dx,anchor[1]+dy)),False))
                    if len(candidates)==24:
                        break
                if not candidates:
                    if displacement or key not in partial:
                        break
                if not displacement and key in partial:
                    candidates.append((1e7,box,row["anchor"],True))
                labels.append(candidates)
            if len(labels)==len(chain["rows"]):
                choices.append((abs(displacement)*2+sum(x[0][0] for x in labels),mm,active_lines,labels,reverse))
        if not choices:
            return [], {"reason":"no clear assignment to measured segments at any local baseline", "object":chain["handle"], "removed":len(removed_lines)}
        chain_options.append(sorted(choices,key=lambda x:x[0])[:25])
    results=[]
    for combination in sorted(product(*chain_options),key=lambda rows:sum(row[0] for row in rows)):
        if time.monotonic() >= deadline:
            break
        lines=[line for choice in combination for line in choice[2]]
        slots=[(i,j,[option for option in label if option[3] or not any(crosses(line,inflate(option[1],.20)) for line in lines)])
               for i,choice in enumerate(combination) for j,label in enumerate(choice[3])]
        slots.sort(key=lambda x:len(x[2]))
        selected={}
        allowance=iter(range(5000))
        def assign(level):
            if level==len(slots):
                return True
            i,j,options=slots[level]
            for _,box,anchor,retained_conflict in options:
                if next(allowance,None) is None:
                    return False
                if any(inflate(box,READ_GAP).intersects(used[0]) and not (retained_conflict and used[2])
                       for used in selected.values()):
                    continue
                projected_along=center(box)[chains[i]["along"]]
                distinct=[]
                for n,row in enumerate(chains[i]["rows"]):
                    a=chains[i]["along"]
                    if (i,n) in selected:
                        other_along=center(selected[i,n][0])[a]
                        here=chains[i]["rows"][j]
                        midpoint=lambda r:sum(projected(r[x],frame,page)[a] for x in ("start","end"))/2
                        distinct.append((midpoint(here)-midpoint(row))*(projected_along-other_along)>0)
                if distinct and not all(distinct):
                    continue
                selected[i,j]=(box,anchor,retained_conflict)
                if assign(level+1):
                    return True
                del selected[i,j]
            return False
        if assign(0) and any(not selected[i,j][2] for i,chain in enumerate(chains)
                              for j,row in enumerate(chain["rows"]) if not partial or (row["handle"],j) in partial):
            results.append([{"handle":chain["handle"],"normal":combination[i][1],"reverse":combination[i][4],
                "text":[selected[i,j][1] for j in range(len(chain["rows"]))]} for i,chain in enumerate(chains)])
            if len(results)==4:
                break
    return results, {"chains":[c["handle"] for c in chains], "removed":len(removed_lines)}
