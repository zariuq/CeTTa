; Exact-image qualification for all four IGGP minimal_decay targets.
;
; Canonical GDL: games/minimal_decay.txt
; SHA-256: 3e42f2344243da41898ceb3fbb32603f417bc83aa0d939fc430b3b167bb9a174
; The excluded Prolog translation reverses the successor arguments;
; the canonical GDL and the independently generated data agree.
; Every atom occurrence in train, validate, and test is classified.
; A missing derivation is reported as not-derived, never as Refuted.

!(import! &self ../../lib/ilp/iggp_minimal_decay_types.metta)
!(import! &self ../../lib/ilp/iggp_minimal_decay_rules.metta)
!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)

(= (iggp:decay:classify $name $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (decay:package $episode) 24 1000000 128 (quote $goal))
    (iggp:classify-occurrences
      $name (quote $goal) $occurrences)))

!(iggp:decay:classify decay:goal:train:state-1:atom-1 decay:goal:train:state-1
  (quote (decay:goal decay:goal:train:state-1 decay:player decay:n0)))
!(iggp:decay:classify decay:goal:train:state-1:atom-2 decay:goal:train:state-1
  (quote (decay:goal decay:goal:train:state-1 decay:player decay:n1)))
!(iggp:decay:classify decay:goal:train:state-1:atom-3 decay:goal:train:state-1
  (quote (decay:goal decay:goal:train:state-1 decay:player decay:n2)))
!(iggp:decay:classify decay:goal:train:state-1:atom-4 decay:goal:train:state-1
  (quote (decay:goal decay:goal:train:state-1 decay:player decay:n3)))
!(iggp:decay:classify decay:goal:train:state-1:atom-5 decay:goal:train:state-1
  (quote (decay:goal decay:goal:train:state-1 decay:player decay:n4)))
!(iggp:decay:classify decay:goal:train:state-1:atom-6 decay:goal:train:state-1
  (quote (decay:goal decay:goal:train:state-1 decay:player decay:n5)))
!(iggp:decay:classify decay:goal:train:state-2:atom-1 decay:goal:train:state-2
  (quote (decay:goal decay:goal:train:state-2 decay:player decay:n0)))
!(iggp:decay:classify decay:goal:train:state-2:atom-2 decay:goal:train:state-2
  (quote (decay:goal decay:goal:train:state-2 decay:player decay:n1)))
!(iggp:decay:classify decay:goal:train:state-2:atom-3 decay:goal:train:state-2
  (quote (decay:goal decay:goal:train:state-2 decay:player decay:n2)))
!(iggp:decay:classify decay:goal:train:state-2:atom-4 decay:goal:train:state-2
  (quote (decay:goal decay:goal:train:state-2 decay:player decay:n3)))
!(iggp:decay:classify decay:goal:train:state-2:atom-5 decay:goal:train:state-2
  (quote (decay:goal decay:goal:train:state-2 decay:player decay:n4)))
!(iggp:decay:classify decay:goal:train:state-2:atom-6 decay:goal:train:state-2
  (quote (decay:goal decay:goal:train:state-2 decay:player decay:n5)))
!(iggp:decay:classify decay:goal:train:state-3:atom-1 decay:goal:train:state-3
  (quote (decay:goal decay:goal:train:state-3 decay:player decay:n0)))
!(iggp:decay:classify decay:goal:train:state-3:atom-2 decay:goal:train:state-3
  (quote (decay:goal decay:goal:train:state-3 decay:player decay:n1)))
!(iggp:decay:classify decay:goal:train:state-3:atom-3 decay:goal:train:state-3
  (quote (decay:goal decay:goal:train:state-3 decay:player decay:n2)))
!(iggp:decay:classify decay:goal:train:state-3:atom-4 decay:goal:train:state-3
  (quote (decay:goal decay:goal:train:state-3 decay:player decay:n3)))
!(iggp:decay:classify decay:goal:train:state-3:atom-5 decay:goal:train:state-3
  (quote (decay:goal decay:goal:train:state-3 decay:player decay:n4)))
!(iggp:decay:classify decay:goal:train:state-3:atom-6 decay:goal:train:state-3
  (quote (decay:goal decay:goal:train:state-3 decay:player decay:n5)))
!(iggp:decay:classify decay:goal:train:state-4:atom-1 decay:goal:train:state-4
  (quote (decay:goal decay:goal:train:state-4 decay:player decay:n0)))
!(iggp:decay:classify decay:goal:train:state-4:atom-2 decay:goal:train:state-4
  (quote (decay:goal decay:goal:train:state-4 decay:player decay:n1)))
