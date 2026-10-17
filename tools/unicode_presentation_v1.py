"""Read shared Unicode data and interpret authored finite class expressions.

The catalog is a first-class data value, not the class algebra's definition.
Version, aliases, domains, intervals, decimal values and simple-fold classes
come from that value, never from the host locale or Unicode installation.
"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path

from gslt2parse_schema_v1 import StringLiteral, Symbol, admit, parse_sexprs


Range = tuple[int, int]
DOMAINS = {
    "Bytes": ((0, 0xFF),),
    "Utf16Units": ((0, 0xFFFF),),
    "Utf32Units": ((0, 0xFFFFFFFF),),
    "CodePoints": ((0, 0x10FFFF),),
    "Scalars": ((0, 0xD7FF), (0xE000, 0x10FFFF)),
}

def normalized(ranges: list[Range]) -> tuple[Range, ...]:
    result: list[Range] = []
    for low, high in sorted(ranges):
        if not 0 <= low <= high <= 0xFFFFFFFF:
            raise ValueError("Interval is outside the uint32 domain")
        if result and low <= result[-1][1] + 1:
            result[-1] = result[-1][0], max(result[-1][1], high)
        else:
            result.append((low, high))
    return tuple(result)


def subtract(left: tuple[Range, ...], right: tuple[Range, ...]) -> tuple[Range, ...]:
    answer: list[Range] = []
    cursor = 0
    for low, high in left:
        while cursor < len(right) and right[cursor][1] < low:
            cursor += 1
        index, position = cursor, low
        while index < len(right) and right[index][0] <= high:
            other_low, other_high = right[index]
            if position < other_low:
                answer.append((position, other_low - 1))
            position = max(position, other_high + 1)
            if position > high:
                break
            index += 1
        if position <= high:
            answer.append((position, high))
    return tuple(answer)


def read_catalog(path: Path) -> dict:
    """Interpret a versioned, first-class interval/list DAG as raw UCD data.

    This is the finite provider for the class algebra, not that algebra's
    definition. Shared identities alter storage only, never class membership.
    """
    forms = parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if (len(forms) != 1 or not isinstance(forms[0], tuple) or not forms[0] or
            forms[0][0] != Symbol("unicode-interval-catalog-v1")):
        raise ValueError("Expected one unicode-interval-catalog-v1 value")
    fields = {}
    for field in forms[0][1:]:
        if (not isinstance(field, tuple) or not field or
                not isinstance(field[0], Symbol) or field[0].text in fields):
            raise ValueError("Malformed or duplicate Unicode data field")
        fields[field[0].text] = field[1:]
    required = {"version", "domains", "intervals", "range-lists", "classes",
                "property-aliases", "value-aliases", "decimal-zeroes", "fold-groups"}
    if set(fields) != required:
        raise ValueError("Unicode data value has missing or unknown fields")

    def text(term) -> str:
        if (not isinstance(term, StringLiteral) or not term.text or
                not term.text.isascii()):
            raise ValueError("Expected an ASCII Unicode data string")
        return term.text

    def number(term, maximum=0xFFFFFFFF) -> int:
        if type(term) is not int or not 0 <= term <= maximum:
            raise ValueError("Unicode data integer is outside its declared domain")
        return term

    def sequence(term, count=None):
        if not isinstance(term, tuple) or (count is not None and len(term) != count):
            raise ValueError("Malformed Unicode data row")
        return term

    def unique_rows(rows, count):
        result = [tuple(text(x) for x in sequence(row, count)) for row in rows]
        if result != sorted(set(result)):
            raise ValueError("Unicode aliases must be ordered and unique")
        return result

    if len(fields["version"]) != 1:
        raise ValueError("Expected one Unicode version")
    version = text(fields["version"][0])
    if any(c not in "0123456789." for c in version):
        raise ValueError("Malformed Unicode version")
    domains = {}
    for row in fields["domains"]:
        domain, ranges = sequence(row, 2)
        name = text(domain)
        if name in domains:
            raise ValueError("Duplicate Unicode domain")
        domains[name] = tuple(tuple(number(x) for x in sequence(r, 2))
                              for r in sequence(ranges))
    if domains != DOMAINS:
        raise ValueError("Unicode data lacks the exact declared universes")
    intervals = [tuple(number(x, 0x10FFFF) for x in sequence(row, 2))
                 for row in fields["intervals"]]
    if intervals != sorted(set(intervals)) or any(lo > hi for lo, hi in intervals):
        raise ValueError("Shared intervals must be ordered, unique and nonempty")
    lists = []
    for row in fields["range-lists"]:
        indexes = sequence(row)
        ranges = tuple(intervals[number(i, len(intervals) - 1)] for i in indexes)
        if ranges != normalized(list(ranges)):
            raise ValueError("A shared range list is not canonical")
        lists.append(ranges)
    if lists != sorted(set(lists)):
        raise ValueError("Shared range lists must be ordered and unique")
    sets = {}
    for row in fields["classes"]:
        p, v, index = sequence(row, 3)
        key = text(p), text(v)
        if key in sets:
            raise ValueError("Duplicate Unicode property class")
        sets[key] = lists[number(index, len(lists) - 1)]
    if list(sets) != sorted(sets) or not sets:
        raise ValueError("Property classes must be nonempty and ordered")
    properties = unique_rows(fields["property-aliases"], 2)
    values = unique_rows(fields["value-aliases"], 3)
    if len({a for a, _ in properties}) != len(properties):
        raise ValueError("Conflicting property alias")
    if len({(p, a) for p, a, _ in values}) != len(values):
        raise ValueError("Conflicting property value alias")
    decimal = [number(z, 0x10FFFF - 9) for z in fields["decimal-zeroes"]]
    if decimal != sorted(set(decimal)) or any(a + 9 >= b for a, b in zip(decimal, decimal[1:])):
        raise ValueError("Decimal blocks overlap or repeat")
    if normalized([(z, z + 9) for z in decimal]) != sets.get(("gc", "Nd")):
        raise ValueError("Decimal blocks differ from Unicode Nd")
    folds = [tuple(number(cp, 0x10FFFF) for cp in sequence(row))
             for row in fields["fold-groups"]]
    seen = set()
    for members in folds:
        if (len(members) < 2 or members != tuple(sorted(set(members))) or
                seen.intersection(members) or
                any(0xD800 <= cp <= 0xDFFF for cp in members)):
            raise ValueError("Invalid simple-fold equivalence class")
        seen.update(members)
    if folds != sorted(folds):
        raise ValueError("Simple-fold classes must be ordered")
    return {"version": version, "sets": sets, "domains": domains,
            "properties": properties, "values": values,
            "decimal_zeroes": decimal, "fold_groups": folds}


def read_lexical_classes(path: Path, catalog: dict) -> dict:
    presentation, = admit([path])
    property_aliases = dict(catalog["properties"])
    value_aliases = {(p, alias): value for p, alias, value in catalog["values"]}

    def loose(value: str) -> str:
        return "".join(c.lower() for c in value if c not in "_- \t\r\n\v\f")

    def name(value) -> str:
        if not isinstance(value, (Symbol, StringLiteral)) or not value.text or not value.text.isascii():
            raise ValueError("Expected an ASCII lexical-class name")
        return value.text

    def point(term) -> int:
        if (not isinstance(term, tuple) or len(term) != 2 or name(term[0]) != "cp" or
                type(term[1]) is not int or not 0 <= term[1] <= 0x10FFFF):
            raise ValueError("Expected a code-point bound")
        return term[1]

    def expression(term) -> tuple[Range, ...]:
        if not isinstance(term, tuple) or not term:
            raise ValueError("Expected a Unicode class expression")
        tag, args = name(term[0]), term[1:]
        if tag == "domain" and len(args) == 1:
            domain = name(args[0])
            if domain not in catalog["domains"]:
                raise ValueError("Lexical class has an unknown domain")
            return catalog["domains"][domain]
        if tag == "property" and len(args) == 3:
            if name(args[0]) != "Unicode16" or catalog["version"] != "16.0.0":
                raise ValueError("Lexical property has a different Unicode version")
            property_name = name(args[1])
            property_name = property_aliases.get(loose(property_name), property_name)
            value = name(args[2])
            value = value_aliases.get((property_name, loose(value)), value)
            key = property_name, value
            if key not in catalog["sets"]:
                raise ValueError("Lexical expression uses an absent Unicode property")
            return catalog["sets"][key]
        if tag == "range" and len(args) == 2:
            return normalized([(point(args[0]), point(args[1]))])
        if tag in {"union", "intersection", "difference"} and len(args) == 2:
            left, right = map(expression, args)
            if tag == "union":
                return normalized(list(left + right))
            if tag == "difference":
                return subtract(left, right)
            return subtract(left, subtract(left, right))
        if tag == "complement" and len(args) == 2:
            domain = name(args[0])
            if domain not in catalog["domains"]:
                raise ValueError("Lexical complement has an unknown universe")
            return subtract(catalog["domains"][domain], expression(args[1]))
        raise ValueError(f"Unknown or malformed lexical class expression: {tag}")

    def scoped_expression(term, domain: str) -> tuple[Range, ...]:
        ranges = normalized(list(expression(term)))
        if subtract(ranges, catalog["domains"][domain]):
            raise ValueError("Lexical class includes values outside its domain")
        return ranges

    classes = {}
    equalities = {}
    digits: dict[str, dict[int, int]] = defaultdict(dict)
    for rule in presentation.rules:
        if rule.body:
            raise ValueError("Lexical class declarations must be ground facts")
        tag, args = name(rule.head[0]), rule.head[1:]
        if tag == "lexical-class-v1" and len(args) == 3:
            key, domain = name(args[0]), name(args[1])
            if key in classes or domain not in catalog["domains"]:
                raise ValueError("Duplicate lexical class or unknown class domain")
            ranges = scoped_expression(args[2], domain)
            classes[key] = (domain, ranges)
        elif tag == "lexical-class-equality-v1" and len(args) == 4:
            key, domain = name(args[0]), name(args[1])
            if key in equalities or domain not in catalog["domains"]:
                raise ValueError("Duplicate lexical equality or unknown equality domain")
            left, right = (scoped_expression(term, domain) for term in args[2:])
            if left != right:
                exclusive = subtract(left, right) + subtract(right, left)
                witness = min(low for low, _ in exclusive)
                raise ValueError(f"False lexical class equality: {key}; "
                                 f"first different value 0x{witness:04x}")
            # These are checked ground claims about interpreted classes. They
            # do not add an equation solver to the fact-only GSLT admission.
            equalities[key] = (domain, left)
        elif tag == "digit-values-v1" and len(args) == 4:
            key, low, high, zero = name(args[0]), point(args[1]), point(args[2]), args[3]
            if type(zero) is not int or not 0 <= zero <= 35 or low > high or high - low + zero > 35:
                raise ValueError("Invalid lexical digit-value range")
            for cp in range(low, high + 1):
                if cp in digits[key]:
                    raise ValueError("Overlapping lexical digit values")
                digits[key][cp] = zero + cp - low
        else:
            raise ValueError("Unexpected lexical class relation")
    for key, values in digits.items():
        if key not in classes or normalized([(cp, cp) for cp in values]) != classes[key][1]:
            raise ValueError("Lexical digit values do not cover precisely their class")
    return {"classes": dict(sorted(classes.items())), "digits": dict(sorted(digits.items())),
            "equalities": dict(sorted(equalities.items()))}
