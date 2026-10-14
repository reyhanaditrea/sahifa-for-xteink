"""Fall back to plain `ar` when the toolchain's `gcc-ar` wrapper won't run.

Windows 11's Smart App Control blocks unsigned executables that lack file
reputation, reporting it as "blocked by your organization's Device Guard
policy" even on a machine with no organization policy at all. It decides per
file, so the toolchain's common binaries (gcc, ranlib, the 1MB ar) pass while
the rarely-invoked 66KB gcc-ar wrapper does not -- every object compiles and
the build then dies at the first `Archiving ... .a`.

gcc-ar differs from ar only by auto-loading the LTO plugin, and this build
sets no -flto, so the two produce identical archives here. Probe once and swap
only when the wrapper is actually unusable: on a machine where gcc-ar runs
nothing changes, and if LTO is ever enabled the probe still hands back the
wrapper that knows how to handle it.

Must run as `post:`. PlatformIO's order is pre-scripts, then the platform
builder, then post-scripts (platformio/builder/main.py:167-180), and $AR only
becomes the toolchain's own name inside the platform builder -- a pre-script
sees SCons's bare default and has nothing to swap.

That ordering also means the Arduino core library is already spoken for by
then: its build script takes its own `env.Clone()` (pioarduino-build.py:183)
before this runs, and that copy keeps the blocked wrapper no matter what is
replaced here. The clone is reachable because the same script prepends the
library it builds to LIBS, so every archiving environment gets patched by
walking those nodes rather than only the environment we were handed.
"""

import subprocess

from SCons.Util import flatten

Import("env")  # noqa: F821 -- injected by SCons

_ar = env.subst("$AR")  # noqa: F821


def _usable(ar):
    try:
        subprocess.run([ar, "--version"], capture_output=True, check=True, timeout=30)
        return True
    except Exception as exc:  # OSError (blocked/missing) or CalledProcessError
        print("ar_fallback: %s is unusable (%s)" % (ar, type(exc).__name__))
        return False


if "gcc-ar" in _ar and not _usable(_ar):
    fallback = _ar.replace("gcc-ar", "ar")

    # env itself, plus the environment behind every library node already built
    # -- node.env is the environment its builder ran in, i.e. the core clone.
    candidates = [env]  # noqa: F821
    candidates += [n.env for n in flatten(env.get("LIBS", [])) if getattr(n, "env", None)]  # noqa: F821

    patched, seen = 0, set()
    for scons_env in candidates:
        if id(scons_env) in seen:
            continue
        seen.add(id(scons_env))
        if "gcc-ar" in scons_env.subst("$AR"):
            scons_env.Replace(AR=fallback)
            patched += 1

    print("ar_fallback: using %s (%d environment(s) patched)" % (fallback, patched))