!(iggp:decay:classify decay:goal:train:state-4:atom-3 decay:goal:train:state-4
  (quote (decay:goal decay:goal:train:state-4 decay:player decay:n2)))
!(iggp:decay:classify decay:goal:train:state-4:atom-4 decay:goal:train:state-4
  (quote (decay:goal decay:goal:train:state-4 decay:player decay:n3)))
!(iggp:decay:classify decay:goal:train:state-4:atom-5 decay:goal:train:state-4
  (quote (decay:goal decay:goal:train:state-4 decay:player decay:n4)))
!(iggp:decay:classify decay:goal:train:state-4:atom-6 decay:goal:train:state-4
  (quote (decay:goal decay:goal:train:state-4 decay:player decay:n5)))
!(iggp:decay:classify decay:goal:validate:state-1:atom-1 decay:goal:validate:state-1
  (quote (decay:goal decay:goal:validate:state-1 decay:player decay:n0)))
!(iggp:decay:classify decay:goal:validate:state-1:atom-2 decay:goal:validate:state-1
  (quote (decay:goal decay:goal:validate:state-1 decay:player decay:n1)))
!(iggp:decay:classify decay:goal:validate:state-1:atom-3 decay:goal:validate:state-1
  (quote (decay:goal decay:goal:validate:state-1 decay:player decay:n2)))
!(iggp:decay:classify decay:goal:validate:state-1:atom-4 decay:goal:validate:state-1
  (quote (decay:goal decay:goal:validate:state-1 decay:player decay:n3)))
!(iggp:decay:classify decay:goal:validate:state-1:atom-5 decay:goal:validate:state-1
  (quote (decay:goal decay:goal:validate:state-1 decay:player decay:n4)))
!(iggp:decay:classify decay:goal:validate:state-1:atom-6 decay:goal:validate:state-1
  (quote (decay:goal decay:goal:validate:state-1 decay:player decay:n5)))
!(iggp:decay:classify decay:goal:test:state-1:atom-1 decay:goal:test:state-1
  (quote (decay:goal decay:goal:test:state-1 decay:player decay:n0)))
!(iggp:decay:classify decay:goal:test:state-1:atom-2 decay:goal:test:state-1
  (quote (decay:goal decay:goal:test:state-1 decay:player decay:n1)))
!(iggp:decay:classify decay:goal:test:state-1:atom-3 decay:goal:test:state-1
  (quote (decay:goal decay:goal:test:state-1 decay:player decay:n2)))
!(iggp:decay:classify decay:goal:test:state-1:atom-4 decay:goal:test:state-1
  (quote (decay:goal decay:goal:test:state-1 decay:player decay:n3)))
!(iggp:decay:classify decay:goal:test:state-1:atom-5 decay:goal:test:state-1
  (quote (decay:goal decay:goal:test:state-1 decay:player decay:n4)))
!(iggp:decay:classify decay:goal:test:state-1:atom-6 decay:goal:test:state-1
  (quote (decay:goal decay:goal:test:state-1 decay:player decay:n5)))
!(iggp:decay:classify decay:goal:test:state-2:atom-1 decay:goal:test:state-2
  (quote (decay:goal decay:goal:test:state-2 decay:player decay:n0)))
!(iggp:decay:classify decay:goal:test:state-2:atom-2 decay:goal:test:state-2
  (quote (decay:goal decay:goal:test:state-2 decay:player decay:n1)))
!(iggp:decay:classify decay:goal:test:state-2:atom-3 decay:goal:test:state-2
  (quote (decay:goal decay:goal:test:state-2 decay:player decay:n2)))
!(iggp:decay:classify decay:goal:test:state-2:atom-4 decay:goal:test:state-2
  (quote (decay:goal decay:goal:test:state-2 decay:player decay:n3)))
!(iggp:decay:classify decay:goal:test:state-2:atom-5 decay:goal:test:state-2
  (quote (decay:goal decay:goal:test:state-2 decay:player decay:n4)))
!(iggp:decay:classify decay:goal:test:state-2:atom-6 decay:goal:test:state-2
  (quote (decay:goal decay:goal:test:state-2 decay:player decay:n5)))
!(iggp:decay:classify decay:legal:train:state-1:atom-1 decay:legal:train:state-1
  (quote (decay:legal decay:legal:train:state-1 decay:player decay:noop)))
