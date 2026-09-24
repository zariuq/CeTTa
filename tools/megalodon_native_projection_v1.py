"""Untrusted specialization of source Megalodon proofs into scoped HOL.

Source admission is handled by megalodon_declaration_import_v1. This module
preserves the selected declaration occurrence and its preceding environment,
materializes explicitly requested closed type instances, and translates the
retained proof tree. The scoped HOL checker and native dependent checker must
both accept the output. Translation itself grants no authority.

Prefix polymorphism is specialized, not identified with dependent universes.
Definitions remain definitions, axioms remain assumptions, and theorem uses
refer to compiled theorems. A materialized declaration records its source
identity, source position and ordered type arguments. Arbitrary first-class
polymorphic proofs are outside this projection.

Source libraries retain the original export and proof trees, the initial
primitive profile, and the requested declaration occurrences/type instances.
Loading always replays ordered source admission. Libraries contain neither
trusted acceptance flags nor executable projection commands.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import megalodon_declaration_import_v1 as source

sx, poly, definitions = source.sx, source.poly, source.evidence


def expr(head: str, *args: sx.SExpr) -> sx.SExpr:
    return (sx.Symbol(head), *args)


def hosted_article_address(article: sx.SExpr, bodies: dict[str, str]) -> sx.SExpr:
    """Address a proof article without treating the address as the article.

    Small articles are the rendered text. A large article is stored once in
    `bodies` and the record keeps only its sha256 address. Matching must open
    that stored text. Hashing a fresh render is not opening it. Inlining every
    large article into each proof record duplicates the source environment.
    """
    rendered = sx.render(article)
    if len(rendered) <= 65536:
        return sx.StringLiteral(rendered)
    digest = hashlib.sha256(rendered.encode()).hexdigest()
    previous = bodies.get(digest)
    if previous is None:
        bodies[digest] = rendered
    elif previous != rendered:
        raise ValueError("hosted article address collides with a different article")
    return expr("sha256", sx.StringLiteral(digest))


def closed_type(value: poly.Tp) -> None:
    poly.plain_type_proof(0, value)


def specialize_type(value: poly.Tp, arguments: tuple[poly.Tp, ...]) -> tuple[poly.Tp, tuple[sx.SExpr, ...]]:
    """Substitute a closed prefix, keeping every substitution article.

    An empty argument list changes nothing and produces no article.
    """
    articles = []
    for argument in reversed(arguments):
        value, article = poly.substitute_type(0, argument, value)
        articles.append(article)
    return value, tuple(articles)


def specialize_term(value: poly.Tm, arguments: tuple[poly.Tp, ...]) -> tuple[poly.Tm, tuple[sx.SExpr, ...]]:
    articles = []
    for argument in reversed(arguments):
        value, article = poly.type_substitute_term(0, argument, value)
        articles.append(article)
    return value, tuple(articles)


def open_prefix(value, tag: str, arguments: tuple[poly.Tp, ...], substitute):
    """Specialize a polymorphic prefix, keeping each substitution article.

    A closed instance is not the unspecialized declaration. The articles are
    the evidence of that representation change. An empty argument list is not
    a specialization.
    """
    articles = []
    for argument in arguments:
        closed_type(argument)
        if value[0] != tag:
            raise ValueError("too many source type arguments")
        value, article = substitute(0, argument, value[1])
        articles.append(article)
    if value[0] == tag:
        raise ValueError("source type prefix needs an explicit closed instance")
    return value, tuple(articles)


@dataclass(frozen=True)
class Instance:
    position: int
    declaration: source.Declaration
    arguments: tuple[poly.Tp, ...]
    name: str
    proposition: sx.SExpr | None


@dataclass(eq=False)
class Projection:
    document: list[source.Declaration]
    initial_primitives: list[poly.Tp] = field(default_factory=list)
    commands: list[sx.SExpr] = field(default_factory=list, init=False)
    instances: dict[tuple[int, tuple[poly.Tp, ...]], Instance] = field(
        default_factory=dict, init=False)
    bases: set[int] = field(default_factory=set, init=False)
    proposition_declared: bool = field(default=False, init=False)
    declaration_cache: dict = field(default_factory=dict, init=False)
    head_cache: dict = field(default_factory=dict, init=False)
    # Innermost list receives representation articles for the active
    # declaration or proof. Nested materialization gets its own frame.
    frames: list = field(default_factory=list, init=False)
    primitive_instances: dict = field(default_factory=dict, init=False)
    prefix_digests: list[bytes] = field(default_factory=list, init=False)
    # sha256 address -> rendered article text. The address is not the article.
    hosted_articles: dict[str, str] = field(default_factory=dict, init=False)

    def prefix_digest(self, position: int) -> str:
        """Content-address the actual ordered prefix, including proof provenance.

        This is a namespace key, not evidence of admission. Later declarations
        do not rename earlier instances; changing an earlier declaration does.
        """
        if not self.prefix_digests:
            initial = sx.render(tuple(poly.encode_tp(t) for t in self.initial_primitives))
            self.prefix_digests.append(hashlib.sha256(initial.encode()).digest())
        while len(self.prefix_digests) <= position + 1:
            item = self.document[len(self.prefix_digests) - 1]
            content = sx.render((sx.Symbol(item.kind), sx.StringLiteral(item.label),
                sx.StringLiteral(item.identifier), item.line,
                () if item.type is None else poly.encode_tp(item.type),
                () if item.body is None else poly.encode_tm(item.body),
                () if item.index is None else item.index,
                () if item.proof is None else item.proof,
                sx.StringLiteral(item.proof_identifier or ""),
                tuple(sx.StringLiteral(s) for s in item.reported_known),
                tuple(sx.StringLiteral(s) for s in item.reported_delta)))
            self.prefix_digests.append(hashlib.sha256(
                self.prefix_digests[-1] + content.encode()).digest())
        return self.prefix_digests[position + 1].hex()

    def lookup(self, identifier: str, before: int, kinds: set[str]) -> int:
        for position in range(before - 1, -1, -1):
            item = self.document[position]
            if item.identifier == identifier and item.kind in kinds:
                return position
        raise ValueError(f"source identity has no preceding declaration: {identifier}")

    def declarations(self, before: int):
        if before in self.declaration_cache:
            return self.declaration_cache[before]
        result = []
        for item in self.document[:before]:
            if item.kind in {"PARAM", "DEF", "PRIM"}:
                body = ("prim", item.index) if item.kind == "PRIM" else item.body
                result.insert(0, (item.identifier, item.type, body))
        self.declaration_cache[before] = result
        return result

    def note(self, article: sx.SExpr) -> None:
        if not self.frames:
            raise RuntimeError("representation article has no active frame")
        self.frames[-1].append(article)

    def head(self, before: int, value: poly.Tm) -> poly.Tm:
        key = (before, value)
        if key not in self.head_cache:
            self.head_cache[key] = definitions.weak_head_with_article(
                self.declarations(before), value)
        reduced, article = self.head_cache[key]
        # An unchanged head is not a representation change. A real reduction is.
        if reduced != value:
            self.note(article)
        return reduced

    def type(self, value: poly.Tp) -> sx.SExpr:
        match value:
            case ("prop",):
                if not self.proposition_declared:
                    self.commands.append(expr("add-atom", sx.Symbol("&self"),
                        expr(":", sx.Symbol("prop"), expr("u", 0))))
                    self.proposition_declared = True
                return sx.Symbol("prop")
            case ("base", index):
                name = sx.Symbol("set" if index == 0 else f"mg-base-{index}")
                if index not in self.bases:
                    # Publish ground carriers for ordinary dependent programs,
                    # not only for the scoped HOL service's internal signature.
                    self.commands.append(expr("add-atom", sx.Symbol("&self"),
                                              expr(":", name, expr("u", 0))))
                    self.bases.add(index)
                return name
            case ("arr", domain, codomain):
                return expr("->", self.type(domain), self.type(codomain))
            case _:
                raise ValueError("native projection requires a closed simple type")

    def term(self, value: poly.Tm, before: int,
             variables: tuple[sx.SExpr, ...] = ()) -> sx.SExpr:
        # Preserve object-level applications and binders. Conversion belongs
        # to the receiving checker; rendering must not choose a beta/eta
        # representative of an admitted source definition.
        # Type applications select a declaration instance, rather than changing
        # the meaning of the unspecialized declaration or unfolding its body.
        arguments = []
        while value[0] == "typeApp":
            arguments.append(value[2])
            value = value[1]
        arguments.reverse()
        if value[0] == "named":
            position = self.lookup(value[1], before, {"PARAM", "DEF", "PRIM"})
            return sx.Symbol(self.materialize(position, tuple(arguments)).name)
        if value[0] == "prim":
            for position in range(before - 1, -1, -1):
                item = self.document[position]
                if item.kind == "PRIM" and item.index == value[1]:
                    return sx.Symbol(self.materialize(position, tuple(arguments)).name)
            return self.initial_primitive(value[1], tuple(arguments))
        if arguments:
            # A local type beta-redex is computed by the same source operation.
            for argument in arguments:
                if value[0] != "typeLam":
                    raise ValueError("non-declaration polymorphic term")
                value, article = poly.type_substitute_term(0, argument, value[1])
                self.note(article)
            return self.term(value, before, variables)
        match value:
            case ("var", index) if 0 <= index < len(variables):
                return variables[index]
            case ("app", function, argument):
                return (self.term(function, before, variables),
                        self.term(argument, before, variables))
            case ("imp", left, right):
                return expr("imp", self.term(left, before, variables),
                            self.term(right, before, variables))
            case ("lam", domain, body) | ("all", domain, body):
                name = sx.Symbol(f"mg-bound-{len(variables)}")
                translated = expr("lam", name,
                    self.term(body, before, (name, *variables)))
                return expr("all", self.type(domain), translated) if value[0] == "all" else translated
            case _:
                raise ValueError(f"unscoped or nonmonomorphic source term: {value!r}")

    def initial_primitive(self, index: int, arguments: tuple[poly.Tp, ...]) -> sx.SExpr:
        key = (index, arguments)
        if key in self.primitive_instances:
            return self.primitive_instances[key]
        if not 0 <= index < len(self.initial_primitives):
            raise ValueError("primitive is absent from both source and initial signature")
        value_type, articles = open_prefix(self.initial_primitives[index], "all", arguments,
                                           poly.substitute_type)
        identity = sx.render((index, poly.encode_tp(self.initial_primitives[index]),
                              *(poly.encode_tp(t) for t in arguments)))
        name = sx.Symbol("mg-primitive-" + hashlib.sha256(identity.encode()).hexdigest())
        self.commands.append(expr("add-atom", sx.Symbol("&self"),
                                  expr(":", name, self.type(value_type))))
        self.commands.append(expr("add-atom", sx.Symbol("&self"), expr(
            "MegalodonPrimitiveInstanceV1", name, index,
            poly.encode_tp(self.initial_primitives[index]),
            tuple(poly.encode_tp(t) for t in arguments))))
        self.retain_specialization(name, "PRIM", arguments, articles)
        self.primitive_instances[key] = name
        return name

    def proof(self, value: sx.SExpr, before: int,
              arguments: tuple[poly.Tp, ...] = (),
              context: tuple[poly.Tp, ...] = (),
              hypotheses: tuple[poly.Tm, ...] = ()) -> tuple[poly.Tm, sx.SExpr, tuple[sx.SExpr, ...]]:
        """Translate one proof, retaining every representation article it checks.

        The returned articles are this proof's own checks. A nested declaration
        keeps its articles on that declaration.
        """
        bucket: list[sx.SExpr] = []
        self.frames.append(bucket)
        try:
            proposition, translated = self._proof(value, before, arguments, context, hypotheses)
        finally:
            self.frames.pop()
        return proposition, translated, tuple(bucket)

    def _proof(self, value: sx.SExpr, before: int,
               arguments: tuple[poly.Tp, ...] = (),
               context: tuple[poly.Tp, ...] = (),
               hypotheses: tuple[poly.Tm, ...] = ()) -> tuple[poly.Tm, sx.SExpr]:
        tag, payload = poly.tag(value, "source proof projection")
        variables = tuple(expr("pf:var", i) for i in range(len(context)))
        if tag == "HYP" and len(payload) == 1:
            index = source.natural(payload[0], "hypothesis index")
            if index >= len(hypotheses):
                raise ValueError("source hypothesis index is outside its context")
            return hypotheses[index], expr("pf:hyp", index)
        if tag in {"KNOWN", "PTPAP"}:
            instantiations = []
            while tag == "PTPAP" and len(payload) == 2:
                instantiated, articles = specialize_type(poly.parse_tp(payload[1]), arguments)
                for article in articles:
                    self.note(article)
                instantiations.append(instantiated)
                tag, payload = poly.tag(payload[0], "polymorphic proof head")
            if tag != "KNOWN" or len(payload) != 1:
                raise ValueError("polymorphic proof is not a declared theorem instance")
            identity = source.string(payload[0], "known identity")
            position = self.lookup(identity, before, {"AXIOM", "THM"})
            instantiations.reverse()
            instance = self.materialize(position, tuple(instantiations))
            proposition, _articles = open_prefix(self.document[position].body, "typeAll",
                tuple(instantiations), poly.type_substitute_term)
            return self.head(before, proposition), expr("pf:known", sx.Symbol(instance.name))
        if tag == "PLAM" and len(payload) == 2:
            specialized, articles = specialize_term(poly.parse_tm(payload[0]), arguments)
            for article in articles:
                self.note(article)
            domain = self.head(before, specialized)
            body, translated = self._proof(payload[1], before, arguments, context,
                                            (domain, *hypotheses))
            proposition = ("imp", domain, body)
            return proposition, expr("pf:typed", self.term(proposition, before, variables),
                                     expr("pf:imp-intro", translated))
        if tag == "TLAM" and len(payload) == 2:
            domain, articles = specialize_type(poly.parse_tp(payload[0]), arguments)
            for article in articles:
                self.note(article)
            closed_type(domain)
            shifted, shift_article = poly.shift_proof_context(1, 0, list(hypotheses))
            self.note(shift_article)
            body, translated = self._proof(payload[1], before, arguments,
                                            (domain, *context), tuple(shifted))
            proposition = ("all", domain, body)
            return proposition, expr("pf:typed", self.term(proposition, before, variables),
                                     expr("pf:all-intro", translated))
        if tag == "PPFAP" and len(payload) == 2:
            function, major = self._proof(payload[0], before, arguments, context, hypotheses)
            argument, minor = self._proof(payload[1], before, arguments, context, hypotheses)
            function = self.head(before, function)
            if function[0] != "imp":
                raise ValueError("source proof application has a mismatched premise")
            _common, article = definitions.demand_conversion_article(
                self.declarations(before), function[1], argument)
            self.note(article)
            return function[2], expr("pf:imp-elim", major, minor)
        if tag == "PTMAP" and len(payload) == 2:
            function, major = self._proof(payload[0], before, arguments, context, hypotheses)
            function = self.head(before, function)
            if function[0] != "all":
                raise ValueError("source term application requires a universal proof")
            argument, articles = specialize_term(poly.parse_tm(payload[1]), arguments)
            for article in articles:
                self.note(article)
            result, subst_article = poly.substitute(0, argument, function[2])
            self.note(subst_article)
            return self.head(before, result), expr("pf:all-elim", major,
                self.term(argument, before, variables))
        raise ValueError(f"unsupported source proof constructor: {tag}")

    def materialize(self, position: int,
                    arguments: tuple[poly.Tp, ...] = ()) -> Instance:
        if not 0 <= position < len(self.document):
            raise ValueError("source position is outside the document")
        key = (position, arguments)
        if key in self.instances:
            return self.instances[key]
        self.frames.append([])
        try:
            return self._materialize(position, arguments, key)
        finally:
            self.frames.pop()

    def _materialize(self, position: int, arguments: tuple[poly.Tp, ...],
                     key: tuple) -> Instance:
        item = self.document[position]
        prefix = self.prefix_digest(position)
        identity = sx.render((sx.StringLiteral(prefix), position,
                              *(poly.encode_tp(t) for t in arguments)))
        name = "mg-" + hashlib.sha256(identity.encode()).hexdigest()
        symbol = sx.Symbol(name)
        proposition = None
        hosted = None
        proof_articles = None
        if item.kind in {"PARAM", "PRIM", "DEF"}:
            value_type, type_articles = open_prefix(
                item.type, "all", arguments, poly.substitute_type)
            native_type = self.type(value_type)
            if item.kind == "DEF":
                body, body_articles = open_prefix(
                    item.body, "typeLam", arguments, poly.type_substitute_term)
                # A source definition names its entire value, including a
                # function value. The native declaration checker supplies its
                # expected type; no saturated equation or eta wrapper is needed.
                command = expr("set:define", symbol, native_type,
                               expr("=", symbol, self.term(body, position)))
                self.retain_specialization(
                    symbol, item.kind, arguments, type_articles + body_articles)
            else:
                command = expr("add-atom", sx.Symbol("&self"), expr(":", symbol, native_type))
                self.retain_specialization(symbol, item.kind, arguments, type_articles)
        elif item.kind in {"AXIOM", "THM"}:
            body, body_articles = open_prefix(
                item.body, "typeAll", arguments, poly.type_substitute_term)
            proposition = self.term(body, position)
            if item.kind == "AXIOM":
                # An imported axiom stays an assumption of a hosted document.
                # It is not a theorem and it does not select a foundation.
                command = expr("set:axiom", symbol, proposition)
                hosted = expr(
                    "MegalodonHostedAssumptionV1", symbol, sx.Symbol("AXIOM"),
                    sx.StringLiteral(item.label), proposition)
                self.retain_specialization(symbol, item.kind, arguments, body_articles)
            else:
                inferred, proof, proof_articles = self.proof(item.proof, position, arguments)
                try:
                    _common, article = definitions.demand_conversion_article(
                        self.declarations(position), inferred, body)
                except definitions.ConversionMismatch as error:
                    raise ValueError("projected proof does not establish the declared source formula") from error
                command = expr("set:theorem", symbol, proposition, proof)
                # The proof's synthesized formula and the declared source formula
                # are identified by this retained conversion article. Dropping it
                # would leave the projection's representation change unchecked.
                hosted = expr(
                    "MegalodonHostedConversionV1", symbol, sx.Symbol("THM"),
                    sx.StringLiteral(item.label),
                    self.term(inferred, position), proposition,
                    sx.StringLiteral(sx.render(article)))
                self.retain_specialization(symbol, item.kind, arguments, body_articles)
        else:
            raise ValueError("unsupported source declaration")
        self.commands.append(command)
        if hosted is not None:
            self.commands.append(expr("add-atom", sx.Symbol("&self"), hosted))
        if proof_articles is not None:
            self.retain_proof_steps(symbol, "THM", item.label, proof_articles)
        term_articles = tuple(self.frames[-1])
        if term_articles:
            self.retain_term_steps(symbol, item.kind, item.label, term_articles)
        # This is inspectable provenance, not an admission rule or proof.
        self.commands.append(expr("add-atom", sx.Symbol("&self"), expr(
            "MegalodonSourceInstanceV1", symbol, position, sx.Symbol(item.kind),
            sx.StringLiteral(item.label), sx.StringLiteral(item.identifier),
            sx.StringLiteral(item.proof_identifier or ""), item.line,
            tuple(poly.encode_tp(t) for t in arguments), sx.StringLiteral(prefix))))
        instance = Instance(position, item, arguments, name, proposition)
        self.instances[key] = instance
        return instance

    def retain_specialization(self, symbol: sx.Symbol, kind: str,
                              arguments: tuple[poly.Tp, ...],
                              articles: tuple[sx.SExpr, ...]) -> None:
        if not arguments:
            return
        if not articles or len(articles) % len(arguments) != 0:
            raise ValueError("specialization evidence does not match its type arguments")
        self.commands.append(expr("add-atom", sx.Symbol("&self"), expr(
            "MegalodonHostedSpecializationV1", symbol, sx.Symbol(kind),
            tuple(poly.encode_tp(argument) for argument in arguments),
            tuple(sx.StringLiteral(sx.render(article)) for article in articles))))

    def host_article(self, article: sx.SExpr) -> sx.SExpr:
        return hosted_article_address(article, self.hosted_articles)

    def open_hosted_article(self, node: sx.SExpr) -> sx.SExpr | None:
        """Read a hosted article. A sha256 is only the address of stored text."""
        if isinstance(node, sx.StringLiteral):
            text = node.text
        elif (isinstance(node, tuple) and len(node) == 2
              and node[0] == sx.Symbol("sha256")
              and isinstance(node[1], sx.StringLiteral)):
            text = self.hosted_articles.get(node[1].text)
            if text is None:
                return None
            if hashlib.sha256(text.encode()).hexdigest() != node[1].text:
                return None
        else:
            return None
        try:
            forms = sx.parse_sexprs(text)
        except sx.SchemaError:
            return None
        if len(forms) != 1:
            return None
        return forms[0]

    def proof_steps_match(self, name: str, articles: tuple[sx.SExpr, ...]) -> bool:
        symbol = sx.Symbol(name)
        matches = []
        for command in self.commands:
            if not (isinstance(command, tuple) and len(command) > 2
                    and isinstance(command[2], tuple) and command[2]
                    and command[2][0] == sx.Symbol("MegalodonHostedProofStepsV1")
                    and command[2][1] == symbol):
                continue
            matches.append(command[2][4])
        if len(matches) != 1 or len(matches[0]) != len(articles):
            return False
        for node, article in zip(matches[0], articles):
            opened = self.open_hosted_article(node)
            if opened != article:
                return False
        return True

    def retain_proof_steps(self, symbol: sx.Symbol, kind: str, label: str,
                           articles: tuple[sx.SExpr, ...]) -> None:
        """Keep the articles a translated proof checked and previously dropped.

        An axiom is not given this record. An empty tuple means the proof was
        translated and none of its steps changed representation.
        """
        self.commands.append(expr("add-atom", sx.Symbol("&self"), expr(
            "MegalodonHostedProofStepsV1", symbol, sx.Symbol(kind),
            sx.StringLiteral(label),
            tuple(self.host_article(article) for article in articles))))

    def retain_term_steps(self, symbol: sx.Symbol, kind: str, label: str,
                          articles: tuple[sx.SExpr, ...]) -> None:
        self.commands.append(expr("add-atom", sx.Symbol("&self"), expr(
            "MegalodonHostedTermStepsV1", symbol, sx.Symbol(kind),
            sx.StringLiteral(label),
            tuple(self.host_article(article) for article in articles))))

    def render(self) -> str:
        return "\n".join("!" + sx.render(command) for command in self.commands) + "\n"


def dependent_use(instance: Instance) -> str:
    """A checked consumer whose result type mentions the exact retained proof.

    Inspect the returned package structurally. Evaluating it again as a let
    source can execute the code stored in its proof-term field.
    """
    return f'''!(let $package (set:native-proof {instance.name})
 (unify $package
      (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
  (let (SetNativeUseV1 $checked $consumer $result $result-type)
       (set:native-use $package (Lam $type (Refl (idx 0))))
   (unify $result-type (Id $carrier $left $right)
    (ImportedNativeUse {instance.position} (== $left $term) (== $right $term)
      (size-atom $assumptions))
    (ImportedNativeUseMalformedType {instance.position})))
  (ImportedNativeUseMalformedPackage {instance.position})))\n'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--megalodon", type=Path)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--include", type=Path, action="append", default=[],
                        help="source signature to import and replay in order; repeat for several")
    parser.add_argument("--load-library", type=Path,
                        help="recheck and load a saved source/proof library without Megalodon")
    parser.add_argument("--save-library", type=Path,
                        help="save the source export and requested instances; never overwrite a file")
    parser.add_argument("--consumer", type=Path,
                        help="subsequent MeTTa program to run after the checked library declarations")
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--profile", choices=["empty", "egal"])
    parser.add_argument("--theorem", action="append", default=[],
                        help="source theorem label; repeat to select several (default: all theorems)")
    parser.add_argument("--axiom", action="append", default=[],
                        help="explicitly select a source axiom as an assumption, not a proved theorem")
    parser.add_argument("--type", action="append", default=[],
                        help="closed source type argument, e.g. '(SET)' or '(AR (SET) (SET))'")
    parser.add_argument("--run", action="store_true",
                        help="run the projected document and its dependent consumers")
    args = parser.parse_args()
    if args.load_library:
        if (args.source or args.megalodon or args.include or args.profile
                or args.theorem or args.axiom or args.type):
            parser.error("a saved library supplies its own source, profile and instances")
        library = SourceLibrary.loads(args.load_library.read_text(encoding="utf-8"))
    else:
        if args.source is None or args.megalodon is None:
            parser.error("provide --source and --megalodon, or --load-library")
        exported = source.export_document(args.megalodon, args.source, args.include)
        library = SourceLibrary.select(exported, args.profile or "empty",
                                       args.theorem, args.axiom, args.type)
    items, requests = library.resolve()
    projection, instances, prefix_length = library.replay(args.cetta)
    selected_theorems = {items[i].label for i, _ in requests if items[i].kind == "THM"}
    selected_axioms = {items[i].label for i, _ in requests if items[i].kind == "AXIOM"}
    if args.save_library:
        with args.save_library.open("x", encoding="utf-8") as output:
            output.write(library.dumps())
    program = projection.render() + "".join(dependent_use(i) for i in instances)
    if args.consumer:
        program += "\n" + args.consumer.read_text(encoding="utf-8")
    print(f"Source declarations={len(items)} admitted_prefix={prefix_length} "
          f"selected_theorems={len(selected_theorems)} selected_axioms={len(selected_axioms)}",
          file=sys.stderr)
    if not args.run:
        print(program, end="")
        return 0
    with tempfile.TemporaryDirectory(prefix="megalodon-native-") as directory:
        path = Path(directory) / "checked-source.metta"
        path.write_text(program, encoding="utf-8")
        result = subprocess.run([str(args.cetta.resolve()), "--lang", "prime", str(path)],
                                capture_output=True, text=True, check=True)
    print(result.stdout, end="")
    for instance in instances:
        if f"[(ImportedNativeUse {instance.position} True True " not in result.stdout:
            raise SystemExit(f"dependent use failed for source {instance.declaration.kind} "
                             f"{instance.declaration.label}")
    return 0


@dataclass(frozen=True)
class SourceLibrary:
    """Portable untrusted source, retained proofs, and explicit closed instances.

    This is a replayable library, not a trusted cache or serialized runtime
    environment. Loading regenerates admission evidence and asks the existing
    NIK authority to check every transition before emitting native declarations.
    The file stores no executable MeTTa commands and no acceptance bit.
    """

    source_export: str
    profile: str
    requests: tuple[tuple[int, str, tuple[str, ...]], ...]

    @classmethod
    def select(cls, exported, profile, theorems, axioms, types):
        items = source.document(exported)
        positions = select_positions(items, theorems, axioms)
        return cls(exported, profile,
                   tuple((i, items[i].kind, tuple(types)) for i in positions))

    def dumps(self) -> str:
        self.resolve()
        return json.dumps({"format": "MegalodonSourceLibraryV1",
                           "source_export": self.source_export,
                           "profile": self.profile,
                           "instances": [{"position": i, "kind": kind,
                                          "type_arguments": list(types)}
                                         for i, kind, types in self.requests]},
                          ensure_ascii=False, indent=2) + "\n"

    @classmethod
    def loads(cls, text: str):
        def unique_fields(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError(f"duplicate library field: {key}")
                result[key] = value
            return result
        data = json.loads(text, object_pairs_hook=unique_fields)
        if (not isinstance(data, dict)
                or set(data) != {"format", "source_export", "profile", "instances"}
                or data["format"] != "MegalodonSourceLibraryV1"
                or not isinstance(data["source_export"], str)
                or not isinstance(data["instances"], list)):
            raise ValueError("malformed Megalodon source library")
        requests = []
        for instance in data["instances"]:
            if (not isinstance(instance, dict)
                    or set(instance) != {"position", "kind", "type_arguments"}
                    or type(instance["position"]) is not int
                    or not isinstance(instance["kind"], str)
                    or not isinstance(instance["type_arguments"], list)
                    or not all(isinstance(t, str) for t in instance["type_arguments"])):
                raise ValueError("malformed library instance")
            requests.append((instance["position"], instance["kind"],
                             tuple(instance["type_arguments"])))
        library = cls(data["source_export"], data["profile"], tuple(requests))
        library.resolve()
        return library

    def resolve(self):
        if self.profile not in ("empty", "egal"):
            raise ValueError("library must declare its initial primitive profile")
        items = source.document(self.source_export)
        requests = []
        for position, kind, arguments in self.requests:
            if (type(position) is not int or not 0 <= position < len(items)
                    or kind not in ("THM", "AXIOM") or items[position].kind != kind):
                raise ValueError("library instance has an absent declaration or the wrong kind")
            types = []
            for text in arguments:
                parsed = sx.parse_sexprs(text, source="library type argument")
                if len(parsed) != 1:
                    raise ValueError("a library type argument must contain one type")
                typ = poly.parse_tp(parsed[0])
                closed_type(typ)
                types.append(typ)
            requests.append((position, tuple(types)))
        if not requests:
            raise ValueError("a library must request at least one theorem or explicit assumption")
        return items, requests

    def replay(self, cetta: Path, *, admission_article: sx.SExpr | None = None):
        """Replay evidence against this source's claim before projecting it.

        An optional retained article saves evidence generation, not checking.
        Its embedded goal, identifiers and previous acceptance are not trusted.
        The requested goal is reconstructed from this library's ordered source.
        """
        items, requests = self.resolve()
        initial = source.egal_initial_primitives() if self.profile == "egal" else []
        prefix_length = max(i for i, _ in requests) + 1
        # Check the connected sequence, not an unordered bag of individually
        # valid declarations. Definitions and theorem premises see their prefix.
        prefix = items[:prefix_length]
        goal = source.document_claim(source.State(primitives=list(initial)), prefix)
        if admission_article is None:
            _, proof = source.compile_document(source.State(primitives=list(initial)), prefix)
            admission_article = source.dag.compile_shared_article(goal, proof).article
        poly.require_public_result(poly.run_cetta(cetta, goal, admission_article), accepted=True)
        projection = Projection(items, initial)
        instances = [projection.materialize(i, types) for i, types in requests]
        return projection, instances, prefix_length


def select_positions(items, theorems, axioms):
    requested = bool(theorems or axioms)
    selected = [(i, item) for i, item in enumerate(items)
                if (item.kind == "THM" and (not requested or item.label in theorems))
                or (item.kind == "AXIOM" and item.label in axioms)]
    selected_theorems = {item.label for _, item in selected if item.kind == "THM"}
    selected_axioms = {item.label for _, item in selected if item.kind == "AXIOM"}
    if (not selected or set(theorems) - selected_theorems or set(axioms) - selected_axioms):
        raise ValueError("the selection contains an absent declaration or a declaration of the wrong kind")
    return [i for i, _ in selected]


if __name__ == "__main__":
    raise SystemExit(main())
