"""Run MeTTa in the current Python interpreter and package environment."""
import argparse
import sys

from . import Engine, __version__


def main(argv=None):
    parser = argparse.ArgumentParser(prog="python -m cetta")
    parser.add_argument("files", nargs="*")
    parser.add_argument("-e", "--expression", action="append", default=[])
    parser.add_argument("--lang", default="petta")
    parser.add_argument("--profile")
    parser.add_argument("--fuel", type=int, default=-1)
    parser.add_argument("--version", action="version", version=f"cetta {__version__}")
    args = parser.parse_args(argv)
    try:
        with Engine(language=args.lang, profile=args.profile) as engine:
            def show(answers):
                for group in answers.groups:
                    if args.lang.split("@")[0] == "he":
                        print("[" + ", ".join(str(a) for a in group) + "]")
                    else:
                        for atom in group:
                            print(atom)
            for path in args.files:
                show(engine.load(path, fuel=args.fuel))
            if args.expression:
                show(engine.run("\n".join(args.expression), fuel=args.fuel))
            if not args.files and not args.expression:
                show(engine.run(sys.stdin.read(), fuel=args.fuel))
    except Exception as error:
        print(f"cetta: {error}", file=sys.stderr)
        return getattr(error, "exit_code", None) or 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
