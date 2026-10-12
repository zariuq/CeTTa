"""Publication of completed compiler artifacts; no compilation or auditing.

The caller certifies content before publication. A generation directory and
each final file have atomic visibility. A multi-file compatibility view is
not a transaction: callers must reject mixed generations at consumption.
"""

import os
from pathlib import Path
import tempfile


def publish_files(output, files):
    output = Path(output)
    for name, content in files.items():
        if not isinstance(name, str) or Path(name).name != name or name in ("", ".", ".."):
            raise ValueError("artifact names must be individual file names")
        if not isinstance(content, bytes):
            raise ValueError("prepared artifact content must be bytes")
    output.mkdir(parents=True, exist_ok=True)
    changed = {name: content for name, content in files.items()
               if not (output / name).is_file() or (output / name).read_bytes() != content}
    if not changed:
        return
    # Prepare the entire set before replacing any caller-visible file.
    with tempfile.TemporaryDirectory(prefix=".publish-", dir=output) as temporary:
        work = Path(temporary)
        for name, content in changed.items():
            with (work / name).open("wb") as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
        for name in changed:
            os.replace(work / name, output / name)


def publish_file(path, content):
    path = Path(path)
    publish_files(path.parent, {path.name: content})


def publish_generation(staged, destination, files):
    """Publish a checked directory, or require an existing generation to match.

Only the declared artifacts are compared; build logs are not program content.
The caller publishes its catalog after this operation succeeds.
"""
    staged, destination = Path(staged), Path(destination)
    if destination.exists():
        for name, content in files.items():
            path = destination / name
            if not path.is_file() or path.read_bytes() != content:
                raise ValueError("existing artifact generation differs from its content identity")
        return
    os.replace(staged, destination)