!(iggp:decay:classify decay:legal:train:state-1:atom-2 decay:legal:train:state-1
  (quote (decay:legal decay:legal:train:state-1 decay:player decay:press-button)))
!(iggp:decay:classify decay:legal:train:state-2:atom-1 decay:legal:train:state-2
  (quote (decay:legal decay:legal:train:state-2 decay:player decay:noop)))
!(iggp:decay:classify decay:legal:train:state-2:atom-2 decay:legal:train:state-2
  (quote (decay:legal decay:legal:train:state-2 decay:player decay:press-button)))
!(iggp:decay:classify decay:legal:train:state-3:atom-1 decay:legal:train:state-3
  (quote (decay:legal decay:legal:train:state-3 decay:player decay:noop)))
!(iggp:decay:classify decay:legal:train:state-3:atom-2 decay:legal:train:state-3
  (quote (decay:legal decay:legal:train:state-3 decay:player decay:press-button)))
!(iggp:decay:classify decay:legal:train:state-4:atom-1 decay:legal:train:state-4
  (quote (decay:legal decay:legal:train:state-4 decay:player decay:noop)))
!(iggp:decay:classify decay:legal:train:state-4:atom-2 decay:legal:train:state-4
  (quote (decay:legal decay:legal:train:state-4 decay:player decay:press-button)))
!(iggp:decay:classify decay:legal:validate:state-1:atom-1 decay:legal:validate:state-1
  (quote (decay:legal decay:legal:validate:state-1 decay:player decay:noop)))
!(iggp:decay:classify decay:legal:validate:state-1:atom-2 decay:legal:validate:state-1
  (quote (decay:legal decay:legal:validate:state-1 decay:player decay:press-button)))
!(iggp:decay:classify decay:legal:test:state-1:atom-1 decay:legal:test:state-1
  (quote (decay:legal decay:legal:test:state-1 decay:player decay:noop)))
!(iggp:decay:classify decay:legal:test:state-1:atom-2 decay:legal:test:state-1
  (quote (decay:legal decay:legal:test:state-1 decay:player decay:press-button)))
!(iggp:decay:classify decay:legal:test:state-2:atom-1 decay:legal:test:state-2
  (quote (decay:legal decay:legal:test:state-2 decay:player decay:noop)))
!(iggp:decay:classify decay:legal:test:state-2:atom-2 decay:legal:test:state-2
  (quote (decay:legal decay:legal:test:state-2 decay:player decay:press-button)))
!(iggp:decay:classify decay:next:train:state-1:atom-1 decay:next:train:state-1
  (quote (decay:next-value decay:next:train:state-1 decay:n0)))
!(iggp:decay:classify decay:next:train:state-1:atom-2 decay:next:train:state-1
  (quote (decay:next-value decay:next:train:state-1 decay:n1)))
!(iggp:decay:classify decay:next:train:state-1:atom-3 decay:next:train:state-1
  (quote (decay:next-value decay:next:train:state-1 decay:n2)))
!(iggp:decay:classify decay:next:train:state-1:atom-4 decay:next:train:state-1
  (quote (decay:next-value decay:next:train:state-1 decay:n3)))
!(iggp:decay:classify decay:next:train:state-1:atom-5 decay:next:train:state-1
  (quote (decay:next-value decay:next:train:state-1 decay:n4)))
!(iggp:decay:classify decay:next:train:state-1:atom-6 decay:next:train:state-1
  (quote (decay:next-value decay:next:train:state-1 decay:n5)))
!(iggp:decay:classify decay:next:train:state-2:atom-1 decay:next:train:state-2
  (quote (decay:next-value decay:next:train:state-2 decay:n0)))
!(iggp:decay:classify decay:next:train:state-2:atom-2 decay:next:train:state-2
  (quote (decay:next-value decay:next:train:state-2 decay:n1)))
!(iggp:decay:classify decay:next:train:state-2:atom-3 decay:next:train:state-2
  (quote (decay:next-value decay:next:train:state-2 decay:n2)))
!(iggp:decay:classify decay:next:train:state-2:atom-4 decay:next:train:state-2
  (quote (decay:next-value decay:next:train:state-2 decay:n3)))
!(iggp:decay:classify decay:next:train:state-2:atom-5 decay:next:train:state-2
  (quote (decay:next-value decay:next:train:state-2 decay:n4)))
!(iggp:decay:classify decay:next:train:state-2:atom-6 decay:next:train:state-2
  (quote (decay:next-value decay:next:train:state-2 decay:n5)))
!(iggp:decay:classify decay:next:train:state-3:atom-1 decay:next:train:state-3
  (quote (decay:next-value decay:next:train:state-3 decay:n0)))
