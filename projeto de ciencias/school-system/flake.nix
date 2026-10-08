{
  description = "Ambiente de desenvolvimento do School System Backend";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            packages = with pkgs; [
              clang
              gnumake
              pkg-config
              sqlite
              curl
              openssl
              gdb
              valgrind
              bubblewrap
            ];

            shellHook = ''
              echo "Ambiente School System pronto. Execute: make"
            '';
          };
        });
    };
}

