import json
import subprocess
import os
import argparse
import sys
import tempfile

# Defaults
VERILOG_FILE = "test_regfile.sv"
TARGET_MODULE = "tech_regfile"
OLD_MACRO_NAME = "old_macro"
NEW_MACRO_DEF_FILE = (
    "test_macro.v"  # This should be the file containing the NEW macro definition
)
NEW_MACRO_NAME = "test_macro"  # Placeholder, should be derived or passed

INSPECTOR_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "inspector/build/inspector"
)


def run_inspector(verilog_file, module_name):
    """Run the inspector tool to get module definition."""
    print(f"正在分析 {verilog_file} 中的模块 {module_name} ...")

    if not os.path.exists(INSPECTOR_PATH):
        print(f"Error: inspector executable not found at {INSPECTOR_PATH}")
        sys.exit(1)

    with tempfile.NamedTemporaryFile(mode="w+", suffix=".json", delete=False) as tmp:
        tmp_json = tmp.name

    cmd = [INSPECTOR_PATH, verilog_file, module_name, "--json", tmp_json]

    try:
        subprocess.run(
            cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE
        )
        with open(tmp_json, "r") as f:
            data = json.load(f)
    except subprocess.CalledProcessError as e:
        print(f"inspector 调用失败: {e}")
        if os.path.exists(tmp_json):
            os.remove(tmp_json)
        return None
    except json.JSONDecodeError:
        print("Error: Failed to parse inspector JSON output.")
        if os.path.exists(tmp_json):
            os.remove(tmp_json)
        return None

    if os.path.exists(tmp_json):
        os.remove(tmp_json)

    return data


def get_macro_ports(macro_file, macro_name):
    """Get ports of the new macro using inspector."""
    data = run_inspector(macro_file, macro_name)
    if not data or not data.get("definition"):
        # Fallback to check instances if definition not found (e.g. blackbox) or maybe inspector needs module name
        return []
    return data["definition"].get("ports", [])


