{ pkgs, ... }:
{
  devShells.default = pkgs.mkShell {
    buildInputs = with pkgs; [
      python3
      python3Packages.pip
      python3Packages.setuptools
      python3Packages.virtualenv
      cmake
      ninja
    ];
  };
}
