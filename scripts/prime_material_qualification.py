"""Input accounting shared by material-profile native replay gates."""

from pathlib import Path
import subprocess

from check_prime_common_set_profiles import digest


def native_inputs(root, binary):
    """Pin the native sources as well as the executable actually replayed."""
    root = root.resolve()
    listed = subprocess.run(
        ["rg", "--files", "--hidden", "--no-ignore", "src"],
        cwd=root, capture_output=True, text=True, check=True,
    )
    paths = {root / "Makefile", binary.resolve(), Path(__file__).resolve()}
    paths.update(root / relative for relative in listed.stdout.splitlines()
                 if Path(relative).suffix in {".c", ".h", ".inc", ".def"})
    return {path: digest(path) for path in sorted(paths)}


def unchanged(inputs):
    for path, expected in inputs.items():
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError("Qualification input changed during replay: " + path.name)


def json_inputs(inputs):
    return {str(path): value for path, value in sorted(inputs.items())}
