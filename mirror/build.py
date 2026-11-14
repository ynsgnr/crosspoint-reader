"""PlatformIO pre-build script: adds mirror/ to the firmware's sources (mirror environments only).

The custom-sdkconfig envs (the X4) first run an Arduino core build inside the same process, in a dummy project that keeps
the source filter but drops build flags and lib_deps, and then start a child `pio run` for the real build. Mirror sources
seen in that first phase would pull the hal library into the dummy project, and Rtc then fails to compile (BoardConfig.h
not found). The platform marks that phase with ARDUINO_LIB_COMPILE_FLAG == "Build" only after pre scripts have run, so the
filter is a lazily evaluated variable that is empty during the core build.
"""
Import("env")  # noqa: F821  # type: ignore[name-defined]


def mirror_sources(target, source, env, for_signature):
    if env.subst("$ARDUINO_LIB_COMPILE_FLAG") == "Build":
        return ""
    return "+<../mirror/core/> +<../mirror/device/> +<../mirror/templates/> +<../mirror/ota/>"


env["MIRROR_SOURCES"] = mirror_sources  # noqa: F821
# PlatformIO's default filter (all of src/) plus the mirror folders.
env.Replace(SRC_FILTER="+<*> -<.git/> -<.svn/> $MIRROR_SOURCES")  # noqa: F821


def _git_rev():
    """Short commit of the source being built, with "+" for changes not committed anywhere in the repo (untracked files
    too). On a pull request CI builds a merge commit nobody knows, so the PR's own head commit is shown instead."""
    import json
    import os
    import subprocess

    root = env.subst("$PROJECT_DIR")  # noqa: F821
    event = os.environ.get("GITHUB_EVENT_PATH")
    if os.environ.get("GITHUB_EVENT_NAME", "").startswith("pull_request") and event:
        try:
            with open(event) as f:
                return json.load(f)["pull_request"]["head"]["sha"][:8]
        except (OSError, KeyError, ValueError):
            pass
    try:
        rev = subprocess.check_output(["git", "rev-parse", "--short=8", "HEAD"], cwd=root, text=True).strip()
        dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip() != ""
        return rev + ("+" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def _write_build_info():
    """mirror/device/BuildInfo.h (not committed): rewritten only when the commit changes, so only the file that shows it
    recompiles."""
    import os

    path = os.path.join(env.subst("$PROJECT_DIR"), "mirror", "device", "BuildInfo.h")  # noqa: F821
    text = '#pragma once\n// Written by mirror/build.py; not committed.\n#define MIRROR_GIT_REV "%s"\n' % _git_rev()
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(path, "w") as f:
        f.write(text)


_write_build_info()
