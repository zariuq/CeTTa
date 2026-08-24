; Exact qualification for every Untwisty Corridor target task.
;
; Canonical GDL presentation: games/untwisty_corridor.pl
; SHA-256: 5202b3e657a95aaa71b0cecd5903a5b14f8631793d07d8bda43e4dbdf1b65d48
; The .pl-suffixed file is the complete parenthesized GDL source.
; No competing .txt rule source exists in the pinned revision.
; Every train, validation, and test atom occurrence is classified.

!(import! &self ../../lib/ilp/iggp_finite_view_types.metta)
!(import! &self ../../lib/ilp/iggp_untwisty_corridor_types.metta)
!(import! &self ../../lib/ilp/iggp_untwisty_corridor_rules.metta)
!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)

(= (iggp:corridor:classify $name $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (corridor:package $episode) 32 2000000 256 (quote $goal))
    (iggp:classify-occurrences
      $name (quote $goal) $occurrences)))

(= (corridor:proof-shape
     (corridor:proof:next-p-from-action $state $action $proof))
   (p-via-action $action))
(= (corridor:proof-shape
     (corridor:proof:next-p-persist $state $proof))
   p-via-persistence)
(= (iggp:corridor:proof-shapes $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (corridor:package $episode) 32 2000000 256 (quote $goal))
    (collapse
      (let (occurrence $proof-data) (superpose $occurrences)
        (let (quote $proof) $proof-data
          (corridor:proof-shape $proof))))))

