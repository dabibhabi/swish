"""
PartClassifier — maps node short-names to human-readable car part categories.

Rules are tested in order; first match wins. The regex patterns are matched
case-insensitively against the short name of the node.
"""

from __future__ import annotations
import re
from .model import CarNode


class PartClassifier:
    """
    Classifies car nodes into named categories based on their short name.

    Categories are ordered from most-specific to least-specific so that,
    e.g., "headlightL_rubber_trim" hits Headlights before Trim / Rubber.
    """

    # (pattern, category) — tested in order, first match wins.
    #
    # Note: node names use underscores as separators; \b does not mark
    # underscore boundaries (since _ is \w). Patterns are written without
    # \b so they match substrings of underscore-delimited tokens.
    RULES: list[tuple[str, str]] = [
        # ── Steering must come BEFORE wheel to catch "steering_wheel" ─
        (r"steering",                               "Gauges & Steering"),
        (r"gauge|tach|speed_min|fuel_min",          "Gauges & Steering"),

        # ── Wheels & tyres ────────────────────────────────────────────
        # (?<!t)rim avoids false-positive match on "trim" (t-r-i-m)
        (r"wheel|tire|tyre|(?<!t)rim",              "Wheels"),

        # ── Brakes ───────────────────────────────────────────────────
        (r"caliper|rotor|disc|brake",               "Brakes"),

        # ── Bumpers ──────────────────────────────────────────────────
        (r"bumper",                                 "Bumpers"),

        # ── Headlights ───────────────────────────────────────────────
        (r"headlight|headlamp|head_light",          "Headlights"),

        # ── Tail lights ──────────────────────────────────────────────
        (r"taillight|tail_light|taillamp",          "Tail Lights"),

        # ── Glass & windows ──────────────────────────────────────────
        (r"glass|window|windshield|windscreen",     "Glass / Windows"),

        # ── Doors ────────────────────────────────────────────────────
        (r"door",                                   "Doors"),

        # ── Hood / bonnet ────────────────────────────────────────────
        (r"hood|bonnet",                            "Hood"),

        # ── Trunk / boot ─────────────────────────────────────────────
        (r"trunk|boot",                             "Trunk"),

        # ── Exhaust ──────────────────────────────────────────────────
        (r"exhaust|muffler",                        "Exhaust"),

        # ── Mirrors ──────────────────────────────────────────────────
        (r"mirror",                                 "Mirrors"),

        # ── Skirts / side-panels ─────────────────────────────────────
        (r"skirt",                                  "Side Skirts"),

        # ── Seats ────────────────────────────────────────────────────
        (r"seat",                                   "Seats"),

        # ── Interior (broad — must come after specific interior parts) ─
        (r"interior|dashboard|dash|console"
         r"|headliner|cockpit",                     "Interior"),

        # ── Body panels ──────────────────────────────────────────────
        (r"body|carpaint|car_paint",                "Body Panels"),

        # ── Emblems & badges ─────────────────────────────────────────
        (r"emblem|badge|logo",                      "Emblems"),

        # ── Chassis & undercarriage ──────────────────────────────────
        (r"undercarriage|frame|chassis",            "Chassis"),

        # ── Rubber & trim (catch-all for seals, weatherstripping) ────
        (r"rubber|trim",                            "Trim / Rubber"),

        # ── Generic lights ───────────────────────────────────────────
        (r"light",                                  "Lights (other)"),
    ]

    def __init__(self) -> None:
        # Pre-compile all patterns for speed
        self._compiled = [
            (re.compile(pat, re.IGNORECASE), cat)
            for pat, cat in self.RULES
        ]

    def classify(self, short_name: str) -> str:
        """Return the category label for a node's short name."""
        for pattern, category in self._compiled:
            if pattern.search(short_name):
                return category
        return "Uncategorized"

    def group(self, nodes: list[CarNode]) -> dict[str, list[CarNode]]:
        """
        Partition mesh nodes into a dict keyed by category.
        Non-mesh nodes are skipped (they are structural pivots).
        """
        groups: dict[str, list[CarNode]] = {}
        for node in nodes:
            if not node.is_mesh:
                continue
            cat = node.category
            groups.setdefault(cat, []).append(node)
        return dict(sorted(groups.items()))
