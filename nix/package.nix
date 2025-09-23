{
  lib,
  stdenv,
  zig,
  ncurses,
  curl,
  xorg,
  wayland,
  libclipboard,
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
      ];
    };

    nativeBuildInputs = [zig.hook];
    buildInputs = [
      ncurses
      curl
      xorg.xorgproto
      xorg.libX11.dev
      wayland.dev
      libclipboard.dev
    ];

    zigBuildFlags = ["-Doptimize=ReleaseSmall"];

    meta = {
      description = "Terminal utility for looking up Nix store paths in binary caches";
      homepage = "https://github.com/notashelf/narnia";
      license = lib.licenses.mpl20;
      maintainers = with lib.maintainers; [NotAShelf];
    };
  }
