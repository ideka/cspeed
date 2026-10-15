#!/usr/bin/env python3
"""Config script for recomp game template"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
EXCLUDED_DIRS = {".git", "build", "build-cmake", "cmake-build", "out", "repo", "node_modules"}
EXCLUDED_FILES = {Path(__file__).name}


def identifier_words(value: str) -> list[str]:
    return re.findall(r"[A-Za-z0-9]+", value)


def pascal_case(value: str) -> str:
    return "".join(word[:1].upper() + word[1:] for word in identifier_words(value))


def slugify(value: str) -> str:
    return "-".join(word.lower() for word in identifier_words(value))


def iter_files() -> list[Path]:
    return [
        path
        for path in ROOT.rglob("*")
        if path.is_file()
        and path.name not in EXCLUDED_FILES
        and not any(part in EXCLUDED_DIRS for part in path.relative_to(ROOT).parts)
    ]


def read_text(path: Path) -> str | None:
    try:
        data = path.read_bytes()
        if b"\0" in data:
            return None
        return data.decode("utf-8")
    except (OSError, UnicodeDecodeError):
        return None


def replace_contents(replacements: list[tuple[str, str]], dry_run: bool) -> int:
    changed = 0
    for path in iter_files():
        content = read_text(path)
        if content is None:
            continue

        updated = content
        for old, new in replacements:
            updated = updated.replace(old, new)

        if updated != content:
            changed += 1
            print(f"{'Would update' if dry_run else 'Updated'} {path.relative_to(ROOT)}")
            if not dry_run:
                path.write_text(updated, encoding="utf-8", newline="")
    return changed


def rename_paths(replacements: list[tuple[str, str]], dry_run: bool) -> int:
    changed = 0
    paths = sorted(
        (path for path in ROOT.rglob("*") if path.name not in EXCLUDED_FILES),
        key=lambda path: len(path.parts),
        reverse=True,
    )

    for path in paths:
        relative = path.relative_to(ROOT)
        if any(part in EXCLUDED_DIRS for part in relative.parts):
            continue

        new_name = path.name
        for old, new in replacements:
            new_name = new_name.replace(old, new)

        if new_name == path.name:
            continue

        destination = path.with_name(new_name)
        if destination.exists():
            raise FileExistsError(f"Cannot rename {relative}: {destination.relative_to(ROOT)} already exists")

        changed += 1
        print(
            f"{'Would rename' if dry_run else 'Renamed'} "
            f"{relative} -> {destination.relative_to(ROOT)}"
        )
        if not dry_run:
            path.rename(destination)
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Configure the N64 recomp template for a game."
    )
    parser.add_argument("game_name", help='Display name, for example "Super Mario 64"')
    parser.add_argument(
        "--game-id",
        help="Lowercase code identifier (default: display name with punctuation removed)",
    )
    parser.add_argument(
        "--project-name",
        help="Executable/project name (default: <GameName>Recompiled)",
    )
    parser.add_argument(
        "--app-id",
        help="Desktop/Flatpak application ID (default: io.github.<game-id>.<project-name>)",
    )
    parser.add_argument(
        "--rom-stem",
        help="ROM/config stem without an extension (default: <game-id>.us)",
    )
    parser.add_argument(
        "--repository",
        help="Project repository URL (default: https://github.com/User/<project-name>)",
    )
    parser.add_argument("--preview", action="store_true", help="Show changes without writing them")
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z]+(?: [A-Za-z]+)*", args.game_name):
        parser.error("game_name may contain only letters and single spaces between words")

    words = identifier_words(args.game_name)

    game_type = pascal_case(args.game_name)
    game_id = args.game_id or "".join(word.lower() for word in words)
    if not re.fullmatch(r"[a-z_][a-z0-9_]*", game_id):
        parser.error("--game-id must be a valid lowercase C/C++ identifier")

    project_name = args.project_name or f"{game_type}Recompiled"
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", project_name):
        parser.error("--project-name must be a valid CMake target/file identifier")

    project_slug = slugify(project_name)
    app_id = args.app_id or f"io.github.{game_id}.{project_slug}"
    rom_stem = args.rom_stem or f"{game_id}.us"
    repository = args.repository or f"https://github.com/User/{project_name}"
    symbol_dir = f"{game_type}RecompSyms"

    replacements = [
        ("https://github.com/User/MyGameRecomp", repository),
        ("io.github.n64gamerecomp.n64gamerecomp", app_id),
        ("MyGameRecompSyms", symbol_dir),
        ("MyGameRecompiled", project_name),
        ("n64gamerecomp", game_id),
        ("mygame.ver", rom_stem),
        ("MYGAME", game_id.upper()),
        ("MyGame", game_type),
        ("mygame", game_id),
        ("My Game", args.game_name),
    ]

    file_changes = replace_contents(replacements, args.preview)
    path_changes = rename_paths(replacements, args.preview)
    action = "Would change" if args.preview else "Changed"
    print(f"{action} {file_changes} file(s) and rename {path_changes} path(s).")


if __name__ == "__main__":
    main()
