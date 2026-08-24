"""Source-faithful presentation data for the pinned IGGP corpus.

The game repository contains two independent authored layers:

* parenthesized GDL forms, occasionally interleaved with foreign Prolog; and
* ordered type-profile statements, including overloads and subtyping.

This module only preserves those layers.  It does not assign authority to a
source, interpret negation as failure, or turn missing declarations into type
errors.  Those are later, evidence-bearing operations over the presentation.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from enum import Enum
import re
from typing import Any


SYMBOL_RE = re.compile(r"(?:[A-Za-z_][A-Za-z0-9_]*|-?[0-9]+)\Z")


class PresentationError(RuntimeError):
    """An authored presentation is syntactically malformed."""


@dataclass(frozen=True)
class GdlFormOccurrence:
    """One authored GDL form with its source occurrence identity."""

    start_line: int
    end_line: int
    form: tuple[Any, ...]


@dataclass(frozen=True)
class ForeignSourceLine:
    """Active non-GDL source retained without interpreting it."""

    line: int
    text: str


@dataclass(frozen=True)
class GdlSourcePresentation:
    """The live GDL occurrences and any active foreign source beside them."""

    forms: tuple[GdlFormOccurrence, ...]
    foreign_code: tuple[ForeignSourceLine, ...]

    @property
    def form_values(self) -> tuple[tuple[Any, ...], ...]:
        return tuple(occurrence.form for occurrence in self.forms)


@dataclass(frozen=True)
class GdlSignatureStatement:
    """An ordered, possibly grouped, type signature statement."""

    start_line: int
    end_line: int
    names: tuple[str, ...]
    argument_types: tuple[str, ...]
    result_type: str


@dataclass(frozen=True)
class GdlSubtypeStatement:
    """An authored coercive subtype edge."""

    start_line: int
    end_line: int
    subtype: str
    supertype: str


GdlTypeStatement = GdlSignatureStatement | GdlSubtypeStatement


@dataclass(frozen=True)
class GdlTypeProfile:
    """An occurrence-preserving ordered IGGP type profile."""

    statements: tuple[GdlTypeStatement, ...]

    @property
    def signatures(self) -> tuple[GdlSignatureStatement, ...]:
        return tuple(
            statement
            for statement in self.statements
            if isinstance(statement, GdlSignatureStatement)
        )

    @property
    def subtypes(self) -> tuple[GdlSubtypeStatement, ...]:
        return tuple(
            statement
            for statement in self.statements
            if isinstance(statement, GdlSubtypeStatement)
        )


@dataclass(frozen=True)
class GdlSourceInventory:
    """Structural facts about a parsed source, without semantic closure."""

    form_count: int
    rule_count: int
    fact_count: int
    foreign_code_lines: int
    negation_count: int
    disjunction_count: int
    distinct_count: int
    maximum_depth: int
    unsafe_head_rules: int
    unsafe_negative_rules: int


@dataclass(frozen=True)
class GdlTypeInventory:
    """Occurrence and overloading facts about an authored type profile."""

    signature_statements: int
    signature_occurrences: int
    subtype_statements: int
    duplicate_signature_occurrences: int
    overloaded_symbols: tuple[str, ...]


@dataclass(frozen=True)
class GdlTypingDemand:
    """Arity-level demands not already supplied by the authored profile."""

    covered_applications: tuple[tuple[str, int], ...]
    implicit_standard_applications: tuple[tuple[str, int], ...]
    missing_applications: tuple[tuple[str, int], ...]
    unmatched_authored_name_applications: tuple[tuple[str, int], ...]


@dataclass(frozen=True)
class GdlSourceOccurrence:
    """A stable path to one expression inside an authored GDL form."""

    form_ordinal: int
    start_line: int
    end_line: int
    path: tuple[int, ...]


@dataclass(frozen=True)
class GdlKnownType:
    """An authored or structurally required type name."""

    name: str


@dataclass(frozen=True)
class GdlOccurrenceType:
    """The as-yet-undecided type of one exact source occurrence."""

    source: GdlSourceOccurrence


@dataclass(frozen=True)
class GdlRuleVariableType:
    """The shared type of one rule-local GDL variable."""

    form_ordinal: int
    name: str


@dataclass(frozen=True)
class GdlDerivedSignatureType:
    """One slot in a possible source-derived profile extension.

    ``argument`` is ``None`` for the result slot.  This is constraint data,
    not a declaration or an inferred verdict.
    """

    name: str
    arity: int
    argument: int | None


GdlTypeExpression = (
    GdlKnownType
    | GdlOccurrenceType
    | GdlRuleVariableType
    | GdlDerivedSignatureType
)


@dataclass(frozen=True)
class GdlSignatureOccurrence:
    """One named occurrence contributed by an authored signature statement."""

    statement_ordinal: int
    name_ordinal: int
    start_line: int
    end_line: int
    name: str
    argument_types: tuple[str, ...]
    result_type: str


class GdlApplicationEvidenceKind(str, Enum):
    """How an application relates to the authored type profile."""

    AUTHORED = "authored"
    STRUCTURAL = "structural"
    PROFILE_MISSING = "profile-missing"
    PROFILE_ARITY_MISMATCH = "profile-arity-mismatch"
    AUTHORED_AMBIGUOUS = "authored-ambiguous"


class GdlConstraintReason(str, Enum):
    """The construction that contributed one type constraint."""

    RULE_VARIABLE = "rule-variable"
    AUTHORED_RESULT = "authored-result"
    AUTHORED_ARGUMENT = "authored-argument"
    STRUCTURAL_RESULT = "structural-result"
    STRUCTURAL_ARGUMENT = "structural-argument"
    DERIVED_RESULT = "derived-result"
    DERIVED_ARGUMENT = "derived-argument"
    LITERAL_RESULT = "literal-result"
    CONNECTIVE_RESULT = "connective-result"


@dataclass(frozen=True)
class GdlTypeEqualityConstraint:
    """Two type expressions denote the same source-level type."""

    left: GdlTypeExpression
    right: GdlTypeExpression
    source: GdlSourceOccurrence
    reason: GdlConstraintReason


@dataclass(frozen=True)
class GdlTypeAcceptanceConstraint:
    """A value type must be accepted at an application or literal boundary.

    The eventual profile may discharge this by equality, an authored subtype
    path, or another explicitly licensed coercion.  Extraction does not pick
    one of those interpretations.
    """

    actual: GdlTypeExpression
    expected: GdlTypeExpression
    source: GdlSourceOccurrence
    reason: GdlConstraintReason


@dataclass(frozen=True)
class GdlApplicationTypingEvidence:
    """Occurrence-preserving evidence for one source application."""

    source: GdlSourceOccurrence
    name: str
    arguments: tuple[GdlOccurrenceType, ...]
    result: GdlOccurrenceType
    kind: GdlApplicationEvidenceKind
    authored_candidates: tuple[GdlSignatureOccurrence, ...]
    structural_signature: tuple[tuple[str, ...], str] | None
    derived_signature: tuple[GdlDerivedSignatureType, ...]

    @property
    def arity(self) -> int:
        return len(self.arguments)


@dataclass(frozen=True)
class GdlLogicalTypingEvidence:
    """A structural GDL connective kept distinct from ordinary symbols."""

    source: GdlSourceOccurrence
    operator: str
    operands: tuple[GdlOccurrenceType, ...]
    result: GdlOccurrenceType


@dataclass(frozen=True)
class GdlUnsupportedTypingShape:
    """A source expression retained when the constraint fragment cannot read it."""

    source: GdlSourceOccurrence
    description: str


@dataclass(frozen=True)
class GdlTypingConstraintPresentation:
    """Neutral evidence from source syntax and an authored partial profile.

    Nothing in this object is a checked extension, typing verdict, or NIK
    admission.  It is the finite input from which those later objects may be
    constructed and checked.
    """

    signature_occurrences: tuple[GdlSignatureOccurrence, ...]
    occurrence_types: tuple[GdlOccurrenceType, ...]
    rule_variable_types: tuple[GdlRuleVariableType, ...]
    applications: tuple[GdlApplicationTypingEvidence, ...]
    logical_forms: tuple[GdlLogicalTypingEvidence, ...]
    equalities: tuple[GdlTypeEqualityConstraint, ...]
    acceptances: tuple[GdlTypeAcceptanceConstraint, ...]
    unsupported: tuple[GdlUnsupportedTypingShape, ...]


@dataclass(frozen=True)
class GdlTypingConstraintInventory:
    """Finite size and coverage facts about extracted typing evidence."""

    occurrence_types: int
    rule_variable_types: int
    application_occurrences: int
    authored_applications: int
    structural_applications: int
    missing_profile_applications: int
    arity_mismatch_applications: int
    ambiguous_authored_applications: int
    derived_signatures: int
    logical_forms: int
    distinct_forms: int
    equality_constraints: int
    acceptance_constraints: int
    unsupported_shapes: int


class GdlConstraintTraversal(str, Enum):
    """How a connectivity witness traverses one extracted constraint."""

    EQUALITY = "equality"
    ACCEPTANCE_FORWARD = "acceptance-forward"
    ACCEPTANCE_REVERSE = "acceptance-reverse"


@dataclass(frozen=True)
class GdlConstraintPathStep:
    """One provenance-bearing edge from a signature question to an anchor."""

    source: GdlSourceOccurrence
    reason: GdlConstraintReason
    traversal: GdlConstraintTraversal
    from_type: GdlTypeExpression
    to_type: GdlTypeExpression


@dataclass(frozen=True)
class GdlKnownTypeAnchor:
    """A known type connected to a derived slot, with one concrete path."""

    type_name: str
    path: tuple[GdlConstraintPathStep, ...]


@dataclass(frozen=True)
class GdlDerivedSignatureSupport:
    """Constraint support for one possible derived-signature slot.

    Connectivity is deliberately weaker than inference: an anchor says which
    authored/structural type evidence can reach the question, not that the
    question has been solved as that type.
    """

    slot: GdlDerivedSignatureType
    anchors: tuple[GdlKnownTypeAnchor, ...]
    incomparable_anchor_pairs: tuple[tuple[str, str], ...]


class GdlDerivedSignatureSupportKind(str, Enum):
    """What the neutral constraint graph reveals about one signature."""

    SINGLE_ANCHOR = "single-anchor"
    SUBTYPE_ORDERED = "subtype-ordered"
    UNANCHORED = "unanchored"
    INCOMPARABLE = "incomparable"


@dataclass(frozen=True)
class GdlDerivedSignatureEvidence:
    """All slot support for one name/arity question."""

    name: str
    arity: int
    slots: tuple[GdlDerivedSignatureSupport, ...]
    kind: GdlDerivedSignatureSupportKind


@dataclass(frozen=True)
class GdlDerivedSupportInventory:
    """Coverage facts about source-derived signature questions."""

    signatures: int
    single_anchor_signatures: int
    subtype_ordered_signatures: int
    unanchored_signatures: int
    incomparable_signatures: int
    slots: int
    unanchored_slots: int
    single_anchor_slots: int
    comparable_multi_anchor_slots: int
    incomparable_anchor_slots: int


@dataclass(frozen=True)
class GdlSubtypePath:
    """One finite, authored witness that a type is accepted as another."""

    actual_type: str
    expected_type: str
    steps: tuple[GdlSubtypeStatement, ...]


@dataclass(frozen=True)
class GdlFiniteTypeUniverse:
    """The type names and authored subtype preorder visible in one profile.

    Structural GDL type names are included because they occur independently
    of an optional benchmark profile.  This is a finite diagnostic universe,
    not a claim that a future checked extension may introduce no other type.
    """

    type_names: tuple[str, ...]
    authored_subtype_edges: tuple[GdlSubtypeStatement, ...]
    acceptance_paths: tuple[GdlSubtypePath, ...]

    def acceptance_path(
        self, actual_type: str, expected_type: str
    ) -> GdlSubtypePath | None:
        return next(
            (
                path
                for path in self.acceptance_paths
                if path.actual_type == actual_type
                and path.expected_type == expected_type
            ),
            None,
        )

    def accepts(self, actual_type: str, expected_type: str) -> bool:
        return self.acceptance_path(actual_type, expected_type) is not None


class GdlDomainEliminationSide(str, Enum):
    """Which endpoint of an acceptance constraint lost a candidate."""

    ACTUAL = "actual"
    EXPECTED = "expected"


@dataclass(frozen=True)
class GdlTypeCandidateElimination:
    """Finite evidence for one arc-consistency candidate removal."""

    candidate_type: str
    side: GdlDomainEliminationSide
    constraint: GdlTypeAcceptanceConstraint
    opposing_candidates: tuple[str, ...]
    iteration: int

    def is_valid_in(self, universe: GdlFiniteTypeUniverse) -> bool:
        if self.side == GdlDomainEliminationSide.ACTUAL:
            return all(
                not universe.accepts(self.candidate_type, opposing)
                for opposing in self.opposing_candidates
            )
        return all(
            not universe.accepts(opposing, self.candidate_type)
            for opposing in self.opposing_candidates
        )


@dataclass(frozen=True)
class GdlTypeDomainComponent:
    """One exact-equality component and its remaining finite candidates."""

    ordinal: int
    members: tuple[GdlTypeExpression, ...]
    exact_type_anchors: tuple[str, ...]
    equality_evidence: tuple[GdlTypeEqualityConstraint, ...]
    candidate_types: tuple[str, ...]
    eliminations: tuple[GdlTypeCandidateElimination, ...]


class GdlDerivedDomainKind(str, Enum):
    """Cardinality of one derived slot's necessary candidate set."""

    EMPTY = "empty"
    SINGLETON = "singleton"
    MULTIPLE = "multiple"


@dataclass(frozen=True)
class GdlDerivedSignatureDomain:
    """Necessary existing-universe candidates for one derived slot.

    Arc consistency is sound for candidate removal but is not presented as a
    global solver.  A singleton therefore says what any full solution must
    use, not that a checked source extension has been constructed.
    """

    slot: GdlDerivedSignatureType
    component_ordinal: int
    candidate_types: tuple[str, ...]
    kind: GdlDerivedDomainKind


@dataclass(frozen=True)
class GdlKnownEqualityConflict:
    """An exact equality directly equates two different known type names."""

    constraint: GdlTypeEqualityConstraint


@dataclass(frozen=True)
class GdlKnownAcceptanceConflict:
    """An authored subtype path cannot discharge a known-known boundary."""

    constraint: GdlTypeAcceptanceConstraint


@dataclass(frozen=True)
class GdlExistingTypeArcAnalysis:
    """Arc-consistent evidence within the existing finite type universe.

    Empty domains are checked obstructions to an assignment in this finite
    profile.  They are not refutations of an open-world checked extension.
    No declaration, principal type, or NIK admission is constructed here.
    """

    universe: GdlFiniteTypeUniverse
    components: tuple[GdlTypeDomainComponent, ...]
    derived_domains: tuple[GdlDerivedSignatureDomain, ...]
    known_equality_conflicts: tuple[GdlKnownEqualityConflict, ...]
    known_acceptance_conflicts: tuple[GdlKnownAcceptanceConflict, ...]
    iterations: int


