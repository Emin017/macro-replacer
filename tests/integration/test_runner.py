import sys
import os
import unittest
from unittest import mock

# Add src to path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../src')))

from macro_replacer import replacer

class TestIntegration(unittest.TestCase):
    def setUp(self):
        self.base_dir = os.path.dirname(os.path.abspath(__file__))
        self.case1_dir = os.path.join(self.base_dir, "test_cases/case1")
        self.output_file = os.path.join(self.case1_dir, "output.sv")

        # Locate inspector
        env_inspector = os.environ.get("INSPECTOR_PATH")
        if env_inspector and os.path.exists(env_inspector):
            self.inspector_path = env_inspector
        else:
            # Assuming we are in tests/integration/
            # Inspector is at ../../inspector/build/inspector
            self.inspector_path = os.path.abspath(os.path.join(self.base_dir, "../../inspector/build/inspector"))

        if not os.path.exists(self.inspector_path):
            self.skipTest(f"Inspector binary not found at {self.inspector_path}. Please build it first.")

        # Patch replacer's inspector path
        replacer.INSPECTOR_PATH = self.inspector_path

    def tearDown(self):
        if os.path.exists(self.output_file):
            os.remove(self.output_file)

    def test_case1(self):
        verilog_file = os.path.join(self.case1_dir, "top_module.sv")
        new_macro_file = os.path.join(self.case1_dir, "new_macro.v")

        # Prepare args
        argv = [
            "replacer.py",
            "--verilog", verilog_file,
            "--module", "top_module",
            "--old-macro", "OLD_MACRO",
            "--new-macro-file", new_macro_file,
            "--new-macro-name", "NEW_MACRO",
            "--out", self.output_file
        ]

        with mock.patch('sys.argv', argv):
            replacer.main()
            
        self.assertTrue(os.path.exists(self.output_file))

        with open(self.output_file, 'r') as f:
            content = f.read()

        print(f"Output:\n{content}")
        
        self.assertIn("NEW_MACRO u_inst", content)
        # Check for expected connections
        # Note: replacer uses padding <10
        self.assertIn(".CLK        (clk)", content)
        self.assertIn(".WEN        (wen)", content) # Heuristic check: CW -> WEN
        self.assertIn(".D          (d)", content)
        self.assertIn(".Q          (q)", content)

    def test_case2(self):
        case_dir = os.path.join(self.base_dir, "test_cases/case2")
        verilog_file = os.path.join(case_dir, "top.sv")
        new_macro_file = os.path.join(case_dir, "new_macro.v")
        output_file = os.path.join(case_dir, "output.sv")

        # Cleanup previous run
        if os.path.exists(output_file):
            os.remove(output_file)

        argv = [
            "replacer.py",
            "--verilog", verilog_file,
            "--module", "top",
            "--old-macro", "OLD_MACRO",
            "--new-macro-file", new_macro_file,
            "--new-macro-name", "NEW_MACRO",
            "--out", output_file
        ]

        with mock.patch('sys.argv', argv):
            replacer.main()

        self.assertTrue(os.path.exists(output_file))

        with open(output_file, 'r') as f:
            content = f.read()

        print(f"Output Case 2:\n{content}")

        self.assertIn("NEW_MACRO u_inst", content)
        self.assertIn(".X          (/* UNCONNECTED */)", content)
        self.assertIn(".Y          (/* UNCONNECTED */)", content)

        # Cleanup
        if os.path.exists(output_file):
            os.remove(output_file)

if __name__ == '__main__':
    unittest.main()
