"""Poll SIM800 RX buffer faster during TLS — default 500ms throttle is too slow."""

from pathlib import Path

Import("env")

PATCH_FROM = "if (millis() - prev_check > 500)"
PATCH_TO = "if (millis() - prev_check > 50)"


def _tinygsm_tcp_path() -> Path:
    return (
        Path(env.subst("$PROJECT_LIBDEPS_DIR"))
        / env.subst("$PIOENV")
        / "TinyGSM"
        / "src"
        / "TinyGsmTCP.tpp"
    )


def apply_tinygsm_patch(source=None, target=None, env_obj=None):
    path = _tinygsm_tcp_path()
    if not path.is_file():
        return

    content = path.read_text(encoding="utf-8")
    if PATCH_FROM not in content:
        if PATCH_TO in content:
            return
        print(f"WARNING: TinyGSM poll patch pattern not found in {path}")
        return

    path.write_text(content.replace(PATCH_FROM, PATCH_TO, 1), encoding="utf-8")
    print("Patched TinyGSM RX poll interval 500ms -> 50ms for TLS on 2G")

    build_dir_name = env.get("BUILD_DIR")
    pioenv = env.get("PIOENV")
    if build_dir_name and pioenv:
        build_dir = Path(str(build_dir_name)) / str(pioenv)
        for obj in build_dir.glob("lib*/TinyGSM/TinyGsmTCP.tpp.o"):
            obj.unlink(missing_ok=True)


def register_patch_hook():
    path = _tinygsm_tcp_path()
    if path.is_file():
        apply_tinygsm_patch()
        return

    def _patch_before_build(source, target, env):
        apply_tinygsm_patch()

    env.AddPreAction("buildprog", _patch_before_build)


register_patch_hook()
