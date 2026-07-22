"""Executable reference semantics for Prime's bare-``$`` tournament.

This model is intentionally smaller than the evaluator.  It isolates the
question under review: lexical identity, matching, discarded bindings, and a
possible root-binder-sensitive elaboration.  Values include grounded
references so the tournament cannot confuse matchability with nameability.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import TypeAlias


@dataclass(frozen=True)
class Symbol:
    name: str


@dataclass(frozen=True)
class Reference:
    name: str


@dataclass(frozen=True)
class PairValue:
    left: "Value"
    right: "Value"


@dataclass(frozen=True)
class FreeVariable:
    identity: int


@dataclass(frozen=True)
class Quoted:
    surface: "Surface"


@dataclass(frozen=True)
class NoMatch:
    pass


Value: TypeAlias = Symbol | Reference | PairValue | FreeVariable | Quoted | NoMatch


@dataclass(frozen=True)
class Dollar:
    pass


@dataclass(frozen=True)
class Named:
    name: str


@dataclass(frozen=True)
class StructuralNamed:
    key: tuple[str, ...]


@dataclass(frozen=True)
class Constant:
    value: Value


@dataclass(frozen=True)
class PairSurface:
    left: "Surface"
    right: "Surface"


@dataclass(frozen=True)
class LetSurface:
    pattern: "Surface"
    source: "Surface"
    body: "Surface"


@dataclass(frozen=True)
class QuoteSurface:
    body: "Surface"


Surface: TypeAlias = (
    Dollar | Named | StructuralNamed | Constant | PairSurface | LetSurface | QuoteSurface
)


@dataclass(frozen=True)
class CoreVariable:
    identity: int
    display_name: str | None


@dataclass(frozen=True)
class CoreConstant:
    value: Value


@dataclass(frozen=True)
class CorePair:
    left: "Core"
    right: "Core"


@dataclass(frozen=True)
class CoreLet:
    pattern: "Core"
    source: "Core"
    body: "Core"


@dataclass(frozen=True)
class CoreQuote:
    surface: Surface


Core: TypeAlias = CoreVariable | CoreConstant | CorePair | CoreLet | CoreQuote


class Policy(Enum):
    LITERAL = "literal"
    FRESH = "fresh"
    SHARED = "shared"
    ROOT_BINDER = "root-binder"


class Elaborator:
    def __init__(self, policy: Policy):
        self.policy = policy
        self.next_identity = 1
        self.named: dict[tuple[str, object], int] = {}
        self.shared_dollar: int | None = None

    def fresh(self) -> int:
        identity = self.next_identity
        self.next_identity += 1
        return identity

    def named_identity(self, kind: str, name: object) -> int:
        key = (kind, name)
        if key not in self.named:
            self.named[key] = self.fresh()
        return self.named[key]

    def dollar(self, anonymous_scope: tuple[int, ...]) -> Core:
        if self.policy is Policy.LITERAL:
            return CoreConstant(Symbol("$"))
        if self.policy is Policy.SHARED:
            if self.shared_dollar is None:
                self.shared_dollar = self.fresh()
            return CoreVariable(self.shared_dollar, None)
        if self.policy is Policy.ROOT_BINDER and anonymous_scope:
            return CoreVariable(anonymous_scope[-1], None)
        return CoreVariable(self.fresh(), None)

    def pattern(
        self,
        surface: Surface,
        anonymous_scope: tuple[int, ...],
    ) -> Core:
        if isinstance(surface, Dollar):
            # A nested dollar in a structured pattern is still a discard slot.
            # Root-binder introduction is handled by the surrounding let.
            if self.policy is Policy.ROOT_BINDER:
                return CoreVariable(self.fresh(), None)
            return self.dollar(anonymous_scope)
        if isinstance(surface, PairSurface):
            return CorePair(
                self.pattern(surface.left, anonymous_scope),
                self.pattern(surface.right, anonymous_scope),
            )
        return self.term(surface, anonymous_scope)

    def term(
        self,
        surface: Surface,
        anonymous_scope: tuple[int, ...] = (),
    ) -> Core:
        if isinstance(surface, Dollar):
            return self.dollar(anonymous_scope)
        if isinstance(surface, Named):
            return CoreVariable(
                self.named_identity("named", surface.name), surface.name
            )
        if isinstance(surface, StructuralNamed):
            return CoreVariable(
                self.named_identity("structural", surface.key), repr(surface.key)
            )
        if isinstance(surface, Constant):
            return CoreConstant(surface.value)
        if isinstance(surface, PairSurface):
            return CorePair(
                self.term(surface.left, anonymous_scope),
                self.term(surface.right, anonymous_scope),
            )
        if isinstance(surface, QuoteSurface):
            # Quotation is a hard elaboration boundary.
            return CoreQuote(surface.body)
        if isinstance(surface, LetSurface):
            source = self.term(surface.source, anonymous_scope)
            if self.policy is Policy.ROOT_BINDER and isinstance(
                surface.pattern, Dollar
            ):
                identity = self.fresh()
                pattern = CoreVariable(identity, None)
                body = self.term(surface.body, anonymous_scope + (identity,))
            else:
                pattern = self.pattern(surface.pattern, anonymous_scope)
                body = self.term(surface.body, anonymous_scope)
            return CoreLet(pattern, source, body)
        raise TypeError(f"unsupported surface term: {surface!r}")


Bindings: TypeAlias = dict[int, Value]


def match(pattern: Core, target: Value, bindings: Bindings) -> bool:
    if isinstance(pattern, CoreVariable):
        previous = bindings.get(pattern.identity)
        if previous is None:
            bindings[pattern.identity] = target
            return True
        return previous == target
    if isinstance(pattern, CoreConstant):
        return pattern.value == target
    if isinstance(pattern, CorePair) and isinstance(target, PairValue):
        return match(pattern.left, target.left, bindings) and match(
            pattern.right, target.right, bindings
        )
    return False


def evaluate(core: Core, bindings: Bindings | None = None) -> Value:
    environment: Bindings = {} if bindings is None else dict(bindings)
    if isinstance(core, CoreVariable):
        return environment.get(core.identity, FreeVariable(core.identity))
    if isinstance(core, CoreConstant):
        return core.value
    if isinstance(core, CorePair):
        return PairValue(
            evaluate(core.left, environment), evaluate(core.right, environment)
        )
    if isinstance(core, CoreQuote):
        return Quoted(core.surface)
    if isinstance(core, CoreLet):
        source = evaluate(core.source, environment)
        extended = dict(environment)
        if not match(core.pattern, source, extended):
            return NoMatch()
        return evaluate(core.body, extended)
    raise TypeError(f"unsupported core term: {core!r}")


def elaborate_and_evaluate(policy: Policy, surface: Surface) -> Value:
    return evaluate(Elaborator(policy).term(surface))


def projected_named_bindings(core: Core, bindings: Bindings) -> dict[str, Value]:
    """Expose named bindings while deliberately omitting anonymous identities."""

    projected: dict[str, Value] = {}

    def visit(term: Core) -> None:
        if isinstance(term, CoreVariable) and term.display_name is not None:
            if term.identity in bindings:
                projected[term.display_name] = bindings[term.identity]
        elif isinstance(term, CorePair):
            visit(term.left)
            visit(term.right)
        elif isinstance(term, CoreLet):
            visit(term.pattern)
            visit(term.source)
            visit(term.body)

    visit(core)
    return projected
