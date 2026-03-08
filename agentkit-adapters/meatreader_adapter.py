#!/usr/bin/env python3
"""Project-specific extraction adapter for meatreader."""

from __future__ import annotations

import re
from typing import Any


HTTP_HANDLER_RE = re.compile(r"\besp_err_t\s+(handle_[A-Za-z_]\w*)\s*\(")
SVELTE_RUNE_RE = re.compile(r"\$(state|derived)\s*\(")
TS_FN_RE = re.compile(r"^\s*(?:export\s+)?function\s+([A-Za-z_]\w*)\s*\(")
TS_ARROW_RE = re.compile(
    r"^\s*(?:export\s+)?(?:const|let|var)\s+([A-Za-z_]\w*)\s*=\s*\([^)]*\)\s*=>\s*\{"
)


def _find_block_end(lines: list[str], start_idx: int) -> int:
    depth = lines[start_idx].count("{") - lines[start_idx].count("}")
    i = start_idx + 1
    while i < len(lines) and depth > 0:
        depth += lines[i].count("{") - lines[i].count("}")
        i += 1
    return i if i > start_idx + 1 else start_idx + 1


def _find_block_start(lines: list[str], start_idx: int, lookahead: int = 6) -> int | None:
    end = min(len(lines), start_idx + lookahead + 1)
    for i in range(start_idx, end):
        if "{" in lines[i]:
            return i
        if ";" in lines[i]:
            return None
    return None


def _emit(symbol: str, kind: str, start_line: int, end_line: int) -> dict[str, Any]:
    return {
        "symbol": symbol,
        "kind": kind,
        "start_line": start_line,
        "end_line": end_line,
    }


def extract(
    path: str,
    rel_path: str,
    ext: str,
    lines: list[str],
    text: str,
    config: dict[str, Any],
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []

    # ESP-IDF HTTP route handlers.
    if "firmware/components/http_server/" in rel_path and ext in {".c", ".h"}:
        for i, line in enumerate(lines):
            m = HTTP_HANDLER_RE.search(line)
            if not m:
                continue
            block_start = _find_block_start(lines, i)
            if block_start is None:
                continue
            end = _find_block_end(lines, block_start)
            out.append(_emit(m.group(1), "http_handler", block_start + 1, end))

    # Svelte/TS live-data/store focused snippets.
    if rel_path.startswith("thermometer-ui/src/") and ext in {".ts", ".svelte"}:
        for i, line in enumerate(lines):
            m = TS_FN_RE.match(line) or TS_ARROW_RE.match(line)
            if m:
                name = m.group(1)
                lname = name.lower()
                if any(k in lname for k in ("live", "event", "stream", "temp", "store", "start", "stop")):
                    end = _find_block_end(lines, i)
                    out.append(_emit(name, "ui_function", i + 1, end))
                    continue

            if SVELTE_RUNE_RE.search(line):
                # include short local window around state/derived runes
                start = max(1, i)
                end = min(len(lines), i + 8)
                out.append(_emit("svelte_rune_block", "ui_state", start, end))

    return out
