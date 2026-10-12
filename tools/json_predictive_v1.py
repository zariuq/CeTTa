#!/usr/bin/env python3
"""Compile the JSON LanguageDef/profile/elaboration into predictive C.

Regular lexical categories are lowered to an NFA for the existing interval
DFA compiler. Structural choices are checked with FIRST/FOLLOW after lexical
folding and the elaboration's whitespace erasure. Conflicts fail the build;
they never silently choose an alternative. This compiler accepts the JSON
elaboration fragment, not arbitrary semantic actions or arbitrary CFGs.
"""
from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

import gslt2parse_schema_v1 as sexpr


class CompileError(ValueError):
    pass


def symbol(value):
    if not isinstance(value, sexpr.Symbol):
        raise CompileError("expected a wire symbol")
    return value.text


def text(value):
    if not isinstance(value, sexpr.StringLiteral):
        raise CompileError("expected wire text")
    return value.text


def items(value):
    result = []
    while isinstance(value, tuple) and len(value) == 3 and symbol(value[0]) == "LCons":
        result.append(value[1]); value = value[2]
    if symbol(value) != "LNil":
        raise CompileError("malformed wire list")
    return result


def load(path, head):
    forms = sexpr.parse_sexprs(path.read_text(), source=str(path))
    if len(forms) != 1 or symbol(forms[0][0]) != head:
        raise CompileError("unexpected language wire root")
    return forms[0]


def grammar(path):
    root = load(path, "GSLTLanguageDefWireV1")
    rules = {}
    for row in items(root[3]):
        if symbol(row[0]) != "GrammarRule" or symbol(row[5]) != "EvalNone":
            raise CompileError("predictive parser requires premise-free syntax rules")
        label, category = text(row[1]), text(row[2])
        params = {}
        for param in items(row[3]):
            if symbol(param[0]) != "TermSimple" or symbol(param[2][0]) != "TBase":
                raise CompileError("unsupported grammar parameter")
            params[text(param[1])] = text(param[2][1])
        rhs = []
        for part in items(row[4]):
            kind = symbol(part[0])
            if kind == "SyntaxTerminal": rhs.append(("t", text(part[1])))
            elif kind == "SyntaxNonTerminal": rhs.append(("n", params[text(part[1])]))
            else: raise CompileError("unsupported syntax term " + kind)
        if label in rules: raise CompileError("duplicate rule " + label)
        rules[label] = dict(category=category, params=list(params.values()), rhs=rhs)
    return rules


def ranges(points, complement=False):
    points = sorted(set(points))
    output = []
    if complement:
        at = 0
        for point in points:
            if at < point: output.append((at, point - 1))
            at = point + 1
        if at <= 0x10ffff: output.append((at, 0x10ffff))
    else:
        for point in points:
            if output and output[-1][1] + 1 == point:
                output[-1] = (output[-1][0], point)
            else: output.append((point, point))
    scalar_ranges = []
    for lo, hi in output:
        if lo < 0xd800: scalar_ranges.append((lo, min(hi, 0xd7ff)))
        if hi >= 0xe000: scalar_ranges.append((max(lo, 0xe000), hi))
    return scalar_ranges


def profile(path):
    root = load(path, "GSLTParserProfileLayerV1")
    classes = {}
    for row in items(root[3]):
        kind = symbol(row[0])
        if kind not in ("LexicalClassPoints", "LexicalClassExcept"):
            raise CompileError("unsupported lexical class")
        classes[text(row[1])] = ranges(items(row[2]), kind == "LexicalClassExcept")
    states = {}
    for row in items(root[4]):
        if symbol(row[0]) != "LexicalState": raise CompileError("unsupported lexical state")
        states[text(row[1])] = classes[text(row[2])]
    # Semantic decoding supports precisely RFC-8259 scalar/escape classes.
    expected = {"JsonWsChar": [9, 10, 13, 32], "JsonDigit": list(range(48, 58)),
                "JsonDigit19": list(range(49, 58)), "JsonHexDigit": list(range(48, 58)) + list(range(65, 71)) + list(range(97, 103)),
                "JsonSimpleEscape": [34, 47, 92, 98, 102, 110, 114, 116],
                "JsonExpMark": [69, 101], "JsonSign": [43, 45]}
    for name, points in expected.items():
        if states.get(name) != ranges(points): raise CompileError("outside scalar elaboration: " + name)
    if states.get("JsonUnescaped") != ranges(list(range(32)) + [34, 92], True):
        raise CompileError("outside Unicode scalar elaboration")
    return states


