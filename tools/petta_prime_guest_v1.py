"""Translate passive PeTTa pattern views and quotation into explicit Prime forms.

This adapter covers the retained closed HOL Light and MM0 checking programs.
It does not make one dialect the definition of another. Sequence-view patterns
are lowered to guarded structural observations; quoted values use Prime's
explicit value return. The existing native scope analysis then makes sharing
explicit under lexical-fresh. Logical proof rules are left in the source.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from library_spelling import Symbol, read_forms, show
from prime_scope_migration import native_plan, translate

# Ground contracts of the admitted closed source fragment. Special forms
# below retain their own evaluation order and raw fields. Other heads absent
# from the source equation/signature table denote data, as in PeTTa's compiler.
PRIMITIVES = frozenset({
    "+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=",
    "and", "or", "not", "xor", "implies", "is-member",
    "get-type", "get-metatype", "size-atom", "index-atom", "decons-atom",
    "cons-atom", "get-atoms", "new-space", "get-state", "change-state!",
    "println!", "readln!", "string-to-chars", "chars-to-string",
    "string-to-bytes", "bytes-to-string", "strcat", "strlen", "parse",
})


def form(head: str, *arguments: object) -> list:
    return [Symbol(head), *arguments]


def variables(node: object) -> set[str]:
    if isinstance(node, Symbol):
        return {str(node)} if node.startswith("$") else set()
    if isinstance(node, (list, tuple)):
        return set().union(*(variables(child) for child in node))
    return set()


class PatternViews:
    def __init__(self, forms: list, external_heads: tuple[str, ...] = (),
                 *, cons_values: str = "flat", capture_external_errors: tuple[str, ...] = (),
                 sort_values: str | None = None):
        if cons_values not in ("flat", "spine-or-flat"):
            raise ValueError("unknown source cons-value representation")
        self.cons_values = cons_values
        if sort_values not in (None, "closed-strings"):
            raise ValueError("unknown source sorting fragment")
        self.sort_values = sort_values
        self.string_sorts = 0
        self.used = variables(forms)
        self.serial = 0
        self.views = 0
        self.quotations = 0
        self.value_sequences = 0
        self.named_state = False
        self.pattern_removal = False
        self.sequence_primitives: set[str] = set()
        self.state_name = "__guest_named_state_v1"
        self.symbols: set[str] = set()
        def collect(node: object) -> None:
            if isinstance(node, Symbol):
                self.symbols.add(str(node))
            elif isinstance(node, (list, tuple)):
                for child in node:
                    collect(child)
        collect(forms)
        self.equations = {str(node[1][0]) for node in forms
                          if isinstance(node, list) and len(node) == 3
                          and node[0] == "=" and isinstance(node[1], list) and node[1]}
        self.external_heads = set(external_heads)
        self.capture_external_errors = set(capture_external_errors)
        self.captured_calls: set[tuple[str, int]] = set()
        if not self.capture_external_errors <= self.external_heads:
            raise ValueError("error capture requires an explicitly declared external operation")
        if self.capture_external_errors & self.equations:
            raise ValueError("an external error profile cannot replace a source equation")
        if any(not name or name.startswith(("$", "&")) or
               any(character.isspace() or character in "()" for character in name)
               for name in self.external_heads):
            raise ValueError("an external declaration must name an application head")
        self.raw_arguments: dict[str, set[int]] = {}
        for node in forms:
            if (isinstance(node, list) and len(node) == 3 and node[0] == ":"
                    and isinstance(node[2], list) and node[2] and node[2][0] == "->"):
                raw = {i for i, ty in enumerate(node[2][1:-1]) if ty == "Atom"}
                name = str(node[1])
                if name in self.raw_arguments and self.raw_arguments[name] != raw:
                    raise ValueError("ambiguous argument demand for " + name)
                self.raw_arguments[name] = raw
        while any(name.startswith(self.state_name) or
                  name.startswith("&" + self.state_name) for name in self.symbols):
            self.state_name += "_"

    def application(self, head: object, arguments: list,
                    *, data: bool = False) -> object:
        """PeTTa's left-to-right value sequence expressed with Prime cases."""
        raw = self.raw_arguments.get(str(head), set()) if not data else set()
        held = []
        for i, argument in enumerate(arguments):
            if i in raw:
                # A source variable already denotes a captured value. A
                # written raw expression is syntax, even if its head also
                # names an executable source function.
                expression = (argument if isinstance(argument, Symbol)
                              and argument.startswith("$") else
                              form("function", form("return", argument)))
            else:
                expression = self.expression(argument)
            held.append((self.fresh(), expression))
        body = [head, *(name for name, _ in held)]
        if data:
            body = form("function", form("return", body))
        for name, expression in reversed(held):
            self.value_sequences += 1
            body = form("case", expression, [[name, body]])
        return body

    def fresh(self) -> Symbol:
        while True:
            self.serial += 1
            name = "$__guest_pattern_" + str(self.serial)
            if name not in self.used:
                self.used.add(name)
                return Symbol(name)

    def error_view(self, node: object) -> object:
        """The selected readout distinguishes error presence, not its payload.

        Prime propagates native Error values. A passive source error tag allows
        the source handlers to inspect the same presence distinction. Exact
        error payload observers require a richer external result contract.
        """
        if isinstance(node, tuple):
            return node[0], self.error_view(node[1])
        if isinstance(node, list):
            result = [self.error_view(child) for child in node]
            if len(result) == 3 and result[0] == "Error":
                result[0] = Symbol(self.state_name + ":SourceError")
            return result
        return node

    @staticmethod
    def has_view(pattern: object) -> bool:
        return (isinstance(pattern, list) and
                ((len(pattern) == 3 and pattern[0] == "cons") or
                 any(PatternViews.has_view(child) for child in pattern)))

    def matching(self, pattern: object, value: object, yes: object, no: object) -> object:
        if isinstance(pattern, list) and len(pattern) == 3 and pattern[0] == "cons":
            self.views += 1
            first, rest = self.fresh(), self.fresh()
            selected = self.matching(pattern[1], first,
                                    self.matching(pattern[2], rest, yes, no), no)
            decomposition = form("decons-atom", value)
            if self.cons_values == "spine-or-flat":
                # CeTTa's PeTTa matcher also opens authored cons spines.
                # Native grammar trees use that physical representation.
                # Ordinary PeTTa's flat-list contract remains selectable.
                # Sequence the selected fields; return must hold their values,
                # rather than the unevaluated index operations.
                left, right = self.fresh(), self.fresh()
                spine = form("case", form("index-atom", value, Symbol("1")),
                    [[left, form("case", form("index-atom", value, Symbol("2")),
                        [[right, form("function", form("return", [left, right]))]])]])
                decomposition = form("if", form("==", form("size-atom", value), Symbol("3")),
                    form("if", form("==", form("index-atom", value, Symbol("0")),
                                    Symbol("cons")), spine, decomposition), decomposition)
            selected = form("let", [first, rest], decomposition, selected)
            nonempty = form("if", form(">", form("size-atom", value), Symbol("0")), selected, no)
            return form("if", form("==", form("get-metatype", value),
                                   Symbol("Expression")), nonempty, no)
        holes: list[tuple[Symbol, object]] = []

        def mask(node: object) -> object:
            if isinstance(node, list):
                if len(node) == 3 and node[0] == "cons":
                    hole = self.fresh()
                    holes.append((hole, node))
                    return hole
                return [mask(child) for child in node]
            return node

        ordinary = mask(pattern)
        for hole, original in reversed(holes):
            yes = self.matching(original, hole, yes, no)
        return form("case", value, [[ordinary, yes], [self.fresh(), no]])

    def rows(self, value: object, rows: list) -> object:
        if not rows:
            return form("superpose", [])
        pattern, body = rows[0]
        return self.matching(pattern, value, self.expression(body),
                             self.rows(value, rows[1:]))

    def expression(self, node: object) -> object:
        if not isinstance(node, list) or not node:
            return node
        head = node[0]
        if head == "empty" and len(node) == 1:
            return form("superpose", [])
        if head == "quote" and len(node) == 2:
            self.quotations += 1
            return form("function", form("return", node[1]))
        if head in ("get-state", "change-state!"):
            self.named_state = True
            name = self.state_name + (":get" if head == "get-state" else ":set")
            return self.application(Symbol(name), node[1:])
        if head == "if" and len(node) in (3, 4):
            branches = [self.expression(child) for child in node[1:]]
            if len(node) == 3:
                branches.append(form("superpose", []))
            return [head, *branches]
        if head == "collapse" and len(node) == 2:
            return form("collapse", self.expression(node[1]))
        if head == "once" and len(node) == 2:
            return form("once", self.expression(node[1]))
        if head == "sort-atom" and head not in self.equations:
            if self.sort_values != "closed-strings" or len(node) != 2:
                raise ValueError("source sort-atom requires a declared closed string-list contract")
            # PeTTa uses SWI's standard string order. Prime's general atom
            # sort instead uses shortlex reader keys. This selected fragment
            # uses Prime's lexical string sort, retaining repeated names.
            self.string_sorts += 1
            return self.application(Symbol("sort-strings"), node[1:])
        if head in ("is-member", "reverse") and len(node) == (3 if head == "is-member" else 2):
            self.sequence_primitives.add(str(head))
            return self.application(Symbol(self.state_name + ":" + str(head)), node[1:])
        if head == "superpose" and len(node) == 2 and isinstance(node[1], list):
            return form("superpose", [self.expression(child) for child in node[1]])
        if head in ("add-atom", "remove-atom") and len(node) == 3:
            space, changed = self.fresh(), self.fresh()
            if head == "remove-atom":
                self.pattern_removal = True
                pattern, rows = self.fresh(), self.fresh()
                erase = form(self.state_name + ":erase-rows", space, rows)
                matches = form("collapse", form("match", space, pattern, pattern))
                selected = form("case", matches, [[rows, erase]])
                guarded = form("if", form("==", form("get-metatype", pattern),
                               Symbol("Expression")),
                               form("if", form(">", form("size-atom", pattern),
                                    Symbol("0")), selected, Symbol("True")), Symbol("True"))
                mutation = form("case", form("function", form("return", node[2])),
                                [[pattern, guarded]])
            else:
                mutation = form("case", form(head, space, node[2]),
                                [[changed, Symbol("True")]])
            return form("case", self.expression(node[1]), [[space, mutation]])
        if head == "match" and len(node) == 4:
            space = self.fresh()
            return form("case", self.expression(node[1]),
                        [[space, form("match", space, node[2], self.expression(node[3]))]])
        if head == "eval" and len(node) == 2:
            # Activation is qualified to frontend-emitted closed requests.
            # This does not translate arbitrary newly received source code.
            return node
        if head == "case" and len(node) == 3 and isinstance(node[2], list):
            if not all(isinstance(row, list) and len(row) == 2 for row in node[2]):
                raise ValueError("malformed case rows")
            if any(self.has_view(row[0]) for row in node[2]):
                value = self.fresh()
                return form("let", value, self.expression(node[1]), self.rows(value, node[2]))
            return form("case", self.expression(node[1]),
                        [[pattern, self.expression(body)] for pattern, body in node[2]])
        if head == "let" and len(node) == 4:
            pattern, value, body = node[1:]
            if self.has_view(pattern):
                local = self.fresh()
                return form("let", local, self.expression(value),
                            self.matching(pattern, local, self.expression(body),
                                          form("superpose", [])))
            local = self.fresh()
            return form("case", self.expression(value),
                        [[local, form("let", pattern, local, self.expression(body))]])
        if head == "let*" and len(node) == 3 and isinstance(node[1], list):
            body = node[2]
            for pair in reversed(node[1]):
                if not isinstance(pair, list) or len(pair) != 2:
                    raise ValueError("malformed let* binding")
                body = form("let", pair[0], pair[1], body)
            return self.expression(body)
        if head == "cons" and len(node) == 3:
            if self.cons_values == "spine-or-flat":
                first, rest = self.fresh(), self.fresh()
                cell = form("function", form("return", [Symbol("cons"), first, rest]))
                flat = form("cons-atom", first, rest)
                # A computed tail is a sequence value, including a visible
                # cons-shaped expression. Quoted parser syntax stays syntax;
                # it is opened by the pattern view above, not implicitly
                # reclassified here as a constructor result. This port does
                # not preserve observers of PeTTa's internal improper-list
                # tags or their printed spelling.
                constructed = form("if", form("==", form("get-metatype", rest),
                                              Symbol("Expression")),
                    flat, cell)
                return form("case", self.expression(node[1]), [[first,
                    form("case", self.expression(node[2]), [[rest, constructed]])]])
            return self.application(Symbol("cons-atom"), node[1:])
        static = isinstance(head, Symbol) and not head.startswith("$")
        if static and head in self.capture_external_errors:
            self.captured_calls.add((str(head), len(node) - 1))
            alias = self.state_name + ":external:" + str(head) + ":" + str(len(node) - 1)
            return self.application(Symbol(alias), node[1:])
        known = static and (head in self.equations or head in PRIMITIVES
                            or head in self.raw_arguments or head in self.external_heads
                            or head.startswith("__cetta_"))
        if static:
            return self.application(head, node[1:], data=not known)
        # In this closed fragment a compound head is an ordinary data-list
        # element. Callable heads are symbols; partial closures and authored
        # lambda syntax require a separate representation contract.
        value = self.fresh()
        return form("case", self.expression(head), [[value,
            form("if", form("==", form("get-metatype", value), Symbol("Expression")),
                 self.application(value, node[1:], data=True),
                 self.application(value, node[1:]))]])

    def declaration(self, node: object) -> object:
        if isinstance(node, tuple):
            return (node[0], self.expression(node[1]))
        if isinstance(node, list) and len(node) == 3 and node[0] == "=":
            pattern, body = node[1:]
            if self.has_view(pattern):
                holes: list[tuple[Symbol, object]] = []

                def mask(child: object) -> object:
                    if self.has_view(child):
                        name = self.fresh()
                        holes.append((name, child))
                        return name
                    return child

                if not isinstance(pattern, list) or not pattern:
                    raise ValueError("a sequence-view equation requires an application head")
                pattern = [pattern[0], *(mask(child) for child in pattern[1:])]
                body = self.expression(body)
                for name, original in reversed(holes):
                    body = self.matching(original, name, body, form("superpose", []))
                return form("=", pattern, body)
            return form("=", pattern, self.expression(body))
        # Types and passive declarations are retained, not interpreted as calls.
        return node


