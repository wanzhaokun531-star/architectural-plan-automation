"""Read a local SKP through SketchUp's C API and export geometry in millimetres."""

import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path


class Ref(ct.Structure):
    _fields_ = [("ptr", ct.c_void_p)]


class Point(ct.Structure):
    _fields_ = [(axis, ct.c_double) for axis in "xyz"]


class Transform(ct.Structure):
    _fields_ = [("values", ct.c_double * 16)]


REF = ct.POINTER(Ref)
SIZE = ct.c_size_t
SU_ERROR_NO_DATA = 9
IDENTITY = Transform((ct.c_double * 16)(*(int(i % 5 == 0) for i in range(16))))


class SketchUp:
    def __init__(self, dll):
        self.dll = dll
        self.functions = {}

    def function(self, name, args, result=ct.c_int):
        if name not in self.functions:
            fn = getattr(self.dll, name)
            fn.argtypes = args
            fn.restype = result
            self.functions[name] = fn
        return self.functions[name]

    def call(self, name, args, *values):
        code = self.function(name, args)(*values)
        if code:
            raise ValueError(f"{name} failed (SUResult {code})")

    def value(self, name, ref, type_=Ref):
        result = type_()
        self.call(name, [Ref, ct.POINTER(type_)], ref, ct.byref(result))
        return result

    def refs(self, base, plural, owner, standalone_only=None):
        count_name = f"{base}GetNum{plural}"
        read_name = f"{base}Get{plural}"
        args = [Ref] if standalone_only is None else [Ref, ct.c_bool]
        values = [owner] if standalone_only is None else [owner, standalone_only]
        count = SIZE()
        self.call(count_name, [*args, ct.POINTER(SIZE)], *values, ct.byref(count))
        if not count.value:
            return []
        refs = (Ref * count.value)()
        written = SIZE()
        self.call(read_name, [*args, SIZE, REF, ct.POINTER(SIZE)],
                  *values, count.value, refs, ct.byref(written))
        if written.value != count.value:
            raise ValueError(f"{read_name} returned an incomplete list")
        return refs

    def entity_id(self, type_name, ref):
        entity = self.function(f"{type_name}ToEntity", [Ref], Ref)(ref)
        return self.value("SUEntityGetPersistentID", entity, ct.c_int64).value

    def name(self, getter, ref):
        string = Ref()
        self.call("SUStringCreate", [REF], ct.byref(string))
        try:
            self.call(getter, [Ref, REF], ref, ct.byref(string))
            length = self.value("SUStringGetUTF8Length", string, SIZE).value
            buffer = ct.create_string_buffer(length + 1)
            self.call("SUStringGetUTF8", [Ref, SIZE, ct.c_char_p, ct.POINTER(SIZE)],
                      string, len(buffer), buffer, ct.byref(SIZE()))
            return buffer.value.decode("utf-8")
        finally:
            self.call("SUStringRelease", [REF], ct.byref(string))

    def tag_state(self, layer):
        name = self.name("SULayerGetName", layer)
        visible = self.value("SULayerGetVisibility", layer, ct.c_bool).value
        folder = Ref()
        getter = "SULayerGetParentLayerFolder"
        current = layer
        while True:
            # SU_ERROR_NO_DATA denotes a tag/folder with no parent folder.
            code = self.function(getter, [Ref, REF])(current, ct.byref(folder))
            if code == SU_ERROR_NO_DATA:
                break
            if code:
                raise ValueError(f"{getter} failed (SUResult {code})")
            visible &= self.value("SULayerFolderGetVisibility", folder, ct.c_bool).value
            getter = "SULayerFolderGetParentLayerFolder"
            current, folder = folder, Ref()
        return name, visible

    def metadata(self, type_name, ref, parent_visible, tags):
        element = self.function(f"{type_name}ToDrawingElement", [Ref], Ref)(ref)
        layer = self.value("SUDrawingElementGetLayer", element)
        if layer.ptr not in tags:
            tags[layer.ptr] = self.tag_state(layer)
        tag, tag_visible = tags[layer.ptr]
        hidden = self.value("SUDrawingElementGetHidden", element, ct.c_bool).value
        return {"id": self.entity_id(type_name, ref), "tag": tag,
                "hidden": hidden, "visible": parent_visible and tag_visible and not hidden}

    def point_mm(self, vertex, transform):
        return self.transform_mm(self.value("SUVertexGetPosition", vertex, Point), transform)

    def transform_mm(self, p, transform):
        self.call("SUPoint3DTransform", [ct.POINTER(Transform), ct.POINTER(Point)],
                  ct.byref(transform), ct.byref(p))
        return [p.x * 25.4, p.y * 25.4, p.z * 25.4]

    def face_mesh(self, face, transform):
        mesh = Ref()
        self.call("SUMeshHelperCreate", [REF, Ref], ct.byref(mesh), face)
        try:
            nvertices = self.value("SUMeshHelperGetNumVertices", mesh, SIZE).value
            nindices = 3 * self.value("SUMeshHelperGetNumTriangles", mesh, SIZE).value
            vertices = (Point * nvertices)()
            indices = (SIZE * nindices)()
            for name, array, length, element_type in (
                ("SUMeshHelperGetVertices", vertices, nvertices, Point),
                ("SUMeshHelperGetVertexIndices", indices, nindices, SIZE),
            ):
                written = SIZE()
                self.call(name, [Ref, SIZE, ct.POINTER(element_type), ct.POINTER(SIZE)],
                          mesh, length, array, ct.byref(written))
                if written.value != length:
                    raise ValueError(f"{name} returned incomplete mesh data")
            return {"vertices_mm": [self.transform_mm(p, transform) for p in vertices],
                    "triangle_indices": list(indices)}
        finally:
            self.call("SUMeshHelperRelease", [REF], ct.byref(mesh))

    def opening_rings_mm(self, face, transform):
        count = self.value("SUFaceGetNumOpenings", face, SIZE).value
        if not count:
            return []
        openings = (Ref * count)()
        written = SIZE()
        try:
            self.call("SUFaceGetOpenings", [Ref, SIZE, REF, ct.POINTER(SIZE)],
                      face, count, openings, ct.byref(written))
            if written.value != count:
                raise ValueError("SUFaceGetOpenings returned an incomplete list")
            rings = []
            for opening in openings:
                npoints = self.value("SUOpeningGetNumPoints", opening, SIZE).value
                points = (Point * npoints)()
                self.call("SUOpeningGetPoints", [Ref, SIZE, ct.POINTER(Point), ct.POINTER(SIZE)],
                          opening, npoints, points, ct.byref(written))
                if npoints < 3 or written.value != npoints:
                    raise ValueError("SUOpeningGetPoints returned an incomplete ring")
                rings.append([self.transform_mm(point, transform) for point in points])
            return rings
        finally:
            for opening in openings:
                if opening.ptr:
                    self.call("SUOpeningRelease", [REF], ct.byref(opening))

    def extract(self, model, include_mesh):
        output = {"instances": [], "faces": [], "edges": [], "omitted": []}
        tags = {}

        def walk(entities, parent, path, visible):
            for face in self.refs("SUEntities", "Faces", entities):
                item = self.metadata("SUFace", face, visible, tags)
                loops = [self.value("SUFaceGetOuterLoop", face),
                         *self.refs("SUFace", "InnerLoops", face)]
                openings = self.opening_rings_mm(face, parent)
                item.update(path=path, loops_mm=[
                    [self.point_mm(vertex, parent) for vertex in self.refs("SULoop", "Vertices", loop)]
                    for loop in loops
                ], attached_openings=len(openings))
                if openings:
                    item["opening_rings_mm"] = openings
                if include_mesh:
                    item["mesh"] = self.face_mesh(face, parent)
                output["faces"].append(item)

            for edge in self.refs("SUEntities", "Edges", entities, False):
                item = self.metadata("SUEdge", edge, visible, tags)
                item.update(path=path, points_mm=[
                    self.point_mm(self.value(f"SUEdgeGet{end}Vertex", edge), parent)
                    for end in ("Start", "End")
                ], soft=self.value("SUEdgeGetSoft", edge, ct.c_bool).value,
                    smooth=self.value("SUEdgeGetSmooth", edge, ct.c_bool).value,
                    face_ids=[self.entity_id("SUFace", face)
                              for face in self.refs("SUEdge", "Faces", edge)])
                output["edges"].append(item)

            for plural in ("Images", "SectionPlanes", "Polyline3ds"):
                count = self.value(f"SUEntitiesGetNum{plural}", entities, SIZE).value
                if count:
                    output["omitted"].append({"path": path, "type": plural, "count": count})

            for plural, kind in (("Groups", "SUGroup"), ("Instances", "SUComponentInstance")):
                for instance in self.refs("SUEntities", plural, entities):
                    item = self.metadata(kind, instance, visible, tags)
                    local = self.value(f"{kind}GetTransform", instance, Transform)
                    world = Transform()
                    self.call("SUTransformationMultiply",
                              [ct.POINTER(Transform)] * 3,
                              ct.byref(parent), ct.byref(local), ct.byref(world))
                    child_path = [*path, item["id"]]
                    if kind == "SUGroup":
                        child = self.value("SUGroupGetEntities", instance)
                        definition_name = ""
                    else:
                        definition = self.value("SUComponentInstanceGetDefinition", instance)
                        child = self.value("SUComponentDefinitionGetEntities", definition)
                        definition_name = self.name("SUComponentDefinitionGetName", definition)
                    item.update(path=child_path, type=kind, name=self.name(f"{kind}GetName", instance),
                                definition_name=definition_name,
                                world_transform_inches=list(world.values))
                    output["instances"].append(item)
                    walk(child, world, child_path, item["visible"])

        walk(self.value("SUModelGetEntities", model), IDENTITY, [], True)
        return output