!(iggp:corridor:classify corridor:goal:train:state-1:atom-1 corridor:goal:train:state-1
  (quote (corridor:goal corridor:goal:train:state-1 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:train:state-1:atom-2 corridor:goal:train:state-1
  (quote (corridor:goal corridor:goal:train:state-1 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:train:state-2:atom-1 corridor:goal:train:state-2
  (quote (corridor:goal corridor:goal:train:state-2 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:train:state-2:atom-2 corridor:goal:train:state-2
  (quote (corridor:goal corridor:goal:train:state-2 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:train:state-3:atom-1 corridor:goal:train:state-3
  (quote (corridor:goal corridor:goal:train:state-3 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:train:state-3:atom-2 corridor:goal:train:state-3
  (quote (corridor:goal corridor:goal:train:state-3 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:train:state-4:atom-1 corridor:goal:train:state-4
  (quote (corridor:goal corridor:goal:train:state-4 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:train:state-4:atom-2 corridor:goal:train:state-4
  (quote (corridor:goal corridor:goal:train:state-4 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:train:state-5:atom-1 corridor:goal:train:state-5
  (quote (corridor:goal corridor:goal:train:state-5 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:train:state-5:atom-2 corridor:goal:train:state-5
  (quote (corridor:goal corridor:goal:train:state-5 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:train:state-6:atom-1 corridor:goal:train:state-6
  (quote (corridor:goal corridor:goal:train:state-6 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:train:state-6:atom-2 corridor:goal:train:state-6
  (quote (corridor:goal corridor:goal:train:state-6 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:train:state-7:atom-1 corridor:goal:train:state-7
  (quote (corridor:goal corridor:goal:train:state-7 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:train:state-7:atom-2 corridor:goal:train:state-7
  (quote (corridor:goal corridor:goal:train:state-7 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:validate:state-1:atom-1 corridor:goal:validate:state-1
  (quote (corridor:goal corridor:goal:validate:state-1 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:validate:state-1:atom-2 corridor:goal:validate:state-1
  (quote (corridor:goal corridor:goal:validate:state-1 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:test:state-1:atom-1 corridor:goal:test:state-1
  (quote (corridor:goal corridor:goal:test:state-1 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:test:state-1:atom-2 corridor:goal:test:state-1
  (quote (corridor:goal corridor:goal:test:state-1 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:test:state-2:atom-1 corridor:goal:test:state-2
  (quote (corridor:goal corridor:goal:test:state-2 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:test:state-2:atom-2 corridor:goal:test:state-2
  (quote (corridor:goal corridor:goal:test:state-2 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:goal:test:state-3:atom-1 corridor:goal:test:state-3
  (quote (corridor:goal corridor:goal:test:state-3 corridor:robot corridor:n0)))
!(iggp:corridor:classify corridor:goal:test:state-3:atom-2 corridor:goal:test:state-3
  (quote (corridor:goal corridor:goal:test:state-3 corridor:robot corridor:n100)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-1 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-2 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-3 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-4 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-5 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-6 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-7 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:train:state-1:atom-8 corridor:legal:train:state-1
  (quote (corridor:legal corridor:legal:train:state-1 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-1 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-2 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-3 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-4 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-5 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-6 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-7 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:train:state-2:atom-8 corridor:legal:train:state-2
  (quote (corridor:legal corridor:legal:train:state-2 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-1 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-2 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-3 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-4 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-5 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-6 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-7 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:train:state-3:atom-8 corridor:legal:train:state-3
  (quote (corridor:legal corridor:legal:train:state-3 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-1 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-2 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-3 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-4 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-5 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-6 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-7 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:train:state-4:atom-8 corridor:legal:train:state-4
  (quote (corridor:legal corridor:legal:train:state-4 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-1 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-2 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-3 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-4 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-5 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-6 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-7 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:train:state-5:atom-8 corridor:legal:train:state-5
  (quote (corridor:legal corridor:legal:train:state-5 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-1 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-2 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-3 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-4 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-5 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-6 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-7 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:train:state-6:atom-8 corridor:legal:train:state-6
  (quote (corridor:legal corridor:legal:train:state-6 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-1 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-2 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-3 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-4 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-5 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-6 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-7 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:train:state-7:atom-8 corridor:legal:train:state-7
  (quote (corridor:legal corridor:legal:train:state-7 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-1 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-2 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-3 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-4 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-5 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-6 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-7 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:validate:state-1:atom-8 corridor:legal:validate:state-1
  (quote (corridor:legal corridor:legal:validate:state-1 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-1 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-2 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-3 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-4 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-5 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-6 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-7 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:test:state-1:atom-8 corridor:legal:test:state-1
  (quote (corridor:legal corridor:legal:test:state-1 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-1 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-2 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-3 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-4 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-5 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-6 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-7 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:test:state-2:atom-8 corridor:legal:test:state-2
  (quote (corridor:legal corridor:legal:test:state-2 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-1 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:a)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-2 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:b)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-3 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:c)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-4 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:d)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-5 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:e)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-6 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:f)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-7 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:g)))
!(iggp:corridor:classify corridor:legal:test:state-3:atom-8 corridor:legal:test:state-3
  (quote (corridor:legal corridor:legal:test:state-3 corridor:robot corridor:h)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-1 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-2 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-3 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-4 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-5 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-6 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-7 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-8 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-9 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-1:atom-10 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-1:atom-11 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-1:atom-12 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-1:atom-13 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-1:atom-14 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-1:atom-15 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-1:atom-16 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-1:atom-17 corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-1 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-2 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-3 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-4 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-5 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-6 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-7 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-8 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-9 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-2:atom-10 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-11 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-12 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-13 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-14 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-15 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-16 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-2:atom-17 corridor:next:train:state-2
  (quote (corridor:next corridor:next:train:state-2 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-1 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-2 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-3 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-4 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-5 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-6 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-7 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-8 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-9 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-3:atom-10 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-11 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-12 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-13 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-14 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-15 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-16 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-3:atom-17 corridor:next:train:state-3
  (quote (corridor:next corridor:next:train:state-3 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-1 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-2 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-3 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-4 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-5 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-6 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-7 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-8 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-9 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-4:atom-10 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-11 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-12 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-13 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-14 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-15 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-16 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-4:atom-17 corridor:next:train:state-4
  (quote (corridor:next corridor:next:train:state-4 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-1 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-2 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-3 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-4 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-5 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-6 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-7 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-8 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-9 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-5:atom-10 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-11 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-12 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-13 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-14 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-15 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-16 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-5:atom-17 corridor:next:train:state-5
  (quote (corridor:next corridor:next:train:state-5 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-1 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-2 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-3 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-4 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-5 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-6 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-7 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-8 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-9 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-6:atom-10 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-11 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-12 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-13 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-14 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-15 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-16 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-6:atom-17 corridor:next:train:state-6
  (quote (corridor:next corridor:next:train:state-6 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-1 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-2 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-3 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-4 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-5 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-6 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-7 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-8 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-9 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-7:atom-10 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-11 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-12 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-13 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-14 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-15 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-16 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-7:atom-17 corridor:next:train:state-7
  (quote (corridor:next corridor:next:train:state-7 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-1 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-2 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-3 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-4 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-5 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-6 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-7 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-8 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-9 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-8:atom-10 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-11 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-12 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-13 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-14 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-15 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-16 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-8:atom-17 corridor:next:train:state-8
  (quote (corridor:next corridor:next:train:state-8 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-1 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-2 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-3 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-4 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-5 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-6 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-7 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-8 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-9 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-9:atom-10 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-11 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-12 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-13 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-14 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-15 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-16 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-9:atom-17 corridor:next:train:state-9
  (quote (corridor:next corridor:next:train:state-9 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-1 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-2 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-3 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-4 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-5 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-6 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-7 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-8 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-9 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-10:atom-10 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-11 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-12 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-13 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-14 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-15 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-16 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-10:atom-17 corridor:next:train:state-10
  (quote (corridor:next corridor:next:train:state-10 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-1 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-2 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-3 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-4 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-5 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-6 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-7 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-8 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-9 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-11:atom-10 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-11 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-12 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-13 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-14 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-15 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-16 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-11:atom-17 corridor:next:train:state-11
  (quote (corridor:next corridor:next:train:state-11 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-1 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-2 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-3 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-4 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-5 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-6 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-7 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-8 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-9 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-12:atom-10 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-11 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-12 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-13 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-14 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-15 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-16 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-12:atom-17 corridor:next:train:state-12
  (quote (corridor:next corridor:next:train:state-12 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-1 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-2 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-3 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-4 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-5 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-6 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-7 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-8 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-9 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-13:atom-10 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-11 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-12 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-13 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-14 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-15 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-16 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-13:atom-17 corridor:next:train:state-13
  (quote (corridor:next corridor:next:train:state-13 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-1 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-2 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-3 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-4 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-5 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-6 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-7 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-8 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-9 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-14:atom-10 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-11 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-12 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-13 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-14 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-15 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-16 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-14:atom-17 corridor:next:train:state-14
  (quote (corridor:next corridor:next:train:state-14 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-1 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-2 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-3 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-4 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-5 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-6 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-7 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-8 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-9 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-15:atom-10 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-11 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-12 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-13 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-14 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-15 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-16 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-15:atom-17 corridor:next:train:state-15
  (quote (corridor:next corridor:next:train:state-15 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-1 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-2 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-3 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-4 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-5 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-6 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-7 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-8 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-9 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-16:atom-10 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-11 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-12 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-13 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-14 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-15 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-16 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-16:atom-17 corridor:next:train:state-16
  (quote (corridor:next corridor:next:train:state-16 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-1 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-2 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-3 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-4 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-5 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-6 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-7 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-8 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-9 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-17:atom-10 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-11 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-12 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-13 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-14 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-15 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-16 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-17:atom-17 corridor:next:train:state-17
  (quote (corridor:next corridor:next:train:state-17 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-1 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-2 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-3 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-4 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-5 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-6 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-7 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-8 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-9 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-18:atom-10 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-11 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-12 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-13 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-14 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-15 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-16 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-18:atom-17 corridor:next:train:state-18
  (quote (corridor:next corridor:next:train:state-18 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-1 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-2 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-3 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-4 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-5 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-6 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-7 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-8 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-9 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-19:atom-10 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-11 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-12 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-13 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-14 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-15 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-16 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-19:atom-17 corridor:next:train:state-19
  (quote (corridor:next corridor:next:train:state-19 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-1 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-2 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-3 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-4 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-5 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-6 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-7 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-8 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-9 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-20:atom-10 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-11 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-12 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-13 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-14 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-15 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-16 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-20:atom-17 corridor:next:train:state-20
  (quote (corridor:next corridor:next:train:state-20 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-1 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-2 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-3 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-4 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-5 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-6 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-7 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-8 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-9 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-21:atom-10 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-11 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-12 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-13 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-14 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-15 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-16 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-21:atom-17 corridor:next:train:state-21
  (quote (corridor:next corridor:next:train:state-21 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-1 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-2 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-3 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-4 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-5 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-6 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-7 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-8 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-9 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-22:atom-10 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-11 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-12 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-13 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-14 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-15 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-16 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-22:atom-17 corridor:next:train:state-22
  (quote (corridor:next corridor:next:train:state-22 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-1 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-2 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-3 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-4 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-5 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-6 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-7 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-8 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-9 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-23:atom-10 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-11 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-12 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-13 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-14 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-15 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-16 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-23:atom-17 corridor:next:train:state-23
  (quote (corridor:next corridor:next:train:state-23 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-1 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-2 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-3 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-4 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-5 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-6 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-7 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-8 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-9 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-24:atom-10 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-11 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-12 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-13 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-14 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-15 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-16 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-24:atom-17 corridor:next:train:state-24
  (quote (corridor:next corridor:next:train:state-24 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-1 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-2 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-3 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-4 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-5 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-6 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-7 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-8 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-9 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-25:atom-10 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-11 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-12 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-13 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-14 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-15 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-16 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-25:atom-17 corridor:next:train:state-25
  (quote (corridor:next corridor:next:train:state-25 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-1 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-2 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-3 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-4 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-5 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-6 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-7 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-8 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-9 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-26:atom-10 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-11 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-12 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-13 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-14 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-15 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-16 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-26:atom-17 corridor:next:train:state-26
  (quote (corridor:next corridor:next:train:state-26 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-1 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-2 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-3 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-4 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-5 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-6 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-7 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-8 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-9 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-27:atom-10 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-11 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-12 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-13 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-14 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-15 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-16 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-27:atom-17 corridor:next:train:state-27
  (quote (corridor:next corridor:next:train:state-27 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-1 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-2 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-3 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-4 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-5 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-6 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-7 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-8 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-9 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-28:atom-10 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-11 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-12 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-13 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-14 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-15 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-16 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-28:atom-17 corridor:next:train:state-28
  (quote (corridor:next corridor:next:train:state-28 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-1 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-2 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-3 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-4 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-5 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-6 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-7 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-8 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-9 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-29:atom-10 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-11 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-12 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-13 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-14 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-15 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-16 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-29:atom-17 corridor:next:train:state-29
  (quote (corridor:next corridor:next:train:state-29 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-1 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-2 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-3 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-4 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-5 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-6 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-7 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-8 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-9 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-30:atom-10 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-11 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-12 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-13 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-14 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-15 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-16 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-30:atom-17 corridor:next:train:state-30
  (quote (corridor:next corridor:next:train:state-30 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-1 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-2 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-3 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-4 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-5 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-6 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-7 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-8 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-9 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-31:atom-10 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-11 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-12 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-13 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-14 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-15 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-16 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-31:atom-17 corridor:next:train:state-31
  (quote (corridor:next corridor:next:train:state-31 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-1 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-2 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-3 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-4 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-5 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-6 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-7 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-8 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-9 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-32:atom-10 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-11 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-12 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-13 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-14 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-15 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-16 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-32:atom-17 corridor:next:train:state-32
  (quote (corridor:next corridor:next:train:state-32 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-1 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-2 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-3 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-4 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-5 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-6 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-7 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-8 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-9 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-33:atom-10 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-11 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-12 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-13 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-14 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-15 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-16 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-33:atom-17 corridor:next:train:state-33
  (quote (corridor:next corridor:next:train:state-33 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-1 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-2 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-3 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-4 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-5 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-6 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-7 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-8 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-9 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-34:atom-10 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-11 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-12 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-13 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-14 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-15 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-16 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-34:atom-17 corridor:next:train:state-34
  (quote (corridor:next corridor:next:train:state-34 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-1 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-2 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-3 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-4 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-5 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-6 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-7 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-8 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-9 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-35:atom-10 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-11 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-12 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-13 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-14 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-15 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-16 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-35:atom-17 corridor:next:train:state-35
  (quote (corridor:next corridor:next:train:state-35 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-1 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-2 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-3 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-4 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-5 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-6 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-7 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-8 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-9 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-36:atom-10 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-11 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-12 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-13 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-14 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-15 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-16 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-36:atom-17 corridor:next:train:state-36
  (quote (corridor:next corridor:next:train:state-36 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-1 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-2 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-3 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-4 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-5 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-6 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-7 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-8 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-9 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-37:atom-10 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-11 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-12 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-13 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-14 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-15 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-16 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-37:atom-17 corridor:next:train:state-37
  (quote (corridor:next corridor:next:train:state-37 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-1 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-2 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-3 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-4 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-5 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-6 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-7 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-8 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-9 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-38:atom-10 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-11 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-12 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-13 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-14 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-15 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-16 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-38:atom-17 corridor:next:train:state-38
  (quote (corridor:next corridor:next:train:state-38 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-1 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-2 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-3 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-4 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-5 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-6 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-7 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-8 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-9 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-39:atom-10 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-11 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-12 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-13 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-14 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-15 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-16 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-39:atom-17 corridor:next:train:state-39
  (quote (corridor:next corridor:next:train:state-39 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-1 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-2 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-3 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-4 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-5 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-6 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-7 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-8 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-9 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-40:atom-10 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-11 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-12 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-13 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-14 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-15 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-16 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-40:atom-17 corridor:next:train:state-40
  (quote (corridor:next corridor:next:train:state-40 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-1 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-2 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-3 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-4 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-5 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-6 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-7 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-8 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-9 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-41:atom-10 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-11 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-12 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-13 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-14 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-15 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-16 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-41:atom-17 corridor:next:train:state-41
  (quote (corridor:next corridor:next:train:state-41 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-1 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-2 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-3 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-4 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-5 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-6 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-7 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-8 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-9 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-42:atom-10 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-11 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-12 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-13 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-14 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-15 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-16 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-42:atom-17 corridor:next:train:state-42
  (quote (corridor:next corridor:next:train:state-42 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-1 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-2 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-3 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-4 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-5 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-6 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-7 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-8 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-9 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-43:atom-10 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-11 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-12 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-13 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-14 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-15 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-16 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-43:atom-17 corridor:next:train:state-43
  (quote (corridor:next corridor:next:train:state-43 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-1 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:p)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-2 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q1)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-3 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q2)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-4 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q3)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-5 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q4)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-6 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q5)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-7 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q6)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-8 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q7)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-9 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 corridor:q8)))
!(iggp:corridor:classify corridor:next:train:state-44:atom-10 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-11 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-12 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-13 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-14 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-15 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-16 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:train:state-44:atom-17 corridor:next:train:state-44
  (quote (corridor:next corridor:next:train:state-44 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-1 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-2 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-3 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-4 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-5 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-6 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-7 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-8 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-9 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-10 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-11 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-12 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-13 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-14 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-15 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-16 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-1:atom-17 corridor:next:validate:state-1
  (quote (corridor:next corridor:next:validate:state-1 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-1 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-2 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-3 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-4 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-5 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-6 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-7 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-8 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-9 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-10 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-11 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-12 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-13 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-14 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-15 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-16 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-2:atom-17 corridor:next:validate:state-2
  (quote (corridor:next corridor:next:validate:state-2 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-1 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-2 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-3 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-4 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-5 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-6 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-7 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-8 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-9 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-10 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-11 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-12 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-13 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-14 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-15 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-16 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-3:atom-17 corridor:next:validate:state-3
  (quote (corridor:next corridor:next:validate:state-3 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-1 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-2 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-3 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-4 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-5 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-6 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-7 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-8 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-9 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-10 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-11 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-12 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-13 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-14 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-15 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-16 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-4:atom-17 corridor:next:validate:state-4
  (quote (corridor:next corridor:next:validate:state-4 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-1 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-2 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-3 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-4 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-5 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-6 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-7 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-8 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-9 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-10 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-11 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-12 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-13 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-14 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-15 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-16 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-5:atom-17 corridor:next:validate:state-5
  (quote (corridor:next corridor:next:validate:state-5 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-1 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-2 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-3 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-4 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-5 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-6 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-7 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-8 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-9 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-10 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-11 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-12 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-13 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-14 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-15 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-16 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-6:atom-17 corridor:next:validate:state-6
  (quote (corridor:next corridor:next:validate:state-6 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-1 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-2 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-3 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-4 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-5 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-6 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-7 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-8 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-9 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-10 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-11 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-12 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-13 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-14 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-15 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-16 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-7:atom-17 corridor:next:validate:state-7
  (quote (corridor:next corridor:next:validate:state-7 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-1 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-2 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-3 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-4 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-5 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-6 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-7 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-8 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-9 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-10 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-11 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-12 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-13 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-14 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-15 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-16 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-8:atom-17 corridor:next:validate:state-8
  (quote (corridor:next corridor:next:validate:state-8 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-1 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-2 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-3 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-4 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-5 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-6 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-7 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-8 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-9 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-10 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-11 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-12 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-13 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-14 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-15 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-16 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-9:atom-17 corridor:next:validate:state-9
  (quote (corridor:next corridor:next:validate:state-9 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-1 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-2 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-3 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-4 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-5 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-6 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-7 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-8 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-9 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-10 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-11 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-12 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-13 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-14 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-15 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-16 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-10:atom-17 corridor:next:validate:state-10
  (quote (corridor:next corridor:next:validate:state-10 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-1 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:p)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-2 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q1)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-3 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q2)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-4 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q3)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-5 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q4)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-6 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q5)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-7 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q6)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-8 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q7)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-9 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 corridor:q8)))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-10 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-11 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-12 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-13 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-14 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-15 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-16 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:validate:state-11:atom-17 corridor:next:validate:state-11
  (quote (corridor:next corridor:next:validate:state-11 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-1 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-2 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-3 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-4 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-5 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-6 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-7 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-8 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-9 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-1:atom-10 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-11 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-12 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-13 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-14 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-15 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-16 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-1:atom-17 corridor:next:test:state-1
  (quote (corridor:next corridor:next:test:state-1 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-1 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-2 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-3 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-4 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-5 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-6 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-7 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-8 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-9 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-2:atom-10 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-11 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-12 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-13 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-14 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-15 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-16 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-2:atom-17 corridor:next:test:state-2
  (quote (corridor:next corridor:next:test:state-2 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-1 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-2 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-3 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-4 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-5 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-6 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-7 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-8 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-9 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-3:atom-10 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-11 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-12 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-13 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-14 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-15 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-16 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-3:atom-17 corridor:next:test:state-3
  (quote (corridor:next corridor:next:test:state-3 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-1 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-2 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-3 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-4 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-5 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-6 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-7 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-8 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-9 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-4:atom-10 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-11 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-12 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-13 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-14 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-15 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-16 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-4:atom-17 corridor:next:test:state-4
  (quote (corridor:next corridor:next:test:state-4 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-1 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-2 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-3 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-4 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-5 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-6 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-7 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-8 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-9 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-5:atom-10 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-11 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-12 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-13 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-14 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-15 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-16 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-5:atom-17 corridor:next:test:state-5
  (quote (corridor:next corridor:next:test:state-5 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-1 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-2 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-3 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-4 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-5 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-6 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-7 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-8 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-9 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-6:atom-10 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-11 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-12 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-13 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-14 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-15 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-16 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-6:atom-17 corridor:next:test:state-6
  (quote (corridor:next corridor:next:test:state-6 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-1 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-2 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-3 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-4 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-5 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-6 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-7 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-8 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-9 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-7:atom-10 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-11 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-12 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-13 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-14 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-15 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-16 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-7:atom-17 corridor:next:test:state-7
  (quote (corridor:next corridor:next:test:state-7 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-1 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-2 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-3 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-4 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-5 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-6 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-7 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-8 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-9 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-8:atom-10 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-11 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-12 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-13 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-14 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-15 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-16 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-8:atom-17 corridor:next:test:state-8
  (quote (corridor:next corridor:next:test:state-8 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-1 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-2 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-3 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-4 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-5 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-6 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-7 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-8 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-9 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-9:atom-10 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-11 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-12 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-13 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-14 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-15 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-16 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-9:atom-17 corridor:next:test:state-9
  (quote (corridor:next corridor:next:test:state-9 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-1 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-2 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-3 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-4 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-5 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-6 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-7 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-8 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-9 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-10:atom-10 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-11 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-12 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-13 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-14 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-15 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-16 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-10:atom-17 corridor:next:test:state-10
  (quote (corridor:next corridor:next:test:state-10 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-1 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:p)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-2 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q1)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-3 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q2)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-4 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q3)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-5 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q4)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-6 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q5)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-7 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q6)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-8 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q7)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-9 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 corridor:q8)))
!(iggp:corridor:classify corridor:next:test:state-11:atom-10 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n1))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-11 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n2))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-12 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n3))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-13 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n4))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-14 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n5))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-15 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n6))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-16 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n7))))
!(iggp:corridor:classify corridor:next:test:state-11:atom-17 corridor:next:test:state-11
  (quote (corridor:next corridor:next:test:state-11 (corridor:step corridor:n8))))
!(iggp:corridor:classify corridor:terminal:train:state-1:atom-1 corridor:terminal:train:state-1
  (quote (corridor:terminal corridor:terminal:train:state-1)))
!(iggp:corridor:classify corridor:terminal:train:state-2:atom-1 corridor:terminal:train:state-2
  (quote (corridor:terminal corridor:terminal:train:state-2)))
!(iggp:corridor:classify corridor:terminal:train:state-3:atom-1 corridor:terminal:train:state-3
  (quote (corridor:terminal corridor:terminal:train:state-3)))
!(iggp:corridor:classify corridor:terminal:train:state-4:atom-1 corridor:terminal:train:state-4
  (quote (corridor:terminal corridor:terminal:train:state-4)))
!(iggp:corridor:classify corridor:terminal:train:state-5:atom-1 corridor:terminal:train:state-5
  (quote (corridor:terminal corridor:terminal:train:state-5)))
!(iggp:corridor:classify corridor:terminal:train:state-6:atom-1 corridor:terminal:train:state-6
  (quote (corridor:terminal corridor:terminal:train:state-6)))
!(iggp:corridor:classify corridor:terminal:train:state-7:atom-1 corridor:terminal:train:state-7
  (quote (corridor:terminal corridor:terminal:train:state-7)))
!(iggp:corridor:classify corridor:terminal:validate:state-1:atom-1 corridor:terminal:validate:state-1
  (quote (corridor:terminal corridor:terminal:validate:state-1)))
!(iggp:corridor:classify corridor:terminal:test:state-1:atom-1 corridor:terminal:test:state-1
  (quote (corridor:terminal corridor:terminal:test:state-1)))
!(iggp:corridor:classify corridor:terminal:test:state-2:atom-1 corridor:terminal:test:state-2
  (quote (corridor:terminal corridor:terminal:test:state-2)))
!(iggp:corridor:classify corridor:terminal:test:state-3:atom-1 corridor:terminal:test:state-3
  (quote (corridor:terminal corridor:terminal:test:state-3)))

!(iggp:corridor:proof-shapes
  corridor:next:train:state-1
  (quote (corridor:next corridor:next:train:state-1 corridor:p)))
