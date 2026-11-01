#!/usr/bin/env python3
# File: tools/gen-gl-thunks.py
# Purpose: Generate the shim's OpenGL entry points (SysV to Microsoft ABI thunks) from the pinned OpenGL registry
# Author: Akilesh
# Date: 2026-11-01
#
# Usage: gen-gl-thunks.py <gl.xml> <output.c>
#
# Every OpenGL command that the desktop profiles use becomes `LIBC_FN type sysv_glName(...)`, which resolves
# the real entry point on first use (gl_resolve in shim/gl/gl_loader.c: opengl32.dll for GL 1.1, otherwise
# wglGetProcAddress) and calls it. OpenGL's own types are fixed width, so the only difference between the two
# sides is the calling convention. Commands that take or return function pointers cannot be forwarded as they
# are (the driver would call a SysV function with the Microsoft convention); they are written by hand in
# shim/gl/ and listed in HANDWRITTEN.

import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

HANDWRITTEN = {
    "glDebugMessageCallback",
    "glDebugMessageCallbackARB",
    "glDebugMessageCallbackKHR",
    "glDebugMessageCallbackAMD",
    "glGetVkProcAddrNV",
}
POINTER = "void *"
# C types for every registry type a command can pass or return by value. Linux (LP64) sizes.
SCALARS = {
    "void": "void", "GLvoid": "void",
    "GLenum": "uint32_t", "GLbitfield": "uint32_t", "GLuint": "uint32_t", "GLhandleARB": "uint32_t",
    "GLint": "int32_t", "GLsizei": "int32_t", "GLfixed": "int32_t", "GLclampx": "int32_t",
    "GLboolean": "uint8_t", "GLubyte": "uint8_t", "GLbyte": "int8_t",
    "GLshort": "int16_t", "GLushort": "uint16_t", "GLhalf": "uint16_t", "GLhalfNV": "uint16_t",
    "GLfloat": "float", "GLclampf": "float", "GLdouble": "double", "GLclampd": "double",
    "GLint64": "int64_t", "GLint64EXT": "int64_t", "GLuint64": "uint64_t", "GLuint64EXT": "uint64_t",
    "GLintptr": "int64_t", "GLintptrARB": "int64_t", "GLsizeiptr": "int64_t", "GLsizeiptrARB": "int64_t",
    "GLvdpauSurfaceNV": "int64_t",
    "GLsync": POINTER, "GLeglImageOES": POINTER, "GLeglClientBufferEXT": POINTER,
    "GLDEBUGPROC": POINTER, "GLDEBUGPROCARB": POINTER, "GLDEBUGPROCKHR": POINTER, "GLDEBUGPROCAMD": POINTER,
    "GLVULKANPROCNV": POINTER,
}
DESKTOP_APIS = {"gl", "glcore"}


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


def c_type_of(element: ET.Element) -> str:
    """The C type a parameter or result is passed as: any pointer is a pointer, anything else by its registry type."""
    text = flatten(element)
    if "*" in text or "[" in text:
        return POINTER
    name = element.findtext("ptype") or (element.text or "").replace("const", "").strip()
    resolved = SCALARS.get(name)
    if resolved is None:
        raise UnsupportedType(name)
    return resolved


def parse_command(element: ET.Element) -> Command:
    proto = element.find("proto")
    assert proto is not None
    params = tuple(Param(c_type_of(p), f"arg{i}") for i, p in enumerate(element.findall("param")))
    return Command(proto.findtext("name") or "", c_type_of(proto), params)


def desktop_command_names(root: ET.Element) -> set[str]:
    """Names of the commands that a desktop feature level or desktop extension requires."""
    names: set[str] = set()
    for feature in root.findall("feature"):
        if feature.get("api") in DESKTOP_APIS:
            names |= {c.get("name", "") for c in feature.iter("command")}
    extensions = root.find("extensions")
    for extension in extensions if extensions is not None else []:
        if DESKTOP_APIS & set((extension.get("supported") or "").split("|")):
            names |= {c.get("name", "") for c in extension.iter("command")}
    return names


def parse_registry(root: ET.Element) -> list[Command]:
    wanted = desktop_command_names(root) - HANDWRITTEN
    section = root.find("commands")
    entries = list(section) if section is not None else []
    commands = [parse_command(entry) for entry in entries if entry.findtext("proto/name") in wanted]
    return sorted(commands, key=lambda c: c.name)


def render_thunk(command: Command, index: int) -> str:
    formals = ", ".join(f"{p.c_type} {p.name}" for p in command.params) or "void"
    actuals = ", ".join(p.name for p in command.params)
    call = f"(({command.returns} (*)({formals}))target)({actuals})"
    if command.returns == "void":
        missing, body = "return;", f"{call};"
    else:
        missing, body = f"return ({command.returns})0;", f"return {call};"
    return (
        f"LIBC_FN {command.returns} sysv_{command.name}({formals})\n"
        "{\n"
        "    static void *volatile cached;\n"
        "    void *target = cached;\n"
        "    if (target == NULL) {\n"
        f"        target = gl_resolve({index});\n"
        "        cached = target;\n"
        "    }\n"
        f"    if (target == NULL) {{\n        {missing}\n    }}\n"
        f"    {body}\n"
        "}\n"
    )


def render(commands: list[Command]) -> str:
    out = [
        "/* Generated by tools/gen-gl-thunks.py from the pinned OpenGL registry. Do not edit. */\n",
        '#include "libc_abi.h"\n#include "gl_loader.h"\n\n',
    ]
    out += [render_thunk(c, i) + "\n" for i, c in enumerate(commands)]
    out.append(f"const size_t g_gl_command_count = {len(commands)};\n\n")
    out.append("const char *const g_gl_command_names[] = {\n")
    out += [f'    "{c.name}",\n' for c in commands]
    out.append("};\n\nconst void *const g_gl_thunks[] = {\n")
    out += [f"    (const void *)sysv_{c.name},\n" for c in commands]
    out.append("};\n")
    return "".join(out)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: gen-gl-thunks.py <gl.xml> <output.c>", file=sys.stderr)
        return 2
    commands = parse_registry(ET.parse(argv[1]).getroot())
    Path(argv[2]).write_text(render(commands), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
