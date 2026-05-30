"""
Rendering layer — converts a CarModel into terminal output.

Three independent renderers, each with a single .render(model, opts) method:
  - StatsReporter  : summary table
  - TreeRenderer   : Unicode tree diagram
  - GroupReporter  : nodes grouped by car-part category
"""

from __future__ import annotations
from dataclasses import dataclass
from .model import CarModel, CarNode


# ── Options ──────────────────────────────────────────────────────────────────

@dataclass
class RenderOptions:
    shorten:        bool = False   # use short_name instead of raw_name
    show_materials: bool = False   # list material names on mesh nodes
    show_bbox:      bool = False   # show W×H×D on mesh nodes


# ── Shared helpers ───────────────────────────────────────────────────────────

_TYPE_ICONS = {
    "MESH":     "[mesh]",
    "EMPTY":    "[pivot]",
    "ARMATURE": "[rig]",
    "LIGHT":    "[light]",
    "CAMERA":   "[cam]",
}

_SEP = "─" * 55


def _node_label(node: CarNode, opts: RenderOptions) -> str:
    return node.short_name if opts.shorten else node.raw_name


def _mesh_detail(node: CarNode, opts: RenderOptions) -> str:
    if not node.is_mesh:
        return ""
    parts: list[str] = [f"{node.mesh_verts}v/{node.mesh_faces}f"]
    if opts.show_bbox and node.bbox:
        parts.append(str(node.bbox))
    if opts.show_materials and node.materials:
        parts.append("mats=[" + ", ".join(node.materials) + "]")
    return "  (" + "  ".join(parts) + ")"


# ── Stats reporter ───────────────────────────────────────────────────────────

class StatsReporter:
    def render(self, model: CarModel, _opts: RenderOptions = RenderOptions()) -> None:
        s = model.stats
        print(_SEP)
        print(f"  {s.filename}  ({s.total_objects} objects)")
        print(_SEP)
        print()
        print("── Summary " + "─" * 45)
        print(f"  Objects    : {s.total_objects}"
              f"  ({s.mesh_count} mesh,  {s.pivot_count} pivot/empty)")
        print(f"  Materials  : {s.material_count}")
        print(f"  Root nodes : {s.root_count}")
        print(f"  Leaf nodes : {s.leaf_count}")
        print(f"  Max depth  : {s.max_depth}")
        print(f"  Total verts: {s.total_verts:,}")
        print(f"  Total faces: {s.total_faces:,}")
        print()


# ── Tree renderer ────────────────────────────────────────────────────────────

class TreeRenderer:
    _PIPE  = "│   "
    _TEE   = "├── "
    _ELBOW = "└── "
    _BLANK = "    "

    def render(self, model: CarModel, opts: RenderOptions) -> None:
        print("── Node Tree " + "─" * 42)
        roots = sorted(model.roots, key=lambda n: n.raw_name)
        for i, root in enumerate(roots):
            self._print_node(root, prefix="", is_last=(i == len(roots) - 1), opts=opts)
        print()

    def _print_node(
        self,
        node:     CarNode,
        prefix:   str,
        is_last:  bool,
        opts:     RenderOptions,
    ) -> None:
        connector  = self._ELBOW if is_last else self._TEE
        icon       = _TYPE_ICONS.get(node.obj_type, f"[{node.obj_type.lower()}]")
        label      = _node_label(node, opts)
        detail     = _mesh_detail(node, opts)
        child_tag  = f"  {len(node.children)} children" if len(node.children) > 1 else ""

        print(f"{prefix}{connector}{label}  {icon}{child_tag}{detail}")

        child_prefix = prefix + (self._BLANK if is_last else self._PIPE)
        children = node.children  # already sorted by loader
        for i, child in enumerate(children):
            self._print_node(child, child_prefix, is_last=(i == len(children) - 1), opts=opts)


# ── Group reporter ───────────────────────────────────────────────────────────

class GroupReporter:
    def render(self, model: CarModel, opts: RenderOptions) -> None:
        print("── By Car Part " + "─" * 40)
        for category, nodes in model.parts.items():
            mesh_nodes = [n for n in nodes if n.is_mesh]
            if not mesh_nodes:
                continue
            print(f"\n  {category}  ({len(mesh_nodes)} mesh nodes)")
            for node in sorted(mesh_nodes, key=lambda n: n.raw_name):
                label  = _node_label(node, opts)
                detail = _mesh_detail(node, opts)
                print(f"    • {label}{detail}")
        print()
