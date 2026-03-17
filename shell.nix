{ }:

let
  sources = import ./sources.nix { };
  pkgs = import sources.nixpkgs {
    config.allowUnfreePredicate =
      pkg:
      builtins.elem (pkgs.lib.getName pkg) [
        "claude-code"
      ];
  };
  agent-sandbox = import sources.agent-sandbox.outPath {
    inherit pkgs;
  };
  claude-sandboxed = agent-sandbox.mkSandbox {
    pkg = pkgs.claude-code;
    binName = "claude";
    outName = "claude-sandboxed";
    allowedPackages = [
      pkgs.cacert
      pkgs.coreutils
      pkgs.which
      pkgs.bash
      pkgs.git
      pkgs.ripgrep
      pkgs.fd
      pkgs.gnused
      pkgs.gnugrep
      pkgs.findutils
      pkgs.jq
    ];
    stateDirs = [ "$HOME/.claude" ];
    stateFiles = [
      "$HOME/.claude.json"
      "$HOME/.claude.json.lock"
    ];
    extraEnv = {
      ANTHROPIC_AUTH_TOKEN = "$ANTHROPIC_AUTH_TOKEN";
      ANTHROPIC_BASE_URL = "$ANTHROPIC_BASE_URL";
      NIX_SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
      SSL_CERT_DIR = "${pkgs.cacert}/etc/ssl/certs";
      SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
    };
    restrictClosure = true;
  };
in

pkgs.mkShell {
  packages = with pkgs; [
    astyle
    claude-sandboxed
  ];
}
