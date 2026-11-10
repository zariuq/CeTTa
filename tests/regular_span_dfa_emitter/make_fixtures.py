#!/usr/bin/env python3
"""Numeric NFA fixtures for emit-regular-span-dfa-v1 (Thompson style).

keywords.nfa  45 tags over scalars:
                0..39  forty keywords, several prefixes of one another
                40     identifier [a-z]+
                41     layout [ \\t\\n]+
                42     "." only at the end of input
                43     ".."
                44     Greek small letters [U+03B1-U+03C9]+, above ASCII
bytes.nfa      4 tags over bytes: [0x80-0xFF]+, 0x00, "ab", [a-z]+
bad_tag.nfa    an accept naming tag 2 of a 2-tag NFA (refused)

with the tag names for --tag-names in keywords.names and bytes.names.
"""
import pathlib
import sys

KEYWORDS = (
    "in int inter interval do done does for forall fun function if iff then "
    "else elif let letrec match with where when while case of type typeclass "
    "class instance module open import export as at by to from begin end"
).split()
assert len(KEYWORDS) == 40 and len(set(KEYWORDS)) == 40


class Nfa:
    def __init__(self):
        self.states, self.edges, self.accepts = 0, [], []
        self.start = self.state()

    def state(self):
        self.states += 1
        return self.states - 1

    def edge(self, a, b, kind, ranges=()):
        self.edges.append((a, b, kind, list(ranges)))

    def literal(self, units, tag):
        at = self.state()
        self.edge(self.start, at, 0)
        for u in units:
            nxt = self.state()
            self.edge(at, nxt, 1, [(u, u)])
            at = nxt
        return at

    def plus(self, ranges, tag):
        a, b = self.state(), self.state()
        self.edge(self.start, a, 0)
        self.edge(a, b, 1, ranges)
        self.edge(b, b, 1, ranges)
        self.accepts.append((b, tag))

    def write(self, path, tags):
        lines = ["%d %d %d %d %d" % (self.states, self.start, len(self.edges), len(self.accepts), tags)]
        for a, b, kind, ranges in self.edges:
            lines.append(" ".join(str(x) for x in [a, b, kind, len(ranges)] + [v for r in ranges for v in r]))
        lines += ["%d %d" % acc for acc in self.accepts]
        pathlib.Path(path).write_text("\n".join(lines) + "\n")


def keywords(path):
    n = Nfa()
    for tag, word in enumerate(KEYWORDS):
        n.accepts.append((n.literal([ord(c) for c in word], tag), tag))
    n.plus([(ord("a"), ord("z"))], 40)
    n.plus([(9, 10), (32, 32)], 41)
    dot = n.literal([ord(".")], 42)
    end = n.state()
    n.edge(dot, end, 2)
    n.accepts.append((end, 42))
    n.accepts.append((n.literal([ord("."), ord(".")], 43), 43))
    n.plus([(0x3B1, 0x3C9)], 44)
    n.write(path, 45)


def byte_fixture(path):
    n = Nfa()
    n.plus([(0x80, 0xFF)], 0)
    n.accepts.append((n.literal([0], 1), 1))
    n.accepts.append((n.literal([ord("a"), ord("b")], 2), 2))
    n.plus([(ord("a"), ord("z"))], 3)
    n.write(path, 4)


def bad_tag(path):
    n = Nfa()
    n.accepts.append((n.literal([ord("x")], 2), 2))
    n.write(path, 2)


if __name__ == "__main__":
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    keywords(out / "keywords.nfa")
    byte_fixture(out / "bytes.nfa")
    bad_tag(out / "bad_tag.nfa")
    (out / "keywords.txt").write_text("\n".join(KEYWORDS) + "\n")
    (out / "keywords.names").write_text(",".join(KEYWORDS + ["ident", "layout", "dot_at_end", "dotdot", "greek"]))
    (out / "bytes.names").write_text("high,nul,ab,lower")
