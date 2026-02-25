#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "NovaBridge" / "Source"

FORBIDDEN_EDITOR_MODULES = {
    "UnrealEd",
    "EditorScriptingUtilities",
    "LevelEditor",
    "AssetTools",
    "MaterialEditor",
    "KismetCompiler",
    "BlueprintGraph",
}

FORBIDDEN_EDITOR_INCLUDE_TOKENS = (
    "editor.h",
    "unrealed",
    "assettoolsmodule",
    "leveleditor",
    "materialeditor",
    "kismetcompiler",
    "blueprintgraph",
    "editorscriptingutilities",
)


def parse_module_dependencies(build_cs_path: Path) -> set[str]:
    text = build_cs_path.read_text(encoding="utf-8")
    modules: set[str] = set()

    add_range_pattern = re.compile(
        r"(?:PublicDependencyModuleNames|PrivateDependencyModuleNames)\.AddRange\(new\s+string\[\]\s*\{(?P<body>.*?)\}\);",
        re.DOTALL,
    )
    add_single_pattern = re.compile(
        r"(?:PublicDependencyModuleNames|PrivateDependencyModuleNames)\.Add\(\s*\"([^\"]+)\"\s*\);"
    )

    for match in add_range_pattern.finditer(text):
        body = match.group("body")
        modules.update(re.findall(r'"([^\"]+)"', body))

    for single_match in add_single_pattern.finditer(text):
        modules.add(single_match.group(1))

    return modules


def collect_includes(module_dir: Path) -> list[tuple[Path, str]]:
    include_pattern = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
    includes: list[tuple[Path, str]] = []
    for file_path in sorted(module_dir.rglob("*")):
        if file_path.suffix.lower() not in {".h", ".hpp", ".cpp", ".cxx", ".inl"}:
            continue
        text = file_path.read_text(encoding="utf-8")
        for match in include_pattern.finditer(text):
            includes.append((file_path, match.group(1)))
    return includes


def extract_declared_handlers(header_path: Path) -> set[str]:
    text = header_path.read_text(encoding="utf-8")
    return set(re.findall(r"\bbool\s+(Handle[A-Za-z0-9_]+)\s*\(", text))


def extract_editor_route_bindings(cpp_path: Path) -> list[tuple[str, str, str]]:
    text = cpp_path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"BindWithAuditName\s*\(\s*TEXT\(\"([^\"]+)\"\)\s*,\s*([^,]+?)\s*,\s*&FNovaBridgeModule::([A-Za-z0-9_]+)\s*\)\s*;"
    )
    return [(m.group(1), re.sub(r"\s+", "", m.group(2)), m.group(3)) for m in pattern.finditer(text)]


def extract_runtime_route_bindings(cpp_path: Path) -> list[tuple[str, str, str]]:
    text = cpp_path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"Bind\s*\(\s*TEXT\(\"([^\"]+)\"\)\s*,\s*([^,]+?)\s*,\s*(?:true|false)\s*,\s*&FNovaBridgeRuntimeModule::([A-Za-z0-9_]+)\s*\)\s*;"
    )
    return [(m.group(1), re.sub(r"\s+", "", m.group(2)), m.group(3)) for m in pattern.finditer(text)]


def check_duplicate_route_bindings(bindings: list[tuple[str, str, str]], scope: str) -> list[str]:
    errors: list[str] = []
    seen: dict[tuple[str, str], str] = {}
    for route, verb_expr, handler in bindings:
        key = (route, verb_expr)
        if key in seen:
            errors.append(
                f"[{scope}] duplicate route binding for {route} {verb_expr}: {seen[key]} and {handler}"
            )
        else:
            seen[key] = handler
    return errors


def main() -> int:
    errors: list[str] = []

    core_build = SOURCE_ROOT / "NovaBridgeCore" / "NovaBridgeCore.Build.cs"
    runtime_build = SOURCE_ROOT / "NovaBridgeRuntime" / "NovaBridgeRuntime.Build.cs"

    for build_path, scope in ((core_build, "NovaBridgeCore"), (runtime_build, "NovaBridgeRuntime")):
        deps = parse_module_dependencies(build_path)
        forbidden = sorted(FORBIDDEN_EDITOR_MODULES.intersection(deps))
        if forbidden:
            errors.append(
                f"[{scope}] depends on editor-only modules: {', '.join(forbidden)}"
            )

    for module_name in ("NovaBridgeCore", "NovaBridgeRuntime"):
        module_dir = SOURCE_ROOT / module_name
        for file_path, include in collect_includes(module_dir):
            lowered = include.lower()
            if any(token in lowered for token in FORBIDDEN_EDITOR_INCLUDE_TOKENS):
                rel = file_path.relative_to(ROOT)
                errors.append(
                    f"[{module_name}] editor-only include in {rel}: #include \"{include}\""
                )

    editor_header = SOURCE_ROOT / "NovaBridge" / "Public" / "NovaBridgeModule.h"
    editor_cpp = SOURCE_ROOT / "NovaBridge" / "Private" / "NovaBridgeHttpServer.cpp"
    runtime_header = SOURCE_ROOT / "NovaBridgeRuntime" / "Public" / "NovaBridgeRuntimeModule.h"
    runtime_cpp = SOURCE_ROOT / "NovaBridgeRuntime" / "Private" / "NovaBridgeRuntimeHttpServer.cpp"

    editor_handlers = extract_declared_handlers(editor_header)
    runtime_handlers = extract_declared_handlers(runtime_header)

    editor_bindings = extract_editor_route_bindings(editor_cpp)
    runtime_bindings = extract_runtime_route_bindings(runtime_cpp)

    if not editor_bindings:
        errors.append("[NovaBridge] failed to parse editor route bindings from NovaBridgeHttpServer.cpp")
    if not runtime_bindings:
        errors.append("[NovaBridgeRuntime] failed to parse runtime route bindings from NovaBridgeRuntimeHttpServer.cpp")

    errors.extend(check_duplicate_route_bindings(editor_bindings, "NovaBridge"))
    errors.extend(check_duplicate_route_bindings(runtime_bindings, "NovaBridgeRuntime"))

    for route, _, handler in editor_bindings:
        if handler not in editor_handlers:
            errors.append(
                f"[NovaBridge] route {route} references missing handler declaration: {handler}"
            )

    for route, _, handler in runtime_bindings:
        if handler not in runtime_handlers:
            errors.append(
                f"[NovaBridgeRuntime] route {route} references missing handler declaration: {handler}"
            )

    if errors:
        print("NovaBridge C++ static guard checks FAILED:")
        for issue in errors:
            print(f" - {issue}")
        return 1

    print("NovaBridge C++ static guard checks passed.")
    print(f" - Parsed editor routes: {len(editor_bindings)}")
    print(f" - Parsed runtime routes: {len(runtime_bindings)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