def read_skp(source, api_dll, include_mesh=False):
    source = Path(source).resolve(strict=True)
    api_dll = Path(api_dll).resolve(strict=True)
    with source.open("rb") as stream:
        sha256 = hashlib.file_digest(stream, "sha256").hexdigest()
    with os.add_dll_directory(str(api_dll.parent)):
        api = SketchUp(ct.CDLL(str(api_dll)))
        api.function("SUInitialize", [], None)()
        model = Ref()
        try:
            status = ct.c_int()
            api.call("SUModelCreateFromFileWithStatus", [REF, ct.c_char_p, ct.POINTER(ct.c_int)],
                     ct.byref(model), str(source).encode("utf-8"), ct.byref(status))
            if status.value != 0:
                raise ValueError("The SKP was saved by a newer SketchUp version. Use a matching API.")
            geometry = api.extract(model, include_mesh)
        finally:
            try:
                if model.ptr:
                    api.call("SUModelRelease", [REF], ct.byref(model))
            finally:
                api.function("SUTerminate", [], None)()
    with source.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != sha256:
            raise ValueError("Source SKP changed while reading")
    return dict(source=str(source), sha256=sha256, api_dll=str(api_dll), units="mm",
                visibility_basis="saved model flags and tag folders", **geometry)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="new JSON file")
    parser.add_argument("--api-dll", type=Path, required=True)
    parser.add_argument("--mesh", action="store_true", help="include native face tessellation for review")
    args = parser.parse_args()
    geometry = read_skp(args.source, args.api_dll, include_mesh=args.mesh)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(geometry, stream, ensure_ascii=False, allow_nan=False)
