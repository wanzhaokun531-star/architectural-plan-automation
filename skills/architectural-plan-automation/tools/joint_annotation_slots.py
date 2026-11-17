"""Find local dimension positions using measured paper glyphs and rasterized ink."""

from collections import defaultdict
from functools import reduce
from itertools import chain
import math
from operator import or_
import time

from PIL import Image, ImageDraw

from check_plan_print_ink import (Ink, PIXELS_PER_POINT, POINTS_PER_MM, add_font_numbers,
                                  center, inflate, match_number, pdf, printed_numbers, projected)
from joint_annotation_search import crosses, model_point, plotted_chain, plotted_tick


_REVERSED_BYTES = bytes(int(f'{value:08b}'[::-1], 2) for value in range(256))


def chain_paper_paths(paths, chain_geometry):
    """Bind actual stroked lines and filled slash marks to native readback.

    Layer is only a preliminary filter. Every endpoint or slash centre must
    match this chain's real geometry; ambiguous or unsupported ink stays fixed.
    """
    c = chain_geometry
    result = []
    for path in paths:
        if path.get('layer') != c['rows'][0]['layer']:
            continue
        items = path['items']
        if not items or any(item[0] != 'l' for item in items):
            continue
        points = [p for item in items for p in item[1:]]
        bounds = pdf.Rect(min(p.x for p in points), min(p.y for p in points),
                          max(p.x for p in points), max(p.y for p in points))
        mid = center(bounds)
        filled_slash = (path.get('type') in ('f','fs') and len(items)==6 and
                        1 < bounds.width < 7 and 1 < bounds.height < 7 and
                        abs(bounds.width-bounds.height)<.35 and
                        items[0][1]==items[2][2] and items[3][1]==items[5][2] and
                        max(p.x+p.y for p in points)-min(p.x+p.y for p in points)<2)
        if (filled_slash or plotted_tick(path,[c])) and (
                abs(mid[c['normal']]-c['baseline'])<=.75 and
                any(abs(mid[c['along']]-s)<=.75 for s in c['stations'])):
            result.append((path,'tick'))
            continue
        if path.get('type') != 's':
            continue
        mappings = []
        for _,a,b in items:
            choices = []
            for u,v in c['lines']:
                baseline = abs(u[c['normal']]-v[c['normal']]) < .01
                for first,last,flags in ((u,v,(baseline,True)),(v,u,(True,baseline))):
                    if abs(a-first)<=.75 and abs(b-last)<=.75:
                        choices.append(flags)
            if not choices or len(set(choices)) != 1:
                break
            mappings.append(choices[0])
        else:
            result.append((path,mappings))
    return result


def shifted_chain_paths(bound, chain_geometry, shift):
    """Move the actual paper ink, retaining line weight, fills and quantization."""
    axis = chain_geometry['normal']
    result = []
    for path, kind in bound:
        items = []
        for index,(_,a,b) in enumerate(path['items']):
            flags = (True,True) if kind == 'tick' else kind[index]
            points = []
            for point,move in zip((a,b),flags):
                xy = [point.x,point.y]
                if move:
                    xy[axis] += shift
                points.append(pdf.Point(xy))
            items.append(('l',*points))
        points = [p for item in items for p in item[1:]]
        result.append({**path,'items':items,'rect':pdf.Rect(
            min(p.x for p in points),min(p.y for p in points),
            max(p.x for p in points),max(p.y for p in points))})
    return result


class PaperMask:
    """Test glyph rectangles with the same floor/ceil pixel bounds as Ink."""

    def __init__(self, ink):
        self.ink = ink
        self.rows = self._rows(ink.image)

    @staticmethod
    def _rows(image):
        bits = image.convert('1', dither=Image.Dither.NONE)
        pitch = (bits.width + 7) // 8
        raw = bits.tobytes().translate(_REVERSED_BYTES)
        return [int.from_bytes(raw[i:i+pitch], 'little') for i in range(0, len(raw), pitch)]

    def clear_columns(self, y, half, groups):
        scale = PIXELS_PER_POINT
        minimum = math.floor((y - half - .18 - self.ink.bounds.y0) * scale)
        maximum = math.ceil((y + half + .18 - self.ink.bounds.y0) * scale)
        if minimum < 0 or maximum > len(self.rows):
            return
        ink = reduce(or_, self.rows[minimum:maximum], 0)
        for x, mask in groups:
            if not ink & mask:
                yield x

    def columns(self, xs, half):
        scale = PIXELS_PER_POINT
        for x in xs:
            left = math.floor((x - half - .18 - self.ink.bounds.x0) * scale)
            right = math.ceil((x + half + .18 - self.ink.bounds.x0) * scale)
            if left >= 0 and right <= self.ink.image.width:
                yield x, ((1 << (right - left)) - 1) << left


