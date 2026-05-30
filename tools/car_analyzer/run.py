#!/usr/bin/env python3
"""
Entry point when running through Blender's interpreter:
    blender --background --python tools/car_analyzer/run.py -- <file.glb> [flags]

Adds this directory to sys.path so the car_analyzer package is importable,
then delegates entirely to car_analyzer.cli.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from car_analyzer.cli import main  # noqa: E402

main()