@dataclass(frozen=True)
class GdlExistingTypeArcInventory:
    """Finite coverage facts for an existing-type arc analysis."""

    type_names: int
    subtype_edges: int
    acceptance_paths: int
    components: int
    empty_components: int
    exact_conflict_components: int
    candidate_eliminations: int
    derived_slots: int
    singleton_derived_slots: int
    multiple_derived_slots: int
    empty_derived_slots: int
    derived_signatures: int
    singleton_derived_signatures: int
    multiple_derived_signatures: int
    empty_derived_signatures: int
    known_equality_conflicts: int
    known_acceptance_conflicts: int


@dataclass(frozen=True)
class GdlEmptyDomainReceipt:
    """Occurrence-level evidence for one empty finite type component."""

    component_ordinal: int
    source_occurrences: tuple[GdlSourceOccurrence, ...]
    derived_slots: tuple[GdlDerivedSignatureType, ...]
    exact_type_anchors: tuple[str, ...]
    equality_evidence: tuple[GdlTypeEqualityConstraint, ...]
    eliminations: tuple[GdlTypeCandidateElimination, ...]


@dataclass(frozen=True)
class GdlEmptyDomainReceiptInventory:
    """Coverage facts for finite-profile discrepancy receipts."""

    receipts: int
    source_occurrences: int
    derived_slots: int
    receipts_with_derived_slots: int
    exact_conflict_receipts: int
    candidate_eliminations: int
    invalid_candidate_eliminations: int


@dataclass(frozen=True)
class GdlFiniteTypeChoice:
    """One component choice in the existing finite type universe."""

    component_ordinal: int
    type_name: str


@dataclass(frozen=True)
class GdlFiniteTypeAssignment:
    """A proposed total assignment for the extracted finite problem.

    This is diagnostic data, not a source declaration, typing derivation, or
    admission.  Component choices are explicit so independent completions are
    never collapsed into an inferred profile.
    """

    choices: tuple[GdlFiniteTypeChoice, ...]


@dataclass(frozen=True)
class GdlFiniteEqualityDischarge:
    """Replay evidence that one extracted equality has equal endpoints."""

    constraint: GdlTypeEqualityConstraint
    type_name: str


@dataclass(frozen=True)
class GdlFiniteAcceptanceDischarge:
    """The authored subtype path discharging one directed boundary."""

    constraint: GdlTypeAcceptanceConstraint
    path: GdlSubtypePath


@dataclass(frozen=True)
class GdlCheckedFiniteTypeAssignment:
    """A finite assignment replayed against every extracted constraint.

    The scope remains exactly the existing type names and authored subtype
    edges.  It witnesses consistency of that finite problem only; it does not
    select a profile or decide open-world source typing.
    """

    assignment: GdlFiniteTypeAssignment
    equalities: tuple[GdlFiniteEqualityDischarge, ...]
    acceptances: tuple[GdlFiniteAcceptanceDischarge, ...]


@dataclass(frozen=True)
class GdlFiniteChoiceCompletion:
    """One locally retained choice with a complete replayed witness."""

    choice: GdlFiniteTypeChoice
    witness: GdlCheckedFiniteTypeAssignment


@dataclass(frozen=True)
class GdlDerivedFiniteCompletionDomain:
    """Global completion support for one possible derived-profile slot."""

    slot: GdlDerivedSignatureType
    component_ordinal: int
    local_candidate_types: tuple[str, ...]
    completions: tuple[GdlFiniteChoiceCompletion, ...]
    globally_unsupported_types: tuple[str, ...]


@dataclass(frozen=True)
class GdlFiniteCompletionProjection:
    """Exact supported-choice projection of a satisfiable finite problem.

    Each candidate retained here extends to at least one complete assignment.
    The witnesses establish existence without selecting a principal profile or
    eagerly enumerating the Cartesian product of independent completions.
    """

    existence_witness: GdlCheckedFiniteTypeAssignment
    derived_domains: tuple[GdlDerivedFiniteCompletionDomain, ...]


@dataclass(frozen=True)
class GdlFiniteResolvedTypeExpression:
    """One extracted type expression resolved by a checked finite witness."""

    expression: GdlTypeExpression
    type_name: str


@dataclass(frozen=True)
class GdlFiniteResolvedApplication:
    """Occurrence-specific application types under one finite assignment."""

    source: GdlSourceOccurrence
    name: str
    kind: GdlApplicationEvidenceKind
    argument_types: tuple[str, ...]
    result_type: str


@dataclass(frozen=True)
class GdlFiniteDerivedSignatureProposal:
    """One complete possible signature, retaining component correlations."""

    name: str
    arity: int
    argument_types: tuple[str, ...]
    result_type: str
    argument_component_ordinals: tuple[int, ...]
    result_component_ordinal: int


@dataclass(frozen=True)
class GdlFiniteTypedOccurrenceProjection:
    """A typed source proposal indexed by its complete replay witness.

    The same extracted source may have several such projections.  This object
    is therefore proposal data for a later checked presentation extension,
    never an inferred declaration or an authority result.
    """

    witness: GdlCheckedFiniteTypeAssignment
    resolved_expressions: tuple[GdlFiniteResolvedTypeExpression, ...]
    applications: tuple[GdlFiniteResolvedApplication, ...]
    derived_signatures: tuple[GdlFiniteDerivedSignatureProposal, ...]


@dataclass(frozen=True)
class GdlTypeOfJudgment:
    """One lowercase ``type:of`` judgment for an exact source occurrence."""

    source: GdlSourceOccurrence
    type_name: str


@dataclass(frozen=True)
class GdlAuthoredTypeOfRule:
    """One authored signature occurrence used by a typing derivation."""

    signature: GdlSignatureOccurrence


@dataclass(frozen=True)
class GdlStructuralTypeOfRule:
    """One rule supplied by the generic GDL typing calculus."""

    name: str
    argument_types: tuple[str, ...]
    result_type: str


@dataclass(frozen=True)
class GdlExtendedTypeOfRule:
    """One signature added by a particular replay-checked completion."""

    signature: GdlFiniteDerivedSignatureProposal


GdlTypeOfRule = (
    GdlAuthoredTypeOfRule
    | GdlStructuralTypeOfRule
    | GdlExtendedTypeOfRule
)


@dataclass(frozen=True)
class GdlTypeOfArgumentPremise:
    """A typed argument and the exact subtype path accepted at its boundary."""

    judgment: GdlTypeOfJudgment
    expected_type: str
    acceptance: GdlFiniteAcceptanceDischarge


@dataclass(frozen=True)
class GdlTypeOfApplicationDerivation:
    """One proof-relevant signature-rule use for a source application."""

    conclusion: GdlTypeOfJudgment
    application: GdlApplicationTypingEvidence
    rule: GdlTypeOfRule
    premises: tuple[GdlTypeOfArgumentPremise, ...]
    result_equality: GdlFiniteEqualityDischarge


@dataclass(frozen=True)
class GdlTypeOfVariableDerivation:
    """One occurrence of a rule variable linked to its shared type slot."""

    conclusion: GdlTypeOfJudgment
    variable: GdlRuleVariableType
    equality: GdlFiniteEqualityDischarge


@dataclass(frozen=True)
class GdlTypeOfLogicalDerivation:
    """One generic logical-form rule, retaining its exact operand judgments."""

    conclusion: GdlTypeOfJudgment
    logical_form: GdlLogicalTypingEvidence
    operands: tuple[GdlTypeOfJudgment, ...]
    result_equality: GdlFiniteEqualityDischarge


GdlTypeOfDerivation = (
    GdlTypeOfApplicationDerivation
    | GdlTypeOfVariableDerivation
    | GdlTypeOfLogicalDerivation
)


@dataclass(frozen=True)
class GdlTypeOfLiteralBoundary:
    """Evidence that one rule literal is accepted at the GDL Boolean boundary."""

    judgment: GdlTypeOfJudgment
    acceptance: GdlFiniteAcceptanceDischarge


@dataclass(frozen=True)
class GdlCheckedTypeOfExtension:
    """A proof-carrying optional typing extension over unchanged GDL.

    One object is indexed by one complete finite assignment.  Alternative
    assignments construct distinct objects; no principal completion is chosen.
    The object contains neither an operational rewrite nor NIK authority data.
    Those layers may consume this evidence later without being defined by it.
    """

    authored_profile: GdlTypeProfile
    proposal: GdlFiniteTypedOccurrenceProjection
    occurrence_judgments: tuple[GdlTypeOfJudgment, ...]
    application_derivations: tuple[GdlTypeOfApplicationDerivation, ...]
    variable_derivations: tuple[GdlTypeOfVariableDerivation, ...]
    logical_derivations: tuple[GdlTypeOfLogicalDerivation, ...]
    literal_boundaries: tuple[GdlTypeOfLiteralBoundary, ...]

    @property
    def judgment_head(self) -> str:
        """The sole public judgment spelling contributed by this calculus."""

        return "type:of"


@dataclass(frozen=True)
class GdlCheckedTypeOfExtensionInventory:
    """Coverage and proof-occurrence facts for one checked extension."""

    occurrence_judgments: int
    application_occurrences: int
    application_derivations: int
    authored_rule_uses: int
    structural_rule_uses: int
    extended_rule_uses: int
    variable_derivations: int
    logical_derivations: int
    literal_boundaries: int
    nontrivial_subtype_uses: int
    derived_signatures: int


@dataclass(frozen=True)
class GdlPositiveBindingBranch:
    """One logical branch that positively binds a rule variable.

    ``occurrences`` retains every occurrence of the variable in that branch.
    A binding through ``or`` has one such witness for every alternative.
    """

    source: GdlSourceOccurrence
    occurrences: tuple[GdlSourceOccurrence, ...]


@dataclass(frozen=True)
class GdlPositiveBindingWitness:
    """One rule-body literal that binds a variable on every branch."""

    source: GdlSourceOccurrence
    branches: tuple[GdlPositiveBindingBranch, ...]


@dataclass(frozen=True)
class GdlNegativeVariableDemand:
    """A variable in a negative premise and its constructive binders."""

    name: str
    negative_occurrences: tuple[GdlSourceOccurrence, ...]
    positive_bindings: tuple[GdlPositiveBindingWitness, ...]


@dataclass(frozen=True)
class GdlFiniteRelationAbsenceDemand:
    """Evidence needed before an ordinary negative atom may establish absence.

    This object records syntax and positive variable bindings only.  It does
    not contain the complete finite relation view required to establish that
    the grounded tuple is absent.
    """

    source: GdlSourceOccurrence
    operand_source: GdlSourceOccurrence
    rule_source: GdlSourceOccurrence
    relation: str
    argument_sources: tuple[GdlSourceOccurrence, ...]
    variables: tuple[GdlNegativeVariableDemand, ...]


@dataclass(frozen=True)
class GdlDistinctRefutationDemand:
    """Evidence needed to establish a negated structural ``distinct``."""

    source: GdlSourceOccurrence
    operand_source: GdlSourceOccurrence
    rule_source: GdlSourceOccurrence
    left_source: GdlSourceOccurrence
    right_source: GdlSourceOccurrence
    variables: tuple[GdlNegativeVariableDemand, ...]


@dataclass(frozen=True)
class GdlUnsupportedNegativePremise:
    """A retained negative occurrence outside the covered demand fragment."""

    source: GdlSourceOccurrence
    form_source: GdlSourceOccurrence
    variables: tuple[GdlNegativeVariableDemand, ...]
    description: str


@dataclass(frozen=True)
class GdlNegativePremisePresentation:
    """Authority-free constructive demands of authored negative premises."""

    relation_absences: tuple[GdlFiniteRelationAbsenceDemand, ...]
    distinct_refutations: tuple[GdlDistinctRefutationDemand, ...]
    unsupported: tuple[GdlUnsupportedNegativePremise, ...]


@dataclass(frozen=True)
class GdlNegativePremiseInventory:
    """Finite size and binding facts about negative-premise demands."""

    relation_absences: int
    distinct_refutations: int
    unsupported: int
    ground_relation_absences: int
    variable_relation_absences: int
    variable_demands: int
    unbound_variable_demands: int
    positive_binding_witnesses: int
    positive_binding_branches: int


@dataclass(frozen=True)
class GdlFiniteTypedRelationAbsenceDemand:
    """A relation-absence demand resolved by one finite typing witness."""

    demand: GdlFiniteRelationAbsenceDemand
    application: GdlFiniteResolvedApplication


@dataclass(frozen=True)
class GdlFiniteTypedDistinctRefutationDemand:
    """A structural-distinct demand with both operand types resolved."""

    demand: GdlDistinctRefutationDemand
    operand_types: tuple[str, str]


@dataclass(frozen=True)
class GdlFiniteTypedNegativePremiseProjection:
    """Typed negative demands, still lacking relation-completeness evidence.

    The replay-checked source proposal fixes the argument types.  A later
    runtime presentation must still supply a complete, revision-bound finite
    relation view before an ordinary absence can become evidence.
    """

    typed_source: GdlFiniteTypedOccurrenceProjection
    relation_absences: tuple[GdlFiniteTypedRelationAbsenceDemand, ...]
    distinct_refutations: tuple[GdlFiniteTypedDistinctRefutationDemand, ...]
    unsupported: tuple[GdlUnsupportedNegativePremise, ...]


STANDARD_GDL_ARITIES = {
    "role": 1,
    "base": 1,
    "input": 2,
    "init": 1,
    "true": 1,
    "does": 2,
    "next": 1,
    "legal": 2,
    "goal": 2,
    "terminal": 0,
    "distinct": 2,
}


# These are formation facts of the GDL language, not additions supplied by an
# optional benchmark type profile.  ``goal`` is deliberately absent because
# the corpus profiles choose several different score types for its second
# argument.  ``distinct`` remains a structural polymorphic connective.
STRUCTURAL_GDL_SIGNATURES = {
    "role": (("agent",), "bool"),
    "base": (("prop",), "bool"),
    "input": (("agent", "action"), "bool"),
    "init": (("prop",), "bool"),
    "true": (("prop",), "bool"),
    "does": (("agent", "action"), "bool"),
    "next": (("prop",), "bool"),
    "legal": (("agent", "action"), "bool"),
    "terminal": ((), "bool"),
}


def parse_gdl(source: str) -> tuple[Any, ...]:
    """Parse a source consisting only of canonical GDL S-expressions."""

    tokens: list[str] = []
    for raw_line in source.splitlines():
        line = raw_line.split(";", 1)[0]
        tokens.extend(re.findall(r"\(|\)|[^\s()]+", line))
    position = 0

    def parse_form() -> Any:
        nonlocal position
        if position >= len(tokens):
            raise PresentationError("unexpected end of canonical GDL")
        token = tokens[position]
        position += 1
        if token != "(":
            if token == ")":
                raise PresentationError(
                    "unexpected close parenthesis in GDL"
                )
            return token
        values: list[Any] = []
        while True:
            if position >= len(tokens):
                raise PresentationError("unterminated canonical GDL form")
            if tokens[position] == ")":
                position += 1
                return tuple(values)
            values.append(parse_form())

    forms: list[Any] = []
    while position < len(tokens):
        forms.append(parse_form())
    return tuple(forms)


