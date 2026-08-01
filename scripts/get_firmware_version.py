Import("env")

import subprocess


def get_version():
    try:
        return (
            subprocess.check_output(
                ["git", "describe", "--tags", "--always", "--dirty"],
                stderr=subprocess.DEVNULL,
                cwd=env["PROJECT_DIR"],
            )
            .strip()
            .decode("utf-8")
        )
    except Exception:
        return "unknown"


env.Append(BUILD_FLAGS=['-DFIRMWARE_VERSION=\\"%s\\"' % get_version()])
