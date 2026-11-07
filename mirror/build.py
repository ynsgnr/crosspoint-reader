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
    return "+<../mirror/core/> +<../mirror/device/> +<../mirror/templates/>"


env["MIRROR_SOURCES"] = mirror_sources  # noqa: F821
# PlatformIO's default filter (all of src/) plus the mirror folders.
env.Replace(SRC_FILTER="+<*> -<.git/> -<.svn/> $MIRROR_SOURCES")  # noqa: F821
