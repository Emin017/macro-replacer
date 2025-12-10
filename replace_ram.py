import json
import subprocess
import os
import argparse
import sys
import tempfile

# Defaults
VERILOG_FILE = "ysyxSoCFull.v"
TARGET_MODULE = "ram_2x3"
MACRO_NAME = "xxxxxxxxxxxxxxxxx"
MACRO_DEF_FILE = "pdk_macro.v"
MACRO_AST_FILE = "macro.ast.json"

INSPECTOR_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "inspector/build/inspector")

def run_inspector(verilog_file, module_name):
    """Run the inspector tool to get module definition."""
    print(f"正在分析 {verilog_file} 中的模块 {module_name} ...")

    if not os.path.exists(INSPECTOR_PATH):
        print(f"Error: inspector executable not found at {INSPECTOR_PATH}")
        sys.exit(1)

    with tempfile.NamedTemporaryFile(mode='w+', suffix='.json', delete=False) as tmp:
        tmp_json = tmp.name

    cmd = [INSPECTOR_PATH, verilog_file, module_name, "--json", tmp_json]
    
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        with open(tmp_json, 'r') as f:
            data = json.load(f)
    except subprocess.CalledProcessError as e:
        print(f"inspector 调用失败: {e}")
        if os.path.exists(tmp_json): os.remove(tmp_json)
        return None
    except json.JSONDecodeError:
        print("Error: Failed to parse inspector JSON output.")
        if os.path.exists(tmp_json): os.remove(tmp_json)
        return None

    if os.path.exists(tmp_json):
        os.remove(tmp_json)

    return data

def gen_verilog(module_def, target_module_name, macro_name, macro_def):
    """Generate a wrapper module named `target_module_name` which instantiates `macro_name`."""
    ports = module_def.get("ports", [])
    port_lines = []

    macro_ports = macro_def.get("ports", []) if macro_def else []

    # Analyze Outer Ports for Heuristics
    outer_inputs = []
    outer_outputs = []

    # Helper to normalize direction
    def normalize_dir(d):
        d = d.lower()
        if "in" in d and "out" not in d: return "input"
        if "out" in d and "in" not in d: return "output"
        return "inout"

    for p in ports:
        d = normalize_dir(p.get("direction", ""))
        n = p.get("name", "")
        if d == "input": outer_inputs.append(n)
        if d == "output": outer_outputs.append(n)

    # 1. Define Outer Module Ports
    for port in ports:
        direction = normalize_dir(port.get("direction", "input"))
        width = port.get("type", "logic")
        if width == "reg": width = "logic" # specific fix for 'reg' type from instances
        name = port.get("name", "")
        
        if width and width != "logic":
            decl = f"{direction} {width} {name}"
        else:
            decl = f"{direction} {name}"
        port_lines.append(decl)

    # 2. Define Instance Connections with Heuristics
    connect_lines = []
    if macro_ports:
        for mp in macro_ports:
            m_name = mp.get("name")
            m_dir = normalize_dir(mp.get("direction", ""))
            m_type = mp.get("type", "logic")

            # Simple Heuristic Matching
            candidate = ""
            if m_dir == "output":
                # If only one outer output, match it
                if len(outer_outputs) == 1:
                    candidate = outer_outputs[0]
            elif m_dir == "input":
                # Try name matching
                m_name_lower = m_name.lower()

                if "clk" in m_name_lower:
                    # Find outer clk
                    clks = [x for x in outer_inputs if "clk" in x.lower()]
                    if clks: candidate = clks[0]
                elif "cen" in m_name_lower:
                    # Chip Enable
                    cens = [x for x in outer_inputs if "en" in x.lower()]
                    if cens: candidate = cens[0]
                elif "bwen" in m_name_lower:
                    # Find outer bwen
                    bwens = [x for x in outer_inputs if "bwen" in x.lower()]
                    if bwens: candidate = bwens[0]
                elif "wen" in m_name_lower:
                    # Write Enable heuristic
                    wens = [x for x in outer_inputs if "w" in x.lower() and "en" in x.lower()]
                    if not wens:
                        wens = [x for x in outer_inputs if "en" in x.lower()]
                    if wens: candidate = wens[0]
                elif "addr" in m_name_lower or m_name == "A":
                    # Address
                    addrs = [x for x in outer_inputs if "addr" in x.lower()]
                    if addrs: candidate = addrs[0]
                elif "data" in m_name_lower or m_name == "D":
                    # Data In
                    datas = [x for x in outer_inputs if "data" in x.lower()]
                    if datas: candidate = datas[0]

            connect_lines.append(f".{m_name}({candidate}), // {m_dir.capitalize()} {m_type}")

    verilog = f"module {target_module_name}(\n    " + ",\n    ".join(port_lines) + "\n);\n\n"
    verilog += f"    {macro_name} u_mem (\n        " + ",\n        ".join(connect_lines) + "\n    );\n\n"
    verilog += "endmodule\n"
    return verilog

