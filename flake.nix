{
  description = "Simple status bar for wayland";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    nixpkgs,
    flake-utils,
    ...
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = import nixpkgs {
          inherit system;
        };
        env = pkgs.mkShell {
          buildInputs = with pkgs; [
            clang-tools
            gnumake
            pkg-config
            wayland
            wayland-scanner
            wayland-protocols
            cairo
          ];
        };
      in {
        devShell = env;
      }
    );
}