def regular(category, rules, classes, stack=()):
    if category in classes: return ("range", classes[category])
    if category in stack: raise CompileError("nonregular lexical recursion: " + category)
    bases, loops = [], []
    for row in rules.values():
        if row["category"] != category: continue
        rhs = list(row["rhs"])
        recursive = rhs and rhs[-1] == ("n", category)
        if recursive: rhs.pop()
        parts = []
        for kind, value in rhs:
            if kind == "t": parts.extend(("range", [(ord(cp), ord(cp))]) for cp in value)
            else: parts.append(regular(value, rules, classes, stack + (category,)))
        (loops if recursive else bases).append(("seq", parts))
    if not bases: raise CompileError("lexical category has no finite base: " + category)
    base = ("alt", bases)
    return ("seq", [("star", ("alt", loops)), base]) if loops else base


class NFA:
    def __init__(self): self.count = 0; self.edges = []; self.accepts = []
    def state(self):
        state = self.count; self.count += 1; return state
    def epsilon(self, start, end): self.edges.append((start, end, 0, []))
    def expression(self, regex):
        kind, body = regex
        start, end = self.state(), self.state()
        if kind == "range": self.edges.append((start, end, 1, body))
        elif kind == "seq":
            at = start
            for part in body:
                left, right = self.expression(part); self.epsilon(at, left); at = right
            self.epsilon(at, end)
        elif kind == "alt":
            for part in body:
                left, right = self.expression(part); self.epsilon(start, left); self.epsilon(right, end)
        elif kind == "star":
            left, right = self.expression(body)
            self.epsilon(start, end); self.epsilon(start, left); self.epsilon(right, left); self.epsilon(right, end)
        else: raise CompileError("unknown regular expression")
        return start, end
    def write(self, path, start):
        output = [f"{self.count} {start} {len(self.edges)} {len(self.accepts)} 3"]
        for left, right, kind, spans in self.edges:
            output.append(f"{left} {right} {kind} {len(spans)} " + " ".join(f"{lo} {hi}" for lo, hi in spans))
        output.extend(f"{state} {tag}" for state, tag in self.accepts)
        path.write_text("\n".join(output) + "\n")


def first_follow(rules):
    cats = {row["category"] for row in rules.values()}
    nullable = set(); first = {cat: set() for cat in cats}; follow = {cat: set() for cat in cats}
    follow["JsonText"].add("$EOF")
    def starts(rhs):
        result = set()
        for kind, value in rhs:
            result |= {value} if kind == "t" else first[value]
            if kind == "t" or value not in nullable: return result, False
        return result, True
    while True:
        before = set(nullable), {k: set(v) for k,v in first.items()}
        for row in rules.values():
            values, empty = starts(row["rhs"]); first[row["category"]] |= values
            if empty: nullable.add(row["category"])
        if (nullable, first) == before: break
    while True:
        before = {k: set(v) for k,v in follow.items()}
        for row in rules.values():
            for index, (kind, value) in enumerate(row["rhs"]):
                if kind != "n": continue
                values, empty = starts(row["rhs"][index+1:]); follow[value] |= values
                if empty: follow[value] |= follow[row["category"]]
        if follow == before: break
    selected = {}
    for label, row in rules.items():
        predicted, empty = starts(row["rhs"])
        if empty: predicted |= follow[row["category"]]
        for token in predicted:
            key = row["category"], token
            if key in selected: raise CompileError(f"FIRST/FOLLOW conflict: {key}: {selected[key]} versus {label}")
            selected[key] = label
    return first, follow, selected