def label_slots(page, frame, item, baseline, mask, blocked, exterior_overhang=0):
    row, real, _ = item
    origin = center(real)
    half = real.width / 2, real.height / 2
    along = int(row['vertical'])
    normal = 1 - along
    ends = sorted(projected(row[k], frame, page)[along] for k in ('start', 'end'))
    tolerance = .35 * POINTS_PER_MM
    reach = (3.5 * row['height'] * frame['ratio'] * POINTS_PER_MM + tolerance
             if row.get('interior') else 38.)
    bounds = [None, None]
    bounds[along] = ends[0] - half[along] - tolerance + .19, ends[1] + half[along] + tolerance - .19
    if not row.get('interior'):
        # Preserve a previously verified exterior short label's overhang. Do
        # not silently impose the interior segment envelope on that expression.
        bounds[along] = min(bounds[along][0],origin[along]), max(bounds[along][1],origin[along])
        padding=exterior_overhang*frame['ratio']*POINTS_PER_MM
        bounds[along] = bounds[along][0]-padding,bounds[along][1]+padding
    bounds[normal] = baseline - half[normal] - reach + .19, baseline - half[normal] - .28

    def samples(axis):
        low, high = bounds[axis]
        values = [low + j / PIXELS_PER_POINT for j in range(max(0, int((high-low)*PIXELS_PER_POINT))+1)]
        return sorted(set(v for v in chain(values, (high, origin[axis])) if low <= v <= high))

    anchor = projected(row['anchor'], frame, page)

    def slot(x, y, pending):
        dx, dy = x-origin[0], y-origin[1]
        return (pending, math.hypot(dx, dy), pdf.Rect(x-half[0], y-half[1], x+half[0], y+half[1]),
                model_point(page, frame, (anchor[0]+dx, anchor[1]+dy)))

    columns = list(mask.columns(samples(0), half[0]))
    preferred = {}
    for y in samples(1):
        for x in mask.clear_columns(y, half[1], columns):
            distance = (x-origin[0])**2 + (y-origin[1])**2
            if distance >= (3500 * frame['ratio'] * POINTS_PER_MM)**2:
                continue
            key = round(x/1.6), round(y/1.6)
            if key not in preferred or distance < preferred[key][0]:
                preferred[key] = distance, x, y
    choices = [slot(x, y, False) for _, x, y in sorted(preferred.values())[:25]]
    if (all(lo <= value <= hi for (lo,hi),value in zip(bounds,origin)) and
            not mask.ink.has_ink(inflate(real,.12))):
        choices.insert(0,slot(*origin,False))
    if blocked and all(lo-.12 <= value <= hi+.12 for (lo, hi), value in zip(bounds, origin)):
        choices.append(slot(*origin, True))
    return choices


def pack_slots(choices, midpoints, along):
    order = sorted(range(len(choices)), key=lambda i: len(choices[i]))
    selected = {}
    budget = iter(range(1300))
    best = None

    def place(depth, pending, distance):
        nonlocal best
        if next(budget, None) is None or best and (pending, distance) >= best[:2]:
            return
        if depth == len(order):
            best = pending, distance, [selected[i] for i in range(len(choices))]
            return
        i = order[depth]
        for item in choices[i]:
            flag, cost, box, _ = item
            normal=1-along
            if any((inflate(box, .17).intersects(other[2]) and not (flag and other[0])) or
                   ((midpoints[i]-midpoints[j])*(center(box)[along]-center(other[2])[along]) <= 0 and
                    not (box[normal+2]+.17 < other[2][normal] or other[2][normal+2]+.17 < box[normal]))
                   for j, other in selected.items()):
                continue
            selected[i] = item
            place(depth + 1, pending + int(flag), distance + cost)
            del selected[i]

    place(0, 0, 0.)
    return best


