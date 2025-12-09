# SlangPortInspector

A SystemVerilog module port inspection tool built on top of the [slang](https://github.com/MikePopoloski/slang) library. This tool helps you analyze and inspect module ports in SystemVerilog designs, supporting both fully defined modules and blackbox instantiations.

## Building

The project includes a convenient build script that handles both slang dependency compilation and the inspector build:

```bash
# Clone the repository (if not already done)
git clone <repository-url>
cd inspector

# Build everything (slang + inspector)
chmod +x build.sh
./build.sh
```

## Usage

### Basic Syntax

```bash
./inspector <verilog_file> <module_name>
```

## Architecture

The tool operates in two modes:

1. **AST Analysis Mode**: When the module is fully defined, it uses slang's AST to extract complete port information including types and directions.

2. **Syntax Tree Mode**: When the module is not defined (blackbox), it falls back to syntax tree analysis to infer port names from instantiation connections.

### Key Components

- **`findModuleInAST()`**: Searches the compiled AST for module definitions and extracts port details
- **`BlackboxVisitor`**: Syntax tree visitor that finds module instantiations and infers port connections
- **`directionToString()`**: Utility function to convert internal direction enums to readable strings

## Dependencies

- **slang**: SystemVerilog language services library (included as git submodule)
- **CMake**: Build system generator
- **Ninja**: Fast build system (recommended)