def compile_sources(syntax, lexical, target, elaboration):
    rules = grammar(syntax); classes = profile(lexical); targets = grammar(target)
    specs = re.findall(r'\{"([^"]+)", "([^"]+)", (\d+)u, \{([^}]*)\}, (CETTA_JSON_ELAB_\w+)\}', elaboration.read_text())
    if len(specs) != len(rules) or {row[0] for row in specs} != set(rules):
        raise CompileError("source operation inventory differs from elaboration")
    for label, category, arity, params, operation in specs:
        row = rules.get(label)
        expected = re.findall(r'"([^"]+)"', params)
        if not row or row["category"] != category or row["params"] != expected:
            raise CompileError("source/elaboration disagreement: " + label)
        expected_operation = "CETTA_JSON_ELAB_" + label.removeprefix("json:").replace("-", "_").upper() + "_V1"
        if int(arity) != len(expected) or operation != expected_operation:
            raise CompileError("outside source elaboration operation: " + label)
        row["operation"] = operation
    required = {"JsonNullV1", "JsonBoolV1", "JsonStringV1", "JsonNumberV1", "JsonObjectV1", "JsonArrayV1", "JsonMemberV1", "JsonSourceSpanV1", "JsonNoSourceSpanV1"}
    if set(targets) != required: raise CompileError("unsupported occurrence-value constructors")
    target_specs = re.findall(r'\{"(Json\w+V1)", "([^"]+)", (\d+)u,\s*\{([^}]*)\}\}', elaboration.read_text())
    if len(target_specs) != len(targets) or {row[0] for row in target_specs} != set(targets):
        raise CompileError("target operation inventory differs from elaboration")
    for label, category, arity, params in target_specs:
        row = targets[label]
        expected = re.findall(r'"([^"]+)"', params)
        if int(arity) != len(expected) or row["category"] != category or row["params"] != expected or row["rhs"] != [("t", label)]:
            raise CompileError("target/elaboration disagreement: " + label)
    # Decoding a lexical token is an elaboration operation. Refuse changes
    # outside that operation's supported language instead of accepting a DFA
    # token whose scalar or number meaning the decoder would misinterpret.
    lexical_templates = {
        "json:ws-empty": [], "json:ws-cons": [("n", "JsonWsChar"), ("n", "JsonWs")],
        "json:string": [("t", '"'), ("n", "JsonStringChars"), ("t", '"')],
        "json:string-chars-empty": [],
        "json:string-chars-cons": [("n", "JsonStringChar"), ("n", "JsonStringChars")],
        "json:string-char-plain": [("n", "JsonUnescaped")],
        "json:string-char-escape": [("n", "JsonEscape")],
        "json:escape-simple": [("t", "\\"), ("n", "JsonSimpleEscape")],
        "json:escape-unicode": [("t", "\\u")] + [("n", "JsonHexDigit")] * 4,
        "json:number": [("n", n) for n in ("JsonMinusOpt", "JsonInt", "JsonFracOpt", "JsonExpOpt")],
        "json:minus-none": [], "json:minus-some": [("t", "-")],
        "json:int-zero": [("t", "0")],
        "json:int-nonzero": [("n", "JsonDigit19"), ("n", "JsonDigits")],
        "json:digits-empty": [], "json:digits-cons": [("n", "JsonDigit"), ("n", "JsonDigits")],
        "json:frac-none": [], "json:frac-some": [("n", "JsonFrac")],
        "json:frac": [("t", "."), ("n", "JsonDigit"), ("n", "JsonDigits")],
        "json:exp-none": [], "json:exp-some": [("n", "JsonExp")],
        "json:exp": [("n", n) for n in ("JsonExpMark", "JsonSignOpt", "JsonDigit", "JsonDigits")],
        "json:sign-none": [], "json:sign-some": [("n", "JsonSign")],
    }
    for label, rhs in lexical_templates.items():
        if rules[label]["rhs"] != rhs: raise CompileError("outside lexical elaboration: " + label)
    # The semantic container loop folds these source productions directly.
    templates = {
        "json:text": ["JsonWs", "JsonValue", "JsonWs"],
        "json:object": [None, "JsonWs", "JsonMembersOpt", "JsonWs", None],
        "json:array": [None, "JsonWs", "JsonElementsOpt", "JsonWs", None],
        "json:member": ["JsonString", "JsonWs", None, "JsonWs", "JsonValue"],
        "json:member-tail-cons": ["JsonWs", None, "JsonWs", "JsonMember", "JsonMemberTail"],
        "json:element-tail-cons": ["JsonWs", None, "JsonWs", "JsonValue", "JsonElementTail"],
        "json:members-none": [], "json:members-some": ["JsonMembers"],
        "json:members": ["JsonMember", "JsonMemberTail"], "json:member-tail-empty": [],
        "json:elements-none": [], "json:elements-some": ["JsonElements"],
        "json:elements": ["JsonValue", "JsonElementTail"], "json:element-tail-empty": [],
        "json:value-object": ["JsonObject"], "json:value-array": ["JsonArray"],
        "json:value-number": ["JsonNumber"], "json:value-string": ["JsonString"],
    }
    for label, pattern in templates.items():
        rhs = rules[label]["rhs"]
        if len(rhs) != len(pattern) or any((kind != "n" or value != expected) if expected else
               (kind != "t" or len(value) != 1 or ord(value) > 127)
               for (kind, value), expected in zip(rhs, pattern)):
            raise CompileError("outside incremental container elaboration: " + label)
    folded = {}
    structural = {"JsonText", "JsonValue", "JsonObject", "JsonMembersOpt", "JsonMembers", "JsonMember",
                  "JsonMemberTail", "JsonArray", "JsonElementsOpt", "JsonElements", "JsonElementTail"}
    for label, row in rules.items():
        if row["category"] not in structural: continue
        rhs = []
        for kind, value in row["rhs"]:
            if kind == "n" and value == "JsonWs": continue
            if kind == "n" and value in ("JsonString", "JsonNumber"):
                rhs.append(("t", "@" + value))
            else: rhs.append((kind, value))
        folded[label] = dict(row, rhs=rhs)
    first, follow, selected = first_follow(folded)
    punctuation = {"JD_OBJECT_OPEN": rules["json:object"]["rhs"][0][1],
        "JD_OBJECT_CLOSE": rules["json:object"]["rhs"][-1][1],
        "JD_ARRAY_OPEN": rules["json:array"]["rhs"][0][1], "JD_ARRAY_CLOSE": rules["json:array"]["rhs"][-1][1],
        "JD_MEMBER_COLON": rules["json:member"]["rhs"][2][1], "JD_MEMBER_COMMA": rules["json:member-tail-cons"]["rhs"][1][1],
        "JD_ELEMENT_COMMA": rules["json:element-tail-cons"]["rhs"][1][1]}
    output = ["/* Generated predictive decisions from LanguageDef + elaboration. */"]
    output.extend(f"#define {name} {ord(value)}u" for name, value in punctuation.items())
    output.append("static JsonStartV1 jd_value_start(JsonDirectV1 *p, Atom **value) {")
    output.append('if (p->at == p->len) goto invalid;')
    output.append("switch(p->bytes[p->at]) {")
    byte_cases = set()
    for label, row in folded.items():
        if row["category"] != "JsonValue": continue
        tokens = {token for (category, token), chosen in selected.items() if category == "JsonValue" and chosen == label}
        starts = set()
        for token in tokens:
            starts |= {34} if token == "@JsonString" else set(range(48,58)) | {45} if token == "@JsonNumber" else {ord(token[0])}
        if byte_cases & starts: raise CompileError("value lookahead conflict: " + label)
        byte_cases |= starts
        output.extend(f"case {point}:" for point in sorted(starts))
        if label == "json:value-object": output.append("return JD_START_OBJECT;")
        elif label == "json:value-array": output.append("return JD_START_ARRAY;")
        elif label == "json:value-number": output.append("*value = jd_number(p); return *value ? JD_START_SCALAR : JD_START_ERROR;")
        elif label == "json:value-string": output.append("*value = jd_string(p,false); return *value ? JD_START_SCALAR : JD_START_ERROR;")
        else:
            literal = row["rhs"][0][1]; length = len(literal.encode())
            output.append(f"if ({length}u > p->len-p->at || memcmp(p->bytes+p->at,{json.dumps(literal)},{length})) break;")
            output.append(f"p->at += {length}; p->scalar += {len(literal)};")
            if label == "json:value-null": output.append('*value = jd_head(p,JD_NULL); return JD_START_SCALAR;')
            elif label in ("json:value-true", "json:value-false"):
                boolean = label == "json:value-true"
                output.append('*value = jd_one(p,JD_BOOL,p->petta ? atom_symbol(p->arena,"' +
                    str(boolean).lower() + '") : atom_bool(p->arena,' + str(boolean).lower() + ')); return JD_START_SCALAR;')
            else: raise CompileError("unsupported value action " + label)
    output.extend(['default: break; }', 'invalid: jd_error(p,CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,"a JSON value");', 'return JD_START_ERROR;', '}'])
    report = dict(source_sha256={p.name: sha256(p.read_bytes()).hexdigest() for p in (syntax, lexical, target, elaboration)},
        first={k: sorted(v) for k,v in first.items()}, follow={k: sorted(v) for k,v in follow.items()},
        structural_nonterminals=len(first), conflicts=[], lexical_categories=["JsonWs", "JsonNumber", "JsonString"],
        transformations=["regular lexical folding", "erase only the elaboration's JsonWs positions", "fold member and element tail recursion into loops"],
        generalized_islands=[])
    nfa = NFA(); start = nfa.state()
    for tag, category in enumerate(report["lexical_categories"]):
        left, right = nfa.expression(regular(category, rules, classes))
        nfa.epsilon(start, left); nfa.accepts.append((right, tag))
    return "\n".join(output) + "\n", report, nfa, start