def paper_slot_plans(page, frame, geometry, blocked, opening_glyphs=(), *, deadline=math.inf,
                     max_offset=1800, diagnostics=None, exterior_overhang=0):
    """Keep all verified labels and offer independently verifiable partial repairs."""
    handle = geometry['handle']
    if not 0 <= max_offset <= 1800:
        raise ValueError('baseline search exceeds the native local repair range')
    paths = page.get_drawings()
    targets = {(row['handle'],row['segment']): row for row in frame['targets']}
    index = printed_numbers(paths, {(r['vertical'],r['digits']) for r in targets.values()})
    text = add_font_numbers(page, index)
    matches = []
    for row in targets.values():
        found = match_number(index, row, frame, page)
        if found:
            matches.append((row, *found))
    own = sorted((item for item in matches if item[0]['handle'] == handle), key=lambda item: item[0]['segment'])
    if len(own) != len(geometry['values']) or not own:
        if diagnostics is not None:
            diagnostics['missingGlyphs'] = len(geometry['values'])-len(own)
        return []
    use = defaultdict(int)
    for _, _, ids in matches:
        for identifier in ids:
            use[identifier] += 1
    excluded = {identifier for _, _, ids in own for identifier in ids}
    if any(use[identifier] != 1 for identifier in excluded):
        return []
    chain_geometry = plotted_chain(page, frame, geometry, targets)
    bound_paths = chain_paper_paths(paths, chain_geometry)
    owned_paths = {path['seqno'] for path,_ in bound_paths}
    along, normal = chain_geometry['along'], chain_geometry['normal']
    region = pdf.Rect(own[0][1])
    for _, box, _ in own[1:]:
        region |= box
    region = inflate(region, 150) & page.rect
    region = pdf.Rect(math.floor(region.x0*PIXELS_PER_POINT)/PIXELS_PER_POINT,
                      math.floor(region.y0*PIXELS_PER_POINT)/PIXELS_PER_POINT,
                      math.ceil(region.x1*PIXELS_PER_POINT)/PIXELS_PER_POINT,
                      math.ceil(region.y1*PIXELS_PER_POINT)/PIXELS_PER_POINT)
    fixed = Ink(region)
    for path in paths:
        if path['seqno'] in excluded or path['seqno'] in owned_paths or not inflate(path['rect'], .5).intersects(region):
            continue
        fixed.add_path(path)
    for identifier, boxes in text.items():
        if identifier not in excluded:
            for box in boxes:
                if region.intersects(box):
                    fixed.drawer.rectangle((fixed.point(box.tl), fixed.point(box.br)), fill=255)
    numbers = [box for row, box, _ in matches if row['handle'] != handle and region.intersects(box)
               and (row['handle'], row['segment']) not in blocked]
    labels = []
    for glyph in opening_glyphs:
        a, b = (projected(point, frame, page) for point in glyph['bounds'])
        box = pdf.Rect(a[0], b[1], b[0], a[1])
        if region.intersects(box):
            labels.append(box)
    initial = [tuple(item[1:]) for path,_ in bound_paths for item in path['items']]
    baseline = chain_geometry['baseline']
    stations = [projected(p, frame, page)[normal] - baseline
                for p in geometry['grips'][2:len(own)+3]]
    midpoints = [sum(projected(row[k],frame,page)[along] for k in ('start','end'))*.5 for row,_,_ in own]
    scope = {(h,i) for h,i in blocked if h == handle}
    ratio = frame['ratio'] * POINTS_PER_MM
    options = []
    offsets = [0] if not geometry.get('movableBaseline', True) else sorted(range(-max_offset,max_offset+1,25), key=lambda n: (abs(n),n))
    for offset in offsets:
        if time.monotonic() >= deadline:
            break
        shift = offset * ratio * (1 if normal == 0 else -1)
        if offset and any(source*(source-shift)<=0 or abs(source-shift)<5 for source in stations):
            continue
        shifted_paths = shifted_chain_paths(bound_paths, chain_geometry, shift)
        strokes = [tuple(item[1:]) for path in shifted_paths for item in path['items']]
        if offset and any(any(crosses(line, inflate(box,.16)) for line in strokes) and
                          not any(crosses(line, inflate(box,.16)) for line in initial) for box in numbers + labels):
            continue
        ink = Ink(region)
        ink.image = fixed.image.copy()
        ink.drawer = ImageDraw.Draw(ink.image)
        for path in shifted_paths:
            ink.add_path(path)
        for box in numbers:
            ink.drawer.rectangle((ink.point(box.tl), ink.point(box.br)), fill=255)
        mask = PaperMask(ink)
        choices = [label_slots(page, frame, item, baseline+shift, mask, (handle,item[0]['segment']) in scope,exterior_overhang)
                   for item in own]
        if diagnostics is not None:
            diagnostics.setdefault('offsets',[]).append({'normal':offset,'counts':[len(x) for x in choices]})
        if any(not choices_for_label for choices_for_label in choices):
            continue
        fit = pack_slots(choices, midpoints, along)
        if fit and fit[0] < len(scope):
            options.append((fit[0], fit[1]+abs(shift)*1.8, offset, [item[3] for item in fit[2]]))
    return [{'handle':handle,'normal':offset,'text':text}
            for _, _, offset, text in sorted(options)[:6]]
