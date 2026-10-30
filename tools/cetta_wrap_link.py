"""Recognize a wrapping link through compiler response files."""
from pathlib import Path
import shlex
import sys


def expand(arguments, active=frozenset()):
    for argument in arguments:
        if argument.startswith('@'):
            path=Path(argument[1:]).resolve()
            if path in active:raise ValueError('recursive response file')
            yield from expand(shlex.split(path.read_text()),active|{path})
        else:yield argument


def wrapping_link(arguments):
    arguments=list(expand(arguments))
    if any(arg in ('-c','-S','-E','-fsyntax-only','-M','-MM') for arg in arguments):return False
    return any(arg in ('--wrap','-Wl,--wrap') or arg.startswith('--wrap=') or
               (arg.startswith('-Wl,') and any(word=='--wrap' or word.startswith('--wrap=')
                 for word in arg[4:].split(','))) for arg in arguments)


if __name__=='__main__':
    try:result=wrapping_link(sys.argv[1:])
    except (OSError,ValueError):result=False
    raise SystemExit(0 if result else 1)
