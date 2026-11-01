# File: tools/test_gen_gl_thunks.py
# Purpose: Unit tests for the OpenGL thunk generator: type mapping, desktop filtering, hand-written commands and rendering
# Author: Akilesh
# Date: 2026-11-01

import importlib.util
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("gen_gl_thunks", Path(__file__).with_name("gen-gl-thunks.py"))
gen = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen)

REGISTRY = """
<registry>
  <commands namespace="GL">
    <command><proto>void <name>glClearColor</name></proto>
      <param><ptype>GLfloat</ptype> <name>red</name></param>
      <param><ptype>GLfloat</ptype> <name>green</name></param>
    </command>
    <command><proto>const <ptype>GLubyte</ptype> *<name>glGetString</name></proto>
      <param><ptype>GLenum</ptype> <name>name</name></param>
    </command>
    <command><proto><ptype>GLsync</ptype> <name>glFenceSync</name></proto>
      <param><ptype>GLenum</ptype> <name>condition</name></param>
      <param><ptype>GLbitfield</ptype> <name>flags</name></param>
    </command>
    <command><proto>void <name>glFinish</name></proto></command>
    <command><proto>void <name>glDebugMessageCallback</name></proto>
      <param><ptype>GLDEBUGPROC</ptype> <name>callback</name></param>
    </command>
    <command><proto>void <name>glGlesOnly</name></proto></command>
    <command><proto>void <name>glNeedsExtension</name></proto>
      <param>const <ptype>GLfloat</ptype> *<name>values</name></param>
    </command>
    <command><proto>void <name>glVertex3fv</name></proto>
      <param>const <ptype>GLfloat</ptype> <name>v</name>[3]</param>
    </command>
  </commands>
  <feature api="gl" name="GL_VERSION_1_0" number="1.0">
    <require>
      <command name="glClearColor"/>
      <command name="glGetString"/>
      <command name="glFinish"/>
      <command name="glVertex3fv"/>
    </require>
  </feature>
  <feature api="gl" name="GL_VERSION_3_2" number="3.2">
    <require>
      <command name="glFenceSync"/>
      <command name="glDebugMessageCallback"/>
    </require>
  </feature>
  <feature api="gles2" name="GL_ES_VERSION_2_0" number="2.0">
    <require><command name="glGlesOnly"/></require>
  </feature>
  <extensions>
    <extension name="GL_ARB_thing" supported="gl|glcore"><require><command name="glNeedsExtension"/></require></extension>
    <extension name="GL_OES_other" supported="gles2"><require><command name="glGlesOnly"/></require></extension>
  </extensions>
</registry>
"""


def commands() -> dict[str, "gen.Command"]:
    return {c.name: c for c in gen.parse_registry(ET.fromstring(REGISTRY))}


class GenerateGlThunksTest(unittest.TestCase):
    def test_scalar_parameters_map_to_their_fixed_width_types(self) -> None:
        clear = commands()["glClearColor"]
        self.assertEqual([p.c_type for p in clear.params], ["float", "float"])
        self.assertEqual(clear.returns, "void")

    def test_pointers_arrays_and_sync_objects_are_pointers(self) -> None:
        found = commands()
        self.assertEqual(found["glGetString"].returns, "void *")
        self.assertEqual(found["glVertex3fv"].params[0].c_type, "void *")
        self.assertEqual(found["glFenceSync"].returns, "void *")
        self.assertEqual([p.c_type for p in found["glFenceSync"].params], ["uint32_t", "uint32_t"])

    def test_only_desktop_commands_are_generated(self) -> None:
        self.assertNotIn("glGlesOnly", commands())
        self.assertIn("glNeedsExtension", commands())

    def test_commands_that_take_callbacks_are_left_to_hand_written_code(self) -> None:
        self.assertNotIn("glDebugMessageCallback", commands())

    def test_commands_are_sorted_by_name_for_binary_search(self) -> None:
        names = [c.name for c in gen.parse_registry(ET.fromstring(REGISTRY))]
        self.assertEqual(names, sorted(names))

    def test_an_unknown_type_is_an_error_not_a_guess(self) -> None:
        element = ET.fromstring("<command><proto>void <name>glX</name></proto><param><ptype>GLmystery</ptype> <name>a</name></param></command>")
        with self.assertRaises(gen.UnsupportedType):
            gen.parse_command(element)

    def test_a_thunk_resolves_lazily_and_does_nothing_without_a_target(self) -> None:
        found = gen.parse_registry(ET.fromstring(REGISTRY))
        text = gen.render(found)
        self.assertIn("LIBC_FN void sysv_glClearColor(float arg0, float arg1)", text)
        self.assertIn("gl_resolve(", text)
        self.assertIn("return (void *)0;", text)
        self.assertIn(f"g_gl_command_count = {len(found)}", text)
        self.assertEqual(text.count('    "gl'), len(found))

    def test_the_real_registry_generates_without_unsupported_types(self) -> None:
        registry = Path(__file__).resolve().parent.parent / "third_party" / "opengl" / "gl.xml"
        generated = gen.parse_registry(ET.parse(registry).getroot())
        names = {c.name for c in generated}
        self.assertGreater(len(generated), 2500)
        self.assertTrue({"glClear", "glDrawArrays", "glBindBuffer", "glCreateShader", "glGetString"} <= names)
        self.assertFalse(names & gen.HANDWRITTEN)


if __name__ == "__main__":
    unittest.main()
