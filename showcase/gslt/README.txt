GSLT application showcases
==========================

From the repository root:

  make showcase-megalodon-reader
  make showcase-mm0-parser
  make showcase-metamath-proof
  make showcase-petta-self-interpretation

The first displays the compiled GSLT reader's construction of Megalodon
items, including how parsing changes when a notation goes out of scope.
The second prints the GLL and GLR results of reading MM0 declarations.
The third checks a Metamath proof through the generated PeTTa program in
CeTTa's C-only runtime. The last runs PeTTa expressions directly and with
the transform-rule PeTTa interpreter, reporting the guest step counts.

Set MEGALODON_SHOWCASE_INPUT, MM0_SHOWCASE_INPUT, or METAMATH_SHOWCASE_INPUT
to run a different input. Whole document collections are opt-in too:

  make showcase-megalodon-gslt-corpus MEGALODON_ROOT=/path/to/megalodon
  make showcase-mm0-textual-corpus MM0_ROOT=/path/to/mm0
  make showcase-metamath-proof-fixtures
  make showcase-metamath-petta-corpus METAMATH_TEST_ROOT_V1=/path/to/corpus
  make showcase-petta-transform-only-corpus PETTA_EXAMPLES_ROOT=/path/to/examples

For the GSLT-to-self-interpreter experiment, explicitly choose a directory
of recorded source invocations:

  make showcase-gslt-petta-replay GSLT_SHOWCASE_INPUT=runtime/my-invocations

The recorded runtime, import context, documents and data inputs still need
to match. The replay refuses changed evidence. It never builds a corpus
by running all readiness checks. To capture particular gates for this
experiment, see showcase/petta/capture_gslt.py --help.

Correctness tests and growth gates never invoke these showcases. They
probe the underlying library calls, compiler and reader boundaries,
proof values, and counted work on small or generated inputs.

The state-transport microbenchmark is independent of the demonstration:
`make bench-transform-state-replacement` counts the work of replacing one
row with an unchanged payload, at three payload widths.
