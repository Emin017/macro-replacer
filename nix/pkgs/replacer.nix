{
  lib,
  python3,
  slangInspector,
}:
with python3.pkgs;
buildPythonApplication {
  pname = "macro-replacer";
  version = "0.1.0";
  pyproject = true;

  src = lib.cleanSource ../../.;

  build-system = [
    hatchling
  ];

  postPatch = ''
    substituteInPlace src/macro_replacer/replacer.py \
      --replace-fail 'os.path.join(os.path.dirname(os.path.abspath(__file__)), "inspector/build/inspector")' '"${slangInspector}/bin/inspector"'
  '';

  meta = {
    mainProgram = "replacer";
  };
}