def main():
    parser = argparse.ArgumentParser(description="Extract module ports using inspector and generate wrapper.")
    parser.add_argument("--verilog", default=VERILOG_FILE)
    parser.add_argument("--module", default=TARGET_MODULE)
    parser.add_argument("--macro", default=MACRO_NAME)
    parser.add_argument("--macro-def", default=MACRO_DEF_FILE, help="Macro definition Verilog file")
    parser.add_argument("--out", default=None)

    args = parser.parse_args()

    verilog_file = args.verilog
    target = args.module
    macro = args.macro
    macro_def_file = args.macro_def
    out_file = args.out or f"new_{target}.v"

    # 1. Inspect Target Module
    inspector_data = run_inspector(verilog_file, target)
    if not inspector_data:
        print(f"Failed to inspect {target} in {verilog_file}!")
        return

    module_info = inspector_data.get("definition")
    if not module_info:
        print(f"Definition for {target} not found. Checking instances...")
        instances = inspector_data.get("instances", [])
        if instances:
            # Infer module definition from the first instance
            first_inst = instances[0]
            print(f"Using instance '{first_inst.get('instanceName')}' to infer ports for {target}.")

            inferred_ports = []
            for conn in first_inst.get("connections", []):
                p = {
                    "name": conn.get("portName"),
                    "direction": conn.get("direction", "Input"), # Default to Input if missing, though typically present now
                    "type": conn.get("signalType", "logic") # Use signal type as port type
                }
                # If width is something like "3", and type is "logic", we might want "logic[2:0]"
                # But inspector provides "signalType" which often includes width like "logic[2:0]" or "reg"
                # If signalType is "reg", we might want to change it to "logic" or check width.
                # Let's rely on signalType mostly, but careful about "reg".

                # Check width from inspector if needed to reconstruct type
                # For now let's assume signalType is good enough or we clean it up in gen_verilog

                inferred_ports.append(p)

            module_info = {"name": target, "ports": inferred_ports}
        else:
            print(f"Module {target} not found (no definition and no instances) in {verilog_file}!")
            return

    # 2. Inspect Macro Module
    macro_info = None
    if macro_def_file and os.path.exists(macro_def_file):
        macro_data = run_inspector(macro_def_file, macro)
        if macro_data:
             macro_info = macro_data.get("definition")
             if not macro_info and macro_data.get("instances"):
                 # Fallback for macro too if needed (unlikely for macro definition file)
                  first_inst = macro_data["instances"][0]
                  inferred_ports = []
                  for conn in first_inst.get("connections", []):
                      inferred_ports.append({
                          "name": conn.get("portName"),
                          "direction": conn.get("direction", "Input"),
                          "type": conn.get("signalType", "logic")
                      })
                  macro_info = {"name": macro, "ports": inferred_ports}

        if not macro_info:
            print(f"[WARN] 无法从 {macro_def_file} 提取到宏 {macro} 的端口")

    # 3. Generate Wrapper
    verilog = gen_verilog(module_info, target, macro, macro_info)
    with open(out_file, "w") as f:
        f.write(verilog)
    print(f"新模块已生成：{out_file}")

    print("\n[端口信息摘要]")
    print(f"Target Module ({target}) Ports:")
    for p in module_info.get("ports", []):
         print(f"  - {p.get('name'):<15} {p.get('direction'):<10} {p.get('type')}")
    
    if macro_info:
        print(f"\nMacro Module ({macro}) Ports:")
        for p in macro_info.get("ports", []):
             print(f"  - {p.get('name'):<15} {p.get('direction'):<10} {p.get('type')}")

if __name__ == "__main__":
    main()