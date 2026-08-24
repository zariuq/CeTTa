#!/usr/bin/env python3

from __future__ import annotations

import copy
from dataclasses import replace
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "scripts"))
from prime_iggp_presentation import (  # noqa: E402
    GdlApplicationEvidenceKind,
    GdlConstraintReason,
    GdlDerivedDomainKind,
    GdlDerivedSignatureType,
    GdlAuthoredTypeOfRule,
    GdlExtendedTypeOfRule,
    GdlFiniteTypeAssignment,
    GdlFiniteTypeChoice,
    GdlKnownType,
    GdlRuleVariableType,
    GdlSignatureStatement,
    GdlSubtypeStatement,
    PresentationError,
    analyze_gdl_existing_type_domains,
    check_gdl_type_of_extension,
    extract_gdl_negative_premise_demands,
    extract_gdl_typing_constraints,
    find_gdl_finite_type_assignment,
    find_gdl_rule_variable_greatest_assignment,
    gdl_empty_domain_receipts,
    gdl_finite_type_universe,
    gdl_derived_signature_supports,
    gdl_typing_demand,
    inventory_gdl_derived_supports,
    inventory_gdl_existing_type_arc_analysis,
    inventory_gdl_empty_domain_receipts,
    inventory_gdl_checked_type_of_extension,
    inventory_gdl_negative_premises,
    inventory_gdl_source,
    inventory_gdl_typing_constraints,
    inventory_gdl_types,
    parse_gdl_source_presentation,
    parse_gdl_type_profile,
    project_gdl_derived_finite_completions,
    project_gdl_finite_typed_negative_premises,
    project_gdl_finite_typed_occurrences,
    replay_gdl_finite_type_assignment,
)
from prime_iggp_type_of_inference import (  # noqa: E402
    lower_gdl_type_of_inference_program,
    render_gdl_type_of_inference_program,
)
from prime_iggp_finite_view import (  # noqa: E402
    FiniteViewBoundary,
    construct_finite_state_view,
    construct_structural_finite_state_view,
    encode_finite_view_dataset_source,
    structural_finite_view_source,
)
from prime_iggp_positive_horn import (  # noqa: E402
    PositiveHornBlock,
    PositiveHornBoundary,
    distinct_evidence_blocks,
    episode_fact_blocks,
    decode_gdl_dataset_ground_application,
    decode_gdl_dataset_query_fibres,
    decode_gdl_dataset_target_query_fibres,
    encode_gdl_dataset_application,
    encode_gdl_dataset_ground_applications,
    encode_positive_horn_dataset_source,
    gdl_dataset_constructor_views,
    gdl_dataset_representation_templates,
    render_package,
    solve_positive_horn_reference,
    structural_positive_horn_source,
    target_query_patterns,
)
from prime_iggp_stratification import (  # noqa: E402
    NegativeDependencyCycle,
    RelationSignature,
    StratificationBoundary,
    construct_target_dependency_slice,
    construct_stratification,
)
from prime_iggp_stratified_model import (  # noqa: E402
    StratifiedModelBoundary,
    construct_stratified_model_basis,
    construct_stratified_model_from_basis,
)
from prime_iggp_episode_evidence import (  # noqa: E402
    EVIDENCE_PATH,
    EpisodeEvidenceError,
    load_episode_evidence,
    validate_episode_evidence,
)
from prime_iggp_target_fibre_evidence import (  # noqa: E402
    EVIDENCE_PATH as TARGET_FIBRE_EVIDENCE_PATH,
    TargetFibreEvidenceError,
    load_target_fibre_evidence,
    validate_target_fibre_evidence,
)
from check_prime_iggp_presentations import (  # noqa: E402
    PresentationAuditError,
    task_presentation_family,
)

MODULE_PATH = REPO / "scripts/check_prime_iggp_manifest.py"
SPEC = importlib.util.spec_from_file_location(
    "check_prime_iggp_manifest", MODULE_PATH
)
assert SPEC is not None and SPEC.loader is not None
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)
GENERATOR_PATH = REPO / "scripts/generate_prime_iggp_minimal_decay.py"
GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_iggp_minimal_decay", GENERATOR_PATH
)
assert GENERATOR_SPEC is not None and GENERATOR_SPEC.loader is not None
GENERATOR = importlib.util.module_from_spec(GENERATOR_SPEC)
sys.modules[GENERATOR_SPEC.name] = GENERATOR
GENERATOR_SPEC.loader.exec_module(GENERATOR)
EVEN_GENERATOR_PATH = REPO / "scripts/generate_prime_iggp_minimal_even.py"
EVEN_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_iggp_minimal_even", EVEN_GENERATOR_PATH
)
assert (
    EVEN_GENERATOR_SPEC is not None
    and EVEN_GENERATOR_SPEC.loader is not None
)
EVEN_GENERATOR = importlib.util.module_from_spec(EVEN_GENERATOR_SPEC)
sys.modules[EVEN_GENERATOR_SPEC.name] = EVEN_GENERATOR
EVEN_GENERATOR_SPEC.loader.exec_module(EVEN_GENERATOR)
SPS_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_iggp_scissors_paper_stone.py"
)
SPS_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_iggp_scissors_paper_stone", SPS_GENERATOR_PATH
)
assert SPS_GENERATOR_SPEC is not None and SPS_GENERATOR_SPEC.loader is not None
SPS_GENERATOR = importlib.util.module_from_spec(SPS_GENERATOR_SPEC)
sys.modules[SPS_GENERATOR_SPEC.name] = SPS_GENERATOR
SPS_GENERATOR_SPEC.loader.exec_module(SPS_GENERATOR)
BUTTONS_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_iggp_buttons_and_lights.py"
)
BUTTONS_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_iggp_buttons_and_lights", BUTTONS_GENERATOR_PATH
)
assert (
    BUTTONS_GENERATOR_SPEC is not None
    and BUTTONS_GENERATOR_SPEC.loader is not None
)
BUTTONS_GENERATOR = importlib.util.module_from_spec(BUTTONS_GENERATOR_SPEC)
sys.modules[BUTTONS_GENERATOR_SPEC.name] = BUTTONS_GENERATOR
BUTTONS_GENERATOR_SPEC.loader.exec_module(BUTTONS_GENERATOR)
MULTI_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_iggp_multiplebuttonsandlights.py"
)
MULTI_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_iggp_multiplebuttonsandlights",
    MULTI_GENERATOR_PATH,
)
assert MULTI_GENERATOR_SPEC is not None and MULTI_GENERATOR_SPEC.loader is not None
MULTI_GENERATOR = importlib.util.module_from_spec(MULTI_GENERATOR_SPEC)
sys.modules[MULTI_GENERATOR_SPEC.name] = MULTI_GENERATOR
MULTI_GENERATOR_SPEC.loader.exec_module(MULTI_GENERATOR)
TRON_GENERATOR_PATH = REPO / "scripts/generate_prime_iggp_tron.py"
TRON_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_iggp_tron", TRON_GENERATOR_PATH
)
assert TRON_GENERATOR_SPEC is not None and TRON_GENERATOR_SPEC.loader is not None
TRON_GENERATOR = importlib.util.module_from_spec(TRON_GENERATOR_SPEC)
sys.modules[TRON_GENERATOR_SPEC.name] = TRON_GENERATOR
TRON_GENERATOR_SPEC.loader.exec_module(TRON_GENERATOR)
CORRIDOR_GENERATOR_PATH = (
    REPO / "scripts/generate_prime_iggp_untwisty_corridor.py"
)
CORRIDOR_GENERATOR_SPEC = importlib.util.spec_from_file_location(
    "generate_prime_iggp_untwisty_corridor",
    CORRIDOR_GENERATOR_PATH,
)
assert (
    CORRIDOR_GENERATOR_SPEC is not None
    and CORRIDOR_GENERATOR_SPEC.loader is not None
)
CORRIDOR_GENERATOR = importlib.util.module_from_spec(CORRIDOR_GENERATOR_SPEC)
sys.modules[CORRIDOR_GENERATOR_SPEC.name] = CORRIDOR_GENERATOR
CORRIDOR_GENERATOR_SPEC.loader.exec_module(CORRIDOR_GENERATOR)
MANIFEST_PATH = REPO / "benchmarks/prime/ilp/iggp_manifest.json"


