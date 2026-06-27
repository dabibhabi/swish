"""
Core domain objects. No bpy imports here — these are plain data.
"""

from __future__ import annotations
import functools
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class BBox:
    """Axis-aligned bounding box dimensions in local space (metres)."""
    w: float  # width  (X axis)
    h: float  # height (Z axis)
    d: float  # depth  (Y axis)

    def __str__(self) -> str:
        return f"{self.w:.2f}×{self.h:.2f}×{self.d:.2f}m"


@dataclass
class CarNode:
    """
    One Blender object imported from the GLB.

    A node is either a MESH (actual geometry) or an EMPTY/pivot
    (pure transform used to group children). The full hierarchy is
    preserved through the children list.
    """
    raw_name:  str                        # exact bpy object name
    short_name: str                       # prefix-stripped display name
    obj_type:  str                        # MESH | EMPTY | ARMATURE | …
    category:  str          = "Uncategorized"
    mesh_verts: int         = 0
    mesh_faces: int         = 0
    materials: list[str]    = field(default_factory=list)
    bbox:      Optional[BBox] = None
    children:  list[CarNode] = field(default_factory=list)

    @property
    def is_mesh(self) -> bool:
        return self.obj_type == "MESH"

    @property
    def is_pivot(self) -> bool:
        return self.obj_type == "EMPTY"

    @property
    def is_leaf(self) -> bool:
        return len(self.children) == 0

    # Intentionally not a @property — O(depth) traversal cost should be visible at call site.
    def depth(self) -> int:
        if not self.children:
            return 1
        return 1 + max(c.depth() for c in self.children)

    def all_descendants(self) -> list["CarNode"]:
        def _walk(node: "CarNode"):
            for child in node.children:
                yield child
                yield from _walk(child)
        return list(_walk(self))


@dataclass
class CarStats:
    """Aggregated statistics for a loaded car model."""
    filename:       str
    total_objects:  int
    mesh_count:     int
    pivot_count:    int
    material_count: int
    root_count:     int
    leaf_count:     int
    max_depth:      int
    total_verts:    int
    total_faces:    int


class CarModel:
    """
    Top-level container for an imported car model.

    Holds the root-level CarNodes (trees), a flat list of all nodes for
    fast iteration, pre-computed stats, and a part-category index built
    by PartClassifier.
    """

    def __init__(
        self,
        filename:   str,
        roots:      list[CarNode],
        all_nodes:  list[CarNode],
        stats:      CarStats,
        parts:      dict[str, list[CarNode]],   # category → mesh nodes
    ) -> None:
        self.filename  = filename
        self.roots     = roots
        self.all_nodes = all_nodes
        self.stats     = stats
        self.parts     = parts

    @functools.cached_property
    def mesh_nodes(self) -> list[CarNode]:
        return [n for n in self.all_nodes if n.is_mesh]

    @functools.cached_property
    def pivot_nodes(self) -> list[CarNode]:
        return [n for n in self.all_nodes if n.is_pivot]
