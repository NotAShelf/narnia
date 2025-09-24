{
  lib,
  stdenv,
  zig,
  pkg-config,
  ncurses,
  curl,
  withX11 ? true,
  withWayland ? true,
  xorg,
  wayland,
}: let
  fs = lib.fileset;
  s = ../.;
in
  stdenv.mkDerivation {
    pname = "narnia";
    version = "0-unstable-2025-09-23";
    src = fs.toSource {
      root = s;
      fileset = fs.unions [
        (s + /main.c)
        (s + /build.zig)
        (s + /include)
      ];
    };

    nativeBuildInputs = [zig.hook pkg-config];
    buildInputs =
      [
        ncurses
        curl
      ]
      ++ lib.optionals withX11 [
        xorg.xorgproto
        xorg.libX11.dev
      ]
      ++ lib.optionals withWayland [wayland.dev];

    zigBuildFlags = ["-Doptimize=ReleaseSmall"];

    meta = {
      description = "Terminal utility for looking up Nix store paths in binary caches";
      homepage = "https://github.com/notashelf/narnia";
      license = lib.licenses.mpl20;
      maintainers = with lib.maintainers; [NotAShelf];
    };
  }