def _parse_one_gdl_form(
    text: str, start_line: int, end_line: int
) -> GdlFormOccurrence:
    parsed = parse_gdl(text)
    if len(parsed) != 1 or not isinstance(parsed[0], tuple):
        raise PresentationError(
            f"lines {start_line}-{end_line}: expected one GDL form"
        )
    if not parsed[0]:
        raise PresentationError(
            f"lines {start_line}-{end_line}: empty GDL form"
        )
    return GdlFormOccurrence(start_line, end_line, parsed[0])


def parse_gdl_source_presentation(source: str) -> GdlSourcePresentation:
    """Separate leading S-expression forms from active foreign source.

    A GDL occurrence begins with ``(`` as the first active token on a line (or
    after a preceding form on the same line).  Semicolon comments and Prolog
    percent-comment lines are inert.  Active non-parenthesized source is kept
    as foreign data rather than fed through the GDL parser.
    """

    forms: list[GdlFormOccurrence] = []
    foreign: list[ForeignSourceLine] = []
    pending: list[str] = []
    pending_start = 0
    depth = 0

    for line_number, raw_line in enumerate(source.splitlines(), 1):
        code = raw_line.split(";", 1)[0]
        position = 0

        if depth:
            segment_start = 0
            while position < len(code):
                character = code[position]
                if character == "(":
                    depth += 1
                elif character == ")":
                    depth -= 1
                    if depth < 0:
                        raise PresentationError(
                            f"line {line_number}: unexpected close "
                            "parenthesis in GDL"
                        )
                    if depth == 0:
                        position += 1
                        pending.append(code[segment_start:position])
                        forms.append(
                            _parse_one_gdl_form(
                                "\n".join(pending),
                                pending_start,
                                line_number,
                            )
                        )
                        pending = []
                        break
                position += 1
            if depth:
                pending.append(code[segment_start:])
                continue

        while position < len(code):
            while position < len(code) and code[position].isspace():
                position += 1
            if position == len(code):
                break
            remainder = code[position:]
            if remainder.startswith("%"):
                break
            if not remainder.startswith("("):
                foreign.append(
                    ForeignSourceLine(line_number, remainder.rstrip())
                )
                break

            pending_start = line_number
            form_start = position
            depth = 0
            while position < len(code):
                character = code[position]
                if character == "(":
                    depth += 1
                elif character == ")":
                    depth -= 1
                    if depth < 0:
                        raise PresentationError(
                            f"line {line_number}: unexpected close "
                            "parenthesis in GDL"
                        )
                    if depth == 0:
                        position += 1
                        forms.append(
                            _parse_one_gdl_form(
                                code[form_start:position],
                                line_number,
                                line_number,
                            )
                        )
                        break
                position += 1
            if depth:
                pending = [code[form_start:]]
                break

    if depth:
        raise PresentationError(
            f"line {pending_start}: unterminated GDL form"
        )
    return GdlSourcePresentation(tuple(forms), tuple(foreign))


def _line_range(source: str, start: int, end: int) -> tuple[int, int]:
    start_line = source.count("\n", 0, start) + 1
    end_line = source.count("\n", 0, end) + 1
    return start_line, end_line


def _validate_symbol(symbol: str, line: int, role: str) -> None:
    if not SYMBOL_RE.fullmatch(symbol):
        raise PresentationError(
            f"line {line}: invalid {role} {symbol!r}"
        )


def parse_gdl_type_profile(source: str) -> GdlTypeProfile:
    """Parse the ordered ``::`` and ``:>`` statements of an IGGP profile."""

    uncommented = "\n".join(
        raw_line.split("%", 1)[0].split(";", 1)[0]
        for raw_line in source.splitlines()
    )
    statements: list[GdlTypeStatement] = []
    statement_start = 0
    for match in re.finditer(r"\.", uncommented):
        statement_end = match.start()
        raw_statement = uncommented[statement_start:statement_end]
        leading_space = len(raw_statement) - len(raw_statement.lstrip())
        trailing_end = len(raw_statement.rstrip())
        start_line, end_line = _line_range(
            uncommented,
            statement_start + leading_space,
            statement_start + trailing_end,
        )
        statement_start = match.end()
        text = " ".join(raw_statement.split())
        if not text:
            continue

        signature_count = text.count("::")
        subtype_count = text.count(":>")
        if signature_count + subtype_count != 1:
            raise PresentationError(
                f"line {start_line}: expected exactly one :: or :>"
            )
        if signature_count:
            left, right = (part.strip() for part in text.split("::", 1))
            names = tuple(
                name.strip() for name in left.split(",") if name.strip()
            )
            types = tuple(
                type_name.strip()
                for type_name in right.split("->")
                if type_name.strip()
            )
            if not names or not types:
                raise PresentationError(
                    f"line {start_line}: incomplete type signature"
                )
            for name in names:
                _validate_symbol(name, start_line, "declared symbol")
            for type_name in types:
                _validate_symbol(type_name, start_line, "type name")
            statements.append(
                GdlSignatureStatement(
                    start_line=start_line,
                    end_line=end_line,
                    names=names,
                    argument_types=types[:-1],
                    result_type=types[-1],
                )
            )
        else:
            left, right = (part.strip() for part in text.split(":>", 1))
            _validate_symbol(left, start_line, "subtype")
            _validate_symbol(right, start_line, "supertype")
            statements.append(
                GdlSubtypeStatement(
                    start_line=start_line,
                    end_line=end_line,
                    subtype=left,
                    supertype=right,
                )
            )

    remainder = " ".join(uncommented[statement_start:].split())
    if remainder:
        line, _ = _line_range(
            uncommented, statement_start, len(uncommented)
        )
        raise PresentationError(
            f"line {line}: unterminated type-profile statement"
        )
    return GdlTypeProfile(tuple(statements))


def _walk(value: Any):
    yield value
    if isinstance(value, tuple):
        for child in value:
            yield from _walk(child)


def _variables(value: Any) -> set[str]:
    return {
        item
        for item in _walk(value)
        if isinstance(item, str) and item.startswith("?")
    }


def _binding_variables(literal: Any) -> set[str]:
    if not isinstance(literal, tuple) or not literal:
        return _variables(literal)
    if literal[0] in {"not", "distinct"}:
        return set()
    if literal[0] == "or":
        alternatives = [
            _binding_variables(alternative) for alternative in literal[1:]
        ]
        if not alternatives:
            return set()
        return set.intersection(*alternatives)
    return _variables(literal)


def _negative_variables(literal: Any) -> set[str]:
    if not isinstance(literal, tuple) or not literal:
        return set()
    if literal[0] == "not":
        return _variables(literal[1:])
    if literal[0] == "or":
        return set().union(
            *(_negative_variables(alternative) for alternative in literal[1:])
        )
    return set()


def extract_gdl_negative_premise_demands(
    presentation: GdlSourcePresentation,
) -> GdlNegativePremisePresentation:
    """Extract what each authored negative premise would need as evidence.

    Ordinary negative atoms demand a grounded tuple and a complete finite
    relation view.  Negated ``distinct`` instead demands equality evidence.
    Positive variable-binding witnesses are retained by exact source path,
    including one branch witness per ``or`` alternative.  No absence,
    refutation, typing judgment, or authority result is constructed here.
    """

    relation_absences: list[GdlFiniteRelationAbsenceDemand] = []
    distinct_refutations: list[GdlDistinctRefutationDemand] = []
    unsupported: list[GdlUnsupportedNegativePremise] = []

    for form_ordinal, form in enumerate(presentation.forms):
        def source_at(path: tuple[int, ...]) -> GdlSourceOccurrence:
            return GdlSourceOccurrence(
                form_ordinal=form_ordinal,
                start_line=form.start_line,
                end_line=form.end_line,
                path=path,
            )

        def variable_occurrences(
            value: Any,
            path: tuple[int, ...],
        ) -> tuple[tuple[str, GdlSourceOccurrence], ...]:
            found: list[tuple[str, GdlSourceOccurrence]] = []

            def visit(current: Any, current_path: tuple[int, ...]) -> None:
                if isinstance(current, str):
                    if current.startswith("?"):
                        found.append((current, source_at(current_path)))
                    return
                if isinstance(current, tuple):
                    for index, child in enumerate(current):
                        visit(child, current_path + (index,))

            visit(value, path)
            return tuple(found)

        def positive_binding_branches(
            literal: Any,
            path: tuple[int, ...],
            variable: str,
        ) -> tuple[GdlPositiveBindingBranch, ...]:
            if isinstance(literal, tuple) and literal:
                if literal[0] in {"not", "distinct"}:
                    return ()
                if literal[0] == "or":
                    if len(literal) < 2:
                        return ()
                    branches: list[GdlPositiveBindingBranch] = []
                    for index, alternative in enumerate(literal[1:], 1):
                        alternative_branches = positive_binding_branches(
                            alternative, path + (index,), variable
                        )
                        if not alternative_branches:
                            return ()
                        branches.extend(alternative_branches)
                    return tuple(branches)
            occurrences = tuple(
                source
                for name, source in variable_occurrences(literal, path)
                if name == variable
            )
            if not occurrences:
                return ()
            return (GdlPositiveBindingBranch(source_at(path), occurrences),)

        is_rule = bool(form.form and form.form[0] == "<=")
        rule_source = source_at(())
        body = (
            tuple((index, literal) for index, literal in enumerate(
                form.form[2:], 2
            ))
            if is_rule
            else ()
        )

        def negative_variables(
            value: Any,
            path: tuple[int, ...],
        ) -> tuple[GdlNegativeVariableDemand, ...]:
            grouped: dict[str, list[GdlSourceOccurrence]] = {}
            for name, source in variable_occurrences(value, path):
                grouped.setdefault(name, []).append(source)
            demands: list[GdlNegativeVariableDemand] = []
            for name, occurrences in grouped.items():
                bindings: list[GdlPositiveBindingWitness] = []
                for literal_index, literal in body:
                    literal_path = (literal_index,)
                    branches = positive_binding_branches(
                        literal, literal_path, name
                    )
                    if branches:
                        bindings.append(
                            GdlPositiveBindingWitness(
                                source_at(literal_path), branches
                            )
                        )
                demands.append(
                    GdlNegativeVariableDemand(
                        name=name,
                        negative_occurrences=tuple(occurrences),
                        positive_bindings=tuple(bindings),
                    )
                )
            return tuple(demands)

        def retain_unsupported(
            source: GdlSourceOccurrence,
            value: Any,
            path: tuple[int, ...],
            description: str,
        ) -> None:
            unsupported.append(
                GdlUnsupportedNegativePremise(
                    source=source,
                    form_source=source_at(()),
                    variables=negative_variables(value, path),
                    description=description,
                )
            )

        def visit_negative(
            value: Any,
            path: tuple[int, ...],
            premise_position: bool,
        ) -> None:
            if not isinstance(value, tuple):
                return
            if value and value[0] == "not":
                source = source_at(path)
                if not premise_position:
                    retain_unsupported(
                        source,
                        value,
                        path,
                        "negative occurrence is outside a rule-body literal",
                    )
                elif len(value) != 2:
                    retain_unsupported(
                        source,
                        value,
                        path,
                        "not does not have exactly one operand",
                    )
                else:
                    operand = value[1]
                    operand_path = path + (1,)
                    variables = negative_variables(operand, operand_path)
                    if isinstance(operand, str) and not operand.startswith("?"):
                        relation_absences.append(
                            GdlFiniteRelationAbsenceDemand(
                                source=source,
                                operand_source=source_at(operand_path),
                                rule_source=rule_source,
                                relation=operand,
                                argument_sources=(),
                                variables=variables,
                            )
                        )
                    elif isinstance(operand, tuple) and operand:
                        head = operand[0]
                        if head == "distinct" and len(operand) == 3:
                            distinct_refutations.append(
                                GdlDistinctRefutationDemand(
                                    source=source,
                                    operand_source=source_at(operand_path),
                                    rule_source=rule_source,
                                    left_source=source_at(operand_path + (1,)),
                                    right_source=source_at(operand_path + (2,)),
                                    variables=variables,
                                )
                            )
                        elif (
                            isinstance(head, str)
                            and not head.startswith("?")
                            and head not in {"<=", "not", "or", "distinct"}
                        ):
                            relation_absences.append(
                                GdlFiniteRelationAbsenceDemand(
                                    source=source,
                                    operand_source=source_at(operand_path),
                                    rule_source=rule_source,
                                    relation=head,
                                    argument_sources=tuple(
                                        source_at(operand_path + (index,))
                                        for index in range(1, len(operand))
                                    ),
                                    variables=variables,
                                )
                            )
                        else:
                            retain_unsupported(
                                source,
                                operand,
                                operand_path,
                                "negative operand is not a covered atom",
                            )
                    else:
                        retain_unsupported(
                            source,
                            operand,
                            operand_path,
                            "negative operand is not a covered atom",
                        )
            if value and value[0] == "or" and premise_position:
                for index, alternative in enumerate(value[1:], 1):
                    visit_negative(
                        alternative, path + (index,), True
                    )
            else:
                for index, child in enumerate(value):
                    visit_negative(child, path + (index,), False)

        if is_rule:
            if len(form.form) > 1:
                visit_negative(form.form[1], (1,), False)
            for index, literal in body:
                visit_negative(literal, (index,), True)
        else:
            visit_negative(form.form, (), False)

    return GdlNegativePremisePresentation(
        relation_absences=tuple(relation_absences),
        distinct_refutations=tuple(distinct_refutations),
        unsupported=tuple(unsupported),
    )


def inventory_gdl_negative_premises(
    presentation: GdlNegativePremisePresentation,
) -> GdlNegativePremiseInventory:
    """Count constructive demands without treating them as decisions."""

    variable_demands = tuple(
        variable
        for demand in (
            *presentation.relation_absences,
            *presentation.distinct_refutations,
            *presentation.unsupported,
        )
        for variable in demand.variables
    )
    bindings = tuple(
        binding
        for variable in variable_demands
        for binding in variable.positive_bindings
    )
    return GdlNegativePremiseInventory(
        relation_absences=len(presentation.relation_absences),
        distinct_refutations=len(presentation.distinct_refutations),
        unsupported=len(presentation.unsupported),
        ground_relation_absences=sum(
            not demand.variables
            for demand in presentation.relation_absences
        ),
        variable_relation_absences=sum(
            bool(demand.variables)
            for demand in presentation.relation_absences
        ),
        variable_demands=len(variable_demands),
        unbound_variable_demands=sum(
            not demand.positive_bindings for demand in variable_demands
        ),
        positive_binding_witnesses=len(bindings),
        positive_binding_branches=sum(
            len(binding.branches) for binding in bindings
        ),
    )


def _depth(value: Any) -> int:
    if not isinstance(value, tuple):
        return 0
    return 1 + max((_depth(child) for child in value), default=0)


