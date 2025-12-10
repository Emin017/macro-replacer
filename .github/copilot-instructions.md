# Macro Replacer - AI Coding Guidelines

## Project Overview

**Macro Replacer** is a SystemVerilog macro instantiation replacement utility that uses AST analysis to intelligently replace old macro definitions with new ones while preserving port connections.

### Architecture

The project has two core components:
1. **SlangPortInspector** (C++, `inspector/`): AST analysis tool using [slang](https://github.com/MikoPopolski/slang) library to parse Verilog/SystemVerilog and extract module definitions and port information
2. **Replacer** (Python, `src/macro_replacer/`): Orchestrates the replacement workflow by analyzing existing instances, mapping signals to new ports heuristically, and generating new instantiation text

### Key Data Flow

```
Verilog file → Inspector (AST) → Module/Instance data (JSON)
                                          ↓
                              Signal mapping heuristics
                                          ↓
                              New instantiation generation
                                          ↓
                              Modified Verilog output
```

## Critical Development Workflows

### Building

Use Nix for reproducible builds across platforms:
```bash
nix run .# -- --help                    # Run replacer directly
nix build                                # Build both replacer and inspector
nix run .#inspector -- file.v module    # Run inspector standalone
```

**Note**: The project supports multiple architectures (x86_64-linux, aarch64-linux, aarch64-darwin) defined in `flake.nix`.

### Testing Workflow

1. Inspector must be built before running replacer:
   - Replacer hardcodes path to inspector binary at runtime (see `INSPECTOR_PATH` in `replacer.py`)
   - During development, build from `inspector/build/inspector`
   - In production (Nix), path is substituted to `/nix/store/.../bin/inspector`

2. Test data flow:
   - Provide a Verilog file with target module containing old macro instance
   - Provide new macro file with replacement definition
   - Inspector analyzes both files separately and returns JSON with ports/connections
   - Replacer matches ports using heuristics and generates output

## Project-Specific Conventions & Patterns

### Inspector Output Format (JSON)

The inspector returns structured data with:
```python
{
  "definition": {
    "ports": [{"name": "PORT_NAME", "direction": "Input|Output"}],
    "instances": [
      {
        "instanceName": "inst_name",
        "definitionName": "macro_type",
        "startOffset": 1234,
        "endOffset": 5678,
        "connections": [
          {"portName": "PORT", "signalType": "signal_expression"}
        ]
      }
    ]
  }
}
```

### Signal Mapping Heuristics

The replacer implements **best-effort port matching** (see lines 112-137 in `replacer.py`):
1. **Exact match**: New port name matches existing connection key
2. **Fuzzy match**: Case-insensitive, underscore-agnostic comparison
3. **Domain-specific rules**: Hard-coded mappings (e.g., `CLK`, `CW` → `WEN`, `BWEB` variants)
4. **Fallback**: Marks unmapped ports as `/* UNCONNECTED */`

**Important**: This heuristic approach is intentional—complete port mapping information isn't always available (especially for blackbox modules).

### Blackbox Module Support

Inspector has two modes:
- **AST Mode**: Full module definition available → extract complete port info
- **Syntax Tree Mode**: Module is blackbox → infer ports from instantiation connections

The replacer handles both transparently.

## Integration Points & External Dependencies

### Dependencies

**Python** (`pyproject.toml`):
- `requires-python = ">=3.11"`
- Build system: Hatchling
- Entry point: `replacer` command

**C++ Inspector** (`inspector/CMakeLists.txt`):
- **slang**: SystemVerilog language services (exact version via Nix)
- **nlohmann_json**: JSON serialization
- **fmt**: String formatting
- **boost**: General utilities
- **mimalloc**: Memory allocator

### Command-Line Interface

All arguments are required or have sensible defaults (see `replacer.py:main()`):
```python
--verilog           # Top-level Verilog file (default: test_regfile.sv)
--module            # Target module containing macro (required conceptually)
--old-macro         # Macro type to replace (default: old_macro)
--new-macro-file    # File with new macro definition (REQUIRED)
--new-macro-name    # New macro module name (REQUIRED)
--out               # Output file path (default: replaced_file.sv)
```

## Key Files & Responsibilities

- `src/macro_replacer/replacer.py` (234 lines): Main orchestration, signal mapping logic, file I/O
- `inspector/src/main.cpp`: AST analysis, JSON generation
- `nix/pkgs/replacer.nix`: Python package definition, path substitution
- `nix/pkgs/inspector.nix`: C++ build configuration
- `flake.nix`: Multi-platform build coordination

## Implementation Notes for AI Agents

1. **Text Offset Precision**: Replacer uses character offsets from inspector (`startOffset`, `endOffset`). When modifying replacement logic, account for off-by-one errors with surrounding whitespace and trailing semicolons.

2. **Heuristic Fragility**: Port mapping is intentionally loose—different macro naming conventions may break assumptions. Consider parametrizing common rules or accepting mapping configuration files.

3. **Error Handling**: Inspector failures are caught but error messages are basic. Enhance diagnostics by propagating inspector stderr to user output.

4. **Nix Path Substitution**: The replacer is designed to work both standalone (hardcoded dev path) and through Nix (substituted prod path). Always test both execution modes when modifying `replacer.py`.

5. **Chinese Comments**: Code contains Chinese comments (e.g., "正在分析模块" - "Analyzing module"). Preserve for consistency or align with project style guidelines.
