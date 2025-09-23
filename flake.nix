{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs?ref=nixos-unstable";
  outputs = {nixpkgs, ...}: let
    systems = ["x86_64-linux" "aarch64-linux"];
    forEachSystem = nixpkgs.lib.genAttrs systems;
    pkgsForEach = nixpkgs.legacyPackages;
  in {
    devShells = forEachSystem (system: let
      pkgs = pkgsForEach.${system};
    in {
      default = pkgs.callPackage ./nix/shell.nix {};
    });

    packages = forEachSystem (system: let
      pkgs = pkgsForEach.${system};
    in {
      default = pkgs.callPackage ./nix/package.nix {};
    });
  };
}
