{

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = [
          pkgs.bison
          pkgs.flex
          pkgs.swig
          pkgs.ncurses
          pkgs.pkg-config
          pkgs.stdenv
          pkgs.git
          pkgs.autoconf
          pkgs.gnumake
          pkgs.bc
          pkgs.bison
          pkgs.elfutils
          pkgs.qemu_full
          pkgs.debootstrap
          pkgs.clang
          pkgs.clang-tools
          pkgs.lld
          pkgs.llvmPackages.libllvm
          pkgs.gnutls
          pkgs.openssl
          pkgs.gcc14
          (pkgs.python3.withPackages (
            python-pkgs: with python-pkgs; [
              setuptools
            ]
          ))
        ];
      };
    };
}
