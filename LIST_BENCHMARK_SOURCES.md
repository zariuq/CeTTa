# List Benchmark Sources

This prototype benchmark lane is inspired by several established benchmark families,
but is intentionally adapted into small CeTTa-native kernels so we can compare
fast lists against explicit clists under one runtime.

Source families:
- Are We Fast Yet?: https://github.com/smarr/are-we-fast-yet
- GHC nofib: https://gitlab.haskell.org/ghc/nofib
- R7RS benchmarks: https://ecraven.github.io/r7rs-benchmarks/
- OCaml Sandmark: https://github.com/ocaml-bench/sandmark

Current kernel mapping:
- `build-sum`: AWFY-style build/traverse
- `reverse-sum`: Scheme/Haskell reverse-style recursion
- `append-sum`: nofib-style append pressure
- `map-sum`: macro/pipeline-style traversal inspired by Sandmark-like workloads

This is a first comparison lane, not a claim of source-level equivalence to the
original suites.