def inventory_gdl_source(
    presentation: GdlSourcePresentation,
) -> GdlSourceInventory:
    """Compute source-shape facts while retaining every occurrence."""

    rules = [
        occurrence.form
        for occurrence in presentation.forms
        if occurrence.form[0] == "<="
    ]
    for rule in rules:
        if len(rule) < 2:
            raise PresentationError("GDL rule is missing its conclusion")
    negations = 0
    disjunctions = 0
    distinctions = 0
    for occurrence in presentation.forms:
        for value in _walk(occurrence.form):
            if not isinstance(value, tuple) or not value:
                continue
            if value[0] == "not":
                negations += 1
            elif value[0] == "or":
                disjunctions += 1
            elif value[0] == "distinct":
                distinctions += 1

    unsafe_heads = 0
    unsafe_negatives = 0
    for rule in rules:
        bound = set().union(
            *(_binding_variables(literal) for literal in rule[2:])
        )
        unsafe_heads += bool(_variables(rule[1]) - bound)
        negative = set().union(
            *(_negative_variables(literal) for literal in rule[2:])
        )
        unsafe_negatives += bool(negative - bound)

    return GdlSourceInventory(
        form_count=len(presentation.forms),
        rule_count=len(rules),
        fact_count=len(presentation.forms) - len(rules),
        foreign_code_lines=len(presentation.foreign_code),
        negation_count=negations,
        disjunction_count=disjunctions,
        distinct_count=distinctions,
        maximum_depth=max(
            (_depth(occurrence.form) for occurrence in presentation.forms),
            default=0,
        ),
        unsafe_head_rules=unsafe_heads,
        unsafe_negative_rules=unsafe_negatives,
    )


def inventory_gdl_types(profile: GdlTypeProfile) -> GdlTypeInventory:
    """Compute duplicate and overloading facts without erasing occurrences."""

    occurrences: list[tuple[str, tuple[str, ...], str]] = []
    for statement in profile.signatures:
        occurrences.extend(
            (name, statement.argument_types, statement.result_type)
            for name in statement.names
        )
    seen: set[tuple[str, tuple[str, ...], str]] = set()
    duplicates = 0
    signatures_by_name: dict[str, set[tuple[tuple[str, ...], str]]] = {}
    for name, arguments, result in occurrences:
        signature = (name, arguments, result)
        if signature in seen:
            duplicates += 1
        seen.add(signature)
        signatures_by_name.setdefault(name, set()).add((arguments, result))
    overloaded = tuple(
        sorted(
            name
            for name, signatures in signatures_by_name.items()
            if len(signatures) > 1
        )
    )
    return GdlTypeInventory(
        signature_statements=len(profile.signatures),
        signature_occurrences=len(occurrences),
        subtype_statements=len(profile.subtypes),
        duplicate_signature_occurrences=duplicates,
        overloaded_symbols=overloaded,
    )


def _atom_applications(atom: Any):
    if isinstance(atom, str):
        yield (atom, 0)
        return
    if not isinstance(atom, tuple) or not atom:
        return
    head = atom[0]
    if not isinstance(head, str):
        return
    yield (head, len(atom) - 1)
    for argument in atom[1:]:
        if isinstance(argument, tuple):
            yield from _term_applications(argument)


def _term_applications(term: tuple[Any, ...]):
    if not term or not isinstance(term[0], str):
        return
    yield (term[0], len(term) - 1)
    for argument in term[1:]:
        if isinstance(argument, tuple):
            yield from _term_applications(argument)


def _literal_applications(literal: Any):
    if isinstance(literal, tuple) and literal:
        if literal[0] == "not":
            if len(literal) == 2:
                yield from _literal_applications(literal[1])
            return
        if literal[0] == "or":
            for alternative in literal[1:]:
                yield from _literal_applications(alternative)
            return
    yield from _atom_applications(literal)


def gdl_applications(
    presentation: GdlSourcePresentation,
) -> tuple[tuple[str, int], ...]:
    """Return every distinct relation/function application by name and arity."""

    applications: set[tuple[str, int]] = set()
    for occurrence in presentation.forms:
        form = occurrence.form
        if form[0] == "<=":
            if len(form) < 2:
                raise PresentationError("GDL rule is missing its conclusion")
            applications.update(_atom_applications(form[1]))
            for literal in form[2:]:
                applications.update(_literal_applications(literal))
        else:
            applications.update(_atom_applications(form))
    return tuple(sorted(applications))


def gdl_typing_demand(
    presentation: GdlSourcePresentation, profile: GdlTypeProfile
) -> GdlTypingDemand:
    """Compare source arities with authored and standard GDL signatures.

    Missing applications are inference obligations, not refutations.  An
    unmatched authored name may require a new overload or may expose a source
    inconsistency; this function records the choice point without resolving it.
    """

    authored: dict[str, set[int]] = {}
    for statement in profile.signatures:
        for name in statement.names:
            authored.setdefault(name, set()).add(
                len(statement.argument_types)
            )

    covered: list[tuple[str, int]] = []
    standard: list[tuple[str, int]] = []
    missing: list[tuple[str, int]] = []
    unmatched: list[tuple[str, int]] = []
    for application in gdl_applications(presentation):
        name, arity = application
        if name in STANDARD_GDL_ARITIES:
            if arity == STANDARD_GDL_ARITIES[name]:
                standard.append(application)
            else:
                unmatched.append(application)
        elif arity in authored.get(name, set()):
            covered.append(application)
        elif name in authored:
            unmatched.append(application)
        else:
            missing.append(application)
    return GdlTypingDemand(
        covered_applications=tuple(covered),
        implicit_standard_applications=tuple(standard),
        missing_applications=tuple(missing),
        unmatched_authored_name_applications=tuple(unmatched),
    )


def gdl_signature_occurrences(
    profile: GdlTypeProfile,
) -> tuple[GdlSignatureOccurrence, ...]:
    """Expand grouped declarations without erasing statement occurrences."""

    occurrences: list[GdlSignatureOccurrence] = []
    for statement_ordinal, statement in enumerate(profile.statements):
        if not isinstance(statement, GdlSignatureStatement):
            continue
        for name_ordinal, name in enumerate(statement.names):
            occurrences.append(
                GdlSignatureOccurrence(
                    statement_ordinal=statement_ordinal,
                    name_ordinal=name_ordinal,
                    start_line=statement.start_line,
                    end_line=statement.end_line,
                    name=name,
                    argument_types=statement.argument_types,
                    result_type=statement.result_type,
                )
            )
    return tuple(occurrences)


def extract_gdl_typing_constraints(
    presentation: GdlSourcePresentation,
    profile: GdlTypeProfile,
    *,
    form_ordinals: frozenset[int] | None = None,
) -> GdlTypingConstraintPresentation:
    """Extract occurrence-indexed typing evidence without deciding it.

    Applications covered by one distinct authored signature contribute that
    signature's argument/result constraints.  Repeated identical declarations
    remain as separate evidence while inducing the same constraints.  A source
    application absent from the profile is connected to stable, shared
    signature slots; those slots are a *question* a profile extension may
    answer, not a synthesized declaration.  Same-arity authored ambiguity and
    unsupported dynamic heads are retained without choosing a branch.
    """

    signatures = gdl_signature_occurrences(profile)
    by_name: dict[str, list[GdlSignatureOccurrence]] = {}
    by_name_arity: dict[
        tuple[str, int], list[GdlSignatureOccurrence]
    ] = {}
    for signature in signatures:
        by_name.setdefault(signature.name, []).append(signature)
        by_name_arity.setdefault(
            (signature.name, len(signature.argument_types)), []
        ).append(signature)

    occurrence_types: list[GdlOccurrenceType] = []
    occurrence_type_by_source: dict[
        GdlSourceOccurrence, GdlOccurrenceType
    ] = {}
    rule_variable_types: list[GdlRuleVariableType] = []
    rule_variable_by_key: dict[
        tuple[int, str], GdlRuleVariableType
    ] = {}
    applications: list[GdlApplicationTypingEvidence] = []
    logical_forms: list[GdlLogicalTypingEvidence] = []
    equalities: list[GdlTypeEqualityConstraint] = []
    acceptances: list[GdlTypeAcceptanceConstraint] = []
    unsupported: list[GdlUnsupportedTypingShape] = []

    def source_at(
        form_ordinal: int,
        form: GdlFormOccurrence,
        path: tuple[int, ...],
    ) -> GdlSourceOccurrence:
        return GdlSourceOccurrence(
            form_ordinal=form_ordinal,
            start_line=form.start_line,
            end_line=form.end_line,
            path=path,
        )

    def occurrence_type(
        source: GdlSourceOccurrence,
    ) -> GdlOccurrenceType:
        existing = occurrence_type_by_source.get(source)
        if existing is not None:
            return existing
        slot = GdlOccurrenceType(source)
        occurrence_type_by_source[source] = slot
        occurrence_types.append(slot)
        return slot

    def rule_variable_type(
        form_ordinal: int, name: str
    ) -> GdlRuleVariableType:
        key = (form_ordinal, name)
        existing = rule_variable_by_key.get(key)
        if existing is not None:
            return existing
        slot = GdlRuleVariableType(form_ordinal, name)
        rule_variable_by_key[key] = slot
        rule_variable_types.append(slot)
        return slot

    def add_application(
        source: GdlSourceOccurrence,
        name: str,
        arguments: tuple[GdlOccurrenceType, ...],
        result: GdlOccurrenceType,
    ) -> None:
        candidates = tuple(by_name_arity.get((name, len(arguments)), ()))
        schemes = {
            (candidate.argument_types, candidate.result_type)
            for candidate in candidates
        }
        structural_signature = STRUCTURAL_GDL_SIGNATURES.get(name)
        if (
            structural_signature is not None
            and len(structural_signature[0]) != len(arguments)
        ):
            structural_signature = None
        derived_signature: tuple[GdlDerivedSignatureType, ...] = ()
        structural_conflict = bool(
            structural_signature is not None
            and schemes
            and schemes != {structural_signature}
        )
        if structural_conflict or len(schemes) > 1:
            kind = GdlApplicationEvidenceKind.AUTHORED_AMBIGUOUS
        elif len(schemes) == 1:
            kind = GdlApplicationEvidenceKind.AUTHORED
            argument_types, result_type = next(iter(schemes))
            equalities.append(
                GdlTypeEqualityConstraint(
                    left=result,
                    right=GdlKnownType(result_type),
                    source=source,
                    reason=GdlConstraintReason.AUTHORED_RESULT,
                )
            )
            for argument, expected in zip(arguments, argument_types):
                acceptances.append(
                    GdlTypeAcceptanceConstraint(
                        actual=argument,
                        expected=GdlKnownType(expected),
                        source=argument.source,
                        reason=GdlConstraintReason.AUTHORED_ARGUMENT,
                    )
                )
        elif structural_signature is not None:
            kind = GdlApplicationEvidenceKind.STRUCTURAL
            argument_types, result_type = structural_signature
            equalities.append(
                GdlTypeEqualityConstraint(
                    left=result,
                    right=GdlKnownType(result_type),
                    source=source,
                    reason=GdlConstraintReason.STRUCTURAL_RESULT,
                )
            )
            for argument, expected in zip(arguments, argument_types):
                acceptances.append(
                    GdlTypeAcceptanceConstraint(
                        actual=argument,
                        expected=GdlKnownType(expected),
                        source=argument.source,
                        reason=GdlConstraintReason.STRUCTURAL_ARGUMENT,
                    )
                )
        else:
            kind = (
                GdlApplicationEvidenceKind.PROFILE_ARITY_MISMATCH
                if name in by_name
                else GdlApplicationEvidenceKind.PROFILE_MISSING
            )
            argument_signature = tuple(
                GdlDerivedSignatureType(name, len(arguments), index)
                for index in range(len(arguments))
            )
            result_signature = GdlDerivedSignatureType(
                name, len(arguments), None
            )
            derived_signature = argument_signature + (result_signature,)
            equalities.append(
                GdlTypeEqualityConstraint(
                    left=result,
                    right=result_signature,
                    source=source,
                    reason=GdlConstraintReason.DERIVED_RESULT,
                )
            )
            for argument, expected in zip(arguments, argument_signature):
                acceptances.append(
                    GdlTypeAcceptanceConstraint(
                        actual=argument,
                        expected=expected,
                        source=argument.source,
                        reason=GdlConstraintReason.DERIVED_ARGUMENT,
                    )
                )
        applications.append(
            GdlApplicationTypingEvidence(
                source=source,
                name=name,
                arguments=arguments,
                result=result,
                kind=kind,
                authored_candidates=candidates,
                structural_signature=structural_signature,
                derived_signature=derived_signature,
            )
        )

    def visit_term(
        form_ordinal: int,
        form: GdlFormOccurrence,
        path: tuple[int, ...],
        term: Any,
    ) -> GdlOccurrenceType:
        source = source_at(form_ordinal, form, path)
        result = occurrence_type(source)
        if isinstance(term, str):
            if term.startswith("?"):
                variable = rule_variable_type(form_ordinal, term)
                equalities.append(
                    GdlTypeEqualityConstraint(
                        left=result,
                        right=variable,
                        source=source,
                        reason=GdlConstraintReason.RULE_VARIABLE,
                    )
                )
            else:
                add_application(source, term, (), result)
            return result

        if not isinstance(term, tuple) or not term:
            unsupported.append(
                GdlUnsupportedTypingShape(source, "empty or non-GDL term")
            )
            return result

        head = term[0]
        arguments = tuple(
            visit_term(form_ordinal, form, path + (index,), argument)
            for index, argument in enumerate(term[1:], 1)
        )
        if (
            not isinstance(head, str)
            or head.startswith("?")
            or head in {"<=", "not", "or", "distinct"}
        ):
            unsupported.append(
                GdlUnsupportedTypingShape(
                    source, "dynamic or structural head in term position"
                )
            )
            return result
        add_application(source, head, arguments, result)
        return result

    def visit_literal(
        form_ordinal: int,
        form: GdlFormOccurrence,
        path: tuple[int, ...],
        literal: Any,
    ) -> GdlOccurrenceType:
        source = source_at(form_ordinal, form, path)
        if isinstance(literal, tuple) and literal:
            operator = literal[0]
            if operator in {"not", "or", "distinct"}:
                result = occurrence_type(source)
                if operator == "not":
                    if len(literal) != 2:
                        unsupported.append(
                            GdlUnsupportedTypingShape(
                                source, "not does not have one operand"
                            )
                        )
                    operands = tuple(
                        visit_literal(
                            form_ordinal,
                            form,
                            path + (index,),
                            operand,
                        )
                        for index, operand in enumerate(literal[1:], 1)
                    )
                elif operator == "or":
                    if len(literal) < 2:
                        unsupported.append(
                            GdlUnsupportedTypingShape(
                                source, "or has no alternatives"
                            )
                        )
                    operands = tuple(
                        visit_literal(
                            form_ordinal,
                            form,
                            path + (index,),
                            operand,
                        )
                        for index, operand in enumerate(literal[1:], 1)
                    )
                else:
                    if len(literal) != 3:
                        unsupported.append(
                            GdlUnsupportedTypingShape(
                                source, "distinct does not have two terms"
                            )
                        )
                    operands = tuple(
                        visit_term(
                            form_ordinal,
                            form,
                            path + (index,),
                            operand,
                        )
                        for index, operand in enumerate(literal[1:], 1)
                    )
                equalities.append(
                    GdlTypeEqualityConstraint(
                        left=result,
                        right=GdlKnownType("bool"),
                        source=source,
                        reason=GdlConstraintReason.CONNECTIVE_RESULT,
                    )
                )
                logical_forms.append(
                    GdlLogicalTypingEvidence(
                        source=source,
                        operator=operator,
                        operands=operands,
                        result=result,
                    )
                )
                return result

        result = visit_term(form_ordinal, form, path, literal)
        acceptances.append(
            GdlTypeAcceptanceConstraint(
                actual=result,
                expected=GdlKnownType("bool"),
                source=source,
                reason=GdlConstraintReason.LITERAL_RESULT,
            )
        )
        return result

    if form_ordinals is not None and any(
        ordinal < 0 or ordinal >= len(presentation.forms)
        for ordinal in form_ordinals
    ):
        raise PresentationError(
            "typing selection names a source form outside the presentation"
        )

    for form_ordinal, form in enumerate(presentation.forms):
        if form_ordinals is not None and form_ordinal not in form_ordinals:
            continue
        if form.form[0] == "<=":
            if len(form.form) < 2:
                unsupported.append(
                    GdlUnsupportedTypingShape(
                        source_at(form_ordinal, form, ()),
                        "rule has no conclusion",
                    )
                )
                continue
            visit_literal(form_ordinal, form, (1,), form.form[1])
            for index, literal in enumerate(form.form[2:], 2):
                visit_literal(form_ordinal, form, (index,), literal)
        else:
            visit_literal(form_ordinal, form, (), form.form)

    return GdlTypingConstraintPresentation(
        signature_occurrences=signatures,
        occurrence_types=tuple(occurrence_types),
        rule_variable_types=tuple(rule_variable_types),
        applications=tuple(applications),
        logical_forms=tuple(logical_forms),
        equalities=tuple(equalities),
        acceptances=tuple(acceptances),
        unsupported=tuple(unsupported),
    )


