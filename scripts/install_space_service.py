#!/usr/bin/env python3
"""Install a verified, versioned CeTTa space-service release for one user."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def atomic_text(path: Path, text: str, mode: int = 0o600):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def select_link(path: Path, target: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not path.is_symlink():
        raise ValueError(f"refusing to replace a non-symlink: {path}")
    fd, temporary = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    os.close(fd)
    os.unlink(temporary)
    try:
        os.symlink(target, temporary)
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.lexists(temporary):
            os.unlink(temporary)


def environment_value(value: str) -> str:
    if "\n" in value or "\r" in value:
        raise ValueError("environment values cannot contain newlines")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def install(server: Path, client: Path, bridge: Path | None, prefix: Path, enable: bool):
    for path in (server, client):
        if not path.is_file() or not os.access(path, os.X_OK):
            raise ValueError(f"missing executable: {path}")
    if bridge is not None and not bridge.is_file():
        raise ValueError(f"missing bridge library: {bridge}")
    if enable and prefix != Path.home() / ".local":
        raise ValueError("--enable requires the default user installation prefix")
    # Resolve every source before changing the selected release, including when
    # installing from an already selected release.
    sources = {"cetta-space-service": server.resolve(), "cetta-space-client": client.resolve(),
               "space_service_client.py": (ROOT / "tools/space_service_client.py").resolve()}
    for name in ("stdlib.metta", "space_service.metta", "system.metta", "petta/system.metta"):
        sources["lib/" + name] = (ROOT / "lib" / name).resolve()
    if bridge is not None:
        sources["libcetta_space_bridge.so"] = bridge.resolve()
    files = {name: digest(path) for name, path in sources.items()}
    manifest = {"format": 1, "files": files, "bridge": bridge is not None}
    text = json.dumps(manifest, sort_keys=True, indent=2) + "\n"
    release_id = hashlib.sha256(text.encode()).hexdigest()[:24]
    base = prefix / "lib/cetta-space"
    releases = base / "releases"
    releases.mkdir(parents=True, exist_ok=True)
    release = releases / release_id
    if not release.exists():
        stage = Path(tempfile.mkdtemp(prefix=".stage-", dir=releases))
        try:
            for name, source in sources.items():
                target = stage / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
                target.chmod(0o755 if name in ("cetta-space-service", "cetta-space-client") else 0o644)
                if digest(target) != files[name]:
                    raise ValueError(f"copy verification failed: {name}")
                with target.open("rb") as stream:
                    os.fsync(stream.fileno())
            atomic_text(stage / "manifest.json", text, 0o644)
            for directory in sorted((p for p in stage.rglob("*") if p.is_dir()),reverse=True):
                fd=os.open(directory,os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(fd)
                finally:
                    os.close(fd)
            os.rename(stage, release)
            fd=os.open(releases,os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
    # Verify existing releases too; a matching directory name is not evidence.
    if (release / "manifest.json").read_text() != text:
        raise ValueError("existing release manifest differs")
    for name, expected in files.items():
        if digest(release / name) != expected:
            raise ValueError(f"release verification failed: {name}")
    commands = ("cetta-space-service", "cetta-space-client")
    for name in commands:
        path = prefix / "bin" / name
        if path.exists() and not path.is_symlink():
            raise ValueError(f"refusing to replace a non-symlink: {path}")
    wrapper = prefix / "bin/cetta-space"
    wrapper_text = ('#!/usr/bin/env python3\n'
                    'import pathlib, runpy\n'
                    'path = pathlib.Path(__file__).resolve().parent.parent / '
                    '"lib/cetta-space/current/space_service_client.py"\n'
                    'runpy.run_path(str(path), run_name="__main__")\n')
    if wrapper.exists() and wrapper.read_text() != wrapper_text:
        raise ValueError(f"refusing to replace an unrelated executable: {wrapper}")
    config = Path.home() / ".config/cetta-space"
    unit = Path.home() / ".config/systemd/user/cetta-space.service"
    if prefix != Path.home() / ".local":
        config = prefix / "etc/cetta-space"
        unit = prefix / "etc/systemd/user/cetta-space.service"
    state_home=Path(os.environ.get("XDG_STATE_HOME",str(prefix / "state")))
    if prefix != Path.home() / ".local":
        state_home=prefix / "state"
    state=state_home / "cetta-space"
    # Some user managers migrate a matching configuration directory by making
    # a state-directory symlink. Create real private storage before that guess.
    state.parent.mkdir(parents=True,exist_ok=True)
    try:
        state.mkdir(mode=0o700)
    except FileExistsError:
        pass
    metadata=state.lstat()
    if not stat.S_ISDIR(metadata.st_mode) or metadata.st_uid!=os.getuid() or metadata.st_mode & 0o077:
        raise ValueError("state directory must be real, private and owned by this user")
    config.mkdir(parents=True, exist_ok=True)
    env = config / "service.env"
    saved_env = env.read_text() if env.exists() else None
    saved_unit = unit.read_text() if unit.exists() else None
    old_lines = saved_env.splitlines() if saved_env is not None else []
    lines = [line for line in old_lines if not line.lstrip().startswith("CETTA_MORK_SPACE_BRIDGE_LIB=")]
    if bridge is not None:
        lines.append("CETTA_MORK_SPACE_BRIDGE_LIB=" + environment_value(str(base / "current/libcetta_space_bridge.so")))
    atomic_text(env, "\n".join(lines) + "\n")
    unit_text = (ROOT / "examples/space-service/cetta-space.service").read_text()
    atomic_text(unit, unit_text, 0o644)
    current = base / "current"
    previous = os.readlink(current) if current.is_symlink() else None
    if previous is not None and previous != "releases/" + release_id:
        select_link(base / "previous", previous)
    select_link(current, "releases/" + release_id)
    for name in commands:
        select_link(prefix / "bin" / name, "../lib/cetta-space/current/" + name)
    atomic_text(wrapper, wrapper_text, 0o755)
    if enable:
        try:
            subprocess.run(["systemctl", "--user", "daemon-reload"], check=True)
            subprocess.run(["systemctl", "--user", "enable", "cetta-space.service"], check=True)
            subprocess.run(["systemctl", "--user", "restart", "cetta-space.service"], check=True)
            socket = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")) / "cetta-space.sock"
            deadline = time.monotonic() + 15
            while True:
                probe = subprocess.run([str(prefix / "bin/cetta-space"), "--socket", str(socket), "hello"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=12)
                if probe.returncode == 0:
                    break
                if time.monotonic() >= deadline:
                    raise ValueError("service failed its protocol readiness check")
                time.sleep(.1)
        except (ValueError, OSError, subprocess.SubprocessError):
            if previous is not None:
                select_link(current, previous)
                for path, saved in ((env, saved_env), (unit, saved_unit)):
                    if saved is not None:
                        atomic_text(path, saved, 0o600 if path == env else 0o644)
                subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)
                subprocess.run(["systemctl", "--user", "restart", "cetta-space.service"], check=False)
            else:
                subprocess.run(["systemctl", "--user", "disable", "--now", "cetta-space.service"], check=False)
            raise
    return release


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    parser.add_argument("--bridge", type=Path)
    parser.add_argument("--prefix", type=Path, default=Path.home() / ".local")
    parser.add_argument("--enable", action="store_true")
    args = parser.parse_args()
    try:
        release = install(args.server, args.client, args.bridge, args.prefix.resolve(), args.enable)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"installation failed: {error}\n")
    print(release)


if __name__ == "__main__":
    main()
