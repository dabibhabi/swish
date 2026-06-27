"""
CarLoader — imports a GLB/GLTF file through Blender's bpy pipeline and
builds a CarModel from the resulting scene objects.
"""

from __future__ import annotations
import os
import re

import bpy

from .model import BBox, CarNode, CarModel, CarStats
from .parts import PartClassifier


# ── Name cleaning ────────────────────────────────────────────────────────────

class _NameCleaner:
    """
    Strips Sketchfab-style long prefixes and trailing vertex/face counts
    from bpy object names so they read as clean part identifiers.

    Input:  "hon_civicsi_99_lod0:hon_civicsi_99_lod0_body_black_2013vertices2526faces_EXT_0"
    Output: "body_black"
    """

    # Matches trailing "_NverticesNfaces" with optional disambiguation suffix
    _VCOUNT  = re.compile(r"_\d+vertices\d+faces\d*", re.IGNORECASE)
    # Matches a trailing material token like "_EXT_0", "_CarPaint_0", "_INT_Metal_0"
    _MAT_SUF = re.compile(r"_[A-Za-z][A-Za-z0-9_]*_\d+$")

    def __init__(self, objects: list[bpy.types.Object]) -> None:
        self._prefixes = self._detect_prefixes(objects)

    @staticmethod
    def _detect_prefixes(objects: list[bpy.types.Object]) -> list[str]:
        """
        Find all "LABEL:LABEL_" prefix patterns present in the scene.
        A model exported from Sketchfab with multiple LOD groups will have
        several distinct prefixes (e.g. lod0 and cockpit variants).
        """
        seen: set[str] = set()
        for obj in objects:
            if ":" in obj.name:
                lhs = obj.name.split(":")[0]
                seen.add(f"{lhs}:{lhs}_")
        return sorted(seen, key=len, reverse=True)

    def clean(self, raw: str) -> str:
        name = raw
        # 1. Strip known model prefix
        for prefix in self._prefixes:
            if name.startswith(prefix):
                name = name[len(prefix):]
                break
        # 2. Strip vertex/face count
        name = self._VCOUNT.sub("", name)
        # 3. Strip trailing material token
        name = self._MAT_SUF.sub("", name)
        return name or raw  # never return empty


# ── Loader ───────────────────────────────────────────────────────────────────

class CarLoader:
    """
    Loads a GLB or GLTF file into a fresh Blender scene and converts the
    resulting bpy object tree into a CarModel.

    Usage:
        model = CarLoader().load("cars/1999_honda_civic_si.glb")
    """

    def __init__(self) -> None:
        self._classifier = PartClassifier()

    # ── Public API ───────────────────────────────────────────────────

    def load(self, path: str) -> CarModel:
        if not os.path.isfile(path):
            raise FileNotFoundError(f"GLB/GLTF not found: {path}")

        abs_path = os.path.abspath(path)
        self._reset_scene()
        bpy.ops.import_scene.gltf(filepath=abs_path)

        all_bpy = list(bpy.data.objects)
        cleaner  = _NameCleaner(all_bpy)

        # Build CarNode for every bpy object (parent links resolved below)
        node_map: dict[str, CarNode] = {}
        for obj in all_bpy:
            node = self._make_node(obj, cleaner)
            node_map[obj.name] = node

        # Wire children in the same order Blender reports them
        for obj in all_bpy:
            node = node_map[obj.name]
            node.children = sorted(
                [node_map[c.name] for c in obj.children],
                key=lambda n: n.raw_name,
            )

        # Mesh nodes inherit short_name from their pivot parent when one
        # exists — pivot names have no material suffix so they clean cleanly.
        for obj in all_bpy:
            node = node_map[obj.name]
            if node.is_mesh and obj.parent and obj.parent.name in node_map:
                parent = node_map[obj.parent.name]
                if parent.is_pivot and parent.short_name:
                    node.short_name = parent.short_name

        all_nodes = list(node_map.values())
        roots     = [node_map[o.name] for o in all_bpy if o.parent is None]

        # Classify every node by car-part category
        for node in all_nodes:
            node.category = self._classifier.classify(node.short_name)

        parts  = self._classifier.group(all_nodes)
        stats  = self._compute_stats(os.path.basename(path), all_nodes, roots)
        return CarModel(os.path.basename(path), roots, all_nodes, stats, parts)

    # ── Private helpers ──────────────────────────────────────────────

    @staticmethod
    def _reset_scene() -> None:
        bpy.ops.wm.read_factory_settings(use_empty=True)

    def _make_node(self, obj: bpy.types.Object, cleaner: _NameCleaner) -> CarNode:
        node = CarNode(
            raw_name   = obj.name,
            short_name = cleaner.clean(obj.name),
            obj_type   = obj.type,
        )

        if obj.type == "MESH" and obj.data:
            mesh = obj.data
            node.mesh_verts = len(mesh.vertices)
            node.mesh_faces = len(mesh.polygons)
            node.materials  = [
                ms.material.name
                for ms in obj.material_slots
                if ms.material
            ]
            node.bbox = self._compute_bbox(obj)

        return node

    @staticmethod
    def _compute_bbox(obj: bpy.types.Object) -> "BBox":
        xs, ys, zs = zip(*obj.bound_box)
        return BBox(
            w=abs(max(xs) - min(xs)),
            h=abs(max(zs) - min(zs)),
            d=abs(max(ys) - min(ys)),
        )

    @staticmethod
    def _compute_stats(
        filename: str,
        all_nodes: list[CarNode],
        roots:     list[CarNode],
    ) -> CarStats:
        mesh_nodes = [n for n in all_nodes if n.is_mesh]
        materials  = {m for n in mesh_nodes for m in n.materials}
        max_depth  = max((r.depth() for r in roots), default=0)
        return CarStats(
            filename       = filename,
            total_objects  = len(all_nodes),
            mesh_count     = len(mesh_nodes),
            pivot_count    = sum(1 for n in all_nodes if n.is_pivot),
            material_count = len(materials),
            root_count     = len(roots),
            leaf_count     = sum(1 for n in all_nodes if n.is_leaf),
            max_depth      = max_depth,
            total_verts    = sum(n.mesh_verts for n in mesh_nodes),
            total_faces    = sum(n.mesh_faces for n in mesh_nodes),
        )