def lower(text: str, external_heads: tuple[str, ...] = (),
          *, cons_values: str = "flat", capture_external_errors: tuple[str, ...] = (),
          sort_values: str | None = None) -> tuple[str, dict]:
    forms = read_forms(text)
    adapter = PatternViews(forms, external_heads, cons_values=cons_values,
                           capture_external_errors=capture_external_errors, sort_values=sort_values)
    translated = [adapter.declaration(adapter.error_view(node) if capture_external_errors else node)
                  for node in forms]
    if adapter.captured_calls:
        # Observe each declared finite deterministic operation once. The
        # result bag keeps an Error passive until its presence is inspected;
        # an unobserved stored value could escape its handler later.
        value, source, reason, unexpected = map(
            Symbol, ("$value", "$source", "$reason", "$unexpected"))
        error_tag = Symbol(adapter.state_name + ":SourceError")
        helpers = []
        for head, arity in sorted(adapter.captured_calls):
            arguments = [Symbol("$argument" + str(i)) for i in range(arity)]
            alias = adapter.state_name + ":external:" + head + ":" + str(arity)
            failed = form("function", form("return", [error_tag, Symbol(head),
                                                        Symbol("ExternalFailure")]))
            body = form("case", form("collapse", form(head, *arguments)),
                        [[[[Symbol("Error"), source, reason]], failed],
                         [[value], form("function", form("return", value))],
                         [[], form("superpose", [])],
                         [unexpected, form("Error", Symbol(head),
                                           Symbol("NonUniqueExternalResult"))]])
            helpers.append(form("=", form(alias, *arguments), body))
        translated = helpers + translated
    if adapter.sequence_primitives:
        # These source primitives are absent from Prime's vocabulary. Their
        # ground flat-sequence contract is authored using existing target
        # operations. Membership retains every positive occurrence, including
        # duplicates; a false result is produced only when none was found.
        item, rows, first, rest, result, acc = map(Symbol,
            ("$item", "$rows", "$first", "$rest", "$result", "$acc"))
        name = adapter.state_name
        helpers = []
        if "is-member" in adapter.sequence_primitives:
            hits = name + ":member-hits"
            helpers.extend([
                form("=", form(hits, item, rows),
                     form("if", form("==", form("size-atom", rows), Symbol("0")),
                          form("superpose", []),
                          form("let", [first, rest], form("decons-atom", rows),
                               form("superpose", [
                                   form("if", form("==", item, first), Symbol("True"),
                                        form("superpose", [])), form(hits, item, rest)])))),
                form("=", form(name + ":is-member", item, rows),
                     form("let", result, form("collapse", form(hits, item, rows)),
                          form("if", form("==", form("size-atom", result), Symbol("0")),
                               Symbol("False"), form("superpose", result)))),
            ])
        if "reverse" in adapter.sequence_primitives:
            loop = name + ":reverse-into"
            helpers.extend([
                form("=", form(loop, rows, acc),
                     form("if", form("==", form("size-atom", rows), Symbol("0")),
                          form("function", form("return", acc)),
                          form("let", [first, rest], form("decons-atom", rows),
                               form("case", form("cons-atom", first, acc),
                                    [[result, form(loop, rest, result)]])))),
                form("=", form(name + ":reverse", rows),
                     form("case", form("function", form("return", [])),
                          [[acc, form(loop, rows, acc)]])),
            ])
        translated = helpers + translated
    if adapter.pattern_removal:
        # This driver is for exclusively owned, ground row spaces. Its source
        # operation lowers to a sequence of exact deletions, so this contract
        # does not authorize concurrent publication or preserve deletion cost.
        space, rows, first, rest, removed = map(Symbol,
            ("$space", "$rows", "$first", "$rest", "$removed"))
        name = adapter.state_name + ":erase-rows"
        erase = form("=", form(name, space, rows),
                     form("if", form("==", form("size-atom", rows), Symbol("0")),
                          Symbol("True"),
                          form("let", [first, rest], form("decons-atom", rows),
                               form("case", form("remove-atom", space, first),
                                    [[removed, form(name, space, rest)]]))))
        translated = [erase, *translated]
    if adapter.named_state:
        name = adapter.state_name
        store = "&" + name
        key, value, values, added = map(Symbol, ("$key", "$value", "$values", "$added"))
        store = Symbol(store)
        get = form("=", form(name + ":get", key),
                   form("let", values, form("collapse", form("match", store, [key, value], value)),
                        form("if", form(">", form("size-atom", values), Symbol("0")),
                             form("index-atom", values, form("-", form("size-atom", values), Symbol("1"))),
                             form("superpose", []))))
        # The private append-only history is observed through its latest row.
        # This realizes named replacement for the value observer; it does not
        # claim equality of allocation cost or exposure of the private history.
        put = form("case", form("add-atom", store, [key, value]), [[added, Symbol("True")]])
        helpers = [form(":", Symbol(name + ":set"), form("->", Symbol("Atom"),
                    Symbol("Atom"), Symbol("%Undefined%"))),
                   get, form("=", form(name + ":set", key, value), put),
                   (Symbol("!"), form("bind!", store, form("new-space")))]
        translated = helpers + translated
    result = "\n".join(show(node) for node in translated) + "\n"
    return result, {"forms": len(forms), "sequence_views": adapter.views,
                    "source_cons_values": adapter.cons_values,
                    "source_sort_values": adapter.sort_values,
                    "lexical_string_sorts": adapter.string_sorts,
                    "improper_cons_observation": "sequence views; internal tags and spelling excluded"
                        if adapter.cons_values == "spine-or-flat" else None,
                    "value_quotations": adapter.quotations,
                    "value_sequences": adapter.value_sequences,
                    "named_state_adapter": adapter.named_state,
                    "declared_external_heads": sorted(adapter.external_heads),
                    "pattern_removal": "owned-ground-space ordered-delete" if
                        adapter.pattern_removal else None,
                    "ground_sequence_primitives": sorted(adapter.sequence_primitives),
                    "captured_external_errors": sorted(adapter.capture_external_errors),
                    "external_error_observation": "presence only; source payload elided"
                        if adapter.capture_external_errors else None}


