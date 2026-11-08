"""PlatformIO pre-build script: adds macro/MacroActivity.cpp to the firmware (macro environments only).

Runs after mirror/build.py, which owns SRC_FILTER, and appends one file to it. Like the mirror sources, the file is left
out of the Arduino core build that the custom-sdkconfig envs run first (see mirror/build.py for why).
"""
Import("env")  # noqa: F821  # type: ignore[name-defined]


def macro_sources(target, source, env, for_signature):
    if env.subst("$ARDUINO_LIB_COMPILE_FLAG") == "Build":
        return ""
    return "+<../macro/MacroActivity.cpp>"


env["MACRO_SOURCES"] = macro_sources  # noqa: F821
env.Append(SRC_FILTER=" $MACRO_SOURCES")  # noqa: F821
