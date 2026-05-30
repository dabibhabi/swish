"""
Command-line interface for car_analyzer.

When executed via Blender, sys.argv contains Blender's own args before '--'.
Everything after '--' is the script's argv. When executed directly (standalone
bpy), sys.argv[1:] is used as-is.
"""

from __future__ import annotations
import sys
import os
import argparse


def _script_argv() -> list[str]:
    """Extract argv intended for this script, ignoring Blender's own flags."""
    if "--" in sys.argv:
        return sys.argv[sys.argv.index("--") + 1:]
    return sys.argv[1:]


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog        = "car_analyzer",
        description = "Analyze the node hierarchy of a GLB/GLTF car model.",
        epilog      = (
            "Run via Blender:\n"
            "  blender --background --python tools/car_analyzer/run.py"
            " -- <file.glb> [flags]\n\n"
            "Run standalone (requires 'bpy' from PyPI):\n"
            "  uv run python -m car_analyzer <file.glb> [flags]"
        ),
        formatter_class = argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("file",
                   help="Path to .glb or .gltf file")
    p.add_argument("--shorten", action="store_true",
                   help="Strip long model prefixes from node names")
    p.add_argument("--materials", action="store_true",
                   help="Show material names on mesh nodes")
    p.add_argument("--bbox", action="store_true",
                   help="Show bounding-box dimensions (W×H×D) on mesh nodes")
    p.add_argument("--stats", action="store_true",
                   help="Print summary statistics only — skip the full tree")
    p.add_argument("--group", action="store_true",
                   help="Print nodes grouped by car-part category")
    return p


def main() -> None:
    args = _build_parser().parse_args(_script_argv())

    if not os.path.isfile(args.file):
        sys.exit(f"[car_analyzer] File not found: {args.file}")

    # Imports deferred so --help works without bpy present
    from .loader   import CarLoader
    from .renderer import RenderOptions, StatsReporter, TreeRenderer, GroupReporter

    opts = RenderOptions(
        shorten        = args.shorten,
        show_materials = args.materials,
        show_bbox      = args.bbox,
    )

    model = CarLoader().load(args.file)

    StatsReporter().render(model, opts)

    if not args.stats:
        TreeRenderer().render(model, opts)

    if args.group:
        GroupReporter().render(model, opts)
