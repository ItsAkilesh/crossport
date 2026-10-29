#!/usr/bin/env python3
# File: tools/gen-vulkan-thunks.py
# Purpose: Generate the shim's Vulkan entry points (SysV to Microsoft ABI thunks) from the pinned Vulkan registry
# Author: Akilesh
# Date: 2026-10-29
#
# Usage: gen-vulkan-thunks.py <vk.xml> <output.c>
#
# Every Vulkan command becomes `LIBC_FN type sysv_vkName(...)`, which resolves the real entry point in
# vulkan-1.dll on first use and calls it. Vulkan's own types are fixed width, so the only difference
# between the two sides is the calling convention. Commands that need real logic (instance creation,
# GetProcAddr, window surfaces) are written by hand in shim/vulkan/ and listed in HANDWRITTEN.

import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

HANDWRITTEN = {
    "vkCreateInstance",
    "vkDestroyInstance",
    "vkGetInstanceProcAddr",
    "vkGetDeviceProcAddr",
}
# C types by name for everything a command parameter or result can be passed by value as.
SCALARS = {
    "void": "void", "int": "int32_t", "float": "float", "double": "double", "size_t": "size_t",
    "int8_t": "int8_t", "uint8_t": "uint8_t", "int16_t": "int16_t", "uint16_t": "uint16_t",
    "int32_t": "int32_t", "uint32_t": "uint32_t", "int64_t": "int64_t", "uint64_t": "uint64_t",
}
POINTER = "void *"
DISPATCH_HANDLE = POINTER
NON_DISPATCH_HANDLE = "uint64_t"
ENUM_TYPE = "int32_t"
FAILURE_RESULT = "-3"  # VK_ERROR_INITIALIZATION_FAILED: what an app sees when no Vulkan loader exists


class UnsupportedType(Exception):
    pass


@dataclass(frozen=True)
class Param:
    c_type: str
    name: str


@dataclass(frozen=True)
class Command:
    name: str
    returns: str
    params: tuple[Param, ...]


def flatten(element: ET.Element) -> str:
    return "".join(element.itertext())


def children(root: ET.Element, tag: str) -> list[ET.Element]:
    section = root.find(tag)
    return list(section) if section is not None else []


def is_vulkan(element: ET.Element) -> bool:
    """False for entries that exist only in the Vulkan SC profile."""
    api = element.get("api")
    return api is None or "vulkan" in api.split(",")


def is_pointer(element: ET.Element) -> bool:
    text = flatten(element)
    return "*" in text or "[" in text


def collect_types(root: ET.Element) -> dict[str, str]:
    """Maps every registry type that can be passed by value to the C type it is passed as."""
    types = dict(SCALARS)
    for entry in children(root, "types"):
        category = entry.get("category")
        name = entry.get("name") or (entry.findtext("name") or "")
        if not name:
            continue
        if category == "handle":
            types[name] = DISPATCH_HANDLE if "VK_DEFINE_HANDLE" in flatten(entry) else NON_DISPATCH_HANDLE
        elif category == "funcpointer":
            types[name] = POINTER
        elif category == "enum":
            types[name] = ENUM_TYPE
        elif category == "bitmask":
            underlying = entry.findtext("type") or "VkFlags"
            types[name] = "uint64_t" if underlying == "VkFlags64" else "uint32_t"
        elif category == "basetype":
            under = entry.find("type")
            if under is not None and "typedef" in flatten(entry):
                types[name] = POINTER if "*" in flatten(entry) else under.text or ""
    types["VkFlags"] = "uint32_t"
    types["VkFlags64"] = "uint64_t"
    return types


def c_type_of(element: ET.Element, types: dict[str, str]) -> str:
    if is_pointer(element):
        return POINTER
    name = element.findtext("type") or ""
    resolved = types.get(name)
    while resolved in types and resolved != types[resolved]:
        resolved = types[resolved]
    if resolved is None:
        raise UnsupportedType(name)
    return resolved


def parse_command(element: ET.Element, types: dict[str, str]) -> Command:
    proto = element.find("proto")
    assert proto is not None
    found = [p for p in element.findall("param") if is_vulkan(p)]
    params = tuple(Param(c_type_of(p, types), f"arg{i}") for i, p in enumerate(found))
    return Command(proto.findtext("name") or "", c_type_of(proto, types), params)


def platform_commands(root: ET.Element) -> set[str]:
    """Commands that belong to platform-specific extensions (xcb, win32, android, provisional, ...)."""
    names: set[str] = set()
    for extension in children(root, "extensions"):
        if extension.get("platform") or extension.get("supported") == "disabled":
            names |= {c.get("name", "") for c in extension.iter("command")}
    return names


def parse_registry(root: ET.Element) -> list[Command]:
    types = collect_types(root)
    skipped = platform_commands(root) | HANDWRITTEN
    elements = {c.findtext("proto/name"): c for c in children(root, "commands") if c.get("alias") is None}
    commands: dict[str, Command] = {}
    for entry in children(root, "commands"):
        alias, name = entry.get("alias"), entry.get("name")
        source = elements.get(alias) if alias else entry
        wanted = name if alias else entry.findtext("proto/name")
        if source is None or wanted is None or wanted in skipped or not is_vulkan(entry):
            continue
        command = parse_command(source, types)
        commands[wanted] = Command(wanted, command.returns, command.params)
    return sorted(commands.values(), key=lambda c: c.name)


def render_thunk(command: Command, index: int) -> str:
    formals = ", ".join(f"{p.c_type} {p.name}" for p in command.params) or "void"
    actuals = ", ".join(p.name for p in command.params)
    call = f"(({command.returns} (*)({formals}))target)({actuals})"
    if command.returns == "void":
        failure, body = "return;", f"{call};"
    elif command.returns == "int32_t" and command.name.startswith("vk") and command.params:
        failure, body = f"return {FAILURE_RESULT};", f"return {call};"
    else:
        failure, body = "shim_die(\"vulkan-1.dll is not available\");", f"return {call};"
    return (
        f"LIBC_FN {command.returns} sysv_{command.name}({formals})\n"
        "{\n"
        f"    void *target = vulkan_target({index});\n"
        f"    if (target == NULL) {{\n        {failure}\n    }}\n"
        f"    {body}\n"
        "}\n"
    )


def render(commands: list[Command]) -> str:
    out = [
        "/* Generated by tools/gen-vulkan-thunks.py from the pinned Vulkan registry. Do not edit. */\n",
        '#include "libc_abi.h"\n#include "vulkan_loader.h"\n\n',
    ]
    out += [render_thunk(c, i) + "\n" for i, c in enumerate(commands)]
    out.append(f"const size_t g_vulkan_command_count = {len(commands)};\n\n")
    out.append("const char *const g_vulkan_command_names[] = {\n")
    out += [f'    "{c.name}",\n' for c in commands]
    out.append("};\n\nconst void *const g_vulkan_thunks[] = {\n")
    out += [f"    (const void *)sysv_{c.name},\n" for c in commands]
    out.append("};\n")
    return "".join(out)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: gen-vulkan-thunks.py <vk.xml> <output.c>", file=sys.stderr)
        return 2
    commands = parse_registry(ET.parse(argv[1]).getroot())
    Path(argv[2]).write_text(render(commands), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
