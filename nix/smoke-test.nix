# Smoke-tests the logos-basecamp binary.
# Launches the app with -platform offscreen and asks it to check its named
# top-level QML views. This avoids fragile log matching while still rejecting
# a running process whose shell QML failed to load.
#
# The LogosBasecamp launcher (bin/LogosBasecamp) bakes in the correct
# QT_PLUGIN_PATH and LD_LIBRARY_PATH at build time for dev builds.
# We only need to add the offscreen platform plugin and GL stubs on Linux.
{ pkgs, appPkg, appBin ? "${appPkg}/bin/LogosBasecamp", timeoutSec ? 5 }:

pkgs.runCommand "logos-basecamp-smoke-test" {
  nativeBuildInputs = [ pkgs.coreutils ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.qt6.qtbase   # provides the offscreen platform plugin
      pkgs.libGL
      pkgs.libglvnd
    ];
} ''

  mkdir -p $out
  export LOGOS_USER_DIR="$out/app-data"
  mkdir -p "$LOGOS_USER_DIR"

  export QT_QPA_PLATFORM=offscreen
  export QT_FORCE_STDERR_LOGGING=1
  export QT_LOGGING_RULES="qt.*.debug=false;default.debug=true"

  ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
    export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}"
    export LD_LIBRARY_PATH="${pkgs.libGL}/lib:${pkgs.libglvnd}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  ''}

  LOG="$out/smoke-test.log"

  echo "Running logos-basecamp QML smoke test (timeout: ${toString timeoutSec}s)..."
  set +e
  timeout ${toString timeoutSec} ${appBin} --smoke-check -platform offscreen > "$LOG" 2>&1
  CODE=$?
  set -e

  cat "$LOG"

  if [ "$CODE" -ne 0 ]; then
    echo "Basecamp QML smoke check failed with exit code $CODE"
    exit 1
  fi

  if ! grep -Fq "Basecamp QML smoke check passed." "$LOG"; then
    echo "Basecamp did not report successful QML startup"
    exit 1
  fi

  echo "Basecamp QML smoke test passed"
''
