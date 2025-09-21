{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs?ref=nixos-unstable";
  outputs = {nixpkgs, ...}: let
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    devShells.${system} = {
      default = pkgs.mkShell {
        packages = with pkgs; [
          clang-tools

          gcc
          pkg-config
          ncurses
          curl
          xorg.xorgproto
          xorg.libX11.dev
          wayland.dev
          libclipboard.dev
        ];

        env.CLANGD_FLAGS = "--query-driver=${pkgs.lib.getExe pkgs.stdenv.cc}";
      };
    };
  };
}
