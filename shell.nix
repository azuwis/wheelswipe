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
    };
  };
in

pkgs.mkShell {
  packages = with pkgs; [
    astyle
    claude-sandboxed
  ];
}