!(iggp:decay:classify decay:next:train:state-3:atom-2 decay:next:train:state-3
  (quote (decay:next-value decay:next:train:state-3 decay:n1)))
!(iggp:decay:classify decay:next:train:state-3:atom-3 decay:next:train:state-3
  (quote (decay:next-value decay:next:train:state-3 decay:n2)))
!(iggp:decay:classify decay:next:train:state-3:atom-4 decay:next:train:state-3
  (quote (decay:next-value decay:next:train:state-3 decay:n3)))
!(iggp:decay:classify decay:next:train:state-3:atom-5 decay:next:train:state-3
  (quote (decay:next-value decay:next:train:state-3 decay:n4)))
!(iggp:decay:classify decay:next:train:state-3:atom-6 decay:next:train:state-3
  (quote (decay:next-value decay:next:train:state-3 decay:n5)))
!(iggp:decay:classify decay:next:train:state-4:atom-1 decay:next:train:state-4
  (quote (decay:next-value decay:next:train:state-4 decay:n0)))
!(iggp:decay:classify decay:next:train:state-4:atom-2 decay:next:train:state-4
  (quote (decay:next-value decay:next:train:state-4 decay:n1)))
!(iggp:decay:classify decay:next:train:state-4:atom-3 decay:next:train:state-4
  (quote (decay:next-value decay:next:train:state-4 decay:n2)))
!(iggp:decay:classify decay:next:train:state-4:atom-4 decay:next:train:state-4
  (quote (decay:next-value decay:next:train:state-4 decay:n3)))
!(iggp:decay:classify decay:next:train:state-4:atom-5 decay:next:train:state-4
  (quote (decay:next-value decay:next:train:state-4 decay:n4)))
!(iggp:decay:classify decay:next:train:state-4:atom-6 decay:next:train:state-4
  (quote (decay:next-value decay:next:train:state-4 decay:n5)))
!(iggp:decay:classify decay:next:train:state-5:atom-1 decay:next:train:state-5
  (quote (decay:next-value decay:next:train:state-5 decay:n0)))
!(iggp:decay:classify decay:next:train:state-5:atom-2 decay:next:train:state-5
  (quote (decay:next-value decay:next:train:state-5 decay:n1)))
!(iggp:decay:classify decay:next:train:state-5:atom-3 decay:next:train:state-5
  (quote (decay:next-value decay:next:train:state-5 decay:n2)))
!(iggp:decay:classify decay:next:train:state-5:atom-4 decay:next:train:state-5
  (quote (decay:next-value decay:next:train:state-5 decay:n3)))
!(iggp:decay:classify decay:next:train:state-5:atom-5 decay:next:train:state-5
  (quote (decay:next-value decay:next:train:state-5 decay:n4)))
!(iggp:decay:classify decay:next:train:state-5:atom-6 decay:next:train:state-5
  (quote (decay:next-value decay:next:train:state-5 decay:n5)))
!(iggp:decay:classify decay:next:train:state-6:atom-1 decay:next:train:state-6
  (quote (decay:next-value decay:next:train:state-6 decay:n0)))
!(iggp:decay:classify decay:next:train:state-6:atom-2 decay:next:train:state-6
  (quote (decay:next-value decay:next:train:state-6 decay:n1)))
!(iggp:decay:classify decay:next:train:state-6:atom-3 decay:next:train:state-6
  (quote (decay:next-value decay:next:train:state-6 decay:n2)))
!(iggp:decay:classify decay:next:train:state-6:atom-4 decay:next:train:state-6
  (quote (decay:next-value decay:next:train:state-6 decay:n3)))
!(iggp:decay:classify decay:next:train:state-6:atom-5 decay:next:train:state-6
  (quote (decay:next-value decay:next:train:state-6 decay:n4)))
!(iggp:decay:classify decay:next:train:state-6:atom-6 decay:next:train:state-6
  (quote (decay:next-value decay:next:train:state-6 decay:n5)))
!(iggp:decay:classify decay:next:train:state-7:atom-1 decay:next:train:state-7
  (quote (decay:next-value decay:next:train:state-7 decay:n0)))
!(iggp:decay:classify decay:next:train:state-7:atom-2 decay:next:train:state-7
  (quote (decay:next-value decay:next:train:state-7 decay:n1)))
!(iggp:decay:classify decay:next:train:state-7:atom-3 decay:next:train:state-7
  (quote (decay:next-value decay:next:train:state-7 decay:n2)))
