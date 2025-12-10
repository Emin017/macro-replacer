# Macro Replacer

`macro-replacer` — a small utility to replace macro/module (including blackbox) instantiations in Verilog/SystemVerilog source files.

## Install
- **Local Development Install**: Run in the repository root:
```bash
python -m pip install -e .
```

- Or run the module directly for quick testing:

```bash
uv run replacer --help
```

Usage (CLI)
- Arguments:
  - `--verilog`: top-level Verilog file path (default: `test_regfile.sv`)
  - `--module`: module name that contains the macro instance to replace (default: `tech_regfile`)
  - `--old-macro`: old macro/module name to replace (default: `old_macro`)
  - `--new-macro-file`: file containing the new macro/module definition (required)
  - `--new-macro-name`: module name of the new macro (required)
  - `--out`: output file path (default: `replaced_file.sv`)

## Example

```bash
replacer --verilog test.v \
  --module tech_regfile \
  --old-macro old_macro \
  --new-macro-file test_macro.v \
  --new-macro-name test_macro \
  --out replaced_ram.v
```

Or run directly with Python:

```bash
python3 -m macro_replacer.replacer --verilog test.v --module tech_regfile --old-macro old_macro --new-macro-file test_macro.v --new-macro-name test_macro --out replaced_ram.v
```