def inventory_gdl_typing_constraints(
    constraints: GdlTypingConstraintPresentation,
) -> GdlTypingConstraintInventory:
    """Count constraint evidence without interpreting it as a verdict."""

    kinds = [application.kind for application in constraints.applications]
    derived_signatures = {
        (application.name, application.arity)
        for application in constraints.applications
        if application.derived_signature
    }
    return GdlTypingConstraintInventory(
        occurrence_types=len(constraints.occurrence_types),
        rule_variable_types=len(constraints.rule_variable_types),
        application_occurrences=len(constraints.applications),
        authored_applications=kinds.count(
            GdlApplicationEvidenceKind.AUTHORED
        ),
        structural_applications=kinds.count(
            GdlApplicationEvidenceKind.STRUCTURAL
        ),
        missing_profile_applications=kinds.count(
            GdlApplicationEvidenceKind.PROFILE_MISSING
        ),
        arity_mismatch_applications=kinds.count(
            GdlApplicationEvidenceKind.PROFILE_ARITY_MISMATCH
        ),
        ambiguous_authored_applications=kinds.count(
            GdlApplicationEvidenceKind.AUTHORED_AMBIGUOUS
        ),
        derived_signatures=len(derived_signatures),
        logical_forms=len(constraints.logical_forms),
        distinct_forms=sum(
            form.operator == "distinct" for form in constraints.logical_forms
        ),
        equality_constraints=len(constraints.equalities),
        acceptance_constraints=len(constraints.acceptances),
        unsupported_shapes=len(constraints.unsupported),
    )


def _gdl_subtype_reachability(
    profile: GdlTypeProfile,
    extra_names: set[str],
) -> dict[str, set[str]]:
    names = set(extra_names)
    for signature in profile.signatures:
        names.update(signature.argument_types)
        names.add(signature.result_type)
    for subtype in profile.subtypes:
        names.add(subtype.subtype)
        names.add(subtype.supertype)
    reachable = {name: {name} for name in names}
    for subtype in profile.subtypes:
        reachable[subtype.subtype].add(subtype.supertype)
    changed = True
    while changed:
        changed = False
        for name in sorted(reachable):
            expanded = set().union(
                *(reachable.get(parent, {parent}) for parent in reachable[name])
            )
            if not expanded <= reachable[name]:
                reachable[name].update(expanded)
                changed = True
    return reachable


def gdl_derived_signature_supports(
    constraints: GdlTypingConstraintPresentation,
    profile: GdlTypeProfile,
) -> tuple[GdlDerivedSignatureSupport, ...]:
    """Find known-type anchors for every derived signature question.

    Equality and acceptance constraints are traversed in both directions only
    to expose connectivity.  The returned paths are not coercions, unifiers,
    declarations, or typing derivations.  Authored subtype edges are used only
    to report whether multiple anchors are ordered or incomparable.
    """

    adjacency: dict[
        GdlTypeExpression,
        list[tuple[GdlTypeExpression, GdlConstraintPathStep]],
    ] = {}

    def add_edge(
        source_type: GdlTypeExpression,
        target_type: GdlTypeExpression,
        source: GdlSourceOccurrence,
        reason: GdlConstraintReason,
        traversal: GdlConstraintTraversal,
    ) -> None:
        adjacency.setdefault(source_type, []).append(
            (
                target_type,
                GdlConstraintPathStep(
                    source=source,
                    reason=reason,
                    traversal=traversal,
                    from_type=source_type,
                    to_type=target_type,
                ),
            )
        )

    for equality in constraints.equalities:
        add_edge(
            equality.left,
            equality.right,
            equality.source,
            equality.reason,
            GdlConstraintTraversal.EQUALITY,
        )
        add_edge(
            equality.right,
            equality.left,
            equality.source,
            equality.reason,
            GdlConstraintTraversal.EQUALITY,
        )
    for acceptance in constraints.acceptances:
        add_edge(
            acceptance.actual,
            acceptance.expected,
            acceptance.source,
            acceptance.reason,
            GdlConstraintTraversal.ACCEPTANCE_FORWARD,
        )
        add_edge(
            acceptance.expected,
            acceptance.actual,
            acceptance.source,
            acceptance.reason,
            GdlConstraintTraversal.ACCEPTANCE_REVERSE,
        )

    slots = {
        slot
        for application in constraints.applications
        for slot in application.derived_signature
    }
    known_names = {
        expression.name
        for expression in adjacency
        if isinstance(expression, GdlKnownType)
    }
    subtype_reachability = _gdl_subtype_reachability(
        profile, known_names
    )

    def slot_key(slot: GdlDerivedSignatureType) -> tuple[str, int, int]:
        position = slot.arity if slot.argument is None else slot.argument
        return slot.name, slot.arity, position

    supports: list[GdlDerivedSignatureSupport] = []
    for slot in sorted(slots, key=slot_key):
        queue = deque([(slot, ())])
        visited: set[GdlTypeExpression] = {slot}
        anchors: dict[str, GdlKnownTypeAnchor] = {}
        while queue:
            current, path = queue.popleft()
            if isinstance(current, GdlKnownType):
                anchors.setdefault(
                    current.name, GdlKnownTypeAnchor(current.name, path)
                )
                # A type name is evidence reached by the constraint graph,
                # not a global junction between every use of that name.
                continue
            for neighbor, step in adjacency.get(current, ()):  # pragma: no branch
                if neighbor in visited:
                    continue
                visited.add(neighbor)
                queue.append((neighbor, path + (step,)))

        anchor_names = tuple(sorted(anchors))
        incomparable: list[tuple[str, str]] = []
        for first_index, first in enumerate(anchor_names):
            for second in anchor_names[first_index + 1 :]:
                if (
                    second not in subtype_reachability.get(first, {first})
                    and first
                    not in subtype_reachability.get(second, {second})
                ):
                    incomparable.append((first, second))
        supports.append(
            GdlDerivedSignatureSupport(
                slot=slot,
                anchors=tuple(anchors[name] for name in anchor_names),
                incomparable_anchor_pairs=tuple(incomparable),
            )
        )
    return tuple(supports)


def inventory_gdl_derived_supports(
    supports: tuple[GdlDerivedSignatureSupport, ...],
) -> GdlDerivedSupportInventory:
    """Classify connectivity without upgrading it to type inference."""

    unanchored = sum(not support.anchors for support in supports)
    single = sum(len(support.anchors) == 1 for support in supports)
    incomparable = sum(
        bool(support.incomparable_anchor_pairs) for support in supports
    )
    comparable_multi = sum(
        len(support.anchors) > 1
        and not support.incomparable_anchor_pairs
        for support in supports
    )
    signatures = group_gdl_derived_signature_supports(supports)
    return GdlDerivedSupportInventory(
        signatures=len(signatures),
        single_anchor_signatures=sum(
            signature.kind == GdlDerivedSignatureSupportKind.SINGLE_ANCHOR
            for signature in signatures
        ),
        subtype_ordered_signatures=sum(
            signature.kind == GdlDerivedSignatureSupportKind.SUBTYPE_ORDERED
            for signature in signatures
        ),
        unanchored_signatures=sum(
            signature.kind == GdlDerivedSignatureSupportKind.UNANCHORED
            for signature in signatures
        ),
        incomparable_signatures=sum(
            signature.kind == GdlDerivedSignatureSupportKind.INCOMPARABLE
            for signature in signatures
        ),
        slots=len(supports),
        unanchored_slots=unanchored,
        single_anchor_slots=single,
        comparable_multi_anchor_slots=comparable_multi,
        incomparable_anchor_slots=incomparable,
    )


def group_gdl_derived_signature_supports(
    supports: tuple[GdlDerivedSignatureSupport, ...],
) -> tuple[GdlDerivedSignatureEvidence, ...]:
    """Group slot evidence without choosing a type for any slot."""

    grouped: dict[
        tuple[str, int], list[GdlDerivedSignatureSupport]
    ] = {}
    for support in supports:
        grouped.setdefault(
            (support.slot.name, support.slot.arity), []
        ).append(support)
    signatures: list[GdlDerivedSignatureEvidence] = []
    for (name, arity), slots in sorted(grouped.items()):
        if any(not support.anchors for support in slots):
            kind = GdlDerivedSignatureSupportKind.UNANCHORED
        elif any(
            support.incomparable_anchor_pairs for support in slots
        ):
            kind = GdlDerivedSignatureSupportKind.INCOMPARABLE
        elif any(len(support.anchors) > 1 for support in slots):
            kind = GdlDerivedSignatureSupportKind.SUBTYPE_ORDERED
        else:
            kind = GdlDerivedSignatureSupportKind.SINGLE_ANCHOR
        signatures.append(
            GdlDerivedSignatureEvidence(
                name=name,
                arity=arity,
                slots=tuple(slots),
                kind=kind,
            )
        )
    return tuple(signatures)


def _gdl_type_expression_key(
    expression: GdlTypeExpression,
) -> tuple[Any, ...]:
    if isinstance(expression, GdlOccurrenceType):
        source = expression.source
        return (
            0,
            source.form_ordinal,
            source.start_line,
            source.end_line,
            source.path,
        )
    if isinstance(expression, GdlRuleVariableType):
        return 1, expression.form_ordinal, expression.name
    if isinstance(expression, GdlDerivedSignatureType):
        position = (
            expression.arity
            if expression.argument is None
            else expression.argument
        )
        return 2, expression.name, expression.arity, position
    return 3, expression.name


def gdl_finite_type_universe(
    profile: GdlTypeProfile,
    constraints: GdlTypingConstraintPresentation | None = None,
) -> GdlFiniteTypeUniverse:
    """Build the finite existing-name preorder with authored path evidence."""

    names: set[str] = set()
    for argument_types, result_type in STRUCTURAL_GDL_SIGNATURES.values():
        names.update(argument_types)
        names.add(result_type)
    for signature in profile.signatures:
        names.update(signature.argument_types)
        names.add(signature.result_type)
    for subtype in profile.subtypes:
        names.add(subtype.subtype)
        names.add(subtype.supertype)
    if constraints is not None:
        for equality in constraints.equalities:
            for expression in (equality.left, equality.right):
                if isinstance(expression, GdlKnownType):
                    names.add(expression.name)
        for acceptance in constraints.acceptances:
            for expression in (acceptance.actual, acceptance.expected):
                if isinstance(expression, GdlKnownType):
                    names.add(expression.name)

    outgoing: dict[str, list[GdlSubtypeStatement]] = {}
    for subtype in profile.subtypes:
        outgoing.setdefault(subtype.subtype, []).append(subtype)

    paths: list[GdlSubtypePath] = []
    for actual_type in sorted(names):
        discovered: dict[str, tuple[GdlSubtypeStatement, ...]] = {
            actual_type: ()
        }
        queue = deque([actual_type])
        while queue:
            current = queue.popleft()
            for edge in outgoing.get(current, ()):
                if edge.supertype in discovered:
                    continue
                discovered[edge.supertype] = (
                    discovered[current] + (edge,)
                )
                queue.append(edge.supertype)
        paths.extend(
            GdlSubtypePath(actual_type, expected_type, path)
            for expected_type, path in sorted(discovered.items())
        )
    return GdlFiniteTypeUniverse(
        type_names=tuple(sorted(names)),
        authored_subtype_edges=profile.subtypes,
        acceptance_paths=tuple(paths),
    )


