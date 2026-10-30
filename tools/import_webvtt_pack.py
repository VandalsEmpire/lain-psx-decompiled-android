#!/usr/bin/env python3
"""Convert lainTSX WebVTT subtitle files into a native Lain SRT pack.

The native client looks for <data-dir>/lang/<code>/subtitles/index.txt and
loads the SRT files listed there. This script keeps each WebVTT basename so
node tracks such as Cou001 and Lda001 keep their existing identifiers.

Example:
  python3 tools/import_webvtt_pack.py \
    --input /path/to/lainvtt/pt-BR \
    --output /path/to/lain-data/lang/pt-BR \
    --name "Português brasileiro"

No game data is downloaded or included. The input folder must come from the
separate lainvtt translation repository.
"""

from __future__ import annotations

import argparse
import html
import re
import sys
from pathlib import Path

TIMESTAMP_RE = re.compile(
    r"(?P<stamp>(?:\d+:)?\d{2}:\d{2}[.,]\d{3})\s*-->\s*"
    r"(?P<end>(?:\d+:)?\d{2}:\d{2}[.,]\d{3})(?:\s+.*)?$"
)
TAG_RE = re.compile(r"<[^>]*>")


def to_srt_timestamp(value: str) -> str:
    """Convert WebVTT mm:ss.mmm or hh:mm:ss.mmm into SRT hh:mm:ss,mmm."""
    value = value.strip().replace(",", ".")
    parts = value.split(":")
    if len(parts) == 2:
        hours = 0
        minutes_text, seconds_text = parts
    elif len(parts) == 3:
        hours_text, minutes_text, seconds_text = parts
        hours = int(hours_text)
    else:
        raise ValueError(f"invalid WebVTT timestamp: {value!r}")

    if "." not in seconds_text:
        raise ValueError(f"timestamp has no millisecond component: {value!r}")
    seconds_text, millis_text = seconds_text.split(".", 1)
    if len(millis_text) != 3 or not millis_text.isdigit():
        raise ValueError(f"timestamp must have three millisecond digits: {value!r}")

    minutes = int(minutes_text)
    seconds = int(seconds_text)
    millis = int(millis_text)
    if not 0 <= minutes <= 59 or not 0 <= seconds <= 59:
        raise ValueError(f"timestamp component out of range: {value!r}")
    return f"{hours:02d}:{minutes:02d}:{seconds:02d},{millis:03d}"


def clean_cue_text(lines: list[str]) -> str:
    """Remove WebVTT presentation tags while retaining readable cue text."""
    text = "\n".join(lines).strip()
    # WebVTT voice/class/language/bold/italic tags do not have an equivalent in
    # the native simple SRT renderer. Their text content remains intact.
    text = TAG_RE.sub("", text)
    text = html.unescape(text)
    return text.strip()


def convert_webvtt(source: str) -> str:
    """Return SRT text from one WebVTT document."""
    source = source.lstrip("\ufeff").replace("\r\n", "\n").replace("\r", "\n")
    blocks = re.split(r"\n\s*\n", source)
    cues: list[tuple[str, str, str]] = []

    for block in blocks:
        lines = [line.rstrip() for line in block.split("\n")]
        if not lines:
            continue
        first = lines[0].strip()
        if first == "WEBVTT" or first.startswith(("NOTE", "STYLE", "REGION")):
            continue

        timing_index = next(
            (i for i, line in enumerate(lines) if "-->" in line), None
        )
        if timing_index is None:
            continue
        match = TIMESTAMP_RE.match(lines[timing_index].strip())
        if not match:
            continue

        start = to_srt_timestamp(match.group("stamp"))
        end = to_srt_timestamp(match.group("end"))
        if end <= start:
            continue
        cue_text = clean_cue_text(lines[timing_index + 1 :])
        if cue_text:
            cues.append((start, end, cue_text))

    if not cues:
        return ""

    rendered = []
    for number, (start, end, text) in enumerate(cues, start=1):
        rendered.append(f"{number}\n{start} --> {end}\n{text}")
    return "\n\n".join(rendered) + "\n"


def build_pack(input_dir: Path, output_dir: Path, name: str) -> tuple[int, int]:
    if not input_dir.is_dir():
        raise FileNotFoundError(f"input directory does not exist: {input_dir}")

    vtt_files = sorted(input_dir.glob("*.vtt"))
    if not vtt_files:
        raise FileNotFoundError(f"no .vtt files found in: {input_dir}")

    subtitle_dir = output_dir / "subtitles"
    subtitle_dir.mkdir(parents=True, exist_ok=True)
    index_names: list[str] = []
    skipped = 0

    for source_path in vtt_files:
        converted = convert_webvtt(source_path.read_text(encoding="utf-8-sig"))
        if not converted:
            skipped += 1
            continue
        target_name = source_path.stem + ".srt"
        (subtitle_dir / target_name).write_text(converted, encoding="utf-8", newline="\n")
        index_names.append(target_name)

    if not index_names:
        raise ValueError("all input files were empty or contained no valid subtitle cues")

    (subtitle_dir / "index.txt").write_text(
        "\n".join(index_names) + "\n", encoding="utf-8", newline="\n"
    )
    (output_dir / "pack.txt").write_text(
        f"name = {name}\nauthor = lainTSX WebVTT contributors\n", 
        encoding="utf-8",
        newline="\n",
    )
    return len(index_names), skipped


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="lainvtt language folder, e.g. pt-BR")
    parser.add_argument("--output", required=True, type=Path, help="native pack output folder, e.g. data/lang/pt-BR")
    parser.add_argument("--name", default="Português brasileiro", help="display name written to pack.txt")
    args = parser.parse_args(argv)

    try:
        converted, skipped = build_pack(args.input, args.output, args.name)
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"subtitle import failed: {exc}", file=sys.stderr)
        return 1

    print(f"Imported {converted} subtitle tracks into {args.output}")
    if skipped:
        print(f"Skipped {skipped} empty or unsupported files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