def compile_source(text: str, binary: Path, intermediate: Path,
                   external_heads: tuple[str, ...] = (),
                   *, cons_values: str = "flat", capture_external_errors: tuple[str, ...] = (),
                   sort_values: str | None = None) -> tuple[str, dict]:
    """Lower views, then use the native elaborator to retain source sharing."""
    viewed, inventory = lower(text, external_heads, cons_values=cons_values,
                              capture_external_errors=capture_external_errors, sort_values=sort_values)
    intermediate.parent.mkdir(parents=True, exist_ok=True)
    intermediate.write_text(viewed)
    records = native_plan(binary.resolve(), intermediate.resolve())
    result, edits, diagnostics = translate(viewed, records)
    header = ("; Generated PeTTa-to-Prime pattern/value adapter.\n"
              "; Logical equations remain source equations; sharing is explicit.\n"
              "(scope:profile lexical-fresh per-call reference)\n\n")
    return header + result, {**inventory, "scope_edits": len(edits),
                             "scope_diagnostics": diagnostics}


def compile_library_group(sources: list[Path], binary: Path,
                          work: Path, *, sort_values: str | None = None) -> dict[Path, tuple[str, dict]]:
    """Analyze a closed declaration group once, then retain its module layout.

    Function and raw-formal discovery uses every module in the group. Treating
    each module alone would misclassify cross-module calls as passive data.
    Stateful initializers need an explicit group driver and are not admitted
    by this pure declaration-library entry point.
    """
    parsed = [(source, read_forms(source.read_text())) for source in sources]
    all_forms = [node for _, nodes in parsed for node in nodes]
    if any(isinstance(node, tuple) for node in all_forms):
        raise ValueError("a declaration library cannot contain executable initializers")
    adapter = PatternViews(all_forms, sort_values=sort_values)
    modules = {}
    for index, (source, nodes) in enumerate(parsed):
        viewed = "\n".join(show(adapter.declaration(node)) for node in nodes) + "\n"
        intermediate = work / (str(index) + ".views.metta")
        intermediate.parent.mkdir(parents=True, exist_ok=True)
        intermediate.write_text(viewed)
        result, edits, diagnostics = translate(viewed, native_plan(binary, intermediate))
        modules[source] = (
            "; Generated by tools/petta_prime_guest_v1.py from a declared PeTTa library group.\n"
            "(scope:profile lexical-fresh per-call reference)\n\n" + result,
            {"scope_edits": len(edits), "scope_diagnostics": diagnostics,
             "source_sort_values": adapter.sort_values})
    if adapter.named_state or adapter.pattern_removal:
        raise ValueError("a stateful library requires its declared group driver")
    return modules


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", type=Path, nargs="+")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--view-source", type=Path, required=True,
                        help="retain the intermediate source consumed by native scope analysis")
    parser.add_argument("--external-head", action="append", default=[],
                        help="declare an application head supplied by the target environment")
    parser.add_argument("--cons-values", choices=("flat", "spine-or-flat"), default="flat",
                        help="source list-value representation; native CeTTa grammar trees use spine-or-flat")
    parser.add_argument("--capture-external-error", action="append", default=[],
                        help="deterministic declared operation whose error-presence readout is retained")
    parser.add_argument("--sort-values", choices=("closed-strings",),
                        help="qualify sort-atom to proper lists of closed string values")
    args = parser.parse_args()
    sources = [{"source": str(source),
                "sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
               for source in args.sources]
    text = "\n".join(source.read_text() for source in args.sources)
    result, inventory = compile_source(text, args.binary, args.view_source,
                                       tuple(args.external_head), cons_values=args.cons_values,
                                       capture_external_errors=tuple(args.capture_external_error),
                                       sort_values=args.sort_values)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    args.receipt.write_text(json.dumps({"schema": "petta-prime-guest-v1",
        "sources": sources, **inventory,
        "output_sha256": hashlib.sha256(result.encode()).hexdigest(),
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest()}, indent=2) + "\n")
    print(f"Translated {inventory['forms']} forms, {inventory['sequence_views']} sequence views, "
          f"{inventory['value_quotations']} value quotations, {inventory['scope_edits']} explicit scope edits")


if __name__ == "__main__":
    main()
