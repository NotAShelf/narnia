{
  lib,
  stdenv,
  mkShell,
  pkg-config,
  zig,
  ncurses,
  curl,
  xorg,
  wayland,
  libclipboard,
}:
mkShell {
  name = "naria-dev";
  nativeBuildInputs = [
    pkg-config
    # Zig offers better plumbing for compiling C projects
    # Let's try it out, 'mkShell' provides gcc anyway, so we can build
    # with whichever.
    zig
  ];

  buildInputs = [
    ncurses
    curl
    xorg.xorgproto
    xorg.libX11.dev
    wayland.dev
    libclipboard.dev
  ];

  env.CLANGD_FLAGS = "--query-driver=${lib.getExe stdenv.cc}";
}
