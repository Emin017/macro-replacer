final: prev: {
  slangInspector = final.callPackage ./pkgs/inspector.nix { };
  macroReplacer = final.callPackage ./pkgs/replacer.nix { };
}
