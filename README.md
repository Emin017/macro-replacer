# Macro Replacer

![main](https://github.com/Emin017/macro-replacer/actions/workflows/build.yml/badge.svg?branch=main)

A small utility by using [**SlangPortInspector**](inspector/README.md) to replace macro/module (including blackbox module) instantiations in Verilog/SystemVerilog source files.

## Install

We use Nix for reproducible builds.

To install Nix, follow the instructions at [nixos.org](https://nixos.org/download.html). (Nix is not NixOS)

```shell
nix run github:Emin017/macro-replacer# -- --help

# or clone repo and run locally
git clone https://github.com/Emin017/macro-replacer.git
cd macro-replacer
nix run .# -- --help
```

Usage (CLI)
- Arguments:
  - `--verilog`: top-level Verilog file path (default: `test_ram.sv`)
  - `--module`: module name that contains the macro instance to replace (default: `ram_tech`)
  - `--old-macro`: old macro/module name to replace (default: `old_macro`)
  - `--new-macro-file`: file containing the new macro/module definition (required)
  - `--new-macro-name`: module name of the new macro (required)
  - `--out`: output file path (default: `replaced_file.sv`)

## Example

```shell
nix run .# -- --verilog test.v \
  --module tech_regfile \
  --old-macro old_macro \
  --new-macro-file test_macro.v \
  --new-macro-name test_macro \
  --out replaced_ram.v
```

Or run directly with Python:

```shell
python3 -m macro_replacer.replacer --verilog test.v --module tech_regfile --old-macro old_macro --new-macro-file test_macro.v --new-macro-name test_macro --out replaced_ram.v
```
