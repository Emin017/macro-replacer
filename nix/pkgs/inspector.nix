{
  lib,
  stdenv,
  cmake,
  ninja,
  sv-lang,
  nlohmann_json,
  fmt,
  boost,
  mimalloc,
}:
stdenv.mkDerivation {
  pname = "inspector";
  version = "0.1.0";

  src =
    with lib.fileset;
    toSource {
      root = ../../inspector;
      fileset = unions [
        ../../inspector/src
        ../../inspector/CMakeLists.txt
      ];
    };

  nativeBuildInputs = [
    cmake
    ninja
  ];

  buildInputs = [
    sv-lang
    nlohmann_json
    fmt
    boost
    mimalloc
  ];

  cmakeBuildType = "Release";

  enableParallelBuild = true;

}
