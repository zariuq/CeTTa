#!/usr/bin/env python3
"""Execute one origin-bearing module through two retained clients.

The existing external module reader is an executable frontend, not a proven
bytes-to-machine compiler. Both generated clients run in this tree's PeTTa
dialect. The fused artifact uses OrderedMatchMachineLanguage's encoded
Program, Decision, Query and Template constructors.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
import secrets
from pathlib import Path
import subprocess

from gslt2parse_schema_v1 import StringLiteral, Symbol, parse_sexprs, render

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
GSLT = WORKSPACE / "Mettapedia/metta/gslt/lib"
NIK = ROOT.parent / "cetta-prime-nik-20260811"
NIL = Symbol("LNil")


def form(head, *args):
    return (Symbol(head), *args)


def headed(value, head, arity=None):
    return isinstance(value, tuple) and bool(value) and value[0] == Symbol(head) and (
        arity is None or len(value) == arity + 1)


def rows(value):
    out = []
    while headed(value, "LCons", 2):
        out.append(value[1])
        value = value[2]
    if value != NIL:
        raise ValueError("malformed finite list")
    return out


def llist(values):
    out = NIL
    for value in reversed(list(values)):
        out = form("LCons", value, out)
    return out


def text(value):
    return StringLiteral(value)


def patt(head, *args):
    return form("PApp", text(head), llist(args))


def wire_list(nil, cons, values):
    out = patt(nil)
    for value in reversed(list(values)):
        out = patt(cons, value, out)
    return out


def nat(number):
    out = patt("zero")
    for _ in range(number):
        out = patt("succ", out)
    return out


def digest(value):
    return hashlib.sha256(render(value).encode()).hexdigest()


def occurrence_bag(occurrences):
    """Observe full occurrence records with multiplicity, ignoring enumeration."""
    return Counter(render(occurrence) for occurrence in occurrences)


def instantiate(term, bindings):
    if headed(term, "FVar", 1):
        return next((b[2] for b in bindings if b[1] == term[1]), term)
    if headed(term, "PApp", 2):
        return form("PApp", term[1], llist(instantiate(t, bindings) for t in rows(term[2])))
    return term


def merge(left, right):
    out = list(left)
    for binding in right:
        found = next((b for b in out if b[1] == binding[1]), None)
        if found is not None:
            if found[2] != binding[2]:
                return None
        else:
            out.insert(0, binding)
    return out


def structural_bind(schema, value):
    if headed(schema, "FVar", 1):
        return [form("Binding", schema[1], value)]
    if not headed(schema, "PApp", 2) or not headed(value, "PApp", 2) or schema[1] != value[1]:
        return None
    schemas, values = rows(schema[2]), rows(value[2])
    if len(schemas) != len(values):
        return None
    def recurse(index):
        if index == len(schemas):
            return []
        head = structural_bind(schemas[index], values[index])
        tail = recurse(index + 1)
        return None if head is None or tail is None else merge(head, tail)
    return recurse(0)


def query_bind(schemas, values, seed):
    if len(schemas) != len(values):
        return None
    def recurse(index, current):
        if index == len(schemas):
            return []
        schema, value = schemas[index], values[index]
        if headed(schema, "FVar", 1):
            found = next((b for b in current if b[1] == schema[1]), None)
            head = ([] if found[2] == value else None) if found else [form("Binding", schema[1], value)]
        else:
            head = structural_bind(schema, value)
        extended = None if head is None else merge(current, head)
        if extended is None:
            return None
        tail = recurse(index + 1, extended)
        return None if tail is None else merge(head, tail)
    extension = recurse(0, seed)
    return None if extension is None else merge(seed, extension)


def native(binary, source, directory, name, *, cwd=ROOT, profile=False):
    directory.mkdir(parents=True, exist_ok=True)
    file = directory / (name + ".metta")
    file.write_text(source, encoding="utf-8")
    command = [str(binary.resolve()), "--lang", "petta", "--quiet"]
    if profile:
        command += ["--profile", "extended"]
    result = subprocess.run(command + [str(file.resolve())], cwd=cwd,
                            env={**os.environ, "CETTA_PETTA_SEARCH_MACHINE": "1"},
                            capture_output=True, text=True, timeout=180)
    (directory / (name + ".stdout")).write_text(result.stdout)
    (directory / (name + ".stderr")).write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(f"native {name} exited {result.returncode}: {result.stderr[-1500:]}")
    values = []
    for line in result.stdout.splitlines():
        if line.startswith("("):
            values.extend(parse_sexprs(line, source=name))
    return values


def read_module(binary, reader, module, theory, directory):
    # This uses the published module:read-file and module:assemble functions
    # and their origin view.
    base = NIK / "tests/module_values/RewriteBundles.module"
    separate = module.resolve() != base.resolve()
    base_read = (f'($base (module:read-file $reader "rewrite-bundles" {render(text(str(base.resolve())))}))'
                 if separate else '($base $module)')
    catalog = '(LCons (ModuleCatalogEntryV1 "RewriteBundles" $base) LNil)' if separate else 'LNil'
    program = f'''!(import! &self {render(text(str(NIK / "lib/lib_module.metta")))})
!(let* (($reader (langdef:load-parser-artifact-checked {render(text(str(reader.resolve())))}))
         ($module (module:read-file $reader "ordered-module-source" {render(text(str(module.resolve())))}))
         {base_read}
         ($natural (module:assemble $base "Naturals" LNil LNil OriginViewV1 "ordered:naturals"))
         ($package (module:assemble $module {render(text(theory))} (LCons $natural LNil) {catalog} OriginViewV1 "ordered:package"))
         ($rules (module:rewrite-rules $package "OrderedModule")))
   (println! (OrderedModuleExportV1 $rules)))
'''
    values = native(binary, program, directory, "frontend", cwd=NIK, profile=True)
    exported = [value[1] for value in values if headed(value, "OrderedModuleExportV1", 1)]
    if len(exported) != 1 or not headed(exported[0], "ModuleRewriteRulesV1", 2):
        raise ValueError("module reader did not produce an origin-bearing rule view")
    (directory / "module-wire.metta").write_text(render(exported[0]) + "\n")
    return exported[0]


@dataclass(frozen=True)
class Package:
    submitted: tuple
    core: tuple
    occurrences: tuple
    provider: tuple
    identity: str
    snapshot: str

    @classmethod
    def admit(cls, bundle, provider, order=None):
        try:
            return cls._admit(bundle, provider, order)
        except ValueError as error:
            refusal = ValueError(str(error))
            refusal.submitted_data = form("OrderedModuleRefusalV1", text(str(error)), bundle, provider)
            raise refusal from error

    @classmethod
    def _admit(cls, bundle, provider, order=None):
        if not headed(bundle, "ModuleRewriteRulesV1", 2) or not headed(bundle[1], "GSLTLanguageDefWireV1", 5):
            raise ValueError("expected existing ModuleRewriteRulesV1 wire")
        core = bundle[1]
        if not isinstance(core[1], StringLiteral):
            raise ValueError("language name must be a string")
        occurrence_rows = rows(bundle[2])
        if any(not headed(o, "ModuleRuleOccurrenceV1", 2) or not headed(o[1], "SyntaxOriginV1", 2) or
               any(not isinstance(t, StringLiteral) for t in o[1][1:])
               for o in occurrence_rows) or [o[2] for o in occurrence_rows] != rows(core[5]):
            raise ValueError("origin map does not cover the exact submitted rule occurrences")
        if rows(core[4]):
            raise ValueError("equations require another execution profile")
        sorts = {}
        for declaration in rows(core[2]):
            if not headed(declaration, "TypeDecl", 2) or not isinstance(declaration[1], StringLiteral) or declaration[2] != Symbol("CarrierAst"):
                raise ValueError("only declared constructor sorts are admitted")
            if declaration[1] in sorts:
                raise ValueError("duplicate sort declaration")
            sorts[declaration[1]] = True
        constructors = {}
        for declaration in rows(core[3]):
            if not headed(declaration, "GrammarRule", 5) or not isinstance(declaration[1], StringLiteral) or declaration[2] not in sorts:
                raise ValueError("malformed constructor declaration")
            if declaration[5] != Symbol("EvalNone"):
                raise ValueError("constructor evaluation fields require another profile")
            arguments = []
            for parameter in rows(declaration[3]):
                if not headed(parameter, "TermSimple", 2) or not isinstance(parameter[1], StringLiteral) or not headed(parameter[2], "TBase", 1) or parameter[2][1] not in sorts:
                    raise ValueError("binders and collection constructors require another profile")
                arguments.append(parameter[2][1])
            if declaration[1] in constructors:
                raise ValueError("ambiguous constructor")
            constructors[declaration[1]] = declaration[2], arguments
        def check(term, variables):
            if headed(term, "FVar", 1):
                if not isinstance(term[1], StringLiteral) or term[1] not in variables:
                    raise ValueError("unbound rule variable")
                return variables[term[1]]
            if not headed(term, "PApp", 2) or term[1] not in constructors:
                raise ValueError("unsupported or undeclared pattern retained in submitted data")
            result, arguments = constructors[term[1]]
            children = rows(term[2])
            if len(children) != len(arguments) or any(check(t, variables) != s for t, s in zip(children, arguments)):
                raise ValueError("constructor arity or sort disagreement")
            return result
        if not headed(provider, "PremiseProvider", 2):
            raise ValueError("expected existing PremiseProvider wire")
        signatures = {}
        for signature in rows(provider[1]):
            if not headed(signature, "PremiseSignature", 3) or not isinstance(signature[1], StringLiteral) or signature[1] in signatures:
                raise ValueError("malformed provider signature")
            types, modes = rows(signature[2]), rows(signature[3])
            if len(types) != len(modes) or any(not headed(t, "TBase", 1) or t[1] not in sorts for t in types):
                raise ValueError("provider signature leaves admitted sorts")
            if any(m not in (Symbol("RelationInput"), Symbol("RelationOutput")) for m in modes):
                raise ValueError("unsupported provider mode")
            signatures[signature[1]] = [t[1] for t in types], modes
        for row in rows(provider[2]):
            if not headed(row, "PremiseProviderRow", 2) or row[1] not in signatures:
                raise ValueError("row lacks checked provider signature")
            values, expected = rows(row[2]), signatures[row[1]][0]
            if len(values) != len(expected) or any(check(v, {}) != s for v, s in zip(values, expected)):
                raise ValueError("provider row arity or sort disagreement")
        for occurrence in occurrence_rows:
            rule = occurrence[2]
            if not headed(rule, "RewriteRule", 5) or not isinstance(rule[1], StringLiteral):
                raise ValueError("unsupported rule retained as data")
            variables = {}
            for binding in rows(rule[2]):
                if not headed(binding, "TypeBinding", 2) or not isinstance(binding[1], StringLiteral) or not headed(binding[2], "TBase", 1) or binding[2][1] not in sorts or binding[1] in variables:
                    raise ValueError("malformed rule typing")
                variables[binding[1]] = binding[2][1]
            if check(rule[4], variables) != check(rule[5], variables):
                raise ValueError("rewrite changes root sort")
            def variable_names(term):
                if headed(term, "FVar", 1):
                    return {term[1]}
                return set().union(*(variable_names(t) for t in rows(term[2])))
            available = variable_names(rule[4])
            for premise in rows(rule[3]):
                if not headed(premise, "RelationQuery", 2) or premise[1] not in signatures:
                    raise ValueError("unsupported or undeclared positive premise")
                if premise[1] == text("eq"):
                    raise ValueError("builtin eq is outside the checked provider-query profile")
                arguments, expected = rows(premise[2]), signatures[premise[1]][0]
                if len(arguments) != len(expected) or any(check(a, variables) != s for a, s in zip(arguments, expected)):
                    raise ValueError("premise sort disagreement")
                modes = signatures[premise[1]][1]
                for argument, mode in zip(arguments, modes):
                    names = variable_names(argument)
                    if mode == Symbol("RelationInput") and not names <= available:
                        raise ValueError("provider input requires an earlier captured binding")
                # Inputs belong to the environment before this query. An
                # output argument cannot license a later input in the same
                # query merely because of its position in the argument list.
                for argument, mode in zip(arguments, modes):
                    if mode == Symbol("RelationOutput"):
                        available |= variable_names(argument)
            if not variable_names(rule[5]) <= available:
                raise ValueError("target requires a binding absent from its rule/premise path")
        if order is None:
            # An explicit portable declaration policy; never Finset iteration.
            order = sorted(range(len(occurrence_rows)), key=lambda i: render(occurrence_rows[i][1]))
        if sorted(order) != list(range(len(occurrence_rows))):
            raise ValueError("declared order must index every occurrence exactly once")
        ordered = tuple(occurrence_rows[i] for i in order)
        core = (*core[:5], llist(o[2] for o in ordered))
        policy = form("OrderedModuleDeclarationPolicyV1", text("explicit-origin-index-map-v1"),
                      llist(form("OrderedModuleOriginV1", i, o[1]) for i, o in enumerate(ordered)))
        identity = digest(form("OrderedModulePresentationV1", bundle, core, policy))
        return cls(bundle, core, ordered, provider, identity, digest(provider))

    def subject_sort(self, subject):
        declarations = {d[1]: (d[2], rows(d[3])) for d in rows(self.core[3])}
        def check(term):
            if not headed(term, "PApp", 2) or term[1] not in declarations:
                raise ValueError("subject is outside admitted constructor data")
            sort, parameters = declarations[term[1]]
            children = rows(term[2])
            if len(children) != len(parameters) or any(check(c) != p[2][1] for c, p in zip(children, parameters)):
                raise ValueError("subject arity or sort mismatch")
            return sort
        return check(subject)

    def authored_jobs(self, subject, work):
        expected = form("TBase", self.subject_sort(subject))
        declarations = llist(form("GrammarTypingV1", d[1], d[2], d[3]) for d in rows(self.core[3]))
        specs, scoped, jobs = [], [], []
        for i, occurrence in enumerate(self.occurrences):
            rule = occurrence[2]
            spec = form("RuleBindingSpec", llist(form("RuleDependency", b[1], NIL) for b in rows(rule[2])), NIL)
            scoped.append(form("ScopedRewriteRuleV1", *rule[1:], form("RuleBindingSome", spec)))
            specs.append(form("PremiseSpecification", i, spec))
        environment = form("PremiseEnvironment", declarations, llist(scoped), llist(specs),
                           self.provider, form("PremiseReflection", NIL, NIL), NIL)
        permissions = form("OperationalPermissions", Symbol("PermissionYes"), Symbol("PermissionNo"),
                           NIL, Symbol("PermissionNo"), Symbol("PermissionNo"), NIL)
        for i, rule in enumerate(scoped):
            declaration = form("RuleJobDeclaration", i, (*rule[:-1], Symbol("RuleBindingNone")),
                               form("RuleBindingPlan", specs[i][2], Symbol("BindingInferredRootInputs")))
            job = form("RuleJob", i, declaration,
                       form("RuleJobSite", NIL, Symbol("ContextHole"), subject, NIL, expected, NIL))
            request = form("RuleJobRequestProjection", environment, permissions, NIL, expected, subject, job)
            jobs.append(form("ProgramJob", request, work))
        return llist(jobs)

    def rule_sort(self, ordinal):
        """Read the admitted declaration's root sort, including a variable root."""
        rule = self.occurrences[ordinal][2]
        left = rule[4]
        if headed(left, "FVar", 1):
            return next(binding[2][1] for binding in rows(rule[2]) if binding[1] == left[1])
        return next(declaration[2] for declaration in rows(self.core[3]) if declaration[1] == left[1])

    def fused_program(self, root_sort):
        # Untyped matching cannot justify a sorted wildcard capture. The
        # public boundary selects the program for the checked subject sort;
        # original declaration ordinals remain the leaf identities.
        eligible = [i for i in range(len(self.occurrences)) if self.rule_sort(i) == root_sort]
        def decision(term, path):
            if headed(term, "FVar", 1):
                return patt("bd-capture", path, patt(term[1].text))
            children = rows(term[2])
            continuation = patt("bd-succeed")
            for index, child in reversed(list(enumerate(children))):
                continuation = patt("bd-join", decision(child, patt("bd-child", path, nat(index))), continuation)
            return patt("bd-check-constructor", path, patt(term[1].text), nat(len(children)), continuation)
        def template(term):
            if headed(term, "FVar", 1):
                return patt("mm-tvar", patt(term[1].text))
            return patt("mm-tapp", patt(term[1].text), wire_list("mm-tnil", "mm-tcons", map(template, rows(term[2]))))
        def shape(term):
            if headed(term, "FVar", 1):
                return patt("mm-wild")
            children = rows(term[2])
            return patt("mm-node", key(term), wire_list("mm-pnil", "mm-pcons", map(shape, children)))
        def key(term):
            n = len(rows(term[2]))
            return patt("mm-key", patt("mm-head-app", patt(term[1].text), nat(n)), nat(n))
        def leaf(i, rule):
            queries = (patt("mm-query", patt(q[1].text), wire_list("mm-tnil", "mm-tcons",
                        (template(a) for a in rows(q[2])))) for q in rows(rule[3]))
            return patt("mm-plan", nat(i), decision(rule[4], patt("bd-root")),
                        wire_list("mm-qnil", "mm-qcons", queries), template(rule[5]))
        # A real constructor dispatch removes the root column. Residual
        # patterns preserve all deeper structure and ordered overlapping rows.
        keys = []
        for i in eligible:
            occurrence = self.occurrences[i]
            left = occurrence[2][4]
            if headed(left, "PApp", 2) and key(left) not in keys:
                keys.append(key(left))
        def chain(selected, residual):
            selected = list(selected)
            patterns_by_rule = {i: (rows(self.occurrences[i][2][4][2]) if residual else [self.occurrences[i][2][4]])
                                for i in selected}
            dropped = 0
            while selected and all(patterns_by_rule[i] and headed(patterns_by_rule[i][0], "FVar", 1)
                                   for i in selected):
                patterns_by_rule = {i: patterns_by_rule[i][1:] for i in selected}
                dropped += 1
            program = patt("mm-failure")
            for i in reversed(selected):
                rule = self.occurrences[i][2]
                patterns = patterns_by_rule[i]
                program = patt("mm-try", leaf(i, rule), wire_list("mm-pnil", "mm-pcons", map(shape, patterns)), program)
            for _ in range(dropped):
                program = patt("mm-drop", program)
            return program
        if any(headed(self.occurrences[i][2][4], "FVar", 1) for i in eligible):
            return chain(eligible, False)
        branches = patt("mm-bnil")
        for k in reversed(keys):
            selected = [i for i in eligible
                        if headed(self.occurrences[i][2][4], "FVar", 1) or key(self.occurrences[i][2][4]) == k]
            branches = patt("mm-bcons", k, chain(selected, True), branches)
        return patt("mm-switch", branches, patt("mm-failure"))

    def observe_bindings(self, ordinal, bindings):
        declared = [b[1] for b in rows(self.occurrences[ordinal][2][2])]
        entries = rows(bindings)
        if any(not headed(b, "Binding", 2) or b[1] not in declared for b in entries):
            raise ValueError("binding receipt has a name outside its declared domain")
        names = [b[1] for b in entries]
        if len(names) != len(set(names)):
            raise ValueError("binding receipt contains repeated names")
        lookup = {b[1]: b[2] for b in entries}
        return llist(form("ObservedBindingV1", name, form("Some", lookup[name]) if name in lookup else Symbol("None"))
                     for name in declared)

    def compiled_artifact(self):
        programs = llist(form("OrderedModuleSortProgramV1", declaration[1],
                              self.fused_program(declaration[1])) for declaration in rows(self.core[2]))
        return form("OrderedModuleCompiledArtifactV1", 1, text(self.identity), text(self.snapshot),
                    self.core, llist(form("OrderedModuleOriginV1", i, o[1]) for i, o in enumerate(self.occurrences)),
                    form("OrderedModuleSortedProgramsV1", programs))

    def validate_artifact(self, artifact):
        if not headed(artifact, "OrderedModuleCompiledArtifactV1", 6) or artifact != self.compiled_artifact():
            raise ValueError("compiled program does not bind the admitted presentation/order/provider snapshot")
        return artifact[6]

    def revalidate(self):
        submitted = rows(self.submitted[2])
        used, order = set(), []
        for occurrence in self.occurrences:
            index = next((i for i, value in enumerate(submitted) if i not in used and value == occurrence), None)
            if index is None:
                raise ValueError("package occurrence does not originate in submitted module data")
            used.add(index)
            order.append(index)
        checked = Package.admit(self.submitted, self.provider, order)
        if checked != self:
            raise ValueError("package metadata differs from its checked source/order/snapshot")


