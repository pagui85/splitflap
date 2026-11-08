#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate UTC metadata when image inputs change; SOURCE_DATE_EPOCH makes it repeatable."""
import datetime as dt
import json
import os
from pathlib import Path
import sys


def generate(output: Path) -> None:
    supplied = os.environ.get("SOURCE_DATE_EPOCH")
    if supplied is not None and (not supplied.isascii() or not supplied.isdecimal()):
        raise ValueError("SOURCE_DATE_EPOCH must be nonnegative decimal Unix seconds")
    epoch = int(supplied) if supplied is not None else int(dt.datetime.now(dt.timezone.utc).timestamp())
    stamp = dt.datetime.fromtimestamp(epoch, dt.timezone.utc)
    if not 1970 <= stamp.year <= 2099:
        raise ValueError("Build UTC must be between 1970 and 2099")
    iso = stamp.strftime("%Y-%m-%dT%H:%M:%SZ")
    source = "SOURCE_DATE_EPOCH" if supplied is not None else "build_host_utc"
    content = ("// Generated; no credentials or local paths.\n#pragma once\n#include <cstdint>\n"
               "namespace bringup {\n"
               f"inline constexpr std::int64_t kBuildEpoch = {epoch}LL;\n"
               f"inline constexpr char kBuildUtc[] = {json.dumps(iso)};\n"
               f"inline constexpr char kBuildSource[] = {json.dumps(source)};\n" + "}\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != content:
        output.write_text(content)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: generate_metadata.py OUTPUT_HEADER")
    try:
        generate(Path(sys.argv[1]))
    except (ValueError, OverflowError, OSError) as error:
        raise SystemExit(str(error))
