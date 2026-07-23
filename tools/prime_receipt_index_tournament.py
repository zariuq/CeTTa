#!/usr/bin/env python3
"""Exact receipt-reachability candidates and deterministic cost witnesses.

The native receipt graph points from each newly allocated frame to zero or
more older parent frames.  This tournament compares representation choices
without changing that graph or selecting a native implementation:

* guarded traversal: current exact traversal after sound constant-time guards;
* materialized bitsets: exact transitive support stored at every frame;
* primary-tree binary lifting: online logarithmic accepts on one parent
  spine, exact traversal for every other case;
* Bloom rejection: constant-space approximate support used only to reject,
  with exact traversal on possible hits.

Every candidate is checked against an independent graph traversal on every
ordered frame pair.  The reported costs are deterministic parent-edge visits
and abstract retained bytes, not Python wall-clock measurements.

The self-test also kills a tempting but unsound shortcut: inheriting one
``(chain root, rank)`` label through every unary-parent frame.  Two unequal
siblings can inherit the same root, making the deeper sibling appear to reach
the shallower one.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import sys
import unittest
from collections.abc import Callable, Iterable
from pathlib import Path


@dataclasses.dataclass(frozen=True)
class Frame:
    session: int
    depth: int
    parents: tuple[int, ...]


@dataclasses.dataclass(frozen=True)
class ReceiptGraph:
    frames: tuple[Frame, ...]

    def __post_init__(self) -> None:
        for child_id, child in enumerate(self.frames):
            if len(set(child.parents)) != len(child.parents):
                raise ValueError(f"frame {child_id} repeats a parent")
            for parent_id in child.parents:
                if parent_id < 0 or parent_id >= child_id:
                    raise ValueError(
                        f"frame {child_id} parent {parent_id} is not older"
                    )
                parent = self.frames[parent_id]
                if parent.session != child.session:
                    raise ValueError(
                        f"frame {child_id} crosses a session boundary"
                    )
                if parent.depth >= child.depth:
                    raise ValueError(
                        f"frame {child_id} parent {parent_id} does not decrease depth"
                    )

    def __len__(self) -> int:
        return len(self.frames)


@dataclasses.dataclass
class Metrics:
    queries: int = 0
    fast_accepts: int = 0
    fast_rejects: int = 0
    fallback_queries: int = 0
    query_parent_edges: int = 0
    build_parent_edges: int = 0
    build_index_steps: int = 0
    query_index_steps: int = 0
    query_frames: int = 0
    retained_bytes: int = 0


def exact_traversal(
    graph: ReceiptGraph,
    child_id: int,
    target_id: int,
    metrics: Metrics | None = None,
) -> bool:
    """Independent reachability oracle with no depth/direct-edge shortcuts."""
    if graph.frames[child_id].session != graph.frames[target_id].session:
        return False
    stack = [child_id]
    seen: set[int] = set()
    while stack:
        frame_id = stack.pop()
        if frame_id in seen:
            continue
        seen.add(frame_id)
        if metrics is not None:
            metrics.query_frames += 1
        if frame_id == target_id:
            return True
        parents = graph.frames[frame_id].parents
        if metrics is not None:
            metrics.query_parent_edges += len(parents)
        stack.extend(parents)
    return False


class Candidate:
    name = "abstract"

    def __init__(self, graph: ReceiptGraph) -> None:
        self.graph = graph
        self.metrics = Metrics()

    def query(self, child_id: int, target_id: int) -> bool:
        raise NotImplementedError


class GuardedTraversal(Candidate):
    name = "guarded-traversal"

    def query(self, child_id: int, target_id: int) -> bool:
        self.metrics.queries += 1
        child = self.graph.frames[child_id]
        target = self.graph.frames[target_id]
        if child.session != target.session:
            self.metrics.fast_rejects += 1
            return False
        if child_id == target_id:
            self.metrics.fast_accepts += 1
            return True
        if child.depth <= target.depth:
            self.metrics.fast_rejects += 1
            return False
        if target_id in child.parents:
            self.metrics.fast_accepts += 1
            return True
        self.metrics.fallback_queries += 1
        return exact_traversal(
            self.graph, child_id, target_id, self.metrics
        )


class MaterializedBitsets(Candidate):
    name = "materialized-bitsets"

    def __init__(self, graph: ReceiptGraph) -> None:
        super().__init__(graph)
        support: list[int] = []
        for frame_id, frame in enumerate(graph.frames):
            bits = 1 << frame_id
            self.metrics.build_index_steps += 1
            for parent_id in frame.parents:
                self.metrics.build_parent_edges += 1
                self.metrics.build_index_steps += (
                    parent_id // 64
                ) + 1
                bits |= support[parent_id]
            support.append(bits)
        self.support = tuple(support)
        self.metrics.retained_bytes = sum(
            max(1, (bits.bit_length() + 7) // 8) for bits in support
        )

    def query(self, child_id: int, target_id: int) -> bool:
        self.metrics.queries += 1
        self.metrics.query_index_steps += 1
        return bool(self.support[child_id] & (1 << target_id))


class PrimaryTreeBinaryLifting(Candidate):
    name = "primary-tree-binary-lifting+fallback"

    def __init__(self, graph: ReceiptGraph) -> None:
        super().__init__(graph)
        tree_depths: list[int] = []
        ancestors: list[tuple[int, ...]] = []
        for child_id, frame in enumerate(graph.frames):
            self.metrics.build_parent_edges += len(frame.parents)
            if not frame.parents:
                tree_depths.append(0)
                ancestors.append(())
                continue
            parent_id = frame.parents[0]
            tree_depths.append(tree_depths[parent_id] + 1)
            row = [parent_id]
            level = 1
            while True:
                previous = row[level - 1]
                previous_row = ancestors[previous]
                if level - 1 >= len(previous_row):
                    break
                row.append(previous_row[level - 1])
                level += 1
            self.metrics.build_index_steps += len(row)
            ancestors.append(tuple(row))

        self.tree_depths = tuple(tree_depths)
        self.ancestors = tuple(ancestors)
        # One tree-depth word plus every retained ancestor pointer.
        self.metrics.retained_bytes = (
            len(graph) * 8
            + sum(len(row) for row in ancestors) * 8
        )

    def _primary_tree_ancestor(
        self, child_id: int, target_id: int
    ) -> bool:
        child_depth = self.tree_depths[child_id]
        target_depth = self.tree_depths[target_id]
        if target_depth > child_depth:
            return False
        current = child_id
        difference = child_depth - target_depth
        level = 0
        while difference:
            if difference & 1:
                row = self.ancestors[current]
                if level >= len(row):
                    return False
                current = row[level]
                self.metrics.query_index_steps += 1
            difference >>= 1
            level += 1
        self.metrics.query_index_steps += 1
        return current == target_id

    def query(self, child_id: int, target_id: int) -> bool:
        self.metrics.queries += 1
        child = self.graph.frames[child_id]
        target = self.graph.frames[target_id]
        if child.session != target.session:
            self.metrics.fast_rejects += 1
            return False
        if child_id == target_id:
            self.metrics.fast_accepts += 1
            return True
        if child.depth <= target.depth:
            self.metrics.fast_rejects += 1
            return False
        if target_id in child.parents:
            self.metrics.fast_accepts += 1
            return True
        if self._primary_tree_ancestor(child_id, target_id):
            self.metrics.fast_accepts += 1
            return True
        self.metrics.fallback_queries += 1
        return exact_traversal(
            self.graph, child_id, target_id, self.metrics
        )


class NaiveUnaryChainLabels(Candidate):
    name = "naive-unary-chain-labels"

    def __init__(self, graph: ReceiptGraph) -> None:
        super().__init__(graph)
        chain_roots: list[int] = []
        chain_ranks: list[int] = []
        for frame_id, frame in enumerate(graph.frames):
            self.metrics.build_parent_edges += len(frame.parents)
            if len(frame.parents) == 1:
                parent_id = frame.parents[0]
                chain_roots.append(chain_roots[parent_id])
                chain_ranks.append(chain_ranks[parent_id] + 1)
            else:
                chain_roots.append(frame_id)
                chain_ranks.append(0)
            self.metrics.build_index_steps += 1
        self.chain_roots = tuple(chain_roots)
        self.chain_ranks = tuple(chain_ranks)
        # One root identifier and one rank word per frame.
        self.metrics.retained_bytes = len(graph) * 16

    def _same_chain_ancestor(
        self, child_id: int, target_id: int
    ) -> bool:
        self.metrics.query_index_steps += 1
        return (
            self.chain_roots[child_id] == self.chain_roots[target_id]
            and self.chain_ranks[target_id] <= self.chain_ranks[child_id]
        )

    def query(self, child_id: int, target_id: int) -> bool:
        self.metrics.queries += 1
        child = self.graph.frames[child_id]
        target = self.graph.frames[target_id]
        if child.session != target.session:
            self.metrics.fast_rejects += 1
            return False
        if child_id == target_id:
            self.metrics.fast_accepts += 1
            return True
        if child.depth <= target.depth:
            self.metrics.fast_rejects += 1
            return False
        if target_id in child.parents:
            self.metrics.fast_accepts += 1
            return True
        if self._same_chain_ancestor(child_id, target_id):
            self.metrics.fast_accepts += 1
            return True
        self.metrics.fallback_queries += 1
        return exact_traversal(
            self.graph, child_id, target_id, self.metrics
        )


class BloomRejectTraversal(Candidate):
    name = "bloom-reject+fallback"

    def __init__(
        self, graph: ReceiptGraph, width: int = 64, hashes: int = 2
    ) -> None:
        if width <= 0 or hashes <= 0:
            raise ValueError("Bloom width and hash count must be positive")
        super().__init__(graph)
        self.width = width
        self.hashes = hashes
        masks: list[int] = []
        for frame_id, frame in enumerate(graph.frames):
            mask = self._frame_mask(frame_id)
            self.metrics.build_index_steps += self.hashes
            for parent_id in frame.parents:
                self.metrics.build_parent_edges += 1
                self.metrics.build_index_steps += (
                    self.width + 63
                ) // 64
                mask |= masks[parent_id]
            masks.append(mask)
        self.masks = tuple(masks)
        self.metrics.retained_bytes = (
            len(graph) * ((width + 7) // 8)
        )

    @staticmethod
    def _mix(value: int) -> int:
        value ^= value >> 16
        value *= 0x7FEB352D
        value ^= value >> 15
        value *= 0x846CA68B
        value ^= value >> 16
        return value & 0xFFFFFFFF

    def _frame_mask(self, frame_id: int) -> int:
        mask = 0
        seed = frame_id + 0x9E3779B9
        for index in range(self.hashes):
            bit = self._mix(seed + index * 0x85EBCA6B) % self.width
            mask |= 1 << bit
        return mask

    def query(self, child_id: int, target_id: int) -> bool:
        self.metrics.queries += 1
        child = self.graph.frames[child_id]
        target = self.graph.frames[target_id]
        if child.session != target.session:
            self.metrics.fast_rejects += 1
            return False
        if child_id == target_id:
            self.metrics.fast_accepts += 1
            return True
        if child.depth <= target.depth:
            self.metrics.fast_rejects += 1
            return False
        if target_id in child.parents:
            self.metrics.fast_accepts += 1
            return True
        self.metrics.query_index_steps += (
            self.hashes + (self.width + 63) // 64
        )
        target_mask = self._frame_mask(target_id)
        if self.masks[child_id] & target_mask != target_mask:
            # Every real ancestor's bits are ORed into the descendant mask.
            # An absent bit is therefore an exact rejection.
            self.metrics.fast_rejects += 1
            return False
        self.metrics.fallback_queries += 1
        return exact_traversal(
            self.graph, child_id, target_id, self.metrics
        )


CandidateFactory = Callable[[ReceiptGraph], Candidate]


def candidate_factories() -> tuple[CandidateFactory, ...]:
    return (
        GuardedTraversal,
        MaterializedBitsets,
        PrimaryTreeBinaryLifting,
        BloomRejectTraversal,
    )


def chain_graph(length: int, session: int = 0) -> ReceiptGraph:
    if length < 1:
        raise ValueError("chain length must be positive")
    frames = [Frame(session=session, depth=0, parents=())]
    for frame_id in range(1, length):
        frames.append(
            Frame(
                session=session,
                depth=frame_id,
                parents=(frame_id - 1,),
            )
        )
    return ReceiptGraph(tuple(frames))


def diamond_graph(levels: int) -> ReceiptGraph:
    if levels < 1:
        raise ValueError("diamond levels must be positive")
    frames = [Frame(session=0, depth=0, parents=())]
    previous = (0,)
    for level in range(1, levels + 1):
        left = len(frames)
        right = left + 1
        frames.append(
            Frame(session=0, depth=level, parents=previous)
        )
        frames.append(
            Frame(session=0, depth=level, parents=previous)
        )
        previous = (left, right)
    return ReceiptGraph(tuple(frames))


def uneven_fork_graph() -> ReceiptGraph:
    return ReceiptGraph(
        (
            Frame(session=0, depth=0, parents=()),
            Frame(session=0, depth=1, parents=(0,)),
            Frame(session=0, depth=1, parents=(0,)),
            Frame(session=0, depth=2, parents=(1,)),
        )
    )


def wide_join_graph(width: int) -> ReceiptGraph:
    if width < 2:
        raise ValueError("wide join width must be at least two")
    frames = [Frame(session=0, depth=0, parents=())]
    leaves = []
    for _ in range(width):
        leaves.append(len(frames))
        frames.append(Frame(session=0, depth=1, parents=(0,)))
    frames.append(
        Frame(session=0, depth=2, parents=tuple(leaves))
    )
    # Same session and shallower than the join, but disconnected.
    frames.append(Frame(session=0, depth=0, parents=()))
    return ReceiptGraph(tuple(frames))


def two_session_graph(length: int) -> ReceiptGraph:
    if length < 1:
        raise ValueError("session-chain length must be positive")
    frames: list[Frame] = []
    for session in (0, 1):
        previous: int | None = None
        for depth in range(length):
            frame_id = len(frames)
            parents = () if previous is None else (previous,)
            frames.append(
                Frame(session=session, depth=depth, parents=parents)
            )
            previous = frame_id
    return ReceiptGraph(tuple(frames))


def depth_only_query(
    graph: ReceiptGraph, child_id: int, target_id: int
) -> bool:
    child = graph.frames[child_id]
    target = graph.frames[target_id]
    return (
        child.session == target.session
        and target.depth <= child.depth
    )


def assert_candidate_exact(candidate: Candidate) -> int:
    reachable_pairs = 0
    for child_id in range(len(candidate.graph)):
        for target_id in range(len(candidate.graph)):
            expected = exact_traversal(
                candidate.graph, child_id, target_id
            )
            actual = candidate.query(child_id, target_id)
            if actual != expected:
                raise AssertionError(
                    f"{candidate.name}: {child_id}->{target_id}: "
                    f"expected {expected}, got {actual}"
                )
            reachable_pairs += int(actual)
    return reachable_pairs


def assert_candidate_exact_on_queries(
    candidate: Candidate,
    queries: Iterable[tuple[int, int]],
) -> int:
    reachable_pairs = 0
    for child_id, target_id in queries:
        expected = exact_traversal(
            candidate.graph, child_id, target_id
        )
        actual = candidate.query(child_id, target_id)
        if actual != expected:
            raise AssertionError(
                f"{candidate.name}: {child_id}->{target_id}: "
                f"expected {expected}, got {actual}"
            )
        reachable_pairs += int(actual)
    return reachable_pairs


def tournament_graphs(size: int) -> Iterable[tuple[str, ReceiptGraph]]:
    yield "chain", chain_graph(size)
    yield "diamonds", diamond_graph(max(1, size // 2))
    yield "uneven-fork", uneven_fork_graph()
    yield "wide-join", wide_join_graph(max(2, size))
    yield "two-sessions", two_session_graph(max(1, size // 2))


def print_header() -> None:
    print(
        "\t".join(
            (
                "workload",
                "frames",
                "candidate",
                "queries",
                "reachable_pairs",
                "fast_accepts",
                "fast_rejects",
                "fallback_queries",
                "build_parent_edges",
                "build_index_steps",
                "query_index_steps",
                "query_parent_edges",
                "query_frames",
                "retained_bytes",
            )
        )
    )


def print_row(
    workload: str,
    graph: ReceiptGraph,
    candidate: Candidate,
    reachable_pairs: int,
) -> None:
    metrics = candidate.metrics
    print(
        "\t".join(
            map(
                str,
                (
                    workload,
                    len(graph),
                    candidate.name,
                    metrics.queries,
                    reachable_pairs,
                    metrics.fast_accepts,
                    metrics.fast_rejects,
                    metrics.fallback_queries,
                    metrics.build_parent_edges,
                    metrics.build_index_steps,
                    metrics.query_index_steps,
                    metrics.query_parent_edges,
                    metrics.query_frames,
                    metrics.retained_bytes,
                ),
            )
        )
    )


def run_tournament(size: int) -> None:
    print_header()
    for workload, graph in tournament_graphs(size):
        for factory in candidate_factories():
            candidate = factory(graph)
            reachable_pairs = assert_candidate_exact(candidate)
            print_row(workload, graph, candidate, reachable_pairs)


def load_trace(
    path: Path,
) -> tuple[ReceiptGraph, tuple[tuple[int, int], ...], int, int]:
    with path.open(encoding="utf-8") as stream:
        document = json.load(stream)
    if document.get("format") != "cetta-prime-receipt-trace-v1":
        raise ValueError("unsupported receipt trace format")

    frames: list[Frame] = []
    for expected_id, raw in enumerate(document["frames"]):
        if raw["id"] != expected_id:
            raise ValueError("receipt trace frame IDs are not contiguous")
        frames.append(
            Frame(
                session=int(raw["session"]),
                depth=int(raw["depth"]),
                parents=tuple(map(int, raw["parents"])),
            )
        )
    graph = ReceiptGraph(tuple(frames))

    queries: list[tuple[int, int]] = []
    for raw in document["queries"]:
        child = int(raw["child"])
        target = int(raw["target"])
        if child < 0 or child >= len(graph):
            raise ValueError("receipt trace child ID is out of range")
        if target < 0 or target >= len(graph):
            raise ValueError("receipt trace target ID is out of range")
        queries.append((child, target))

    return (
        graph,
        tuple(queries),
        int(document.get("empty_target_queries", 0)),
        int(document.get("boundary_queries", 0)),
    )


def run_trace(path: Path) -> None:
    graph, queries, empty_targets, boundaries = load_trace(path)
    print_header()
    for factory in candidate_factories():
        candidate = factory(graph)
        reachable_pairs = assert_candidate_exact_on_queries(
            candidate, queries
        )
        print_row("native-trace", graph, candidate, reachable_pairs)
    print(
        "PrimeReceiptTraceReplaySummary "
        f"frames={len(graph)} indexed-queries={len(queries)} "
        f"empty-target={empty_targets} boundary={boundaries}",
        file=sys.stderr,
    )


class ReceiptIndexTournamentTests(unittest.TestCase):
    def test_every_candidate_is_exact_on_adversarial_graphs(self) -> None:
        for _, graph in tournament_graphs(12):
            for factory in candidate_factories():
                with self.subTest(
                    graph=len(graph), candidate=factory.__name__
                ):
                    assert_candidate_exact(factory(graph))

    def test_depth_only_has_a_same_session_false_positive(self) -> None:
        graph = wide_join_graph(4)
        join = len(graph) - 2
        orphan = len(graph) - 1
        self.assertTrue(depth_only_query(graph, join, orphan))
        self.assertFalse(exact_traversal(graph, join, orphan))

    def test_materialized_bitsets_trade_build_space_for_no_query_walks(
        self,
    ) -> None:
        graph = chain_graph(32)
        candidate = MaterializedBitsets(graph)
        assert_candidate_exact(candidate)
        self.assertEqual(candidate.metrics.query_parent_edges, 0)
        self.assertGreater(candidate.metrics.retained_bytes, 0)
        self.assertEqual(
            candidate.metrics.build_parent_edges, len(graph) - 1
        )
        self.assertGreater(candidate.metrics.build_index_steps, 0)
        self.assertEqual(
            candidate.metrics.query_index_steps,
            candidate.metrics.queries,
        )

    def test_primary_tree_binary_lifting_accepts_chain_without_fallback(
        self,
    ) -> None:
        graph = chain_graph(32)
        candidate = PrimaryTreeBinaryLifting(graph)
        self.assertTrue(candidate.query(31, 0))
        self.assertEqual(candidate.metrics.fallback_queries, 0)
        self.assertEqual(candidate.metrics.query_parent_edges, 0)
        self.assertGreater(candidate.metrics.query_index_steps, 0)

    def test_naive_unary_chain_labels_have_a_fork_false_positive(
        self,
    ) -> None:
        graph = uneven_fork_graph()
        candidate = NaiveUnaryChainLabels(graph)
        self.assertTrue(candidate.query(3, 2))
        self.assertFalse(exact_traversal(graph, 3, 2))

    def test_bloom_rejection_retains_exact_fallback(self) -> None:
        graph = wide_join_graph(24)
        candidate = BloomRejectTraversal(
            graph, width=8, hashes=2
        )
        # The deliberately tiny filter creates collisions; exact traversal on
        # possible hits must nevertheless preserve every answer.
        assert_candidate_exact(candidate)
        self.assertGreater(candidate.metrics.fallback_queries, 0)

    def test_trace_replay_preserves_duplicate_query_occurrences(
        self,
    ) -> None:
        candidate = GuardedTraversal(chain_graph(3))
        reachable = assert_candidate_exact_on_queries(
            candidate,
            ((2, 0), (2, 0), (0, 2)),
        )
        self.assertEqual(reachable, 2)
        self.assertEqual(candidate.metrics.queries, 3)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--size",
        type=int,
        default=64,
        help="base graph size for deterministic operation counts",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--self-test",
        action="store_true",
        help="run exactness and negative-witness tests",
    )
    mode.add_argument(
        "--trace",
        type=Path,
        help="replay a native trace captured under GDB",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.size < 2:
        raise SystemExit("--size must be at least two")
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(
            ReceiptIndexTournamentTests
        )
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        if result.wasSuccessful():
            print(
                "PrimeReceiptIndexTournamentSummary "
                "PASS exact-candidates=4 "
                "depth-only-killed=1 naive-chain-killed=1"
            )
            return 0
        return 1
    if args.trace is not None:
        run_trace(args.trace)
    else:
        run_tournament(args.size)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
