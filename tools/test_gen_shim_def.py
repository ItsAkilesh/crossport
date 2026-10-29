# File: tools/test_gen_shim_def.py
# Purpose: Unit tests for the shim export-list generator: LIBC_FN functions, data, macro-defined functions and aliases
# Author: Akilesh
# Date: 2026-10-29

import importlib.util
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("gen_shim_def", Path(__file__).with_name("gen-shim-def.py"))
gen = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen)


class ScanTests(unittest.TestCase):
    def test_plain_functions_are_found_by_their_real_name(self):
        functions, data = gen.scan("LIBC_FN int sysv_open(const char *path, int flags)\n{\n}\n")
        self.assertEqual(functions, {"open"})
        self.assertEqual(data, set())

    def test_multi_word_return_types_are_found(self):
        functions, _ = gen.scan("LIBC_FN unsigned long long sysv_strtoull(const char *text)\n{\n}\n")
        self.assertEqual(functions, {"strtoull"})

    def test_static_helpers_are_not_exported(self):
        functions, _ = gen.scan("static int sysv_helper(void)\n{\n}\n")
        self.assertEqual(functions, set())

    def test_macro_defined_families_are_found(self):
        source = "MATH1(sqrt) MATH2(pow) MATHF1(sqrtf) NARROW(isalpha, CLASS_ALPHA) WIDE(iswalpha, CLASS_ALPHA) STUB_FAIL(mount) STUB_ZERO(sync) STUB_NULL(fmemopen)"
        functions, _ = gen.scan(source)
        self.assertEqual(
            functions, {"sqrt", "pow", "sqrtf", "isalpha", "iswalpha", "mount", "sync", "fmemopen"}
        )

    def test_the_macro_parameter_name_is_not_a_function(self):
        functions, _ = gen.scan("#define STUB_FAIL(name) LIBC_FN int sysv_##name(void)\n#define MATH1(name) x\n")
        self.assertNotIn("name", functions)

    def test_data_symbols_and_aliases(self):
        functions, data = gen.scan("LIBC_DATA(char **, environ);\n")
        self.assertEqual(data, {"environ"})
        text = gen.render(functions, data)
        self.assertIn("environ=crossport_data_environ DATA", text)
        self.assertIn("__environ=crossport_data_environ DATA", text)


class MainTests(unittest.TestCase):
    def test_the_output_lists_functions_sorted_with_their_sysv_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "a.c").write_text("LIBC_FN int sysv_zeta(void)\n{\n}\nLIBC_FN int sysv_alpha(void)\n{\n}\n")
            (root / "sub").mkdir()
            (root / "sub" / "b.c").write_text("STUB_FAIL(beta)\n")
            output = root / "out.def"
            self.assertEqual(gen.main(["gen-shim-def.py", str(output), str(root)]), 0)
            lines = output.read_text().splitlines()
            self.assertEqual(lines[0], "EXPORTS")
            self.assertEqual(lines[1:], ["    alpha=sysv_alpha", "    beta=sysv_beta", "    zeta=sysv_zeta"])


if __name__ == "__main__":
    unittest.main()