def analyze_gdl_existing_type_domains(
    constraints: GdlTypingConstraintPresentation,
    profile: GdlTypeProfile,
) -> GdlExistingTypeArcAnalysis:
    """Compute necessary type candidates inside the existing finite profile.

    Exact equalities collapse unknown type expressions.  A known type remains
    an immutable boundary label rather than a shared constraint variable; one
    malformed occurrence therefore cannot erase unrelated uses of that name.
    Directed acceptance constraints are reduced to arc consistency over the
    authored subtype preorder.  Candidate removal is sound, but surviving
    candidates are not advertised as a global solution or checked extension.
    """

    universe = gdl_finite_type_universe(profile, constraints)
    accepted = {
        (path.actual_type, path.expected_type)
        for path in universe.acceptance_paths
    }
    variables = {
        expression
        for equality in constraints.equalities
        for expression in (equality.left, equality.right)
        if not isinstance(expression, GdlKnownType)
    }
    variables.update(
        expression
        for acceptance in constraints.acceptances
        for expression in (acceptance.actual, acceptance.expected)
        if not isinstance(expression, GdlKnownType)
    )
    parent = {expression: expression for expression in variables}

    def find(expression: GdlTypeExpression) -> GdlTypeExpression:
        root = expression
        while parent[root] != root:
            root = parent[root]
        while parent[expression] != expression:
            next_expression = parent[expression]
            parent[expression] = root
            expression = next_expression
        return root

    def union(
        left: GdlTypeExpression, right: GdlTypeExpression
    ) -> None:
        left_root = find(left)
        right_root = find(right)
        if left_root == right_root:
            return
        if _gdl_type_expression_key(right_root) < _gdl_type_expression_key(
            left_root
        ):
            left_root, right_root = right_root, left_root
        parent[right_root] = left_root

    for equality in constraints.equalities:
        if isinstance(equality.left, GdlKnownType) or isinstance(
            equality.right, GdlKnownType
        ):
            continue
        union(equality.left, equality.right)

    members_by_root: dict[
        GdlTypeExpression, list[GdlTypeExpression]
    ] = {}
    for expression in variables:
        members_by_root.setdefault(find(expression), []).append(expression)
    ordered_groups = sorted(
        (
            tuple(sorted(members, key=_gdl_type_expression_key))
            for members in members_by_root.values()
        ),
        key=lambda members: _gdl_type_expression_key(members[0]),
    )
    component_by_expression = {
        expression: ordinal
        for ordinal, members in enumerate(ordered_groups)
        for expression in members
    }
    exact_anchors: list[set[str]] = [set() for _ in ordered_groups]
    equality_evidence: list[list[GdlTypeEqualityConstraint]] = [
        [] for _ in ordered_groups
    ]
    known_equality_conflicts: list[GdlKnownEqualityConflict] = []
    for equality in constraints.equalities:
        left_known = isinstance(equality.left, GdlKnownType)
        right_known = isinstance(equality.right, GdlKnownType)
        if left_known and right_known:
            if equality.left.name != equality.right.name:
                known_equality_conflicts.append(
                    GdlKnownEqualityConflict(equality)
                )
            continue
        variable = equality.right if left_known else equality.left
        ordinal = component_by_expression[variable]
        equality_evidence[ordinal].append(equality)
        if left_known:
            exact_anchors[ordinal].add(equality.left.name)
        elif right_known:
            exact_anchors[ordinal].add(equality.right.name)

    all_candidates = set(universe.type_names)
    domains: list[set[str]] = []
    for anchors in exact_anchors:
        if not anchors:
            domains.append(set(all_candidates))
        elif len(anchors) == 1:
            domains.append(set(anchors))
        else:
            domains.append(set())
    eliminations: list[list[GdlTypeCandidateElimination]] = [
        [] for _ in ordered_groups
    ]

    def endpoint_domain(expression: GdlTypeExpression) -> set[str]:
        if isinstance(expression, GdlKnownType):
            return {expression.name}
        return domains[component_by_expression[expression]]

    def supported(
        actual_candidates: set[str], expected_candidates: set[str]
    ) -> bool:
        return any(
            (actual, expected) in accepted
            for actual in actual_candidates
            for expected in expected_candidates
        )

    iterations = 0
    changed = True
    while changed:
        changed = False
        iterations += 1
        for acceptance in constraints.acceptances:
            actual_known = isinstance(acceptance.actual, GdlKnownType)
            expected_known = isinstance(acceptance.expected, GdlKnownType)
            actual_ordinal = (
                None
                if actual_known
                else component_by_expression[acceptance.actual]
            )
            expected_ordinal = (
                None
                if expected_known
                else component_by_expression[acceptance.expected]
            )
            if (
                actual_ordinal is not None
                and actual_ordinal == expected_ordinal
            ):
                continue
            actual_candidates = set(endpoint_domain(acceptance.actual))
            expected_candidates = set(endpoint_domain(acceptance.expected))
            retained_actual = {
                actual
                for actual in actual_candidates
                if supported({actual}, expected_candidates)
            }
            retained_expected = {
                expected
                for expected in expected_candidates
                if supported(actual_candidates, {expected})
            }
            if (
                actual_ordinal is not None
                and retained_actual != actual_candidates
            ):
                for candidate in sorted(
                    actual_candidates - retained_actual
                ):
                    eliminations[actual_ordinal].append(
                        GdlTypeCandidateElimination(
                            candidate_type=candidate,
                            side=GdlDomainEliminationSide.ACTUAL,
                            constraint=acceptance,
                            opposing_candidates=tuple(
                                sorted(expected_candidates)
                            ),
                            iteration=iterations,
                        )
                    )
                domains[actual_ordinal] = retained_actual
                changed = True
            if (
                expected_ordinal is not None
                and retained_expected != expected_candidates
            ):
                for candidate in sorted(
                    expected_candidates - retained_expected
                ):
                    eliminations[expected_ordinal].append(
                        GdlTypeCandidateElimination(
                            candidate_type=candidate,
                            side=GdlDomainEliminationSide.EXPECTED,
                            constraint=acceptance,
                            opposing_candidates=tuple(
                                sorted(actual_candidates)
                            ),
                            iteration=iterations,
                        )
                    )
                domains[expected_ordinal] = retained_expected
                changed = True

    known_acceptance_conflicts = tuple(
        GdlKnownAcceptanceConflict(acceptance)
        for acceptance in constraints.acceptances
        if isinstance(acceptance.actual, GdlKnownType)
        and isinstance(acceptance.expected, GdlKnownType)
        and not supported(
            {acceptance.actual.name}, {acceptance.expected.name}
        )
    )
    components = tuple(
        GdlTypeDomainComponent(
            ordinal=ordinal,
            members=members,
            exact_type_anchors=tuple(sorted(exact_anchors[ordinal])),
            equality_evidence=tuple(equality_evidence[ordinal]),
            candidate_types=tuple(sorted(domains[ordinal])),
            eliminations=tuple(eliminations[ordinal]),
        )
        for ordinal, members in enumerate(ordered_groups)
    )
    slots = {
        slot
        for application in constraints.applications
        for slot in application.derived_signature
    }

    def slot_key(slot: GdlDerivedSignatureType) -> tuple[str, int, int]:
        position = slot.arity if slot.argument is None else slot.argument
        return slot.name, slot.arity, position

    derived_domains: list[GdlDerivedSignatureDomain] = []
    for slot in sorted(slots, key=slot_key):
        ordinal = component_by_expression[slot]
        candidates = tuple(sorted(domains[ordinal]))
        kind = (
            GdlDerivedDomainKind.EMPTY
            if not candidates
            else GdlDerivedDomainKind.SINGLETON
            if len(candidates) == 1
            else GdlDerivedDomainKind.MULTIPLE
        )
        derived_domains.append(
            GdlDerivedSignatureDomain(
                slot=slot,
                component_ordinal=ordinal,
                candidate_types=candidates,
                kind=kind,
            )
        )
    return GdlExistingTypeArcAnalysis(
        universe=universe,
        components=components,
        derived_domains=tuple(derived_domains),
        known_equality_conflicts=tuple(known_equality_conflicts),
        known_acceptance_conflicts=known_acceptance_conflicts,
        iterations=iterations,
    )


def _gdl_component_by_expression(
    analysis: GdlExistingTypeArcAnalysis,
) -> dict[GdlTypeExpression, int]:
    return {
        expression: component.ordinal
        for component in analysis.components
        for expression in component.members
    }


def replay_gdl_finite_type_assignment(
    constraints: GdlTypingConstraintPresentation,
    analysis: GdlExistingTypeArcAnalysis,
    assignment: GdlFiniteTypeAssignment,
) -> GdlCheckedFiniteTypeAssignment:
    """Replay a total finite assignment against the raw extracted evidence.

    Success retains every equality and the concrete authored subtype path for
    every directed acceptance.  Failure raises ``PresentationError`` rather
    than silently repairing a partial or incompatible proposal.
    """

    components = {
        component.ordinal: component for component in analysis.components
    }
    choices: dict[int, str] = {}
    for choice in assignment.choices:
        if choice.component_ordinal in choices:
            raise PresentationError(
                "finite type assignment repeats component "
                f"{choice.component_ordinal}"
            )
        component = components.get(choice.component_ordinal)
        if component is None:
            raise PresentationError(
                "finite type assignment names unknown component "
                f"{choice.component_ordinal}"
            )
        if choice.type_name not in component.candidate_types:
            raise PresentationError(
                f"finite type assignment chooses {choice.type_name!r} "
                f"outside component {choice.component_ordinal}'s domain"
            )
        choices[choice.component_ordinal] = choice.type_name
    missing = tuple(sorted(set(components) - set(choices)))
    if missing:
        raise PresentationError(
            "finite type assignment omits components "
            + ", ".join(str(ordinal) for ordinal in missing)
        )

    component_by_expression = _gdl_component_by_expression(analysis)

    def assigned_type(expression: GdlTypeExpression) -> str:
        if isinstance(expression, GdlKnownType):
            return expression.name
        ordinal = component_by_expression.get(expression)
        if ordinal is None:
            raise PresentationError(
                "finite type analysis does not contain an extracted "
                "expression"
            )
        return choices[ordinal]

    equality_discharges: list[GdlFiniteEqualityDischarge] = []
    for equality in constraints.equalities:
        left_type = assigned_type(equality.left)
        right_type = assigned_type(equality.right)
        if left_type != right_type:
            raise PresentationError(
                "finite type assignment violates an exact equality at "
                f"line {equality.source.start_line}"
            )
        equality_discharges.append(
            GdlFiniteEqualityDischarge(equality, left_type)
        )

    acceptance_discharges: list[GdlFiniteAcceptanceDischarge] = []
    for acceptance in constraints.acceptances:
        actual_type = assigned_type(acceptance.actual)
        expected_type = assigned_type(acceptance.expected)
        path = analysis.universe.acceptance_path(
            actual_type, expected_type
        )
        if path is None:
            raise PresentationError(
                "finite type assignment has no authored acceptance path at "
                f"line {acceptance.source.start_line}: "
                f"{actual_type} is not accepted as {expected_type}"
            )
        acceptance_discharges.append(
            GdlFiniteAcceptanceDischarge(acceptance, path)
        )

    normalized_assignment = GdlFiniteTypeAssignment(
        tuple(
            GdlFiniteTypeChoice(ordinal, choices[ordinal])
            for ordinal in sorted(components)
        )
    )
    return GdlCheckedFiniteTypeAssignment(
        assignment=normalized_assignment,
        equalities=tuple(equality_discharges),
        acceptances=tuple(acceptance_discharges),
    )


def find_gdl_finite_type_assignment(
    constraints: GdlTypingConstraintPresentation,
    analysis: GdlExistingTypeArcAnalysis,
    required_choices: tuple[GdlFiniteTypeChoice, ...] = (),
) -> GdlCheckedFiniteTypeAssignment | None:
    """Find one replay-checked completion of the exact finite problem.

    The search is exhaustive but returns only an existence witness.  Alternate
    completions remain available by supplying different ``required_choices``;
    no returned witness is promoted into an authored or preferred profile.
    Independent constraint components are solved separately so their product
    is preserved intensionally instead of being eagerly enumerated.
    """

    component_by_expression = _gdl_component_by_expression(analysis)
    domains = {
        component.ordinal: set(component.candidate_types)
        for component in analysis.components
    }
    if (
        analysis.known_equality_conflicts
        or analysis.known_acceptance_conflicts
        or any(not domain for domain in domains.values())
    ):
        return None

    seen_required: set[int] = set()
    for choice in required_choices:
        if choice.component_ordinal in seen_required:
            raise PresentationError(
                "required finite type choices repeat component "
                f"{choice.component_ordinal}"
            )
        seen_required.add(choice.component_ordinal)
        domain = domains.get(choice.component_ordinal)
        if domain is None:
            raise PresentationError(
                "required finite type choice names unknown component "
                f"{choice.component_ordinal}"
            )
        if choice.type_name not in domain:
            return None
        domains[choice.component_ordinal] = {choice.type_name}

    def expression_domain(
        expression: GdlTypeExpression,
        current: dict[int, set[str]],
    ) -> set[str]:
        if isinstance(expression, GdlKnownType):
            return {expression.name}
        ordinal = component_by_expression.get(expression)
        if ordinal is None:
            raise PresentationError(
                "finite type analysis does not contain an extracted "
                "expression"
            )
        return current[ordinal]

    def reduce_domains(
        current: dict[int, set[str]],
        acceptances: tuple[GdlTypeAcceptanceConstraint, ...],
    ) -> bool:
        changed = True
        while changed:
            changed = False
            for acceptance in acceptances:
                actual_domain = set(
                    expression_domain(acceptance.actual, current)
                )
                expected_domain = set(
                    expression_domain(acceptance.expected, current)
                )
                retained_actual = {
                    actual
                    for actual in actual_domain
                    if any(
                        analysis.universe.accepts(actual, expected)
                        for expected in expected_domain
                    )
                }
                retained_expected = {
                    expected
                    for expected in expected_domain
                    if any(
                        analysis.universe.accepts(actual, expected)
                        for actual in actual_domain
                    )
                }
                if not retained_actual or not retained_expected:
                    return False
                if not isinstance(acceptance.actual, GdlKnownType):
                    ordinal = component_by_expression[acceptance.actual]
                    if retained_actual != current[ordinal]:
                        current[ordinal] = retained_actual
                        changed = True
                elif retained_actual != actual_domain:
                    return False
                if not isinstance(acceptance.expected, GdlKnownType):
                    ordinal = component_by_expression[acceptance.expected]
                    if retained_expected != current[ordinal]:
                        current[ordinal] = retained_expected
                        changed = True
                elif retained_expected != expected_domain:
                    return False
        return True

    if not reduce_domains(domains, constraints.acceptances):
        return None

    adjacency = {ordinal: set() for ordinal in domains}
    incident: dict[int, list[GdlTypeAcceptanceConstraint]] = {
        ordinal: [] for ordinal in domains
    }
    for acceptance in constraints.acceptances:
        ordinals = {
            component_by_expression[expression]
            for expression in (acceptance.actual, acceptance.expected)
            if not isinstance(expression, GdlKnownType)
        }
        for ordinal in ordinals:
            incident[ordinal].append(acceptance)
        if len(ordinals) == 2:
            left, right = tuple(ordinals)
            adjacency[left].add(right)
            adjacency[right].add(left)

    seen: set[int] = set()
    factors: list[tuple[int, ...]] = []
    for start in sorted(adjacency):
        if start in seen:
            continue
        pending = [start]
        seen.add(start)
        factor: list[int] = []
        while pending:
            ordinal = pending.pop()
            factor.append(ordinal)
            for neighbour in sorted(adjacency[ordinal], reverse=True):
                if neighbour not in seen:
                    seen.add(neighbour)
                    pending.append(neighbour)
        factors.append(tuple(sorted(factor)))

    for factor in factors:
        if len(factor) == 1 and not adjacency[factor[0]]:
            ordinal = factor[0]
            domains[ordinal] = {min(domains[ordinal])}
            continue
        factor_set = set(factor)
        factor_acceptances = tuple(
            dict.fromkeys(
                acceptance
                for ordinal in factor
                for acceptance in incident[ordinal]
            )
        )
        unsatisfiable_states: set[
            tuple[tuple[int, tuple[str, ...]], ...]
        ] = set()

        def solve(
            current: dict[int, set[str]],
        ) -> dict[int, set[str]] | None:
            if not reduce_domains(current, factor_acceptances):
                return None
            state = tuple(
                (ordinal, tuple(sorted(current[ordinal])))
                for ordinal in factor
            )
            if state in unsatisfiable_states:
                return None
            undecided = [
                ordinal
                for ordinal in factor
                if len(current[ordinal]) > 1
            ]
            if not undecided:
                return current
            selected = min(
                undecided,
                key=lambda ordinal: (
                    len(current[ordinal]),
                    -len(adjacency[ordinal] & factor_set),
                    ordinal,
                ),
            )
            for candidate in sorted(current[selected]):
                branch = {
                    ordinal: set(current[ordinal]) for ordinal in factor
                }
                branch[selected] = {candidate}
                solution = solve(branch)
                if solution is not None:
                    return solution
            unsatisfiable_states.add(state)
            return None

        local_domains = {
            ordinal: set(domains[ordinal]) for ordinal in factor
        }
        solution = solve(local_domains)
        if solution is None:
            return None
        for ordinal in factor:
            domains[ordinal] = solution[ordinal]

    assignment = GdlFiniteTypeAssignment(
        tuple(
            GdlFiniteTypeChoice(ordinal, next(iter(domains[ordinal])))
            for ordinal in sorted(domains)
        )
    )
    return replay_gdl_finite_type_assignment(
        constraints, analysis, assignment
    )


