"""Resolve and fingerprint the tools selected by an existing ESP-IDF build."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess


def file_identity(path: Path) -> dict:
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": path.as_posix(), "sha256": digest}


def _absolute_path(value: str, label: str, *, directory: bool = False) -> Path:
    path = Path(value)
    if not path.is_absolute():
        raise ValueError(f"{label}: expected an absolute generated path")
    path = path.resolve(strict=True)
    if not (path.is_dir() if directory else path.is_file()):
        raise ValueError(f"{label}: generated path has the wrong file type")
    return path


def resolve_build_tools(build: Path, chip: str) -> dict[str, Path]:
    """Use generated metadata, never PATH or a guessed PlatformIO package folder."""
    build = build.resolve(strict=True)
    description = json.loads((build / "project_description.json").read_text(encoding="utf-8"))
    if description["target"] != chip:
        raise ValueError(f"{build}: unexpected target in toolchain metadata")
    if _absolute_path(description["build_dir"], "build_dir", directory=True) != build:
        raise ValueError(f"{build}: stale build directory in toolchain metadata")
    if _absolute_path(description["project_path"], "project_path", directory=True) != build.parents[2]:
        raise ValueError(f"{build}: stale project directory in toolchain metadata")
    compiler = _absolute_path(description["c_compiler"], "c_compiler")
    suffix = ".exe" if compiler.name.endswith(".exe") else ""
    compiler_name = compiler.name.removesuffix(suffix) if suffix else compiler.name
    if not compiler_name.endswith("-gcc"):
        raise ValueError(f"{build}: expected an ESP GCC compiler")
    prefix = compiler_name[:-3]
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value
    if "CMAKE_C_COMPILER" in cache and _absolute_path(cache["CMAKE_C_COMPILER"], "CMAKE_C_COMPILER") != compiler:
        raise ValueError(f"{build}: compiler metadata disagrees with CMake cache")
    result = {"compiler": compiler}
    for name, key in (("nm", "CMAKE_NM"), ("objdump", "CMAKE_OBJDUMP")):
        tool = _absolute_path(cache[key], key)
        if tool.parent != compiler.parent or tool.name != prefix + name + suffix:
            raise ValueError(f"{build}: {key} does not belong to the selected compiler directory")
        result[name] = tool
    result["idf"] = _absolute_path(description["idf_path"], "idf_path", directory=True)
    return result


def toolchain_provenance(build: Path, chip: str) -> dict:
    """Record actual tools separately from source inputs that precede a build."""
    tools = resolve_build_tools(build, chip)
    result = {name: file_identity(tools[name]) for name in ("compiler", "nm", "objdump")}
    try:
        version = subprocess.check_output(
            [str(tools["compiler"]), "--version"], text=True, encoding="utf-8", timeout=15
        ).splitlines()
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"Cannot identify selected compiler: {error}") from error
    if not version:
        raise ValueError("Selected compiler returned no version")
    result["compiler"]["version"] = version[0]
    for name, directory in (("package", tools["compiler"].parent.parent), ("framework", tools["idf"])):
        manifest = directory / "package.json"
        metadata = json.loads(manifest.read_text(encoding="utf-8"))
        result[name] = {**file_identity(manifest), "name": metadata["name"], "version": metadata["version"]}
    return result