def main():
    parser = argparse.ArgumentParser(
        description="Replace macro instantiation in Verilog file."
    )
    parser.add_argument(
        "--verilog", default=VERILOG_FILE, help="Top level Verilog file"
    )
    parser.add_argument(
        "--module", default=TARGET_MODULE, help="Module containing the macro to replace"
    )
    parser.add_argument(
        "--old-macro", default=OLD_MACRO_NAME, help="Name of the macro to replace"
    )
    parser.add_argument(
        "--new-macro-file", required=True, help="File containing new macro definition"
    )
    parser.add_argument(
        "--new-macro-name", required=True, help="Name of the new macro module"
    )
    parser.add_argument("--out", default="replaced_file.sv", help="Output file path")

    args = parser.parse_args()

    verilog_file = args.verilog
    target_module = args.module
    old_macro = args.old_macro
    new_macro_file = args.new_macro_file
    new_macro_name = args.new_macro_name
    out_file = args.out

    # 1. Analyze the Target Module to find the Old Macro Instance
    target_data = run_inspector(verilog_file, target_module)
    if not target_data:
        print("Failed to analyze target module.")
        return

    # Find the instance in definition -> instances (nested)
    def_info = target_data.get("definition", {})
    sub_instances = def_info.get("instances", [])

    target_instance = None
    for inst in sub_instances:
        if inst.get("definitionName") == old_macro:
            target_instance = inst
            break

    if not target_instance:
        print(
            f"Error: Instance of macro '{old_macro}' not found in module '{target_module}'."
        )
        # Try checking if it's in the instances list at top level (unlikely for blackbox internal)
        return

    print(
        f"Found instance '{target_instance['instanceName']}' at offsets {target_instance['startOffset']}-{target_instance['endOffset']}"
    )

    # 2. Extract Existing Connections
    # Map PortName -> SignalExpression (string)
    # The inspector now returns 'signalType' as the expression string if available from syntax

    existing_connections = {}
    for conn in target_instance.get("connections", []):
        port = conn.get("portName")
        signal = conn.get(
            "signalType"
        )  # This holds the expression string now, e.g. "clk_i" or "dat_i[63:0]"
        if port and signal:
            existing_connections[port] = signal

    print("Existing Connections:", existing_connections)

    # 3. Analyze New Macro to get its Ports
    new_macro_ports = get_macro_ports(new_macro_file, new_macro_name)
    if not new_macro_ports:
        print(
            f"Failed to get ports for new macro '{new_macro_name}' from '{new_macro_file}'."
        )
        # Generate some dummy ports for testing if file doesn't match macro name or such
        # new_macro_ports = [{'name': 'CLK', 'direction': 'Input'}, {'name': 'CEN', 'direction': 'Input'}, {'name': 'WEN', 'direction': 'Input'}, {'name': 'A', 'direction': 'Input'}, {'name': 'D', 'direction': 'Input'}, {'name': 'BWEN', 'direction': 'Input'}, {'name': 'Q', 'direction': 'Output'}]
        # return
        pass

    # Fallback if inspector returned nothing (e.g. invalid macro file passed in command line but valid for this tool context)
    if not new_macro_ports:
        print("Warning: No ports found for new macro. Instantiation will be empty.")

    print(f"New Macro Ports: {[p['name'] for p in new_macro_ports]}")

    # 4. Map Signals to New Ports (Heuristic)
    new_connections = []

    # Helper to clean names for matching
    def clean(s):
        return s.lower().replace("_", "")

    for new_port in new_macro_ports:
        np_name = new_port["name"]

        # Heuristic 1: Exact Match
        if np_name in existing_connections:
            signal = existing_connections[np_name]
            new_connections.append((np_name, signal))
            continue

        # Heuristic 2: Fuzzy Name Match
        best_old_port = None
        for old_port, signal in existing_connections.items():
            if clean(old_port) == clean(np_name):
                best_old_port = old_port
                break

            # Common renames
            if clean(old_port) == "clk" and "clk" in clean(np_name):
                best_old_port = old_port
            if clean(old_port) == "cw" and clean(np_name) == "wen":
                best_old_port = old_port

            # Map BWEB (new) to BWEN (old) or BWEB (old)
            if "bweb" in clean(np_name) and clean(old_port) in ["bwen", "bweb"]:
                best_old_port = old_port
            # ... add more heuristics as needed

        # Special casing for the specific macro if needed
        if not best_old_port and np_name == "CLK":
            best_old_port = "CLK"

        if best_old_port and best_old_port in existing_connections:
            signal = existing_connections[best_old_port]
            new_connections.append((np_name, signal))
            print(f"Mapped {np_name} <- {signal} (via old port {best_old_port})")
        else:
            # Try finding signal by name if port mapping fails?
            # e.g. if new port is "addr", maybe connect to "addr_i"?
            if "clk" in clean(np_name):
                # simplistic
                pass

            print(f"Warning: Could not automatically map port '{np_name}'")
            new_connections.append((np_name, "/* UNCONNECTED */"))

    # 5. Generate New Instantiation Text

    indent = "      "

    new_inst_lines = []
    new_inst_lines.append(f"  {new_macro_name} {target_instance['instanceName']} (")

    for i, (port, sig) in enumerate(new_connections):
        comma = "," if i < len(new_connections) - 1 else ""
        new_inst_lines.append(f"{indent}.{port: <10} ({sig}){comma}")

    new_inst_lines.append("  );")

    new_inst_text = "\n".join(new_inst_lines)

    # 6. Apply Replacement
    with open(verilog_file, "r") as f:
        content = f.read()

    start = target_instance["startOffset"]
    end = target_instance["endOffset"]

    # Heuristic: Search backwards for the macro type name
    # The syntax tree range for UninstantiatedDef might only cover the instance variable, not the type.

    # scan backwards from start
    cursor = start - 1
    while cursor >= 0 and content[cursor].isspace():
        cursor -= 1

    # Now cursor is at end of the previous token.
    # Find the start of that token.
    token_end = cursor + 1
    while cursor >= 0 and (content[cursor].isalnum() or content[cursor] == "_"):
        cursor -= 1
    token_start = cursor + 1

    preceding_word = content[token_start:token_end]
    print(f"Preceding word: '{preceding_word}'")

    if preceding_word == old_macro:
        print(f"Expanding replace range to include macro type name at {token_start}")
        start = token_start
    else:
        print(
            f"Warning: Preceding word '{preceding_word}' does not match old macro '{old_macro}'. Replacement might Result in invalid syntax."
        )

    # Also consume the trailing semicolon from the original content if present
    cursor = end
    while cursor < len(content) and content[cursor].isspace():
        cursor += 1
    if cursor < len(content) and content[cursor] == ";":
        print(f"Consuming trailing semicolon at {cursor}")
        end = cursor + 1

    new_content = content[:start] + new_inst_text + content[end:]

    with open(out_file, "w") as f:
        f.write(new_content)

    print(f"Successfully generated {out_file} with replaced macro.")


if __name__ == "__main__":
    main()
