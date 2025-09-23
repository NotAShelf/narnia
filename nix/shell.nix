{
  lib,
  stdenv,
  mkShell,
  ncurses,
  curl,
  xorg,
  wayland,
  libclipboard,
  zig,
}:
mkShell {
  packages = [
    ncurses
    curl
    xorg.xorgproto
    xorg.libX11.dev
    wayland.dev
    libclipboard.dev

    # Zig offers better plumbing for compiling C projects
    # Let's try it out, 'mkShell' provides gcc anyway, so we can build
    # with whichever.
    zig
  ];

  env.CLANGD_FLAGS = "--query-driver=${lib.getExe stdenv.cc}";
}
