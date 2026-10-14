"""
PlatformIO pre-build script: apply Midad's simulator patches via `git apply`.

crosspoint-simulator shims the upstream CrossPoint HAL and ESP-IDF surface.
Midad's lib/hal and its ESP-IDF usage have grown past that (HalClock system-time
helpers, HalDisplay overloads, esp_http_client header calls, ...), so the pinned
simulator no longer compiles the firmware on its own. The patches in
`scripts/simulator_patches/` add the missing pieces; this script applies each
one against the libdep working tree.

Each patch's idempotency is decided by git itself:
  * `git apply --check --reverse` succeeds  -> already applied, skip
  * `git apply --check`            succeeds  -> apply
  * neither succeeds                          -> abort the build

The patches are written against the commit pinned in platformio.ini. Moving the
pin means regenerating them. Applied in lexical order.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import subprocess
import sys


PATCH_DIR = os.path.join(env["PROJECT_DIR"], "scripts", "simulator_patches")  # noqa: F821


def patch_simulator(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    if not os.path.isdir(libdeps_dir):
        return
    patches = _patch_files()
    for env_dir in os.listdir(libdeps_dir):
        sim_dir = os.path.join(libdeps_dir, env_dir, "simulator")
        if not os.path.isdir(os.path.join(sim_dir, ".git")):
            continue
        for patch in patches:
            _apply_one(sim_dir, patch)


def _patch_files():
    if not os.path.isdir(PATCH_DIR):
        raise RuntimeError(
            "Simulator patches missing -- aborting build (expected directory %s)"
            % PATCH_DIR
        )
    patches = sorted(
        os.path.join(PATCH_DIR, name)
        for name in os.listdir(PATCH_DIR)
        if name.endswith(".patch")
    )
    if not patches:
        raise RuntimeError(
            "Simulator patches missing -- aborting build (no .patch files in %s)"
            % PATCH_DIR
        )
    return patches


def _apply_one(sim_dir, patch_path):
    name = os.path.basename(patch_path)
    if _git_apply_succeeds(sim_dir, patch_path, reverse=True):
        return
    if not _git_apply_succeeds(sim_dir, patch_path, reverse=False):
        # Not applied, not appliable -- the libdep source has diverged from
        # what the patch expects. Don't write a half-patched file.
        result = subprocess.run(
            ["git", "apply", "--check", patch_path],
            cwd=sim_dir,
            capture_output=True,
            text=True,
        )
        sys.stderr.write(
            "ERROR: simulator patch %s does not apply cleanly:\n%s%s\n"
            % (name, result.stdout, result.stderr)
        )
        raise SystemExit(1)
    subprocess.run(["git", "apply", patch_path], cwd=sim_dir, check=True)
    print("Applied simulator patch: %s" % name)


def _git_apply_succeeds(sim_dir, patch_path, *, reverse):
    cmd = ["git", "apply", "--check"]
    if reverse:
        cmd.append("--reverse")
    cmd.append(patch_path)
    return subprocess.run(
        cmd, cwd=sim_dir, capture_output=True, text=True
    ).returncode == 0


patch_simulator(env)  # noqa: F821
