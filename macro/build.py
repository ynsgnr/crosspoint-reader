"""PlatformIO pre-build script: adds the macro/ sources to the firmware (macro environments only).

Runs after mirror/build.py, which owns SRC_FILTER, and appends to it. Like the mirror sources, the files are left
out of the Arduino core build that the custom-sdkconfig envs run first (see mirror/build.py for why).
"""
Import("env")  # noqa: F821  # type: ignore[name-defined]

MACRO_FILES = ["MacroActivity.cpp", "MacroSettings.cpp", "MacroUsb.cpp"]


def macro_sources(target, source, env, for_signature):
    if env.subst("$ARDUINO_LIB_COMPILE_FLAG") == "Build":
        return ""
    return " ".join("+<../macro/%s>" % f for f in MACRO_FILES)


env["MACRO_SOURCES"] = macro_sources  # noqa: F821
env.Append(SRC_FILTER=" $MACRO_SOURCES")  # noqa: F821