ARTIFACT_FILES = ("lexical_v1.generated.h", "predictive_v1.generated.h",
                  "predictive_v1.generated.inc", "predictive_v1.report.json")


def artifact_identity(prefix, digest):
    return "".join(f"#define JD_{prefix}_ARTIFACT_{i} UINT32_C(0x{digest[i*8:i*8+8]})\n"
                   for i in range(8))


def build_artifact(syntax, lexical, target, elaboration, dfa_compiler, output):
    code, report, nfa, start = compile_sources(syntax, lexical, target, elaboration)
    report["format"] = "JsonPredictiveArtifactV1"
    report["compiler_sha256"] = {
        "lowering": sha256(Path(__file__).read_bytes()).hexdigest(),
        "schema": sha256(Path(sexpr.__file__).read_bytes()).hexdigest(),
        "dfa": sha256(dfa_compiler.read_bytes()).hexdigest(),
    }
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="predictive-", dir=output) as temporary:
        work = Path(temporary); nfa.write(work/"lexical.nfa", start)
        subprocess.run([str(dfa_compiler.resolve()), str(work/"lexical.nfa"), str(work/"lexical.h")], check=True)
        lexical_code = (work/"lexical.h").read_bytes()
        identity = dict(report=report, decisions_sha256=sha256(code.encode()).hexdigest(),
                        lexical_sha256=sha256(lexical_code).hexdigest())
        digest = sha256(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        report["artifact_sha256"] = digest
        checks = "".join(
            f'_Static_assert(JD_LEXICAL_ARTIFACT_{i} == JD_PREDICTIVE_ARTIFACT_{i} && '
            f'JD_PREDICTIVE_ARTIFACT_{i} == JD_IMPLEMENTATION_ARTIFACT_{i}, '
            '"JSON generated artifacts differ");\n' for i in range(8))
        prepared = {
            ARTIFACT_FILES[0]: artifact_identity("LEXICAL", digest).encode() + lexical_code,
            ARTIFACT_FILES[1]: (artifact_identity("PREDICTIVE", digest) + code.split("static JsonStartV1 jd_value_start", 1)[0]).encode(),
            ARTIFACT_FILES[2]: (artifact_identity("IMPLEMENTATION", digest) + checks + code).encode(),
            ARTIFACT_FILES[3]: (json.dumps(report, indent=2, sort_keys=True) + "\n").encode(),
        }
        # Finish every file before publication. Each replacement is atomic;
        # an interrupted mixed generation is rejected by the C assertions.
        for name, content in prepared.items():
            (work/name).write_bytes(content)
        for name in ARTIFACT_FILES:
            (work/name).replace(output/name)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--syntax", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--elaboration", type=Path, required=True)
    parser.add_argument("--dfa-compiler", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = build_artifact(args.syntax, args.profile, args.target, args.elaboration,
                            args.dfa_compiler, args.output)
    print(f"JSON: {report['structural_nonterminals']} predictive nonterminals, one lexical DFA with 3 tags, no structural conflicts")


if __name__ == "__main__": main()