def find_gdl_rule_variable_greatest_assignment(
    constraints: GdlTypingConstraintPresentation,
    analysis: GdlExistingTypeArcAnalysis,
) -> GdlCheckedFiniteTypeAssignment | None:
    """Find one coherent completion using every rule variable's greatest type.

    A rule-variable component contributes a choice only when its complete
    locally retained domain has one member accepting every other member.  The
    resulting choices are then solved together and replayed against the whole
    finite problem.  Thus success is a checked common upgrade, while an
    incomparable frontier or an incompatible family of local upgrades remains
    visible as failure rather than being broken by candidate order.

    This selects no derived signature independently.  Those components are
    completed by the same exact search, so the returned witness is one
    coherent optional typing extension rather than a product of local views.
    """

    greatest_choices: list[GdlFiniteTypeChoice] = []
    for component in analysis.components:
        if not any(
            isinstance(member, GdlRuleVariableType)
            for member in component.members
        ):
            continue
        greatest = tuple(
            candidate
            for candidate in component.candidate_types
            if all(
                analysis.universe.accepts(other, candidate)
                for other in component.candidate_types
            )
        )
        if len(greatest) != 1:
            return None
        greatest_choices.append(
            GdlFiniteTypeChoice(component.ordinal, greatest[0])
        )
    return find_gdl_finite_type_assignment(
        constraints, analysis, tuple(greatest_choices)
    )


def project_gdl_derived_finite_completions(
    constraints: GdlTypingConstraintPresentation,
    analysis: GdlExistingTypeArcAnalysis,
) -> GdlFiniteCompletionProjection | None:
    """Project globally supported choices for every derived signature slot.

    Arc consistency proves that removed candidates cannot occur.  This
    stronger projection asks the complete finite search for one replayed
    extension of every survivor.  A game with no total finite assignment has
    no projection; its obstruction evidence remains the appropriate result.
    """

    existence_witness = find_gdl_finite_type_assignment(
        constraints, analysis
    )
    if existence_witness is None:
        return None
    base_choices = {
        choice.component_ordinal: choice.type_name
        for choice in existence_witness.assignment.choices
    }
    completion_cache: dict[
        tuple[int, str], GdlCheckedFiniteTypeAssignment | None
    ] = {}
    projected_domains: list[GdlDerivedFiniteCompletionDomain] = []
    for domain in analysis.derived_domains:
        completions: list[GdlFiniteChoiceCompletion] = []
        unsupported: list[str] = []
        for type_name in domain.candidate_types:
            choice = GdlFiniteTypeChoice(
                domain.component_ordinal, type_name
            )
            cache_key = (choice.component_ordinal, choice.type_name)
            if cache_key not in completion_cache:
                completion_cache[cache_key] = (
                    existence_witness
                    if base_choices[choice.component_ordinal]
                    == choice.type_name
                    else find_gdl_finite_type_assignment(
                        constraints, analysis, (choice,)
                    )
                )
            witness = completion_cache[cache_key]
            if witness is None:
                unsupported.append(type_name)
            else:
                completions.append(
                    GdlFiniteChoiceCompletion(choice, witness)
                )
        projected_domains.append(
            GdlDerivedFiniteCompletionDomain(
                slot=domain.slot,
                component_ordinal=domain.component_ordinal,
                local_candidate_types=domain.candidate_types,
                completions=tuple(completions),
                globally_unsupported_types=tuple(unsupported),
            )
        )
    return GdlFiniteCompletionProjection(
        existence_witness=existence_witness,
        derived_domains=tuple(projected_domains),
    )


def project_gdl_finite_typed_occurrences(
    constraints: GdlTypingConstraintPresentation,
    analysis: GdlExistingTypeArcAnalysis,
    assignment: GdlFiniteTypeAssignment,
) -> GdlFiniteTypedOccurrenceProjection:
    """Resolve the extracted source under one replay-checked assignment.

    All application occurrences and rule-local variables retain their source
    identity through ``resolved_expressions``.  Missing-profile slots are
    packaged only when every argument and result position is present, and the
    equality-component ordinals remain visible so later code cannot assume
    that independently displayed fields are independent choices.
    """

    witness = replay_gdl_finite_type_assignment(
        constraints, analysis, assignment
    )
    choice_by_component = {
        choice.component_ordinal: choice.type_name
        for choice in witness.assignment.choices
    }
    component_by_expression = _gdl_component_by_expression(analysis)

    def resolved_type(expression: GdlTypeExpression) -> str:
        if isinstance(expression, GdlKnownType):
            return expression.name
        ordinal = component_by_expression.get(expression)
        if ordinal is None:
            raise PresentationError(
                "finite typed projection does not contain an extracted "
                "expression"
            )
        return choice_by_component[ordinal]

    resolved_expressions = tuple(
        GdlFiniteResolvedTypeExpression(
            expression, choice_by_component[component.ordinal]
        )
        for component in analysis.components
        for expression in component.members
    )
    applications = tuple(
        GdlFiniteResolvedApplication(
            source=application.source,
            name=application.name,
            kind=application.kind,
            argument_types=tuple(
                resolved_type(argument)
                for argument in application.arguments
            ),
            result_type=resolved_type(application.result),
        )
        for application in constraints.applications
    )

    grouped: dict[
        tuple[str, int], dict[int | None, GdlDerivedSignatureDomain]
    ] = {}
    for domain in analysis.derived_domains:
        key = (domain.slot.name, domain.slot.arity)
        positions = grouped.setdefault(key, {})
        if domain.slot.argument in positions:
            raise PresentationError(
                "finite typed projection repeats a derived-signature slot"
            )
        positions[domain.slot.argument] = domain

    signatures: list[GdlFiniteDerivedSignatureProposal] = []
    for (name, arity), positions in sorted(grouped.items()):
        expected_positions = set(range(arity)) | {None}
        if set(positions) != expected_positions:
            raise PresentationError(
                f"finite typed projection has incomplete signature {name}/"
                f"{arity}"
            )
        arguments = tuple(positions[index] for index in range(arity))
        result = positions[None]
        signatures.append(
            GdlFiniteDerivedSignatureProposal(
                name=name,
                arity=arity,
                argument_types=tuple(
                    choice_by_component[domain.component_ordinal]
                    for domain in arguments
                ),
                result_type=choice_by_component[
                    result.component_ordinal
                ],
                argument_component_ordinals=tuple(
                    domain.component_ordinal for domain in arguments
                ),
                result_component_ordinal=result.component_ordinal,
            )
        )
    return GdlFiniteTypedOccurrenceProjection(
        witness=witness,
        resolved_expressions=resolved_expressions,
        applications=applications,
        derived_signatures=tuple(signatures),
    )


def check_gdl_type_of_extension(
    constraints: GdlTypingConstraintPresentation,
    profile: GdlTypeProfile,
    proposal: GdlFiniteTypedOccurrenceProjection,
) -> GdlCheckedTypeOfExtension:
    """Construct the optional GDL ``type:of`` layer from checked evidence.

    The authored profile and the generic structural rules are replayed at
    every exact source occurrence.  Missing-profile signatures enter only
    through the proposal's complete finite-assignment witness.  Recomputing
    that projection here makes this a checking boundary rather than a cast.

    The result is proof-carrying finite-profile extension data for later
    inference-presentation lowering.  It is not an operational rewrite or a
    NIK admission.  Duplicate authored rules remain distinct derivation
    alternatives, and another complete assignment yields another checked
    extension.
    """

    if constraints.signature_occurrences != gdl_signature_occurrences(profile):
        raise PresentationError(
            "type:of extension profile does not match extracted signatures"
        )
    if constraints.unsupported:
        raise PresentationError(
            "type:of extension has unsupported source typing shapes"
        )

    analysis = analyze_gdl_existing_type_domains(constraints, profile)
    expected_proposal = project_gdl_finite_typed_occurrences(
        constraints, analysis, proposal.witness.assignment
    )
    if proposal != expected_proposal:
        raise PresentationError(
            "type:of extension proposal does not replay from its witness"
        )
    witness = expected_proposal.witness

    if len(witness.equalities) != len(constraints.equalities):
        raise PresentationError(
            "type:of extension does not discharge every equality"
        )
    if len(witness.acceptances) != len(constraints.acceptances):
        raise PresentationError(
            "type:of extension does not discharge every acceptance"
        )
    for constraint, discharge in zip(
        constraints.equalities, witness.equalities
    ):
        if discharge.constraint != constraint:
            raise PresentationError(
                "type:of equality evidence changed occurrence order"
            )
    for constraint, discharge in zip(
        constraints.acceptances, witness.acceptances
    ):
        if discharge.constraint != constraint:
            raise PresentationError(
                "type:of acceptance evidence changed occurrence order"
            )

    resolved: dict[GdlTypeExpression, str] = {}
    for item in expected_proposal.resolved_expressions:
        previous = resolved.get(item.expression)
        if previous is not None and previous != item.type_name:
            raise PresentationError(
                "type:of extension assigns two types to one expression"
            )
        resolved[item.expression] = item.type_name

    def resolved_type(expression: GdlTypeExpression) -> str:
        if isinstance(expression, GdlKnownType):
            return expression.name
        type_name = resolved.get(expression)
        if type_name is None:
            raise PresentationError(
                "type:of extension has an unresolved extracted expression"
            )
        return type_name

    occurrence_judgments = tuple(
        GdlTypeOfJudgment(
            occurrence.source, resolved_type(occurrence)
        )
        for occurrence in constraints.occurrence_types
    )
    judgment_by_source: dict[GdlSourceOccurrence, GdlTypeOfJudgment] = {}
    for judgment in occurrence_judgments:
        if judgment.source in judgment_by_source:
            raise PresentationError(
                "type:of extension repeats a source occurrence"
            )
        judgment_by_source[judgment.source] = judgment

    used_equalities: set[int] = set()
    used_acceptances: set[int] = set()

    def take_equality(
        left: GdlTypeExpression,
        right: GdlTypeExpression,
        source: GdlSourceOccurrence,
        reason: GdlConstraintReason,
    ) -> GdlFiniteEqualityDischarge:
        matches = [
            index
            for index, constraint in enumerate(constraints.equalities)
            if index not in used_equalities
            and constraint.left == left
            and constraint.right == right
            and constraint.source == source
            and constraint.reason == reason
        ]
        if len(matches) != 1:
            raise PresentationError(
                "type:of extension lacks one exact equality discharge"
            )
        index = matches[0]
        used_equalities.add(index)
        return witness.equalities[index]

    def take_acceptance(
        actual: GdlTypeExpression,
        expected: GdlTypeExpression,
        source: GdlSourceOccurrence,
        reason: GdlConstraintReason,
    ) -> GdlFiniteAcceptanceDischarge:
        matches = [
            index
            for index, constraint in enumerate(constraints.acceptances)
            if index not in used_acceptances
            and constraint.actual == actual
            and constraint.expected == expected
            and constraint.source == source
            and constraint.reason == reason
        ]
        if len(matches) != 1:
            raise PresentationError(
                "type:of extension lacks one exact acceptance discharge"
            )
        index = matches[0]
        used_acceptances.add(index)
        return witness.acceptances[index]

    derived_by_key: dict[
        tuple[str, int], GdlFiniteDerivedSignatureProposal
    ] = {}
    for signature in expected_proposal.derived_signatures:
        key = (signature.name, signature.arity)
        if key in derived_by_key:
            raise PresentationError(
                "type:of extension repeats a derived signature"
            )
        derived_by_key[key] = signature

    if len(constraints.applications) != len(expected_proposal.applications):
        raise PresentationError(
            "type:of extension lost an application occurrence"
        )

    application_derivations: list[GdlTypeOfApplicationDerivation] = []
    application_sources: list[GdlSourceOccurrence] = []
    for application, projected in zip(
        constraints.applications, expected_proposal.applications
    ):
        if (
            projected.source != application.source
            or projected.name != application.name
            or projected.kind != application.kind
            or len(projected.argument_types) != application.arity
        ):
            raise PresentationError(
                "type:of extension application projection changed source data"
            )

        origins: tuple[GdlTypeOfRule, ...]
        expected_arguments: tuple[GdlTypeExpression, ...]
        expected_result: GdlTypeExpression
        argument_reason: GdlConstraintReason
        result_reason: GdlConstraintReason
        if application.kind == GdlApplicationEvidenceKind.AUTHORED:
            schemes = {
                (candidate.argument_types, candidate.result_type)
                for candidate in application.authored_candidates
            }
            if len(schemes) != 1 or not application.authored_candidates:
                raise PresentationError(
                    "type:of authored application has no unique signature"
                )
            argument_types, result_type = next(iter(schemes))
            expected_arguments = tuple(
                GdlKnownType(type_name) for type_name in argument_types
            )
            expected_result = GdlKnownType(result_type)
            origins = tuple(
                GdlAuthoredTypeOfRule(candidate)
                for candidate in application.authored_candidates
            )
            argument_reason = GdlConstraintReason.AUTHORED_ARGUMENT
            result_reason = GdlConstraintReason.AUTHORED_RESULT
        elif application.kind == GdlApplicationEvidenceKind.STRUCTURAL:
            if application.structural_signature is None:
                raise PresentationError(
                    "type:of structural application has no structural rule"
                )
            argument_types, result_type = application.structural_signature
            expected_arguments = tuple(
                GdlKnownType(type_name) for type_name in argument_types
            )
            expected_result = GdlKnownType(result_type)
            origins = (
                GdlStructuralTypeOfRule(
                    application.name, argument_types, result_type
                ),
            )
            argument_reason = GdlConstraintReason.STRUCTURAL_ARGUMENT
            result_reason = GdlConstraintReason.STRUCTURAL_RESULT
        elif application.kind in {
            GdlApplicationEvidenceKind.PROFILE_MISSING,
            GdlApplicationEvidenceKind.PROFILE_ARITY_MISMATCH,
        }:
            signature = derived_by_key.get(
                (application.name, application.arity)
            )
            if signature is None:
                raise PresentationError(
                    "type:of extension lacks a complete proposed signature"
                )
            if len(application.derived_signature) != application.arity + 1:
                raise PresentationError(
                    "type:of extension has incomplete derived signature slots"
                )
            expected_arguments = application.derived_signature[:-1]
            expected_result = application.derived_signature[-1]
            origins = (GdlExtendedTypeOfRule(signature),)
            argument_reason = GdlConstraintReason.DERIVED_ARGUMENT
            result_reason = GdlConstraintReason.DERIVED_RESULT
        else:
            raise PresentationError(
                "type:of extension cannot choose an ambiguous authored rule"
            )

        expected_argument_names = tuple(
            resolved_type(expression) for expression in expected_arguments
        )
        expected_result_name = resolved_type(expected_result)
        first_origin = origins[0]
        if isinstance(first_origin, GdlAuthoredTypeOfRule):
            rule_argument_types = first_origin.signature.argument_types
            rule_result_type = first_origin.signature.result_type
        elif isinstance(first_origin, GdlStructuralTypeOfRule):
            rule_argument_types = first_origin.argument_types
            rule_result_type = first_origin.result_type
        else:
            rule_argument_types = first_origin.signature.argument_types
            rule_result_type = first_origin.signature.result_type
        if (
            projected.argument_types
            != tuple(resolved_type(item) for item in application.arguments)
            or projected.result_type != resolved_type(application.result)
            or expected_argument_names != rule_argument_types
            or expected_result_name != rule_result_type
        ):
            raise PresentationError(
                "type:of extension signature does not match its projection"
            )

        result_equality = take_equality(
            application.result,
            expected_result,
            application.source,
            result_reason,
        )
        if result_equality.type_name != projected.result_type:
            raise PresentationError(
                "type:of extension result equality has the wrong type"
            )

        premises: list[GdlTypeOfArgumentPremise] = []
        for argument, expected, expected_name in zip(
            application.arguments,
            expected_arguments,
            expected_argument_names,
        ):
            judgment = judgment_by_source.get(argument.source)
            if judgment is None:
                raise PresentationError(
                    "type:of extension lost an argument judgment"
                )
            acceptance = take_acceptance(
                argument, expected, argument.source, argument_reason
            )
            if (
                acceptance.path.actual_type != judgment.type_name
                or acceptance.path.expected_type != expected_name
            ):
                raise PresentationError(
                    "type:of extension has a mismatched subtype path"
                )
            premises.append(
                GdlTypeOfArgumentPremise(
                    judgment, expected_name, acceptance
                )
            )

        conclusion = judgment_by_source.get(application.source)
        if conclusion is None or conclusion.type_name != projected.result_type:
            raise PresentationError(
                "type:of extension lost an application conclusion"
            )
        application_sources.append(application.source)
        application_derivations.extend(
            GdlTypeOfApplicationDerivation(
                conclusion=conclusion,
                application=application,
                rule=origin,
                premises=tuple(premises),
                result_equality=result_equality,
            )
            for origin in origins
        )

    variable_derivations: list[GdlTypeOfVariableDerivation] = []
    variable_sources: list[GdlSourceOccurrence] = []
    for constraint in constraints.equalities:
        if constraint.reason != GdlConstraintReason.RULE_VARIABLE:
            continue
        if not isinstance(constraint.left, GdlOccurrenceType) or not isinstance(
            constraint.right, GdlRuleVariableType
        ):
            raise PresentationError(
                "type:of variable equality has the wrong shape"
            )
        equality = take_equality(
            constraint.left,
            constraint.right,
            constraint.source,
            constraint.reason,
        )
        conclusion = judgment_by_source.get(constraint.left.source)
        if (
            conclusion is None
            or equality.type_name != conclusion.type_name
            or resolved_type(constraint.right) != conclusion.type_name
        ):
            raise PresentationError(
                "type:of variable occurrence has inconsistent evidence"
            )
        variable_sources.append(constraint.left.source)
        variable_derivations.append(
            GdlTypeOfVariableDerivation(
                conclusion, constraint.right, equality
            )
        )

    logical_derivations: list[GdlTypeOfLogicalDerivation] = []
    logical_sources: list[GdlSourceOccurrence] = []
    for logical in constraints.logical_forms:
        expected_result = GdlKnownType("bool")
        equality = take_equality(
            logical.result,
            expected_result,
            logical.source,
            GdlConstraintReason.CONNECTIVE_RESULT,
        )
        conclusion = judgment_by_source.get(logical.source)
        operands: list[GdlTypeOfJudgment] = []
        for operand in logical.operands:
            judgment = judgment_by_source.get(operand.source)
            if judgment is None:
                raise PresentationError(
                    "type:of extension lost a logical operand judgment"
                )
            operands.append(judgment)
        if conclusion is None or conclusion.type_name != "bool":
            raise PresentationError(
                "type:of logical form does not conclude bool"
            )
        if logical.operator in {"not", "or"} and any(
            operand.type_name != "bool" for operand in operands
        ):
            raise PresentationError(
                "type:of logical premise is not a Boolean literal"
            )
        logical_sources.append(logical.source)
        logical_derivations.append(
            GdlTypeOfLogicalDerivation(
                conclusion, logical, tuple(operands), equality
            )
        )

    literal_boundaries: list[GdlTypeOfLiteralBoundary] = []
    for constraint in constraints.acceptances:
        if constraint.reason != GdlConstraintReason.LITERAL_RESULT:
            continue
        if not isinstance(constraint.actual, GdlOccurrenceType):
            raise PresentationError(
                "type:of literal boundary has no occurrence judgment"
            )
        acceptance = take_acceptance(
            constraint.actual,
            constraint.expected,
            constraint.source,
            constraint.reason,
        )
        judgment = judgment_by_source.get(constraint.actual.source)
        if (
            judgment is None
            or acceptance.path.actual_type != judgment.type_name
            or acceptance.path.expected_type != "bool"
        ):
            raise PresentationError(
                "type:of literal boundary has inconsistent evidence"
            )
        literal_boundaries.append(
            GdlTypeOfLiteralBoundary(judgment, acceptance)
        )

    if len(used_equalities) != len(constraints.equalities):
        raise PresentationError(
            "type:of extension left an equality outside its derivations"
        )
    if len(used_acceptances) != len(constraints.acceptances):
        raise PresentationError(
            "type:of extension left an acceptance outside its derivations"
        )

    derivation_sources = (
        application_sources + variable_sources + logical_sources
    )
    expected_sources = [
        occurrence.source for occurrence in constraints.occurrence_types
    ]
    if (
        len(derivation_sources) != len(expected_sources)
        or len(set(derivation_sources)) != len(derivation_sources)
        or set(derivation_sources) != set(expected_sources)
    ):
        raise PresentationError(
            "type:of extension does not derive every occurrence exactly once"
        )

    return GdlCheckedTypeOfExtension(
        authored_profile=profile,
        proposal=expected_proposal,
        occurrence_judgments=occurrence_judgments,
        application_derivations=tuple(application_derivations),
        variable_derivations=tuple(variable_derivations),
        logical_derivations=tuple(logical_derivations),
        literal_boundaries=tuple(literal_boundaries),
    )


