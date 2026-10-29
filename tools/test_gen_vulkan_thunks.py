# File: tools/test_gen_vulkan_thunks.py
# Purpose: Unit tests for the Vulkan thunk generator: type mapping, platform filtering, aliases and rendering
# Author: Akilesh
# Date: 2026-10-29

import importlib.util
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("gen_vulkan_thunks", Path(__file__).with_name("gen-vulkan-thunks.py"))
gen = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen)

REGISTRY = """
<registry>
  <types>
    <type category="basetype">typedef <type>uint32_t</type> <name>VkBool32</name>;</type>
    <type category="basetype">typedef <type>uint64_t</type> <name>VkDeviceSize</name>;</type>
    <type category="bitmask">typedef <type>VkFlags</type> <name>VkFooFlags</name>;</type>
    <type category="handle"><type>VK_DEFINE_HANDLE</type>(<name>VkDevice</name>)</type>
    <type category="handle"><type>VK_DEFINE_NON_DISPATCHABLE_HANDLE</type>(<name>VkBuffer</name>)</type>
    <type category="enum" name="VkResult"/>
    <type category="funcpointer">typedef void (VKAPI_PTR *<name>PFN_vkVoidFunction</name>)(void);</type>
  </types>
  <commands>
    <command><proto><type>VkResult</type> <name>vkDoThing</name></proto>
      <param><type>VkDevice</type> <name>device</name></param>
      <param><type>VkBuffer</type> <name>buffer</name></param>
      <param><type>VkDeviceSize</type> <name>size</name></param>
      <param>const <type>VkFoo</type>* <name>pInfo</name></param>
      <param api="vulkansc"><type>uint32_t</type> <name>scOnly</name></param>
    </command>
    <command><proto><type>void</type> <name>vkSetWidth</name></proto>
      <param><type>VkDevice</type> <name>device</name></param>
      <param><type>float</type> <name>width</name></param>
    </command>
    <command><proto><type>VkResult</type> <name>vkCreateXcbSurfaceKHR</name></proto>
      <param><type>xcb_window_t</type> <name>window</name></param>
    </command>
    <command name="vkDoThingKHR" alias="vkDoThing"/>
  </commands>
  <extensions>
    <extension name="VK_KHR_xcb_surface" platform="xcb" supported="vulkan">
      <require><command name="vkCreateXcbSurfaceKHR"/></require>
    </extension>
  </extensions>
</registry>
"""


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.commands = {c.name: c for c in gen.parse_registry(ET.fromstring(REGISTRY))}

    def test_scalar_handle_and_pointer_types_map_to_abi_classes(self):
        params = [p.c_type for p in self.commands["vkDoThing"].params]
        self.assertEqual(params, ["void *", "uint64_t", "uint64_t", "void *"])

    def test_floats_stay_floats(self):
        self.assertEqual(self.commands["vkSetWidth"].params[1].c_type, "float")

    def test_platform_commands_are_left_out(self):
        self.assertNotIn("vkCreateXcbSurfaceKHR", self.commands)

    def test_aliases_get_their_own_thunk_with_the_target_signature(self):
        alias = self.commands["vkDoThingKHR"]
        self.assertEqual(alias.params, self.commands["vkDoThing"].params)

    def test_commands_are_sorted_for_binary_search(self):
        names = [c.name for c in gen.parse_registry(ET.fromstring(REGISTRY))]
        self.assertEqual(names, sorted(names))

    def test_vulkan_result_functions_fail_with_a_result_code(self):
        text = gen.render(list(self.commands.values()))
        self.assertIn("return -3;", text)
        self.assertIn("sysv_vkDoThing(", text)

    def test_struct_by_value_is_rejected(self):
        broken = REGISTRY.replace("<type>float</type> <name>width</name>", "<type>VkFoo</type> <name>width</name>")
        with self.assertRaises(gen.UnsupportedType):
            gen.parse_registry(ET.fromstring(broken))


if __name__ == "__main__":
    unittest.main()