!(iggp:decay:classify decay:next:train:state-7:atom-4 decay:next:train:state-7
  (quote (decay:next-value decay:next:train:state-7 decay:n3)))
!(iggp:decay:classify decay:next:train:state-7:atom-5 decay:next:train:state-7
  (quote (decay:next-value decay:next:train:state-7 decay:n4)))
!(iggp:decay:classify decay:next:train:state-7:atom-6 decay:next:train:state-7
  (quote (decay:next-value decay:next:train:state-7 decay:n5)))
!(iggp:decay:classify decay:next:train:state-8:atom-1 decay:next:train:state-8
  (quote (decay:next-value decay:next:train:state-8 decay:n0)))
!(iggp:decay:classify decay:next:train:state-8:atom-2 decay:next:train:state-8
  (quote (decay:next-value decay:next:train:state-8 decay:n1)))
!(iggp:decay:classify decay:next:train:state-8:atom-3 decay:next:train:state-8
  (quote (decay:next-value decay:next:train:state-8 decay:n2)))
!(iggp:decay:classify decay:next:train:state-8:atom-4 decay:next:train:state-8
  (quote (decay:next-value decay:next:train:state-8 decay:n3)))
!(iggp:decay:classify decay:next:train:state-8:atom-5 decay:next:train:state-8
  (quote (decay:next-value decay:next:train:state-8 decay:n4)))
!(iggp:decay:classify decay:next:train:state-8:atom-6 decay:next:train:state-8
  (quote (decay:next-value decay:next:train:state-8 decay:n5)))
!(iggp:decay:classify decay:next:train:state-9:atom-1 decay:next:train:state-9
  (quote (decay:next-value decay:next:train:state-9 decay:n0)))
!(iggp:decay:classify decay:next:train:state-9:atom-2 decay:next:train:state-9
  (quote (decay:next-value decay:next:train:state-9 decay:n1)))
!(iggp:decay:classify decay:next:train:state-9:atom-3 decay:next:train:state-9
  (quote (decay:next-value decay:next:train:state-9 decay:n2)))
!(iggp:decay:classify decay:next:train:state-9:atom-4 decay:next:train:state-9
  (quote (decay:next-value decay:next:train:state-9 decay:n3)))
!(iggp:decay:classify decay:next:train:state-9:atom-5 decay:next:train:state-9
  (quote (decay:next-value decay:next:train:state-9 decay:n4)))
!(iggp:decay:classify decay:next:train:state-9:atom-6 decay:next:train:state-9
  (quote (decay:next-value decay:next:train:state-9 decay:n5)))
!(iggp:decay:classify decay:next:validate:state-1:atom-1 decay:next:validate:state-1
  (quote (decay:next-value decay:next:validate:state-1 decay:n0)))
!(iggp:decay:classify decay:next:validate:state-1:atom-2 decay:next:validate:state-1
  (quote (decay:next-value decay:next:validate:state-1 decay:n1)))
!(iggp:decay:classify decay:next:validate:state-1:atom-3 decay:next:validate:state-1
  (quote (decay:next-value decay:next:validate:state-1 decay:n2)))
!(iggp:decay:classify decay:next:validate:state-1:atom-4 decay:next:validate:state-1
  (quote (decay:next-value decay:next:validate:state-1 decay:n3)))
!(iggp:decay:classify decay:next:validate:state-1:atom-5 decay:next:validate:state-1
  (quote (decay:next-value decay:next:validate:state-1 decay:n4)))
!(iggp:decay:classify decay:next:validate:state-1:atom-6 decay:next:validate:state-1
  (quote (decay:next-value decay:next:validate:state-1 decay:n5)))
!(iggp:decay:classify decay:next:validate:state-2:atom-1 decay:next:validate:state-2
  (quote (decay:next-value decay:next:validate:state-2 decay:n0)))
!(iggp:decay:classify decay:next:validate:state-2:atom-2 decay:next:validate:state-2
  (quote (decay:next-value decay:next:validate:state-2 decay:n1)))
!(iggp:decay:classify decay:next:validate:state-2:atom-3 decay:next:validate:state-2
  (quote (decay:next-value decay:next:validate:state-2 decay:n2)))
!(iggp:decay:classify decay:next:validate:state-2:atom-4 decay:next:validate:state-2
  (quote (decay:next-value decay:next:validate:state-2 decay:n3)))
!(iggp:decay:classify decay:next:validate:state-2:atom-5 decay:next:validate:state-2
  (quote (decay:next-value decay:next:validate:state-2 decay:n4)))
