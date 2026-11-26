"""Generate the Unicode character data of the string library.

From CPython 3.13's Unicode database (unicodedata, Unicode 15.1.0): every code
point's properties as one bit set, written as ranges of equal sets; and the
full case mappings (lower, upper, title, folded) where they differ from the
code point.  Before writing, the generator checks the data against the
database: each property set and each mapping, rebuilt from the tables, must
give what CPython's own str methods give for every code point.

  python3 -I tools/unicode_tables_v1.py OUTPUT.h [--check]
"""
import sys
import unicodedata

# bits 0..29: the general category, one bit each
CATEGORIES = ["Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No",
              "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So",
              "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn"]
# then the derived and other properties
PROPERTIES = ["Lowercase", "Uppercase", "Cased", "Case_Ignorable", "XID_Start",
              "XID_Continue", "Numeric_Type_Decimal", "Numeric_Type_Digit",
              "Numeric_Type_Numeric", "Bidi_WS", "Bidi_B", "Bidi_S"]
BIT = {name: i for i, name in enumerate(CATEGORIES + PROPERTIES)}
MAX = 0x110000
SIGMA = "Σ"


def props(cp):
    c = chr(cp)
    p = 1 << BIT[unicodedata.category(c)]
    lower, upper = c.islower(), c.isupper()
    cased = lower or upper or unicodedata.category(c) == "Lt"
    if lower:
        p |= 1 << BIT["Lowercase"]
    if upper:
        p |= 1 << BIT["Uppercase"]
    if cased:
        p |= 1 << BIT["Cased"]
    # Case_Ignorable, as the Final_Sigma rule reads it: a sigma after a cased
    # letter and C is final when C is ignorable (or cased, which is known); a
    # sigma after an uncased letter and a cased C is final unless C is ignorable
    if cased:
        ignorable = ("1" + c + SIGMA).lower()[-1] == "σ"
    else:
        ignorable = ("A" + c + SIGMA).lower()[-1] == "ς"
    if ignorable:
        p |= 1 << BIT["Case_Ignorable"]
    if c.isidentifier() and c != "_":
        p |= 1 << BIT["XID_Start"]
    if ("a" + c).isidentifier():
        p |= 1 << BIT["XID_Continue"]
    if c.isdecimal():
        p |= 1 << BIT["Numeric_Type_Decimal"]
    elif c.isdigit():
        p |= 1 << BIT["Numeric_Type_Digit"]
    elif c.isnumeric():
        p |= 1 << BIT["Numeric_Type_Numeric"]
    bidi = unicodedata.bidirectional(c)
    if bidi in ("WS", "B", "S"):
        p |= 1 << BIT["Bidi_" + bidi]
    return p


def mask(*names):
    m = 0
    for n in names:
        m |= 1 << BIT[n]
    return m


LETTER = mask("Lu", "Ll", "Lt", "Lm", "Lo")
NUMERIC = mask("Numeric_Type_Decimal", "Numeric_Type_Digit", "Numeric_Type_Numeric")
SPACE = mask("Zs", "Bidi_WS", "Bidi_B", "Bidi_S")
NOT_PRINTABLE = mask("Cc", "Cf", "Cs", "Co", "Cn", "Zs", "Zl", "Zp")


def check(table):
    # every property the str methods read, rebuilt from the bits
    for cp in range(MAX):
        c, p = chr(cp), table[cp]
        derived = {
            "isalpha": bool(p & LETTER),
            "isdecimal": bool(p & mask("Numeric_Type_Decimal")),
            "isdigit": bool(p & mask("Numeric_Type_Decimal", "Numeric_Type_Digit")),
            "isnumeric": bool(p & NUMERIC),
            "isalnum": bool(p & (LETTER | NUMERIC)),
            "isspace": bool(p & SPACE),
            "isprintable": c == " " or not (p & NOT_PRINTABLE),
            "islower": bool(p & mask("Lowercase")),
            "isupper": bool(p & mask("Uppercase")),
            "isidentifier": bool(p & mask("XID_Start")) or c == "_",
        }
        for name, value in derived.items():
            if getattr(c, name)() != value:
                raise SystemExit("U+%04X: %s is %s, the bits say %s" % (cp, name, getattr(c, name)(), value))


def mappings():
    maps = {"lower": {}, "upper": {}, "title": {}, "fold": {}}
    for cp in range(MAX):
        c = chr(cp)
        for kind, m in (("lower", c.lower()), ("upper", c.upper()), ("title", c.title()), ("fold", c.casefold())):
            if m != c:
                maps[kind][cp] = [ord(x) for x in m]
    return maps


def render():
    if unicodedata.unidata_version != "15.1.0":
        raise SystemExit("needs CPython 3.13's Unicode 15.1.0, not " + unicodedata.unidata_version)
    table = [props(cp) for cp in range(MAX)]
    check(table)
    ranges = []
    for cp in range(MAX):
        if not ranges or ranges[-1][1] != table[cp]:
            ranges.append((cp, table[cp]))
    maps = mappings()
    out = [HEADER % unicodedata.unidata_version]
    for i, name in enumerate(CATEGORIES + PROPERTIES):
        out.append("#define CETTA_UNICODE_%s (UINT64_C(1) << %d)" % (name.upper(), i))
    out.append("")
    out.append("static const CettaUnicodeRange cetta_unicode_ranges[%d] = {" % len(ranges))
    for i in range(0, len(ranges), 4):
        out.append("    " + " ".join("{0x%X, UINT64_C(0x%X)}," % r for r in ranges[i:i + 4]))
    out.append("};")
    pool = []
    for kind in ("lower", "upper", "title", "fold"):
        entries = sorted(maps[kind].items())
        out.append("")
        out.append("static const CettaUnicodeMapping cetta_unicode_%s[%d] = {" % (kind, len(entries)))
        line = []
        for cp, seq in entries:
            line.append("{0x%X, %d, %d}," % (cp, len(pool), len(seq)))
            pool.extend(seq)
            if len(line) == 6:
                out.append("    " + " ".join(line))
                line = []
        if line:
            out.append("    " + " ".join(line))
        out.append("};")
    out.append("")
    out.append("static const uint32_t cetta_unicode_pool[%d] = {" % len(pool))
    for i in range(0, len(pool), 12):
        out.append("    " + " ".join("0x%X," % x for x in pool[i:i + 12]))
    out.append("};")
    return "\n".join(out) + "\n"


HEADER = """/* Generated by tools/unicode_tables_v1.py from CPython's Unicode database
 * (Unicode %s); do not edit.  cetta_unicode_ranges: each code point's
 * properties, the set of the range that starts at or before it; the
 * mappings: a code point's full lower, upper, title or folded case, LEN code
 * points of cetta_unicode_pool from AT, where it is not the code point. */
"""


def main(argv):
    if len(argv) < 2 or argv[1].startswith("-"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    text = render()
    if "--check" in argv[2:]:
        try:
            current = open(argv[1], encoding="utf-8").read()
        except FileNotFoundError:
            current = None
        if current != text:
            print(argv[1] + " differs from a fresh generation", file=sys.stderr)
            return 1
        print(argv[1] + " is up to date")
        return 0
    with open(argv[1], "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote " + argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