AUTHORED_FILES = ("gslt_scoped_execution.metta", "gslt_typed_sites.metta", "gslt_execution_driver.metta",
                  "gslt_rule_readout_cache.metta", "gslt_program_driver.metta", "gslt_program_resumption.metta")
AUTHORED_HASHES = dict(zip(AUTHORED_FILES, (
    "fc5299ce7c11b012277e07aff675758bb7fbd9f42ac9c9d487a6f887d074de57",
    "3377d53517fafc55073b26eb5fb4f3c2737efb0ff09f8ab751e7b734086f0f60",
    "02558b35a3acbd2d18ccb45457bfee24d04e6814afb174e5756746f8cb7db74c",
    "aeec6c4a03d4071e1d4253a0e5093133962b44eceec49b53b4b3de0c4d9ac9a8",
    "240bf1fca75125a9c2baa1df7fcbabc4d26f92fd866128b13ff4b9882689edf1",
    "75a25de881ac755715af9600cb56171da430b51452ddeb52c70add2872679c85")))


class Session:
    def __init__(self, package, binary, directory, route, subject, *, artifact=None,
                 enumeration="declaration"):
        if route not in ("authored", "fused"):
            raise ValueError("unknown client")
        if enumeration not in ("declaration", "reverse-complete") or (
                route == "authored" and enumeration != "declaration"):
            raise ValueError("unknown enumeration policy for this client")
        package.revalidate()
        root_sort = package.subject_sort(subject)
        artifact = package.compiled_artifact() if artifact is None else artifact
        sorted_programs = package.validate_artifact(artifact)
        self.program = next(row[2] for row in rows(sorted_programs[1]) if row[1] == root_sort)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / (route + "-compiled-artifact.metta")).write_text(render(artifact) + "\n")
        self.package, self.binary, self.directory, self.route, self.subject = package, binary, directory, route, subject
        self.enumeration = enumeration
        self.state = None
        self.delivered = 0
        self.sequence = 0
        self.binding = (package.identity, package.snapshot, route, digest(subject), enumeration)
        self.lineage = text(secrets.token_hex(16))
        self.cursor = form("OrderedDeliveryCursorV1", self.lineage, 0, NIL)
        self.publication = form("ProgramPublication", NIL, Symbol("ProgramPublicationPending"))
        self.library_hashes = {f: hashlib.sha256((GSLT / f).read_bytes()).hexdigest() for f in AUTHORED_FILES}
        if self.library_hashes != AUTHORED_HASHES:
            raise ValueError("authored source differs from the qualified quotation revision")
        self.generated_library_hashes = {
            name: hashlib.sha256((ROOT / "lib" / name).read_bytes()).hexdigest()
            for name in ("ordered_module_fused_v1.metta", "ordered_module_delivery_v1.metta")
        }
        self.binary_hash = hashlib.sha256(self.binary.read_bytes()).hexdigest()

    def advance(self, work, answer_limit, *, package=None, checkpoint=None, cursor=None):
        if work < 0 or answer_limit < 0:
            raise ValueError("budgets must be nonnegative")
        p = self.package if package is None else package
        p.revalidate()
        if (p.identity, p.snapshot, self.route, digest(self.subject), self.enumeration) != self.binding:
            raise ValueError("stale package/provider continuation refused")
        if checkpoint is not None and checkpoint != self.binding:
            raise ValueError("foreign continuation refused")
        if any(hashlib.sha256((GSLT / f).read_bytes()).hexdigest() != h for f, h in self.library_hashes.items()):
            raise ValueError("authored library revision changed during retained execution")
        if any(hashlib.sha256((ROOT / "lib" / name).read_bytes()).hexdigest() != expected
               for name, expected in self.generated_library_hashes.items()):
            raise ValueError("execution library revision changed during retained execution")
        if hashlib.sha256(self.binary.read_bytes()).hexdigest() != self.binary_hash:
            raise ValueError("runtime revision changed during retained execution")
        incoming = self.cursor if cursor is None else cursor
        source = "\n".join((GSLT / f).read_text() for f in AUTHORED_FILES)
        source += "\n" + (ROOT / "lib/ordered_module_delivery_v1.metta").read_text()
        if self.route == "authored":
            call = form("gslt:program:run", p.authored_jobs(self.subject, work)) if self.state is None else form("gslt:program:resume", self.state, work)
            publication = "(gslt:program:publication $state)"
        else:
            source += "\n" + (ROOT / "lib/ordered_module_fused_v1.metta").read_text()
            if self.state is None:
                start = form("ordered:fused:start", self.program, self.subject)
                call_source = f"(let $start {render(start)} (ordered:fused:run {render(p.provider)} {work} $start))"
            else:
                start = self.state
                call_source = render(form("ordered:fused:run", p.provider, work, start))
            call = form("ordered:fused:run", p.provider, work, start)
            publication = "(ordered:fused:publication $state)"
            if self.enumeration == "reverse-complete":
                publication = (
                    "(let (ProgramPublication $entries $status) " + publication +
                    " (if (== $status ProgramPublicationComplete) "
                    "(ProgramPublication (ordered:fused:reverse $entries LNil) $status) "
                    "(ProgramPublication LNil $status)))")
        if self.route == "authored":
            call_source = render(call)
        source += f'''\n!(if (if (== {render(incoming)} {render(self.cursor)})
            (ordered:delivery:valid {render(self.lineage)} {render(incoming)} {render(self.publication)}) False)
          (let $state {call_source} (let $publication {publication}
            (let $delivery (ordered:delivery:advance {render(self.lineage)} {render(incoming)} {answer_limit} $publication $state)
              (println! (OrderedModuleClientRawV1 $state $publication $delivery)))))
          (println! (OrderedModuleClientRefusedV1 {render(incoming)})))\n'''
        values = native(self.binary, source, self.directory, f"{self.route}-{self.sequence}")
        self.sequence += 1
        if any(headed(v, "OrderedModuleClientRefusedV1", 1) for v in values):
            raise ValueError("native cursor lineage/count/delivered-prefix validation refused")
        records = [v for v in values if headed(v, "OrderedModuleClientRawV1", 3)]
        if len(records) != 1:
            raise ValueError("client did not produce exactly one retained observation")
        state = records[0][1]
        publication = records[0][2]
        delivery = records[0][3]
        if not headed(delivery, "OrderedDeliveryV1", 5):
            raise ValueError("native cursor validation rejected changed publication")
        self.state = state
        self.cursor = delivery[1]
        self.publication = publication
        receipts = rows(publication[1])
        occurrences = [self.readout(r) for r in receipts]
        if self.delivered > len(occurrences):
            raise ValueError("runtime restarted or removed delivered occurrences")
        emitted = [self.readout(r) for r in rows(delivery[2])]
        self.delivered = self.cursor[2]
        pending = publication[2] != Symbol("ProgramPublicationComplete") or self.delivered != len(occurrences)
        primitive_work = (sum(j[2][4] for j in rows(state[1])) if self.route == "authored" else state[3])
        return {"route": self.route, "enumeration": self.enumeration, "binding": self.binding, "emitted": emitted, "available": occurrences,
                "delivered": self.delivered, "pending": pending, "status": publication[2],
                "completion": "pending" if pending else ("complete" if occurrences else "unmatched"),
                "state_bytes": len(render(self.state).encode()), "primitive_work": primitive_work,
                "matched_rows": sum(len(rows(o[5])) for o in occurrences)}

    def readout(self, receipt):
        if self.route == "fused":
            if not headed(receipt, "FusedRuleEmissionV1", 5):
                raise ValueError("malformed fused receipt")
            index, initial, final, choices, target = receipt[1:]
        else:
            if not headed(receipt, "RuleJobReceipt", 7):
                raise ValueError("malformed authored receipt")
            index = receipt[1][1]
            def plain(bindings):
                out = []
                for binding in rows(bindings):
                    if not headed(binding, "Binding", 2) or not headed(binding[2], "ContextualValue", 3) or binding[2][1:3] != (NIL, 0):
                        raise ValueError("receipt leaves first-order root profile")
                    out.append(form("Binding", binding[1], binding[2][3]))
                return llist(out)
            initial, final, target = plain(receipt[3]), plain(receipt[4]), receipt[7]
            choices_list = []
            rule = self.package.occurrences[index][2]
            seed = rows(initial)
            for event in rows(receipt[5]):
                if not headed(event, "PremiseQueryEvent", 4):
                    raise ValueError("unsupported authored premise history")
                premise_index, _, row_index, _ = event[1:]
                query = rows(rule[3])[premise_index]
                row = rows(self.package.provider[2])[row_index]
                # Arguments are instantiated at this decision boundary.
                arguments = llist(instantiate(a, seed) for a in rows(query[2]))
                choices_list.append(form("QueryRowChoiceV1", query[1], arguments, Symbol("ProviderRow"), row_index, row[2]))
                seed = query_bind(rows(query[2]), rows(row[2]), seed)
                if seed is None:
                    raise ValueError("actual query event cannot extend its captured seed")
            choices = llist(choices_list)
        if not isinstance(index, int) or not 0 <= index < len(self.package.occurrences):
            raise ValueError("receipt ordinal absent from compiled origin table")
        normalized_choices = []
        provider_rows = rows(self.package.provider[2])
        for choice in rows(choices):
            if not headed(choice, "QueryRowChoiceV1", 5) or choice[3] != Symbol("ProviderRow") or not isinstance(choice[4], int):
                raise ValueError("malformed provider choice receipt")
            row_index = choice[4]
            if not 0 <= row_index < len(provider_rows) or provider_rows[row_index][1:] != (choice[1], choice[5]):
                raise ValueError("choice does not identify its actual snapshot row")
            relation_index = sum(row[1] == choice[1] for row in provider_rows[:row_index])
            normalized_choices.append((*choice[:4], relation_index, choice[5]))
        choices = llist(normalized_choices)
        initial = self.package.observe_bindings(index, initial)
        final = self.package.observe_bindings(index, final)
        return form("OrderedModuleOccurrenceV1", index, self.package.occurrences[index][1], initial, final, choices, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--reader-cetta", type=Path, required=True)
    parser.add_argument("--reader-artifact", type=Path, required=True)
    parser.add_argument("--module", type=Path, default=NIK / "tests/module_values/RewriteBundles.module")
    parser.add_argument("--theory", default="DuplicatePaths")
    parser.add_argument("--provider", type=Path)
    parser.add_argument("--rule-occurrences", type=Path, help="existing ModuleRuleOccurrenceV1 rows; explicit admitted-rule textual boundary")
    parser.add_argument("--subject", default='(PApp "NatAdd" (LCons (PApp "NatZ" LNil) (LCons (PApp "NatZ" LNil) LNil)))')
    parser.add_argument("--route", choices=("authored", "fused", "both"), default="both")
    parser.add_argument("--fused-enumeration", choices=("declaration", "reverse-complete"),
                        default="declaration", help="fused worker's enumeration policy")
    parser.add_argument("--work", type=int, default=128)
    parser.add_argument("--answers", type=int, default=100)
    parser.add_argument("--schedule", help="retained slices as work:answer-limit,work:answer-limit,...")
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/ordered-module")
    args = parser.parse_args()
    bundle = read_module(args.reader_cetta, args.reader_artifact, args.module, args.theory, args.output)
    if args.rule_occurrences:
        added = parse_sexprs(args.rule_occurrences.read_text(), source="admitted rule occurrences")
        occurrences = rows(bundle[2]) + added
        core = (*bundle[1][:5], llist(o[2] for o in occurrences))
        bundle = form("ModuleRewriteRulesV1", core, llist(occurrences))
    provider = parse_sexprs(args.provider.read_text(), source="provider")[0] if args.provider else form("PremiseProvider", NIL, NIL)
    try:
        package = Package.admit(bundle, provider)
    except ValueError as error:
        (args.output / "refused-data.metta").write_text(render(error.submitted_data) + "\n")
        raise
    routes = ("authored", "fused") if args.route == "both" else (args.route,)
    schedule = ([tuple(map(int, entry.split(":"))) for entry in args.schedule.split(",")]
                if args.schedule else [(args.work, args.answers)])
    if any(len(entry) != 2 for entry in schedule):
        raise ValueError("schedule entries require work:answer-limit")
    artifact = package.compiled_artifact()
    package.validate_artifact(artifact)
    (args.output / "compiled-artifact.metta").write_text(render(artifact) + "\n")
    outputs = []
    for route in routes:
        session = Session(package, args.cetta, args.output, route, parse_sexprs(args.subject, source="subject")[0],
                          enumeration=args.fused_enumeration if route == "fused" else "declaration")
        all_emitted = []
        for work, limit in schedule:
            observed = session.advance(work, limit)
            all_emitted.extend(observed["emitted"])
        outputs.append(observed)
        print(route, "occurrences", len(all_emitted), "pending", observed["pending"])
        for occurrence in all_emitted:
            print(render(occurrence))
    if len(outputs) == 2 and all(output["status"] == Symbol("ProgramPublicationComplete") for output in outputs):
        if occurrence_bag(outputs[0]["available"]) != occurrence_bag(outputs[1]["available"]):
            raise AssertionError("native occurrence multiset comparison failed")
    manifest = {"frontend_evidence": "existing checked reader; native qualification only",
                "cross_machine_observer": "full occurrence multiset; multiplicity, bindings and provenance",
                "comparison_status": ("verified-complete" if len(outputs) == 2 and
                    all(output["status"] == Symbol("ProgramPublicationComplete") for output in outputs)
                    else "not-complete"),
                "enumeration_policies": {output["route"]: output["enumeration"] for output in outputs},
                "presentation_sha256": package.identity, "snapshot_sha256": package.snapshot,
                "reader_binary_sha256": hashlib.sha256(args.reader_cetta.read_bytes()).hexdigest(),
                "reader_artifact_sha256": hashlib.sha256(args.reader_artifact.read_bytes()).hexdigest(),
                "runtime_sha256": hashlib.sha256(args.cetta.read_bytes()).hexdigest(),
                "authored_library_sha256": {f: hashlib.sha256((GSLT / f).read_bytes()).hexdigest() for f in AUTHORED_FILES},
                "fused_artifact_sha256": digest(package.compiled_artifact())}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