!(iggp:decay:classify decay:next:validate:state-2:atom-6 decay:next:validate:state-2
  (quote (decay:next-value decay:next:validate:state-2 decay:n5)))
!(iggp:decay:classify decay:next:test:state-1:atom-1 decay:next:test:state-1
  (quote (decay:next-value decay:next:test:state-1 decay:n0)))
!(iggp:decay:classify decay:next:test:state-1:atom-2 decay:next:test:state-1
  (quote (decay:next-value decay:next:test:state-1 decay:n1)))
!(iggp:decay:classify decay:next:test:state-1:atom-3 decay:next:test:state-1
  (quote (decay:next-value decay:next:test:state-1 decay:n2)))
!(iggp:decay:classify decay:next:test:state-1:atom-4 decay:next:test:state-1
  (quote (decay:next-value decay:next:test:state-1 decay:n3)))
!(iggp:decay:classify decay:next:test:state-1:atom-5 decay:next:test:state-1
  (quote (decay:next-value decay:next:test:state-1 decay:n4)))
!(iggp:decay:classify decay:next:test:state-1:atom-6 decay:next:test:state-1
  (quote (decay:next-value decay:next:test:state-1 decay:n5)))
!(iggp:decay:classify decay:next:test:state-2:atom-1 decay:next:test:state-2
  (quote (decay:next-value decay:next:test:state-2 decay:n0)))
!(iggp:decay:classify decay:next:test:state-2:atom-2 decay:next:test:state-2
  (quote (decay:next-value decay:next:test:state-2 decay:n1)))
!(iggp:decay:classify decay:next:test:state-2:atom-3 decay:next:test:state-2
  (quote (decay:next-value decay:next:test:state-2 decay:n2)))
!(iggp:decay:classify decay:next:test:state-2:atom-4 decay:next:test:state-2
  (quote (decay:next-value decay:next:test:state-2 decay:n3)))
!(iggp:decay:classify decay:next:test:state-2:atom-5 decay:next:test:state-2
  (quote (decay:next-value decay:next:test:state-2 decay:n4)))
!(iggp:decay:classify decay:next:test:state-2:atom-6 decay:next:test:state-2
  (quote (decay:next-value decay:next:test:state-2 decay:n5)))
!(iggp:decay:classify decay:next:test:state-3:atom-1 decay:next:test:state-3
  (quote (decay:next-value decay:next:test:state-3 decay:n0)))
!(iggp:decay:classify decay:next:test:state-3:atom-2 decay:next:test:state-3
  (quote (decay:next-value decay:next:test:state-3 decay:n1)))
!(iggp:decay:classify decay:next:test:state-3:atom-3 decay:next:test:state-3
  (quote (decay:next-value decay:next:test:state-3 decay:n2)))
!(iggp:decay:classify decay:next:test:state-3:atom-4 decay:next:test:state-3
  (quote (decay:next-value decay:next:test:state-3 decay:n3)))
!(iggp:decay:classify decay:next:test:state-3:atom-5 decay:next:test:state-3
  (quote (decay:next-value decay:next:test:state-3 decay:n4)))
!(iggp:decay:classify decay:next:test:state-3:atom-6 decay:next:test:state-3
  (quote (decay:next-value decay:next:test:state-3 decay:n5)))
!(iggp:decay:classify decay:terminal:train:state-1:atom-1 decay:terminal:train:state-1
  (quote (decay:terminal decay:terminal:train:state-1)))
!(iggp:decay:classify decay:terminal:train:state-2:atom-1 decay:terminal:train:state-2
  (quote (decay:terminal decay:terminal:train:state-2)))
!(iggp:decay:classify decay:terminal:train:state-3:atom-1 decay:terminal:train:state-3
  (quote (decay:terminal decay:terminal:train:state-3)))
!(iggp:decay:classify decay:terminal:train:state-4:atom-1 decay:terminal:train:state-4
  (quote (decay:terminal decay:terminal:train:state-4)))
!(iggp:decay:classify decay:terminal:validate:state-1:atom-1 decay:terminal:validate:state-1
  (quote (decay:terminal decay:terminal:validate:state-1)))
!(iggp:decay:classify decay:terminal:test:state-1:atom-1 decay:terminal:test:state-1
  (quote (decay:terminal decay:terminal:test:state-1)))
!(iggp:decay:classify decay:terminal:test:state-2:atom-1 decay:terminal:test:state-2
  (quote (decay:terminal decay:terminal:test:state-2)))