def inventory_gdl_checked_type_of_extension(
    extension: GdlCheckedTypeOfExtension,
) -> GdlCheckedTypeOfExtensionInventory:
    """Count checked proof occurrences without quotienting rule alternatives."""

    rules = [item.rule for item in extension.application_derivations]
    return GdlCheckedTypeOfExtensionInventory(
        occurrence_judgments=len(extension.occurrence_judgments),
        application_occurrences=len(extension.proposal.applications),
        application_derivations=len(extension.application_derivations),
        authored_rule_uses=sum(
            isinstance(rule, GdlAuthoredTypeOfRule) for rule in rules
        ),
        structural_rule_uses=sum(
            isinstance(rule, GdlStructuralTypeOfRule) for rule in rules
        ),
        extended_rule_uses=sum(
            isinstance(rule, GdlExtendedTypeOfRule) for rule in rules
        ),
        variable_derivations=len(extension.variable_derivations),
        logical_derivations=len(extension.logical_derivations),
        literal_boundaries=len(extension.literal_boundaries),
        nontrivial_subtype_uses=sum(
            bool(discharge.path.steps)
            for discharge in extension.proposal.witness.acceptances
        ),
        derived_signatures=len(extension.proposal.derived_signatures),
    )


def project_gdl_finite_typed_negative_premises(
    demands: GdlNegativePremisePresentation,
    typed_source: GdlFiniteTypedOccurrenceProjection,
) -> GdlFiniteTypedNegativePremiseProjection:
    """Resolve negative-premise argument types without claiming absence.

    Ordinary relation demands are joined to their exact application
    occurrence.  Structural ``distinct`` operands are joined to their exact
    occurrence types.  The result still carries no relation enumeration,
    closed-world assumption, refutation, or authority token.
    """

    application_by_source: dict[
        GdlSourceOccurrence, GdlFiniteResolvedApplication
    ] = {}
    for application in typed_source.applications:
        if application.source in application_by_source:
            raise PresentationError(
                "finite typed source repeats an application occurrence"
            )
        application_by_source[application.source] = application

    occurrence_type_by_source: dict[GdlSourceOccurrence, str] = {}
    for resolved in typed_source.resolved_expressions:
        if not isinstance(resolved.expression, GdlOccurrenceType):
            continue
        source = resolved.expression.source
        previous = occurrence_type_by_source.get(source)
        if previous is not None and previous != resolved.type_name:
            raise PresentationError(
                "finite typed source assigns two types to one occurrence"
            )
        occurrence_type_by_source[source] = resolved.type_name

    relation_absences: list[GdlFiniteTypedRelationAbsenceDemand] = []
    for demand in demands.relation_absences:
        application = application_by_source.get(demand.operand_source)
        if (
            application is None
            or application.name != demand.relation
            or len(application.argument_types)
            != len(demand.argument_sources)
        ):
            raise PresentationError(
                "negative relation demand has no exact typed application"
            )
        relation_absences.append(
            GdlFiniteTypedRelationAbsenceDemand(demand, application)
        )

    distinct_refutations: list[
        GdlFiniteTypedDistinctRefutationDemand
    ] = []
    for demand in demands.distinct_refutations:
        left_type = occurrence_type_by_source.get(demand.left_source)
        right_type = occurrence_type_by_source.get(demand.right_source)
        if left_type is None or right_type is None:
            raise PresentationError(
                "negative distinct demand has unresolved operand type"
            )
        distinct_refutations.append(
            GdlFiniteTypedDistinctRefutationDemand(
                demand=demand,
                operand_types=(left_type, right_type),
            )
        )

    return GdlFiniteTypedNegativePremiseProjection(
        typed_source=typed_source,
        relation_absences=tuple(relation_absences),
        distinct_refutations=tuple(distinct_refutations),
        unsupported=demands.unsupported,
    )


def inventory_gdl_existing_type_arc_analysis(
    analysis: GdlExistingTypeArcAnalysis,
) -> GdlExistingTypeArcInventory:
    """Count finite diagnostic results without promoting them to verdicts."""

    grouped: dict[
        tuple[str, int], list[GdlDerivedSignatureDomain]
    ] = {}
    for domain in analysis.derived_domains:
        grouped.setdefault(
            (domain.slot.name, domain.slot.arity), []
        ).append(domain)
    signature_kinds: list[GdlDerivedDomainKind] = []
    for domains in grouped.values():
        if any(domain.kind == GdlDerivedDomainKind.EMPTY for domain in domains):
            signature_kinds.append(GdlDerivedDomainKind.EMPTY)
        elif any(
            domain.kind == GdlDerivedDomainKind.MULTIPLE
            for domain in domains
        ):
            signature_kinds.append(GdlDerivedDomainKind.MULTIPLE)
        else:
            signature_kinds.append(GdlDerivedDomainKind.SINGLETON)
    return GdlExistingTypeArcInventory(
        type_names=len(analysis.universe.type_names),
        subtype_edges=len(analysis.universe.authored_subtype_edges),
        acceptance_paths=len(analysis.universe.acceptance_paths),
        components=len(analysis.components),
        empty_components=sum(
            not component.candidate_types
            for component in analysis.components
        ),
        exact_conflict_components=sum(
            len(component.exact_type_anchors) > 1
            for component in analysis.components
        ),
        candidate_eliminations=sum(
            len(component.eliminations)
            for component in analysis.components
        ),
        derived_slots=len(analysis.derived_domains),
        singleton_derived_slots=sum(
            domain.kind == GdlDerivedDomainKind.SINGLETON
            for domain in analysis.derived_domains
        ),
        multiple_derived_slots=sum(
            domain.kind == GdlDerivedDomainKind.MULTIPLE
            for domain in analysis.derived_domains
        ),
        empty_derived_slots=sum(
            domain.kind == GdlDerivedDomainKind.EMPTY
            for domain in analysis.derived_domains
        ),
        derived_signatures=len(grouped),
        singleton_derived_signatures=signature_kinds.count(
            GdlDerivedDomainKind.SINGLETON
        ),
        multiple_derived_signatures=signature_kinds.count(
            GdlDerivedDomainKind.MULTIPLE
        ),
        empty_derived_signatures=signature_kinds.count(
            GdlDerivedDomainKind.EMPTY
        ),
        known_equality_conflicts=len(analysis.known_equality_conflicts),
        known_acceptance_conflicts=len(
            analysis.known_acceptance_conflicts
        ),
    )


def gdl_empty_domain_receipts(
    analysis: GdlExistingTypeArcAnalysis,
) -> tuple[GdlEmptyDomainReceipt, ...]:
    """Project every empty finite domain into occurrence-level evidence."""

    receipts: list[GdlEmptyDomainReceipt] = []
    for component in analysis.components:
        if component.candidate_types:
            continue
        sources = {
            member.source
            for member in component.members
            if isinstance(member, GdlOccurrenceType)
        }
        sources.update(
            equality.source for equality in component.equality_evidence
        )
        sources.update(
            elimination.constraint.source
            for elimination in component.eliminations
        )
        source_occurrences = tuple(
            sorted(
                sources,
                key=lambda source: (
                    source.form_ordinal,
                    source.start_line,
                    source.end_line,
                    source.path,
                ),
            )
        )
        derived_slots = tuple(
            sorted(
                (
                    member
                    for member in component.members
                    if isinstance(member, GdlDerivedSignatureType)
                ),
                key=_gdl_type_expression_key,
            )
        )
        receipts.append(
            GdlEmptyDomainReceipt(
                component_ordinal=component.ordinal,
                source_occurrences=source_occurrences,
                derived_slots=derived_slots,
                exact_type_anchors=component.exact_type_anchors,
                equality_evidence=component.equality_evidence,
                eliminations=component.eliminations,
            )
        )
    return tuple(receipts)


def inventory_gdl_empty_domain_receipts(
    receipts: tuple[GdlEmptyDomainReceipt, ...],
    universe: GdlFiniteTypeUniverse,
) -> GdlEmptyDomainReceiptInventory:
    """Count receipts and replay every finite candidate elimination."""

    eliminations = tuple(
        elimination
        for receipt in receipts
        for elimination in receipt.eliminations
    )
    return GdlEmptyDomainReceiptInventory(
        receipts=len(receipts),
        source_occurrences=sum(
            len(receipt.source_occurrences) for receipt in receipts
        ),
        derived_slots=sum(
            len(receipt.derived_slots) for receipt in receipts
        ),
        receipts_with_derived_slots=sum(
            bool(receipt.derived_slots) for receipt in receipts
        ),
        exact_conflict_receipts=sum(
            len(receipt.exact_type_anchors) > 1 for receipt in receipts
        ),
        candidate_eliminations=len(eliminations),
        invalid_candidate_eliminations=sum(
            not elimination.is_valid_in(universe)
            for elimination in eliminations
        ),
    )