class IggpManifestTests(unittest.TestCase):
    def test_native_episode_evidence_is_task_complete(self) -> None:
        evidence = load_episode_evidence(REPO / EVIDENCE_PATH)

        validate_episode_evidence(evidence, REPO)

        self.assertEqual(evidence["summary"]["tasks"], 200)
        self.assertEqual(evidence["summary"]["established_tasks"], 119)
        self.assertEqual(
            evidence["summary"]["corpus_contradiction_tasks"], 1
        )

    def test_native_episode_evidence_rejects_summary_drift(self) -> None:
        evidence = load_episode_evidence(REPO / EVIDENCE_PATH)
        evidence["summary"]["established_tasks"] += 1

        with self.assertRaisesRegex(
            EpisodeEvidenceError, "summary disagrees"
        ):
            validate_episode_evidence(evidence, REPO)

    def test_native_episode_evidence_cannot_erase_a_conflict(self) -> None:
        evidence = load_episode_evidence(REPO / EVIDENCE_PATH)
        firesheep = evidence["partially_established_games"]["firesheep"]
        firesheep["corpus_contradictions"].pop("next")
        firesheep["established_targets"].append("next")

        with self.assertRaisesRegex(
            EpisodeEvidenceError, "task partition is not exact"
        ):
            validate_episode_evidence(evidence, REPO)

    def test_native_episode_evidence_retains_outside_reason(self) -> None:
        evidence = load_episode_evidence(REPO / EVIDENCE_PATH)
        evidence["outside_source_image"]["asylum"]["blockers"] = []

        with self.assertRaisesRegex(
            EpisodeEvidenceError, "has no source-image blocker"
        ):
            validate_episode_evidence(evidence, REPO)

    def test_native_target_fibre_evidence_refines_task_coverage(self) -> None:
        evidence = load_target_fibre_evidence(
            REPO / TARGET_FIBRE_EVIDENCE_PATH
        )

        validate_target_fibre_evidence(evidence, REPO)

        self.assertEqual(
            evidence["summary"]["effective_established_tasks"], 127
        )
        self.assertEqual(evidence["summary"]["effective_outside_tasks"], 72)

    def test_native_target_fibre_evidence_cannot_erase_a_result(self) -> None:
        evidence = load_target_fibre_evidence(
            REPO / TARGET_FIBRE_EVIDENCE_PATH
        )
        evidence["games"]["farming"]["target_outcomes"]["next"] = {
            "outcome": "OutsideFragment",
            "reason": "tampered",
        }

        with self.assertRaisesRegex(
            TargetFibreEvidenceError, "summary disagrees"
        ):
            validate_target_fibre_evidence(evidence, REPO)

    def test_native_target_fibre_evidence_retains_abstention_reason(
        self,
    ) -> None:
        evidence = load_target_fibre_evidence(
            REPO / TARGET_FIBRE_EVIDENCE_PATH
        )
        evidence["games"]["coins"]["uniform_outcome"]["reason"] = ""

        with self.assertRaisesRegex(
            TargetFibreEvidenceError, "has no reason"
        ):
            validate_target_fibre_evidence(evidence, REPO)

    def test_task_presentation_family_uses_unanimous_imported_core(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            games = root / "games"
            games.mkdir()
            (games / "sokoban.txt").write_text(
                "(canonical one-map source)\n", encoding="utf-8"
            )
            (games / "sokoban_core.txt").write_text(
                "(shared task semantics)\n", encoding="utf-8"
            )
            (games / "sokoban_map1.txt").write_text(
                "import sokoban_core\n(init map1)\n", encoding="utf-8"
            )
            (games / "sokoban_map2.txt").write_text(
                "import sokoban_core\n(init map2)\n", encoding="utf-8"
            )

            family = task_presentation_family(root, "sokoban")

            self.assertEqual(family.source_path, games / "sokoban_core.txt")
            self.assertEqual(
                family.instance_paths,
                (games / "sokoban_map1.txt", games / "sokoban_map2.txt"),
            )

    def test_task_presentation_family_rejects_incomparable_cores(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            games = root / "games"
            games.mkdir()
            (games / "game.txt").write_text("(canonical)\n", encoding="utf-8")
            (games / "left.txt").write_text("(left)\n", encoding="utf-8")
            (games / "right.txt").write_text("(right)\n", encoding="utf-8")
            (games / "game_map1.txt").write_text(
                "import left\n", encoding="utf-8"
            )
            (games / "game_map2.txt").write_text(
                "import right\n", encoding="utf-8"
            )

            with self.assertRaisesRegex(
                PresentationAuditError, "do not share one semantic core"
            ):
                task_presentation_family(root, "game")

    def setUp(self) -> None:
        self.manifest = json.loads(
            MANIFEST_PATH.read_text(encoding="utf-8")
        )

    def test_positive_horn_representation_retains_occurrences_and_proof_order(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation(
            "(edge a b)\n"
            "(edge b c)\n"
            "(<= (path ?x ?z) (edge ?x ?y) (edge ?y ?z))\n"
        )
        program = encode_positive_horn_dataset_source(presentation)
        self.assertEqual(
            (program.source_form_count, program.source_rule_count,
             program.source_fact_count, program.distinct_premise_count),
            (3, 1, 2, 0),
        )
        rule = program.blocks[2]
        self.assertEqual(rule.conclusion, ("path", "$gdl-v0", "$gdl-v1"))
        self.assertEqual(
            tuple(premise.goal for premise in rule.premises),
            (
                ("edge", "$gdl-v0", "$gdl-v2"),
                ("edge", "$gdl-v2", "$gdl-v1"),
            ),
        )
        rendered = render_package(program.blocks)
        self.assertIn("(gdl:source-occurrence 3 3 3)", rendered)
        self.assertIn(
            "(gdl:premises $gdl-p0 $gdl-p1)", rendered
        )

    def test_nested_gdl_terms_encode_to_dataset_relation_shapes(self) -> None:
        variables = {"?role": "$role", "?x": "$x"}
        self.assertEqual(
            encode_gdl_dataset_application(
                ("legal", "?role", ("choose", "?x")), variables
            ),
            ("legal_choose", "$role", "$x"),
        )
        self.assertEqual(
            encode_gdl_dataset_application(
                ("next", ("cell", "1", "2", "red"))
            ),
            ("next_cell", "1", "2", "red"),
        )

    def test_typed_partial_dataset_constructor_view(self) -> None:
        source = parse_gdl_source_presentation(
            "(role red)\n"
            "(index 1)\n"
            "(index 2)\n"
            "(<= (input ?r (put_action ?x)) (role ?r) (index ?x))\n"
            "(<= (seen ?r ?x) (does ?r (put_action ?x)))\n"
        )

        def basis_with(constructor_profile: str):
            profile = parse_gdl_type_profile(
                "role :: agent -> bool.\n"
                "input, does :: agent -> action -> bool.\n"
                "seen :: agent -> int -> bool.\n"
                "red :: agent.\n"
                "1 :: small.\n"
                "small :> int.\n"
                "2 :: int.\n"
                f"{constructor_profile}\n"
                "index :: int -> bool.\n"
            )
            return profile, construct_stratified_model_basis(source, profile)

        profile, basis = basis_with("put :: small -> action.")
        views = gdl_dataset_constructor_views(
            profile, basis.typed_source, basis.carrier
        )
        self.assertEqual(len(views), 1)
        view = views[0]
        self.assertEqual(
            (
                view.source_name,
                view.represented_name,
                view.source_argument_types,
                view.represented_argument_types,
                view.result_type,
            ),
            ("put_action", "put", ("int",), ("small",), "action"),
        )
        templates = gdl_dataset_representation_templates(source, views)
        represented = ("does_put", "red", "1")
        authored = decode_gdl_dataset_ground_application(
            templates,
            represented,
            carrier=basis.carrier,
        )
        self.assertEqual(authored, ("does", "red", ("put_action", "1")))
        self.assertEqual(
            encode_gdl_dataset_application(
                ("does", "red", ("put_action", 1)),
                constructor_views=views,
                carrier=basis.carrier,
            ),
            ("does_put", "red", 1),
        )
        witness = construct_stratified_model_from_basis(
            basis, initial_facts=(authored,)
        )
        self.assertEqual(
            witness.stats.episode_typing_proof_occurrences, 1
        )
        with self.assertRaisesRegex(
            PositiveHornBoundary, "outside the authored representation image"
        ):
            decode_gdl_dataset_ground_application(
                templates,
                ("does_put", "red", "2"),
                carrier=basis.carrier,
            )
        self.assertEqual(
            encode_gdl_dataset_ground_applications(
                ("does", "red", ("put_action", 1)),
                constructor_views=views,
                carrier=basis.carrier,
            ),
            (("does_put_action", "red", 1), ("does_put", "red", 1)),
        )

        self.assertEqual(
            encode_gdl_dataset_ground_applications(
                ("goal", "robot", 0)
            ),
            (("goal", "robot", 0),),
        )

        self.assertEqual(
            encode_gdl_dataset_ground_applications(
                ("legal", "black", ("drop", 3))
            ),
            (("legal_drop", "black", 3),),
        )
        with self.assertRaisesRegex(
            PositiveHornBoundary, "outside its typed dataset view"
        ):
            encode_gdl_dataset_application(
                ("does", "red", ("put_action", 2)),
                constructor_views=views,
                carrier=basis.carrier,
            )

        ambiguous_profile, ambiguous_basis = basis_with(
            "put :: small -> action.\n"
            "put :: tiny -> action.\n"
            "tiny :> int.\n"
            "3 :: tiny."
        )
        self.assertEqual(
            gdl_dataset_constructor_views(
                ambiguous_profile,
                ambiguous_basis.typed_source,
                ambiguous_basis.carrier,
            ),
            (),
        )
        incompatible_profile, incompatible_basis = basis_with(
            "put :: text -> action.\nword :: text."
        )
        self.assertEqual(
            gdl_dataset_constructor_views(
                incompatible_profile,
                incompatible_basis.typed_source,
                incompatible_basis.carrier,
            ),
            (),
        )

    def test_structural_program_does_not_identify_dataset_name_collision(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation(
            "(next (at a))\n(next_at a)\n"
        )
        structural = structural_positive_horn_source(presentation)
        represented = encode_positive_horn_dataset_source(presentation)
        self.assertEqual(
            tuple(block.conclusion for block in structural.blocks),
            (("next", ("at", "a")), ("next_at", "a")),
        )
        self.assertEqual(
            tuple(block.conclusion for block in represented.blocks),
            (("next_at", "a"), ("next_at", "a")),
        )

    def test_source_templates_lift_only_the_exact_unambiguous_image(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation(
            "(<= (next (value ?n)) (true (value ?m)))\n"
        )
        templates = gdl_dataset_representation_templates(presentation)
        self.assertEqual(
            decode_gdl_dataset_ground_application(
                templates, ("true_value", "1")
            ),
            ("true", ("value", "1")),
        )
        with self.assertRaisesRegex(
            PositiveHornBoundary,
            "outside the authored representation image",
        ):
            decode_gdl_dataset_ground_application(
                templates, ("true_other", "1")
            )

        episode_templates = gdl_dataset_representation_templates(
            parse_gdl_source_presentation(
                "(<= (base (cell ?x)) (index ?x))\n"
                "(<= (input ?role noop) (role ?role))\n"
            )
        )
        self.assertEqual(
            decode_gdl_dataset_ground_application(
                episode_templates, ("true_cell", "7")
            ),
            ("true", ("cell", "7")),
        )
        self.assertEqual(
            decode_gdl_dataset_ground_application(
                episode_templates, ("does", "blue", "noop")
            ),
            ("does", "blue", "noop"),
        )
        legal_episode_templates = gdl_dataset_representation_templates(
            parse_gdl_source_presentation(
                "(<= (legal ?role noop) (role ?role))\n"
            )
        )
        self.assertEqual(
            decode_gdl_dataset_ground_application(
                legal_episode_templates, ("does", "wolf", "noop")
            ),
            ("does", "wolf", "noop"),
        )
        undeclared_episode_templates = gdl_dataset_representation_templates(
            parse_gdl_source_presentation("(role blue)\n")
        )
        with self.assertRaisesRegex(
            PositiveHornBoundary,
            "outside the authored representation image",
        ):
            decode_gdl_dataset_ground_application(
                undeclared_episode_templates, ("does", "blue", "noop")
            )

        ambiguous = gdl_dataset_representation_templates(
            parse_gdl_source_presentation(
                "(<= (p ?x) (true (left ?x)))\n"
                "(<= (q ?x) (true_left ?x))\n"
            )
        )
        with self.assertRaisesRegex(
            PositiveHornBoundary, "multiple authored lifting images"
        ):
            decode_gdl_dataset_ground_application(
                ambiguous, ("true_left", "7")
            )

        query_templates = gdl_dataset_representation_templates(
            parse_gdl_source_presentation(
                "(init (value 5))\n"
                "(legal player noop)\n"
                "(legal player press)\n"
            )
        )
        self.assertEqual(
            decode_gdl_dataset_query_fibres(
                query_templates, ("init_value", "$answer")
            ),
            (("init", ("value", "5")),),
        )
        self.assertEqual(
            decode_gdl_dataset_query_fibres(
                query_templates,
                ("legal", "$role", "$action"),
            ),
            (
                ("legal", "player", "noop"),
                ("legal", "player", "press"),
            ),
        )
        self.assertEqual(
            decode_gdl_dataset_query_fibres(
                query_templates, ("goal", "$role", "$score")
            ),
            (("goal", "$role", "$score"),),
        )

        separate_query_fibres = gdl_dataset_representation_templates(
            parse_gdl_source_presentation(
                "(next (at a))\n(next_at a)\n"
            )
        )
        self.assertEqual(
            decode_gdl_dataset_query_fibres(
                separate_query_fibres, ("next_at", "$value")
            ),
            (("next", ("at", "a")), ("next_at", "a")),
        )
        self.assertEqual(
            decode_gdl_dataset_target_query_fibres(
                separate_query_fibres,
                ("next_at", "$value"),
                "next",
            ),
            (("next", ("at", "a")),),
        )
        self.assertEqual(
            decode_gdl_dataset_target_query_fibres(
                separate_query_fibres,
                ("next_at", "$value"),
                "next_at",
            ),
            (("next_at", "a"),),
        )
        with self.assertRaisesRegex(
            PositiveHornBoundary, "no authored source-relation fibre"
        ):
            decode_gdl_dataset_target_query_fibres(
                separate_query_fibres,
                ("next_at", "$value"),
                "unrelated",
            )

    def test_profile_prop_constructors_compose_with_state_contexts(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation(
            "(<= terminal (true (step ?n)) (game_end ?n))\n"
        )
        profile = parse_gdl_type_profile(
            "true, next :: prop -> bool.\n"
            "step, game_end :: int -> prop.\n"
            "legal :: agent -> action -> bool.\n"
            "move :: int -> int -> action.\n"
            "status :: bool.\n"
        )
        templates = gdl_dataset_representation_templates(
            presentation, profile=profile
        )
        self.assertEqual(
            decode_gdl_dataset_ground_application(
                templates, ("next_game_end", "80")
            ),
            ("next", ("game_end", "80")),
        )
        self.assertEqual(
            decode_gdl_dataset_ground_application(
                templates, ("true_step", "7")
            ),
            ("true", ("step", "7")),
        )
        self.assertEqual(
            decode_gdl_dataset_ground_application(
                templates, ("legal_move", "red", "1", "2")
            ),
            ("legal", "red", ("move", "1", "2")),
        )
        with self.assertRaisesRegex(
            PositiveHornBoundary,
            "outside the authored representation image",
        ):
            decode_gdl_dataset_ground_application(
                templates, "next_status"
            )

    def test_positive_horn_boundary_rejects_lossy_or_generative_shapes(
        self,
    ) -> None:
        with self.assertRaisesRegex(PositiveHornBoundary, "uses not"):
            encode_positive_horn_dataset_source(
                parse_gdl_source_presentation(
                    "(<= (p ?x) (domain ?x) (not (blocked ?x)))\n"
                )
            )
        with self.assertRaisesRegex(PositiveHornBoundary, "mixed GDL"):
            encode_positive_horn_dataset_source(
                parse_gdl_source_presentation(
                    "(p a)\nforeign(X) :- opaque(X).\n"
                )
            )
        with self.assertRaisesRegex(
            PositiveHornBoundary, "distinct may test only"
        ):
            encode_positive_horn_dataset_source(
                parse_gdl_source_presentation(
                    "(<= (different ?x ?y) (distinct ?x ?y))\n"
                )
            )

    def test_episode_typing_retains_distinct_subtype_paths(self) -> None:
        presentation = parse_gdl_source_presentation("(p x)\n")
        profile = parse_gdl_type_profile(
            "p :: top -> bool.\n"
            "x :: leaf.\n"
            "leaf :> left.\n"
            "leaf :> right.\n"
            "left :> top.\n"
            "right :> top.\n"
        )
        basis = construct_stratified_model_basis(presentation, profile)
        witness = construct_stratified_model_from_basis(
            basis, initial_facts=(("p", "x"),)
        )
        self.assertEqual(witness.stats.episode_fact_occurrences, 1)
        self.assertEqual(
            witness.stats.episode_typing_proof_occurrences, 2
        )

    def test_finite_distinct_and_episode_facts_are_proof_relevant(self) -> None:
        distinct = distinct_evidence_blocks(
            "typed-pos-domain-r0", ("1", "2", "3")
        )
        self.assertEqual(len(distinct), 6)
        self.assertEqual(distinct[0].conclusion, ("distinct", "1", "2"))
        self.assertNotIn(
            ("distinct", "1", "1"),
            {block.conclusion for block in distinct},
        )
        episode = episode_fact_blocks(
            "episode-r0", ("true_cell(1, 2, red)", "does(red, noop)")
        )
        self.assertEqual(
            tuple(block.conclusion for block in episode),
            (("true_cell", "1", "2", "red"),
             ("does", "red", "noop")),
        )

    def test_whole_answer_queries_follow_first_seen_target_signatures(
        self,
    ) -> None:
        self.assertEqual(
            target_query_patterns(
                (
                    "legal_jump(red, 1, 2)",
                    "legal_move(red, 1, 2)",
                    "legal_jump(black, 2, 3)",
                    "terminal",
                )
            ),
            (
                ("legal_jump", "$gdl-answer-0", "$gdl-answer-1",
                 "$gdl-answer-2"),
                ("legal_move", "$gdl-answer-0", "$gdl-answer-1",
                 "$gdl-answer-2"),
                "terminal",
            ),
        )

    def test_positive_horn_reference_preserves_order_and_multiplicity(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation(
            "(edge a b)\n"
            "(edge a c)\n"
            "(<= (path ?x ?y) (edge ?x ?y))\n"
            "(<= (path ?x ?y) (edge ?x ?y))\n"
        )
        program = encode_positive_horn_dataset_source(presentation)
        run = solve_positive_horn_reference(
            program.blocks, ("path", "a", "$answer"), depth=4
        )
        self.assertEqual(
            tuple(answer.conclusion for answer in run.answers),
            (
                ("path", "a", "b"),
                ("path", "a", "c"),
                ("path", "a", "b"),
                ("path", "a", "c"),
            ),
        )
        self.assertEqual(
            tuple(answer.proof[1] for answer in run.answers),
            (
                ("gdl:source-occurrence", "3", "3", "3"),
                ("gdl:source-occurrence", "3", "3", "3"),
                ("gdl:source-occurrence", "4", "4", "4"),
                ("gdl:source-occurrence", "4", "4", "4"),
            ),
        )

    def test_positive_horn_reference_rejects_unbound_answer(self) -> None:
        with self.assertRaisesRegex(
            PositiveHornBoundary, "unbound runtime variable"
        ):
            solve_positive_horn_reference(
                (
                    PositiveHornBlock(
                        identity="malformed",
                        source=("source", "malformed"),
                        proof=("proof", "$unbound"),
                        premises=(),
                        conclusion=("value", "a"),
                    ),
                ),
                ("value", "a"),
            )

    def test_finite_view_represents_negation_as_explicit_positive_evidence(
        self,
    ) -> None:
        program = encode_finite_view_dataset_source(
            parse_gdl_source_presentation(
                "(base p)\n"
                "(base q)\n"
                "(domain p)\n"
                "(domain q)\n"
                "(<= (missing ?x)\n"
                "    (domain ?x)\n"
                "    (not (true ?x)))\n"
            )
        )
        self.assertEqual(program.negative_premise_count, 1)
        self.assertEqual(
            tuple(member.represented_literal for member in program.domain),
            (("true", "p"), ("true", "q")),
        )
        rule = program.blocks[-1]
        self.assertEqual(
            tuple(premise.goal for premise in rule.premises),
            (
                ("domain", "$gdl-v0"),
                ("gdl:finite-relation-absent-v1:true", "$gdl-v0"),
            ),
        )

        episode = construct_finite_state_view(
            program,
            ("space:episode", "synthetic", "r1"),
            ("true(q)", "true(q)"),
        )
        self.assertEqual(len(episode.positive_blocks), 2)
        self.assertEqual(len(episode.absence_blocks), 1)
        self.assertEqual(
            episode.absence_blocks[0].conclusion,
            ("gdl:finite-relation-absent-v1:true", "p"),
        )
        run = solve_positive_horn_reference(
            program.blocks + episode.blocks,
            ("missing", "$answer"),
            depth=4,
        )
        self.assertEqual(
            tuple(answer.conclusion for answer in run.answers),
            (("missing", "p"),),
        )

    def test_finite_view_abstains_outside_its_constructive_image(
        self,
    ) -> None:
        with self.assertRaisesRegex(
            FiniteViewBoundary, "bound by earlier positive premises"
        ):
            encode_finite_view_dataset_source(
                parse_gdl_source_presentation(
                    "(base p)\n"
                    "(<= (missing ?x) (not (true ?x)))\n"
                )
            )
        with self.assertRaisesRegex(
            FiniteViewBoundary, "only absence from"
        ):
            encode_finite_view_dataset_source(
                parse_gdl_source_presentation(
                    "(base p)\n"
                    "(<= answer (not (blocked p)))\n"
                )
            )

        program = encode_finite_view_dataset_source(
            parse_gdl_source_presentation(
                "(base p)\n(<= answer (not (true p)))\n"
            )
        )
        with self.assertRaisesRegex(
            FiniteViewBoundary, "outside the source-declared base domain"
        ):
            construct_finite_state_view(
                program, "episode-r1", ("true(q)",)
            )

    def test_structural_finite_view_retains_nested_absence_judgment(
        self,
    ) -> None:
        program = structural_finite_view_source(
            parse_gdl_source_presentation(
                "(base (cell 1))\n"
                "(domain (cell 1))\n"
                "(<= missing\n"
                "    (domain (cell 1))\n"
                "    (not (true (cell 1))))\n"
            )
        )
        episode = construct_structural_finite_state_view(
            program, "episode-r1", ()
        )
        self.assertEqual(
            episode.absence_blocks[0].conclusion,
            (
                "gdl:finite-relation-absent-v1:true",
                ("cell", "1"),
            ),
        )
        run = solve_positive_horn_reference(
            program.blocks + episode.blocks, "missing", depth=4
        )
        self.assertEqual(
            tuple(answer.conclusion for answer in run.answers),
            ("missing",),
        )

    def test_stratification_constructs_the_least_dependency_levels(
        self,
    ) -> None:
        witness = construct_stratification(
            parse_gdl_source_presentation(
                "(p)\n"
                "(<= q p)\n"
                "(<= r (not q))\n"
                "(<= s (or q r))\n"
            )
        )
        self.assertEqual(witness.stratum_of(RelationSignature("p", 0)), 0)
        self.assertEqual(witness.stratum_of(RelationSignature("q", 0)), 0)
        self.assertEqual(witness.stratum_of(RelationSignature("r", 0)), 1)
        self.assertEqual(witness.stratum_of(RelationSignature("s", 0)), 1)
        self.assertEqual(witness.maximum_stratum, 1)
        self.assertEqual(
            tuple((edge.body.name, edge.negative) for edge in witness.edges),
            (("p", False), ("q", True), ("q", False), ("r", False)),
        )
        self.assertEqual(
            tuple(edge.path for edge in witness.edges[-2:]),
            ((2, 1), (2, 2)),
        )

    def test_stratification_allows_positive_recursion(self) -> None:
        witness = construct_stratification(
            parse_gdl_source_presentation(
                "(<= p q)\n"
                "(<= q p)\n"
            )
        )
        self.assertEqual(witness.maximum_stratum, 0)
        self.assertEqual(len(witness.edges), 2)

    def test_stratification_does_not_flatten_predicates_into_terms(self) -> None:
        witness = construct_stratification(
            parse_gdl_source_presentation(
                "(<= (next (at ?x)) (next_at ?x))\n"
                "(<= (next_at ?x) (not (blocked ?x)))\n"
            )
        )
        self.assertEqual(
            tuple(relation.signature for relation in witness.relations),
            (
                RelationSignature("next", 1),
                RelationSignature("next_at", 1),
                RelationSignature("blocked", 1),
            ),
        )
        self.assertEqual(
            witness.stratum_of(RelationSignature("next_at", 1)), 1
        )
        self.assertEqual(
            witness.stratum_of(RelationSignature("next", 1)), 1
        )

    def test_target_slice_is_dependency_closed_without_renumbering(self) -> None:
        presentation = parse_gdl_source_presentation(
            "(bad red)\n"
            "(q red)\n"
            "(<= (goal red 100) (q red))\n"
        )
        profile = parse_gdl_type_profile(
            "red :: agent.\n"
            "100 :: int.\n"
            "bad :: agent -> prop.\n"
            "q :: agent -> bool.\n"
            "goal :: agent -> int -> bool.\n"
        )
        target = RelationSignature("goal", 2)

        selected = construct_target_dependency_slice(
            presentation, target
        )
        basis = construct_stratified_model_basis(
            presentation, profile, target=target
        )

        self.assertEqual(selected.form_ordinals, (1, 2))
        self.assertEqual(
            selected.reachable_relations,
            (RelationSignature("goal", 2), RelationSignature("q", 1)),
        )
        self.assertEqual(selected.external_relations, ())
        self.assertEqual(basis.source_forms, 2)
        self.assertEqual(
            {
                resolved.expression.source.form_ordinal
                for resolved in basis.typed_source.resolved_expressions
                if hasattr(resolved.expression, "source")
            },
            {1, 2},
        )
        with self.assertRaises(StratifiedModelBoundary):
            construct_stratified_model_basis(presentation, profile)

    def test_target_slice_cannot_hide_a_reachable_type_obstruction(self) -> None:
        presentation = parse_gdl_source_presentation(
            "(bad red)\n"
            "(<= terminal (bad red))\n"
        )
        profile = parse_gdl_type_profile(
            "red :: agent.\n"
            "bad :: agent -> prop.\n"
            "terminal :: bool.\n"
        )
        target = RelationSignature("terminal", 0)

        selected = construct_target_dependency_slice(
            presentation, target
        )

        self.assertEqual(selected.form_ordinals, (0, 1))
        with self.assertRaises(StratifiedModelBoundary):
            construct_stratified_model_basis(
                presentation, profile, target=target
            )

    def test_target_slice_requires_an_authored_target(self) -> None:
        with self.assertRaisesRegex(
            StratificationBoundary,
            "no authored defining occurrence",
        ):
            construct_target_dependency_slice(
                parse_gdl_source_presentation("(q red)\n"),
                RelationSignature("goal", 2),
            )

    def test_stratification_refutes_a_negative_dependency_cycle(self) -> None:
        with self.assertRaises(NegativeDependencyCycle) as raised:
            construct_stratification(
                parse_gdl_source_presentation(
                    "(<= p (not q))\n"
                    "(<= q p)\n"
                )
            )
        self.assertTrue(raised.exception.edges)
        self.assertTrue(any(edge.negative for edge in raised.exception.edges))
        self.assertEqual(
            {edge.head.name for edge in raised.exception.edges},
            {"p", "q"},
        )

    def test_source_presentation_separates_gdl_from_foreign_code(self) -> None:
        presentation = parse_gdl_source_presentation(
            "; authored source\n"
            "(role robot)\n"
            "foreign(X) :- opaque(X).\n"
            "(<= (goal robot 100)\n"
            "    (true finished))\n"
            "(terminal) (legal robot noop)\n"
        )
        self.assertEqual(
            presentation.form_values,
            (
                ("role", "robot"),
                ("<=", ("goal", "robot", "100"), ("true", "finished")),
                ("terminal",),
                ("legal", "robot", "noop"),
            ),
        )
        self.assertEqual(
            tuple((line.line, line.text) for line in presentation.foreign_code),
            ((3, "foreign(X) :- opaque(X)."),),
        )
        self.assertEqual(
            tuple(
                (form.start_line, form.end_line)
                for form in presentation.forms
            ),
            ((2, 2), (4, 5), (6, 6), (6, 6)),
        )

    def test_source_inventory_treats_absence_as_data_not_failure(self) -> None:
        presentation = parse_gdl_source_presentation(
            "(<= (goal ?player 0)\n"
            "    (role ?player)\n"
            "    (not (true (won ?player))))\n"
            "(<= (legal ?player ?move)\n"
            "    (role ?player)\n"
            "    (or (left ?move) (right ?move)))\n"
        )
        inventory = inventory_gdl_source(presentation)
        self.assertEqual(inventory.rule_count, 2)
        self.assertEqual(inventory.negation_count, 1)
        self.assertEqual(inventory.disjunction_count, 1)
        self.assertEqual(inventory.unsafe_head_rules, 0)
        self.assertEqual(inventory.unsafe_negative_rules, 0)

    def test_negative_demands_retain_binders_and_uncovered_shapes(self) -> None:
        demands = extract_gdl_negative_premise_demands(
            parse_gdl_source_presentation(
                "(<= (q ?x)\n"
                "    (or (left ?x) (right ?x))\n"
                "    (not (blocked ?x)))\n"
                "(<= (same ?x ?y)\n"
                "    (thing ?x)\n"
                "    (thing ?y)\n"
                "    (not (distinct ?x ?y)))\n"
                "(<= unsafe (not (lost ?z)))\n"
                "(<= odd\n"
                "    (domain ?x)\n"
                "    (not (or (a ?x) (b ?x))))\n"
            )
        )
        inventory = inventory_gdl_negative_premises(demands)
        self.assertEqual(inventory.relation_absences, 2)
        self.assertEqual(inventory.distinct_refutations, 1)
        self.assertEqual(inventory.unsupported, 1)
        self.assertEqual(inventory.variable_demands, 5)
        self.assertEqual(inventory.unbound_variable_demands, 1)
        self.assertEqual(inventory.positive_binding_witnesses, 4)
        self.assertEqual(inventory.positive_binding_branches, 5)

        blocked = demands.relation_absences[0]
        self.assertEqual(blocked.relation, "blocked")
        self.assertEqual(blocked.source.path, (3,))
        self.assertEqual(blocked.operand_source.path, (3, 1))
        self.assertEqual(blocked.variables[0].name, "?x")
        binding = blocked.variables[0].positive_bindings[0]
        self.assertEqual(binding.source.path, (2,))
        self.assertEqual(
            tuple(branch.source.path for branch in binding.branches),
            ((2, 1), (2, 2)),
        )
        self.assertEqual(
            tuple(
                branch.occurrences[0].path for branch in binding.branches
            ),
            ((2, 1, 1), (2, 2, 1)),
        )
        self.assertFalse(demands.relation_absences[1].variables[0].positive_bindings)
        distinct = demands.distinct_refutations[0]
        self.assertEqual(distinct.left_source.path, (4, 1, 1))
        self.assertEqual(distinct.right_source.path, (4, 1, 2))
        self.assertIn("covered atom", demands.unsupported[0].description)

    def test_typed_negative_projection_does_not_manufacture_absence(
        self,
    ) -> None:
        source = parse_gdl_source_presentation(
            "(<= (p ?x) (domain ?x) (not (blocked ?x)))\n"
            "(<= (same ?x ?y)\n"
            "    (domain ?x)\n"
            "    (domain ?y)\n"
            "    (not (distinct ?x ?y)))\n"
        )
        profile = parse_gdl_type_profile(
            "domain :: item -> bool.\n"
            "blocked :: item -> bool.\n"
        )
        constraints = extract_gdl_typing_constraints(source, profile)
        analysis = analyze_gdl_existing_type_domains(constraints, profile)
        assignment = find_gdl_finite_type_assignment(
            constraints, analysis
        )
        self.assertIsNotNone(assignment)
        assert assignment is not None
        typed_source = project_gdl_finite_typed_occurrences(
            constraints, analysis, assignment.assignment
        )
        projection = project_gdl_finite_typed_negative_premises(
            extract_gdl_negative_premise_demands(source), typed_source
        )
        self.assertEqual(len(projection.relation_absences), 1)
        typed_demand = projection.relation_absences[0]
        self.assertEqual(typed_demand.application.name, "blocked")
        self.assertEqual(typed_demand.application.argument_types, ("item",))
        self.assertEqual(typed_demand.application.result_type, "bool")
        self.assertEqual(len(projection.distinct_refutations), 1)
        self.assertEqual(
            projection.distinct_refutations[0].operand_types,
            ("item", "item"),
        )
        self.assertEqual(projection.unsupported, ())

    def test_type_profile_preserves_order_overloads_and_duplicates(self) -> None:
        profile = parse_gdl_type_profile(
            "a, a :: small.\n"
            "small :> large.\n"
            "f :: small -> large.\n"
            "f :: large -> small.\n"
            "pair :: small ->\n"
            "        large -> product.\n"
        )
        self.assertIsInstance(profile.statements[0], GdlSignatureStatement)
        self.assertIsInstance(profile.statements[1], GdlSubtypeStatement)
        self.assertEqual(profile.signatures[0].names, ("a", "a"))
        self.assertEqual(profile.signatures[-1].start_line, 5)
        self.assertEqual(profile.signatures[-1].end_line, 6)
        inventory = inventory_gdl_types(profile)
        self.assertEqual(inventory.signature_occurrences, 5)
        self.assertEqual(inventory.duplicate_signature_occurrences, 1)
        self.assertEqual(inventory.overloaded_symbols, ("f",))

    def test_typing_demand_records_missing_signatures_without_refuting(self) -> None:
        presentation = parse_gdl_source_presentation(
            "(role robot)\n"
            "(<= (goal robot 100)\n"
            "    (helper ?x)\n"
            "    (not (blocked ?x)))\n"
        )
        profile = parse_gdl_type_profile(
            "role :: agent -> bool.\n"
            "goal :: agent -> score -> bool.\n"
            "robot :: agent.\n"
            "100 :: score.\n"
        )
        demand = gdl_typing_demand(presentation, profile)
        self.assertEqual(
            demand.missing_applications,
            (("blocked", 1), ("helper", 1)),
        )
        self.assertEqual(demand.unmatched_authored_name_applications, ())

    def test_malformed_presentations_are_not_silently_repaired(self) -> None:
        with self.assertRaisesRegex(PresentationError, "unterminated GDL"):
            parse_gdl_source_presentation("(role robot\n")
        with self.assertRaisesRegex(
            PresentationError, "unterminated type-profile"
        ):
            parse_gdl_type_profile("role :: agent -> bool\n")

    def test_typing_constraints_retain_source_and_profile_evidence(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation(
            "(<= (wins ?player)\n"
            "    (does ?player rock))\n"
        )
        profile = parse_gdl_type_profile(
            "does :: agent -> action -> bool.\n"
            "rock :: action.\n"
        )
        constraints = extract_gdl_typing_constraints(
            presentation, profile
        )
        wins = next(
            application
            for application in constraints.applications
            if application.name == "wins"
        )
        self.assertEqual(
            wins.kind, GdlApplicationEvidenceKind.PROFILE_MISSING
        )
        self.assertEqual(
            wins.derived_signature,
            (
                GdlDerivedSignatureType("wins", 1, 0),
                GdlDerivedSignatureType("wins", 1, None),
            ),
        )
        self.assertEqual(
            constraints.rule_variable_types,
            (GdlRuleVariableType(0, "?player"),),
        )
        player_equalities = [
            equality
            for equality in constraints.equalities
            if equality.reason == GdlConstraintReason.RULE_VARIABLE
        ]
        self.assertEqual(len(player_equalities), 2)
        self.assertEqual(
            {equality.right for equality in player_equalities},
            {GdlRuleVariableType(0, "?player")},
        )
        self.assertTrue(
            any(
                acceptance.expected == GdlKnownType("agent")
                for acceptance in constraints.acceptances
            )
        )

    def test_identical_profile_duplicates_do_not_create_false_ambiguity(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation("(p a)\n(q a)\n")
        profile = parse_gdl_type_profile(
            "a :: small.\n"
            "p :: small -> bool.\n"
            "p :: small -> bool.\n"
            "q :: small -> bool.\n"
            "q :: large -> bool.\n"
        )
        constraints = extract_gdl_typing_constraints(
            presentation, profile
        )
        p_application = next(
            application
            for application in constraints.applications
            if application.name == "p"
        )
        q_application = next(
            application
            for application in constraints.applications
            if application.name == "q"
        )
        self.assertEqual(
            p_application.kind, GdlApplicationEvidenceKind.AUTHORED
        )
        self.assertEqual(len(p_application.authored_candidates), 2)
        self.assertEqual(
            q_application.kind,
            GdlApplicationEvidenceKind.AUTHORED_AMBIGUOUS,
        )
        self.assertEqual(q_application.derived_signature, ())

    def test_gdl_structure_supplies_base_and_exposes_profile_conflict(
        self,
    ) -> None:
        structural = extract_gdl_typing_constraints(
            parse_gdl_source_presentation("(base fluent)\n"),
            parse_gdl_type_profile("fluent :: prop.\n"),
        )
        base = next(
            application
            for application in structural.applications
            if application.name == "base"
        )
        self.assertEqual(
            base.kind, GdlApplicationEvidenceKind.STRUCTURAL
        )
        self.assertEqual(
            base.structural_signature, (("prop",), "bool")
        )

        conflicting = extract_gdl_typing_constraints(
            parse_gdl_source_presentation("(init fluent)\n"),
            parse_gdl_type_profile(
                "fluent :: prop.\ninit :: action -> bool.\n"
            ),
        )
        init = next(
            application
            for application in conflicting.applications
            if application.name == "init"
        )
        self.assertEqual(
            init.kind, GdlApplicationEvidenceKind.AUTHORED_AMBIGUOUS
        )
        self.assertEqual(init.derived_signature, ())

    def test_distinct_is_structural_and_does_not_monomorphize_terms(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation("(distinct a b)\n")
        profile = parse_gdl_type_profile("a :: left.\nb :: right.\n")
        constraints = extract_gdl_typing_constraints(
            presentation, profile
        )
        self.assertEqual(
            tuple(form.operator for form in constraints.logical_forms),
            ("distinct",),
        )
        self.assertNotIn(
            "distinct",
            {application.name for application in constraints.applications},
        )
        self.assertFalse(
            any(
                acceptance.reason
                == GdlConstraintReason.DERIVED_ARGUMENT
                for acceptance in constraints.acceptances
            )
        )

    def test_rule_variable_identity_is_scoped_per_form(self) -> None:
        presentation = parse_gdl_source_presentation(
            "(<= (p ?x) (q ?x))\n"
            "(<= (r ?x) (s ?x))\n"
        )
        constraints = extract_gdl_typing_constraints(
            presentation, parse_gdl_type_profile("")
        )
        self.assertEqual(
            constraints.rule_variable_types,
            (
                GdlRuleVariableType(0, "?x"),
                GdlRuleVariableType(1, "?x"),
            ),
        )

    def test_unsupported_dynamic_head_is_retained_not_reinterpreted(
        self,
    ) -> None:
        presentation = parse_gdl_source_presentation("(?relation a)\n")
        constraints = extract_gdl_typing_constraints(
            presentation, parse_gdl_type_profile("a :: entity.\n")
        )
        inventory = inventory_gdl_typing_constraints(constraints)
        self.assertEqual(inventory.unsupported_shapes, 1)
        self.assertEqual(inventory.application_occurrences, 1)
        self.assertEqual(constraints.applications[0].name, "a")

    def test_derived_support_retains_subtype_order_and_paths(self) -> None:
        presentation = parse_gdl_source_presentation(
            "(<= (p ?x) (narrow ?x) (wide ?x))\n"
        )
        profile = parse_gdl_type_profile(
            "narrow :: small -> bool.\n"
            "wide :: large -> bool.\n"
            "small :> large.\n"
        )
        constraints = extract_gdl_typing_constraints(
            presentation, profile
        )
        supports = {
            support.slot: support
            for support in gdl_derived_signature_supports(
                constraints, profile
            )
        }
        argument = supports[GdlDerivedSignatureType("p", 1, 0)]
        self.assertEqual(
            tuple(anchor.type_name for anchor in argument.anchors),
            ("large", "small"),
        )
        self.assertEqual(argument.incomparable_anchor_pairs, ())
        self.assertTrue(all(anchor.path for anchor in argument.anchors))
        self.assertEqual(
            {step.source.form_ordinal for anchor in argument.anchors
             for step in anchor.path},
            {0},
        )

    def test_incomparable_and_unanchored_support_stay_unresolved(self) -> None:
        incomparable_profile = parse_gdl_type_profile(
            "left :: first -> bool.\n"
            "right :: second -> bool.\n"
        )
        incomparable_constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation(
                "(<= (p ?x) (left ?x) (right ?x))\n"
            ),
            incomparable_profile,
        )
        support = next(
            item
            for item in gdl_derived_signature_supports(
                incomparable_constraints, incomparable_profile
            )
            if item.slot == GdlDerivedSignatureType("p", 1, 0)
        )
        self.assertEqual(
            support.incomparable_anchor_pairs, (("first", "second"),)
        )

        open_profile = parse_gdl_type_profile("")
        open_constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation("(<= (p ?x) (q ?x))\n"),
            open_profile,
        )
        inventory = inventory_gdl_derived_supports(
            gdl_derived_signature_supports(
                open_constraints, open_profile
            )
        )
        self.assertEqual(inventory.signatures, 2)
        self.assertEqual(inventory.unanchored_signatures, 2)
        self.assertEqual(inventory.slots, 4)
        self.assertEqual(inventory.unanchored_slots, 2)
        self.assertEqual(inventory.single_anchor_slots, 2)

    def test_finite_type_universe_retains_authored_subtype_paths(self) -> None:
        profile = parse_gdl_type_profile(
            "small :> medium.\nmedium :> large.\n"
        )
        universe = gdl_finite_type_universe(profile)
        small_large = universe.acceptance_path("small", "large")
        self.assertIsNotNone(small_large)
        assert small_large is not None
        self.assertEqual(
            tuple(
                (edge.subtype, edge.supertype)
                for edge in small_large.steps
            ),
            (("small", "medium"), ("medium", "large")),
        )
        self.assertEqual(
            universe.acceptance_path("small", "small").steps, ()
        )
        self.assertIsNone(universe.acceptance_path("large", "small"))
        self.assertTrue({"agent", "action", "prop", "bool"} <= set(
            universe.type_names
        ))

    def test_existing_type_domains_keep_known_labels_as_boundaries(
        self,
    ) -> None:
        constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation(
                "(ill a)\n"
                "(<= helper (p a))\n"
            ),
            parse_gdl_type_profile(
                "a :: small.\n"
                "ill :: small -> prop.\n"
                "p :: small -> bool.\n"
            ),
        )
        profile = parse_gdl_type_profile(
            "a :: small.\n"
            "ill :: small -> prop.\n"
            "p :: small -> bool.\n"
        )
        analysis = analyze_gdl_existing_type_domains(
            constraints, profile
        )
        helper_result = next(
            domain
            for domain in analysis.derived_domains
            if domain.slot == GdlDerivedSignatureType("helper", 0, None)
        )
        self.assertEqual(helper_result.candidate_types, ("bool",))
        self.assertEqual(helper_result.kind, GdlDerivedDomainKind.SINGLETON)
        self.assertTrue(
            any(not component.candidate_types for component in analysis.components)
        )

    def test_existing_type_domains_preserve_subtype_ambiguity(self) -> None:
        profile = parse_gdl_type_profile(
            "narrow :: small -> bool.\nsmall :> large.\n"
        )
        analysis = analyze_gdl_existing_type_domains(
            extract_gdl_typing_constraints(
                parse_gdl_source_presentation(
                    "(<= (p ?x) (narrow ?x))\n"
                ),
                profile,
            ),
            profile,
        )
        argument = next(
            domain
            for domain in analysis.derived_domains
            if domain.slot == GdlDerivedSignatureType("p", 1, 0)
        )
        self.assertEqual(argument.candidate_types, ("large", "small"))
        self.assertEqual(argument.kind, GdlDerivedDomainKind.MULTIPLE)
        self.assertTrue(
            analysis.components[argument.component_ordinal].eliminations
        )

    def test_finite_assignments_replay_and_preserve_alternatives(self) -> None:
        profile = parse_gdl_type_profile(
            "narrow :: small -> bool.\nsmall :> large.\n"
        )
        constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation(
                "(<= (p ?x) (narrow ?x))\n"
            ),
            profile,
        )
        analysis = analyze_gdl_existing_type_domains(
            constraints, profile
        )
        argument = next(
            domain
            for domain in analysis.derived_domains
            if domain.slot == GdlDerivedSignatureType("p", 1, 0)
        )
        small = find_gdl_finite_type_assignment(
            constraints,
            analysis,
            (
                GdlFiniteTypeChoice(
                    argument.component_ordinal, "small"
                ),
            ),
        )
        large = find_gdl_finite_type_assignment(
            constraints,
            analysis,
            (
                GdlFiniteTypeChoice(
                    argument.component_ordinal, "large"
                ),
            ),
        )
        self.assertIsNotNone(small)
        self.assertIsNotNone(large)
        assert small is not None and large is not None
        self.assertNotEqual(small.assignment, large.assignment)
        self.assertEqual(len(large.equalities), len(constraints.equalities))
        self.assertEqual(
            len(large.acceptances), len(constraints.acceptances)
        )
        self.assertTrue(
            any(discharge.path.steps for discharge in large.acceptances)
        )
        projection = project_gdl_derived_finite_completions(
            constraints, analysis
        )
        self.assertIsNotNone(projection)
        assert projection is not None
        projected_argument = next(
            domain
            for domain in projection.derived_domains
            if domain.slot == argument.slot
        )
        self.assertEqual(
            tuple(
                completion.choice.type_name
                for completion in projected_argument.completions
            ),
            ("large", "small"),
        )
        self.assertEqual(
            projected_argument.globally_unsupported_types, ()
        )
        small_typed = project_gdl_finite_typed_occurrences(
            constraints, analysis, small.assignment
        )
        large_typed = project_gdl_finite_typed_occurrences(
            constraints, analysis, large.assignment
        )
        small_signature = next(
            signature
            for signature in small_typed.derived_signatures
            if (signature.name, signature.arity) == ("p", 1)
        )
        large_signature = next(
            signature
            for signature in large_typed.derived_signatures
            if (signature.name, signature.arity) == ("p", 1)
        )
        self.assertEqual(
            (small_signature.argument_types, small_signature.result_type),
            (("small",), "bool"),
        )
        self.assertEqual(
            (large_signature.argument_types, large_signature.result_type),
            (("large",), "bool"),
        )
        self.assertEqual(
            small_signature.argument_component_ordinals,
            large_signature.argument_component_ordinals,
        )

        self.assertEqual(
            len(small_typed.applications), len(constraints.applications)
        )
        self.assertEqual(
            len(small_typed.resolved_expressions),
            sum(len(component.members) for component in analysis.components),
        )
        small_extension = check_gdl_type_of_extension(
            constraints, profile, small_typed
        )
        large_extension = check_gdl_type_of_extension(
            constraints, profile, large_typed
        )
        self.assertEqual(small_extension.judgment_head, "type:of")
        self.assertEqual(large_extension.judgment_head, "type:of")
        self.assertNotEqual(
            small_extension.proposal.witness.assignment,
            large_extension.proposal.witness.assignment,
        )
        self.assertEqual(
            len(small_extension.occurrence_judgments),
            len(constraints.occurrence_types),
        )
        small_rule = next(
            derivation.rule
            for derivation in small_extension.application_derivations
            if derivation.application.name == "p"
        )
        large_rule = next(
            derivation.rule
            for derivation in large_extension.application_derivations
            if derivation.application.name == "p"
        )
        self.assertIsInstance(small_rule, GdlExtendedTypeOfRule)
        self.assertIsInstance(large_rule, GdlExtendedTypeOfRule)
        assert isinstance(small_rule, GdlExtendedTypeOfRule)
        assert isinstance(large_rule, GdlExtendedTypeOfRule)
        self.assertEqual(small_rule.signature.argument_types, ("small",))
        self.assertEqual(large_rule.signature.argument_types, ("large",))

    def test_rule_variable_greatest_assignment_is_checked_jointly(
        self,
    ) -> None:
        profile = parse_gdl_type_profile(
            "wide :: large -> bool.\nsmall :> large.\n"
        )
        constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation(
                "(<= (p ?x) (wide ?x))\n"
            ),
            profile,
        )
        analysis = analyze_gdl_existing_type_domains(
            constraints, profile
        )
        witness = find_gdl_rule_variable_greatest_assignment(
            constraints, analysis
        )
        self.assertIsNotNone(witness)
        assert witness is not None
        projection = project_gdl_finite_typed_occurrences(
            constraints, analysis, witness.assignment
        )
        resolved = {
            item.expression: item.type_name
            for item in projection.resolved_expressions
        }
        self.assertEqual(
            resolved[GdlRuleVariableType(0, "?x")], "large"
        )

    def test_rule_variable_incomparable_frontier_is_not_selected(
        self,
    ) -> None:
        profile = parse_gdl_type_profile("")
        constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation(
                "(<= (p ?x) (q ?x))\n"
            ),
            profile,
        )
        analysis = analyze_gdl_existing_type_domains(
            constraints, profile
        )
        self.assertIsNotNone(
            find_gdl_finite_type_assignment(constraints, analysis)
        )
        self.assertIsNone(
            find_gdl_rule_variable_greatest_assignment(
                constraints, analysis
            )
        )

    def test_checked_type_of_retains_duplicate_authored_rules(self) -> None:
        profile = parse_gdl_type_profile(
            "a :: item.\na :: item.\np :: item -> bool.\n"
        )
        constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation("(p a)\n"), profile
        )
        analysis = analyze_gdl_existing_type_domains(
            constraints, profile
        )
        assignment = find_gdl_finite_type_assignment(
            constraints, analysis
        )
        self.assertIsNotNone(assignment)
        assert assignment is not None
        proposal = project_gdl_finite_typed_occurrences(
            constraints, analysis, assignment.assignment
        )
        extension = check_gdl_type_of_extension(
            constraints, profile, proposal
        )
        a_rules = tuple(
            derivation.rule
            for derivation in extension.application_derivations
            if derivation.application.name == "a"
        )
        self.assertEqual(len(a_rules), 2)
        self.assertTrue(
            all(isinstance(rule, GdlAuthoredTypeOfRule) for rule in a_rules)
        )
        self.assertEqual(
            tuple(rule.signature.statement_ordinal for rule in a_rules),
            (0, 1),
        )
        inventory = inventory_gdl_checked_type_of_extension(extension)
        self.assertEqual(inventory.application_occurrences, 2)
        self.assertEqual(inventory.application_derivations, 3)

    def test_type_of_inference_lowering_preserves_proof_occurrences(
        self,
    ) -> None:
        source_text = "(p a)\n"
        profile_text = (
            "a :: item.\n"
            "a :: item.\n"
            "p :: item -> bool.\n"
        )
        source = parse_gdl_source_presentation(source_text)
        profile = parse_gdl_type_profile(profile_text)
        constraints = extract_gdl_typing_constraints(source, profile)
        analysis = analyze_gdl_existing_type_domains(constraints, profile)
        assignment = find_gdl_finite_type_assignment(
            constraints, analysis
        )
        self.assertIsNotNone(assignment)
        assert assignment is not None
        proposal = project_gdl_finite_typed_occurrences(
            constraints, analysis, assignment.assignment
        )
        extension = check_gdl_type_of_extension(
            constraints, profile, proposal
        )
        program = lower_gdl_type_of_inference_program(
            source,
            extension,
            source_digest=hashlib.sha256(source_text.encode()).hexdigest(),
            profile_digest=hashlib.sha256(
                profile_text.encode()
            ).hexdigest(),
        )

        self.assertEqual(
            tuple(
                (case.kind, case.source.path, case.expected_proofs)
                for case in program.cases
            ),
            (
                ("type:of", (), 2),
                ("type:of", (1,), 2),
                ("literal", (), 2),
            ),
        )
        self.assertEqual(
            sum(
                rule.identifier.startswith("gdl:signature-authored:")
                for rule in program.rules
            ),
            3,
        )
        rendered = render_gdl_type_of_inference_program(program)
        self.assertIn('(JDecl "type:of" 3)', rendered)
        self.assertIn("(GProof (GRuleInst", rendered)
        self.assertNotIn("NikAuthorityFrame", rendered)
        self.assertNotIn("(mode ", rendered)
        self.assertNotIn("authority", rendered.lower())

    def test_type_of_inference_lowering_rejects_source_rebinding(
        self,
    ) -> None:
        source_text = "(p a)\n"
        profile_text = "a :: item.\np :: item -> bool.\n"
        source = parse_gdl_source_presentation(source_text)
        profile = parse_gdl_type_profile(profile_text)
        constraints = extract_gdl_typing_constraints(source, profile)
        analysis = analyze_gdl_existing_type_domains(constraints, profile)
        assignment = find_gdl_finite_type_assignment(
            constraints, analysis
        )
        self.assertIsNotNone(assignment)
        assert assignment is not None
        proposal = project_gdl_finite_typed_occurrences(
            constraints, analysis, assignment.assignment
        )
        extension = check_gdl_type_of_extension(
            constraints, profile, proposal
        )

        with self.assertRaisesRegex(
            PresentationError, "source occurrences do not match"
        ):
            lower_gdl_type_of_inference_program(
                parse_gdl_source_presentation("(p a b)\n"),
                extension,
                source_digest=hashlib.sha256(b"(p a b)\n").hexdigest(),
                profile_digest=hashlib.sha256(
                    profile_text.encode()
                ).hexdigest(),
            )

    def test_checked_type_of_rejects_ambiguous_or_tampered_input(self) -> None:
        profile = parse_gdl_type_profile(
            "f :: small -> bool.\n"
            "f :: large -> bool.\n"
            "a :: small.\n"
        )
        constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation("(f a)\n"), profile
        )
        analysis = analyze_gdl_existing_type_domains(
            constraints, profile
        )
        assignment = find_gdl_finite_type_assignment(
            constraints, analysis
        )
        self.assertIsNotNone(assignment)
        assert assignment is not None
        proposal = project_gdl_finite_typed_occurrences(
            constraints, analysis, assignment.assignment
        )
        with self.assertRaisesRegex(
            PresentationError, "ambiguous authored rule"
        ):
            check_gdl_type_of_extension(constraints, profile, proposal)

        unambiguous_profile = parse_gdl_type_profile(
            "f :: small -> bool.\na :: small.\n"
        )
        unambiguous_constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation("(f a)\n"),
            unambiguous_profile,
        )
        unambiguous_analysis = analyze_gdl_existing_type_domains(
            unambiguous_constraints, unambiguous_profile
        )
        unambiguous_assignment = find_gdl_finite_type_assignment(
            unambiguous_constraints, unambiguous_analysis
        )
        self.assertIsNotNone(unambiguous_assignment)
        assert unambiguous_assignment is not None
        unambiguous_proposal = project_gdl_finite_typed_occurrences(
            unambiguous_constraints,
            unambiguous_analysis,
            unambiguous_assignment.assignment,
        )
        tampered = replace(
            unambiguous_proposal,
            applications=unambiguous_proposal.applications[:-1],
        )
        with self.assertRaisesRegex(
            PresentationError, "does not replay from its witness"
        ):
            check_gdl_type_of_extension(
                unambiguous_constraints, unambiguous_profile, tampered
            )

    def test_finite_assignment_rejects_partial_or_impossible_choice(
        self,
    ) -> None:
        profile = parse_gdl_type_profile(
            "narrow :: small -> bool.\nsmall :> large.\n"
        )
        constraints = extract_gdl_typing_constraints(
            parse_gdl_source_presentation(
                "(<= (p ?x) (narrow ?x))\n"
            ),
            profile,
        )
        analysis = analyze_gdl_existing_type_domains(
            constraints, profile
        )
        argument = next(
            domain
            for domain in analysis.derived_domains
            if domain.slot == GdlDerivedSignatureType("p", 1, 0)
        )
        self.assertIsNone(
            find_gdl_finite_type_assignment(
                constraints,
                analysis,
                (
                    GdlFiniteTypeChoice(
                        argument.component_ordinal, "bool"
                    ),
                ),
            )
        )
        with self.assertRaisesRegex(
            PresentationError, "omits components"
        ):
            replay_gdl_finite_type_assignment(
                constraints,
                analysis,
                GdlFiniteTypeAssignment(()),
            )

    def test_existing_type_domains_expose_finite_inconsistency(self) -> None:
        profile = parse_gdl_type_profile(
            "left :: first -> bool.\nright :: second -> bool.\n"
        )
        analysis = analyze_gdl_existing_type_domains(
            extract_gdl_typing_constraints(
                parse_gdl_source_presentation(
                    "(<= (p ?x) (left ?x) (right ?x))\n"
                ),
                profile,
            ),
            profile,
        )
        argument = next(
            domain
            for domain in analysis.derived_domains
            if domain.slot == GdlDerivedSignatureType("p", 1, 0)
        )
        self.assertEqual(argument.kind, GdlDerivedDomainKind.EMPTY)
        component = analysis.components[argument.component_ordinal]
        self.assertEqual(component.candidate_types, ())
        self.assertTrue(component.eliminations)
        self.assertIsNone(
            project_gdl_derived_finite_completions(
                extract_gdl_typing_constraints(
                    parse_gdl_source_presentation(
                        "(<= (p ?x) (left ?x) (right ?x))\n"
                    ),
                    profile,
                ),
                analysis,
            )
        )

    def test_existing_type_domains_leave_open_questions_multiple(self) -> None:
        profile = parse_gdl_type_profile("")
        analysis = analyze_gdl_existing_type_domains(
            extract_gdl_typing_constraints(
                parse_gdl_source_presentation(
                    "(<= (p ?x) (q ?x))\n"
                ),
                profile,
            ),
            profile,
        )
        inventory = inventory_gdl_existing_type_arc_analysis(analysis)
        self.assertEqual(inventory.derived_signatures, 2)
        self.assertGreater(inventory.multiple_derived_slots, 0)
        self.assertEqual(inventory.empty_derived_slots, 0)

    def test_empty_domain_receipt_retains_occurrence_and_evidence(self) -> None:
        profile = parse_gdl_type_profile(
            "a :: small.\n"
            "ill :: small -> prop.\n"
            "p :: small -> bool.\n"
        )
        analysis = analyze_gdl_existing_type_domains(
            extract_gdl_typing_constraints(
                parse_gdl_source_presentation(
                    "(ill a)\n"
                    "(<= helper (p a))\n"
                ),
                profile,
            ),
            profile,
        )
        receipts = gdl_empty_domain_receipts(analysis)
        self.assertEqual(len(receipts), 1)
        self.assertEqual(
            {
                (source.start_line, source.end_line)
                for source in receipts[0].source_occurrences
            },
            {(1, 1)},
        )
        self.assertEqual(receipts[0].derived_slots, ())
        self.assertTrue(receipts[0].equality_evidence)
        self.assertTrue(receipts[0].eliminations)
        inventory = inventory_gdl_empty_domain_receipts(
            receipts, analysis.universe
        )
        self.assertEqual(inventory.invalid_candidate_eliminations, 0)

    def test_empty_derived_domain_receipt_names_the_exact_slot(self) -> None:
        profile = parse_gdl_type_profile(
            "left :: first -> bool.\nright :: second -> bool.\n"
        )
        analysis = analyze_gdl_existing_type_domains(
            extract_gdl_typing_constraints(
                parse_gdl_source_presentation(
                    "(<= (p ?x) (left ?x) (right ?x))\n"
                ),
                profile,
            ),
            profile,
        )
        receipts = gdl_empty_domain_receipts(analysis)
        receipt = next(
            receipt
            for receipt in receipts
            if GdlDerivedSignatureType("p", 1, 0)
            in receipt.derived_slots
        )
        self.assertEqual(
            receipt.derived_slots,
            (GdlDerivedSignatureType("p", 1, 0),),
        )
        self.assertEqual(
            {source.form_ordinal for source in receipt.source_occurrences},
            {0},
        )

    def test_checked_manifest_accounts_for_all_200_tasks(self) -> None:
        self.assertEqual(
            CHECKER.validate_manifest(self.manifest, REPO), (28, 172)
        )

    def test_qualified_slice_is_seven_complete_games(self) -> None:
        self.assertEqual(
            tuple(item["game"] for item in self.manifest["planned_slice"]),
            (
                "minimal_decay",
                "minimal_even",
                "scissors_paper_stone",
                "buttons_and_lights",
                "multiplebuttonsandlights",
                "tron",
                "untwisty_corridor",
            ),
        )
        selected_tasks = [
            task
            for task in self.manifest["tasks"]
            if task["game"]
            in {
                "minimal_decay",
                "minimal_even",
                "scissors_paper_stone",
                "buttons_and_lights",
                "multiplebuttonsandlights",
                "tron",
                "untwisty_corridor",
            }
        ]
        self.assertEqual(len(selected_tasks), 28)
        self.assertEqual(
            {task["target"] for task in selected_tasks},
            {"goal", "legal", "next", "terminal"},
        )
        self.assertEqual(
            {task["conversion"]["status"] for task in selected_tasks},
            {"qualified"},
        )
        self.assertEqual(
            sum(
                task["conversion"]["cases"]["atom_occurrences"]
                for task in selected_tasks
            ),
            44188,
        )
        self.assertEqual(
            sum(
                task["conversion"]["cases"]["proof_occurrences"]
                for task in selected_tasks
            ),
            12124,
        )

    def test_pending_task_cannot_hide_its_reason(self) -> None:
        altered = copy.deepcopy(self.manifest)
        altered["tasks"][0]["conversion"].pop("reason")
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "pending conversion has no reason"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_qualified_task_requires_a_real_fixture_and_oracle(self) -> None:
        altered = copy.deepcopy(self.manifest)
        altered["tasks"][0]["conversion"] = {
            "status": "qualified",
            "coverage": "all-pinned-splits",
            "fixture": "examples/prime/not-an-iggp-qualification.metta",
        }
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "fixture or oracle is missing"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_proof_occurrences_cannot_erase_derived_cases(self) -> None:
        altered = copy.deepcopy(self.manifest)
        task = next(
            entry
            for entry in altered["tasks"]
            if entry["game"] == "minimal_even"
            and entry["target"] == "legal"
        )
        task["conversion"]["cases"]["proof_occurrences"] = 703
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "invalid qualification case counts"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_ttcc4_duplicate_occurrences_cannot_be_repaired_silently(
        self,
    ) -> None:
        altered = copy.deepcopy(self.manifest)
        anomalies = altered["source_anomalies"]
        anomalies["duplicate_atom_occurrences"][
            "data/train/ttcc4_goal_train.dat"
        ] = 0
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "source anomaly inventory drift"
        ):
            CHECKER.validate_manifest(altered, REPO)

    def test_task_parser_retains_duplicate_atoms_and_empty_positives(
        self,
    ) -> None:
        parsed = CHECKER.parse_task_data(
            b"atoms:\n\tp(a)\n\tp(a)\n---\n"
            b"statics:\n\tq(a)\n---\n"
            b"background:\n\tq(a)\n---\n"
            b"positives:\n\tp(a)\n---\n"
            b"background:\n---\n"
            b"positives:\n---\n",
            "synthetic.dat",
        )
        self.assertEqual(parsed["atoms"], ("p(a)", "p(a)"))
        self.assertEqual(parsed["metrics"]["atom_occurrences"], 2)
        self.assertEqual(parsed["metrics"]["unique_atoms"], 1)
        self.assertEqual(parsed["metrics"]["state_pairs"], 2)
        self.assertEqual(parsed["metrics"]["empty_positive_states"], 1)
        self.assertEqual(
            parsed["states"],
            (
                {"background": ("q(a)",), "positives": ("p(a)",)},
                {"background": (), "positives": ()},
            ),
        )

    def test_positive_outside_atoms_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            CHECKER.ManifestError, "positive is absent from atoms"
        ):
            CHECKER.parse_task_data(
                b"atoms:\n\tp(a)\n---\n"
                b"statics:\n---\n"
                b"background:\n---\n"
                b"positives:\n\tp(b)\n---\n",
                "synthetic.dat",
            )

    def test_task_digest_is_occurrence_sensitive(self) -> None:
        paths = {
            split: (f"data/{split}/g_goal_{split}.dat", b"p(a)\n")
            for split in CHECKER.SPLITS
        }
        first = CHECKER.task_source_digest(
            "types/g.typ", b"goal :: bool.\n", paths
        )
        changed = dict(paths)
        changed["train"] = (
            "data/train/g_goal_train.dat",
            b"p(a)\np(a)\n",
        )
        second = CHECKER.task_source_digest(
            "types/g.typ", b"goal :: bool.\n", changed
        )
        self.assertNotEqual(first, second)

    def test_minimal_decay_parser_retains_nested_ground_structure(self) -> None:
        self.assertEqual(
            GENERATOR.parse_ground_atom("outer(inner(1), value_2)"),
            GENERATOR.GroundAtom(
                "outer",
                (
                    GENERATOR.GroundAtom(
                        "inner", (GENERATOR.GroundAtom("1"),)
                    ),
                    GENERATOR.GroundAtom("value_2"),
                ),
            ),
        )

    def test_minimal_decay_successor_projection_keeps_gdl_direction(self) -> None:
        self.assertEqual(
            GENERATOR.prime_atom("succ(2, 3)", "episode"),
            "(decay:succ episode decay:n2 decay:n3)",
        )

    def test_minimal_even_keeps_both_num_proofs(self) -> None:
        self.assertEqual(EVEN_GENERATOR.NUM_PROOF_COUNTS[0], 1)
        self.assertEqual(EVEN_GENERATOR.NUM_PROOF_COUNTS[5], 2)
        self.assertEqual(EVEN_GENERATOR.NUM_PROOF_COUNTS[10], 1)

    def test_minimal_even_parity_is_an_ordinary_recursive_relation(self) -> None:
        self.assertEqual(
            EVEN_GENERATOR.EVEN_PROOF_COUNTS,
            {value: int(value % 2 == 0) for value in range(11)},
        )

    def test_minimal_even_retains_nested_choose_structure(self) -> None:
        self.assertEqual(
            EVEN_GENERATOR.prime_atom(
                "does_choose(player, 9)", "episode"
            ),
            "(minimal-even:does episode minimal-even:player "
            "(minimal-even:choose minimal-even:n9))",
        )

    def test_sps_score_route_retains_opponent_and_actions(self) -> None:
        state = SPS_GENERATOR.State(
            target="next",
            split="synthetic",
            ordinal=1,
            episode="episode",
            atoms=(),
            statics=SPS_GENERATOR.STATIC_CLOSURE,
            background=(
                "does(p1, scissors)",
                "does(p2, paper)",
                "true_score(p1, 1)",
                "true_step(1)",
            ),
            positives=(),
        )
        self.assertEqual(
            SPS_GENERATOR.score_routes(state, "p1", "2"),
            {
                ("win", "p2", "scissors", "paper", "1", "2"): 1,
            },
        )

    def test_buttons_state_view_constructs_scoped_absence(self) -> None:
        state = BUTTONS_GENERATOR.State(
            target="goal",
            split="synthetic",
            ordinal=1,
            episode="episode",
            atoms=("goal(robot, 0)",),
            statics=BUTTONS_GENERATOR.STATIC_CLOSURE,
            background=("true(2)", "true(p)"),
            positives=("goal(robot, 0)",),
        )
        self.assertEqual(
            BUTTONS_GENERATOR.state_view(state),
            ("present", "absent", "absent", "2"),
        )
        self.assertEqual(
            BUTTONS_GENERATOR.proof_count(state, "goal(robot, 0)"), 2
        )

    def test_finite_status_view_rejects_duplicate_or_unknown_members(
        self,
    ) -> None:
        self.assertEqual(
            MULTI_GENERATOR.finite_status_view(
                ("p",), ("p", "q", "r"), "synthetic"
            ),
            ("present", "absent", "absent"),
        )
        with self.assertRaisesRegex(
            MULTI_GENERATOR.GenerationError, "contains duplicates"
        ):
            MULTI_GENERATOR.finite_status_view(
                ("p", "p"), ("p", "q", "r"), "synthetic"
            )
        with self.assertRaisesRegex(
            MULTI_GENERATOR.GenerationError, "outside its universe"
        ):
            MULTI_GENERATOR.finite_status_view(
                ("x",), ("p", "q", "r"), "synthetic"
            )

    def test_multi_state_and_action_absence_are_episode_views(self) -> None:
        state = MULTI_GENERATOR.State(
            target="next",
            split="synthetic",
            ordinal=1,
            episode="episode",
            atoms=("next_p(9)",),
            statics=MULTI_GENERATOR.STATIC_CLOSURE,
            background=("true_p(9)", "true_q(1)", "true_step(7)"),
            positives=("next_p(9)",),
        )
        view = MULTI_GENERATOR.episode_view(state)
        self.assertEqual(view.light("9"), ("present", "absent", "absent"))
        self.assertEqual(view.action("9"), ("absent", "absent", "absent"))
        self.assertEqual(
            MULTI_GENERATOR.proof_count(state, "next_p(9)"), 1
        )

    def test_multi_persistence_retains_both_action_absence_proofs(self) -> None:
        declarations = "\n".join(MULTI_GENERATOR.render_target_proof_types())
        self.assertIn("multi:proof:both-not-does-a-b", declarations)
        self.assertIn(
            "(not-a : (multi:not-does state multi:robot (multi:a index)))",
            declarations,
        )
        self.assertIn(
            "(not-b : (multi:not-does state multi:robot (multi:b index)))",
            declarations,
        )
        self.assertIn(
            "(absences : (multi:both-not-does state multi:robot",
            declarations,
        )

    def test_tron_player_view_constructs_dead_and_not_dead(self) -> None:
        dead_state = TRON_GENERATOR.State(
            target="goal",
            split="synthetic",
            ordinal=1,
            episode="episode",
            atoms=("goal(black, 0)",),
            statics=TRON_GENERATOR.STATIC_CLOSURE,
            background=(
                "true_at(2, 2, x)",
                "true_at(5, 4, o)",
                "true_marked(2, 2)",
            ),
            positives=("goal(black, 0)",),
        )
        view = TRON_GENERATOR.episode_view(dead_state)
        self.assertEqual(view.player("black"), ("2", "2", "present"))
        self.assertEqual(view.player("white"), ("5", "4", "absent"))
        self.assertEqual(
            TRON_GENERATOR.proof_count(dead_state, "goal(black, 0)"), 1
        )
        self.assertEqual(
            TRON_GENERATOR.proof_count(dead_state, "goal(white, 100)"), 1
        )

    def test_tron_mark_persistence_retains_both_proof_routes(self) -> None:
        state = TRON_GENERATOR.State(
            target="next",
            split="synthetic",
            ordinal=1,
            episode="episode",
            atoms=("next_marked(2, 2)",),
            statics=TRON_GENERATOR.STATIC_CLOSURE,
            background=(
                "true_at(2, 2, x)",
                "true_at(5, 4, o)",
                "true_marked(2, 2)",
            ),
            positives=("next_marked(2, 2)",),
        )
        self.assertEqual(
            TRON_GENERATOR.proof_count(state, "next_marked(2, 2)"), 2
        )

    def test_corridor_action_and_persistence_are_distinct_proofs(self) -> None:
        state = CORRIDOR_GENERATOR.State(
            target="next",
            split="synthetic",
            ordinal=1,
            episode="episode",
            atoms=("next(p)",),
            statics=CORRIDOR_GENERATOR.STATIC_CLOSURE,
            background=(
                "does(robot, g)",
                "true(p)",
                "true(q1)",
                "true_step(7)",
            ),
            positives=("next(p)",),
        )
        self.assertEqual(
            CORRIDOR_GENERATOR.proof_count(state, "next(p)"), 2
        )

    def test_corridor_absence_advances_only_the_exact_h_path(self) -> None:
        state = CORRIDOR_GENERATOR.State(
            target="next",
            split="synthetic",
            ordinal=1,
            episode="episode",
            atoms=("next(q2)",),
            statics=CORRIDOR_GENERATOR.STATIC_CLOSURE,
            background=(
                "does(robot, h)",
                "true(q1)",
                "true_step(1)",
            ),
            positives=("next(q2)",),
        )
        self.assertEqual(
            CORRIDOR_GENERATOR.proof_count(state, "next(q2)"), 1
        )
        blocked = CORRIDOR_GENERATOR.State(
            **{
                **state.__dict__,
                "background": (
                    "does(robot, h)",
                    "true(p)",
                    "true(q1)",
                    "true_step(1)",
                ),
                "positives": (),
            }
        )
        self.assertEqual(
            CORRIDOR_GENERATOR.proof_count(blocked, "next(q2)"), 0
        )


if __name__ == "__main__":
    unittest.main()
