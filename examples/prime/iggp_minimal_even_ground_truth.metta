; Exact proof-relevant qualification for all minimal_even target tasks.
;
; Canonical GDL: games/minimal_even.txt
; SHA-256: f8b27304bd1770b425de9951b48c419490b60d629e0de351a460d8a7edccd238
; The excluded Prolog translation misspells does_choose as does_chose.
; Every train, validate, and test atom occurrence is classified.
; Proof counts retain relational multiplicity beyond binary labels.

!(import! &self ../../lib/ilp/iggp_minimal_even_types.metta)
!(import! &self ../../lib/ilp/iggp_minimal_even_rules.metta)
!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)

(= (iggp:minimal-even:classify $name $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (minimal-even:package $episode) 32 2000000 256 (quote $goal))
    (iggp:classify-occurrences
      $name (quote $goal) $occurrences)))

(= (minimal-even:num-proof-shape
     (minimal-even:proof:num-first $state $first $second $proof))
   num-from-first)
(= (minimal-even:num-proof-shape
     (minimal-even:proof:num-second $state $first $second $proof))
   num-from-second)
(= (minimal-even:num-proof-shape (unquote (quote $proof)))
   (minimal-even:num-proof-shape $proof))
(= (minimal-even:proof-shape
     (minimal-even:proof:legal $state $value $number-proof))
   (legal-via (minimal-even:num-proof-shape $number-proof)))
(= (minimal-even:proof-shape
     (minimal-even:proof:next-from-does $state $value $proof))
   next-from-does)
(= (minimal-even:proof-shape
     (minimal-even:proof:next-from-true $state $value $proof))
   next-from-true)

(= (iggp:minimal-even:proof-shapes $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (minimal-even:package $episode) 32 2000000 256 (quote $goal))
    (collapse
      (let (occurrence $proof-data) (superpose $occurrences)
        (let (quote $proof) $proof-data
          (minimal-even:proof-shape $proof))))))

!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-1 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-2 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-3 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-4 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-5 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-6 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-7 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-8 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-9 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-10 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-1:atom-11 minimal-even:goal:train:state-1
  (quote (minimal-even:goal minimal-even:goal:train:state-1 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-1 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-2 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-3 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-4 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-5 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-6 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-7 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-8 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-9 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-10 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-2:atom-11 minimal-even:goal:train:state-2
  (quote (minimal-even:goal minimal-even:goal:train:state-2 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-1 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-2 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-3 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-4 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-5 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-6 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-7 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-8 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-9 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-10 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-3:atom-11 minimal-even:goal:train:state-3
  (quote (minimal-even:goal minimal-even:goal:train:state-3 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-1 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-2 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-3 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-4 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-5 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-6 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-7 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-8 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-9 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-10 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-4:atom-11 minimal-even:goal:train:state-4
  (quote (minimal-even:goal minimal-even:goal:train:state-4 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-1 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-2 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-3 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-4 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-5 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-6 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-7 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-8 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-9 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-10 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-5:atom-11 minimal-even:goal:train:state-5
  (quote (minimal-even:goal minimal-even:goal:train:state-5 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-1 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-2 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-3 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-4 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-5 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-6 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-7 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-8 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-9 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-10 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-6:atom-11 minimal-even:goal:train:state-6
  (quote (minimal-even:goal minimal-even:goal:train:state-6 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-1 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-2 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-3 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-4 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-5 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-6 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-7 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-8 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-9 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-10 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-7:atom-11 minimal-even:goal:train:state-7
  (quote (minimal-even:goal minimal-even:goal:train:state-7 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-1 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-2 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-3 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-4 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-5 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-6 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-7 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-8 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-9 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-10 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-8:atom-11 minimal-even:goal:train:state-8
  (quote (minimal-even:goal minimal-even:goal:train:state-8 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-1 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-2 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-3 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-4 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-5 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-6 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-7 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-8 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-9 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-10 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-9:atom-11 minimal-even:goal:train:state-9
  (quote (minimal-even:goal minimal-even:goal:train:state-9 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-1 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-2 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-3 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-4 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-5 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-6 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-7 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-8 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-9 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-10 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-10:atom-11 minimal-even:goal:train:state-10
  (quote (minimal-even:goal minimal-even:goal:train:state-10 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-1 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-2 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-3 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-4 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-5 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-6 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-7 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-8 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-9 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-10 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-11:atom-11 minimal-even:goal:train:state-11
  (quote (minimal-even:goal minimal-even:goal:train:state-11 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-1 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-2 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-3 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-4 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-5 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-6 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-7 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-8 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-9 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-10 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-12:atom-11 minimal-even:goal:train:state-12
  (quote (minimal-even:goal minimal-even:goal:train:state-12 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-1 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-2 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-3 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-4 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-5 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-6 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-7 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-8 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-9 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-10 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-13:atom-11 minimal-even:goal:train:state-13
  (quote (minimal-even:goal minimal-even:goal:train:state-13 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-1 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-2 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-3 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-4 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-5 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-6 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-7 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-8 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-9 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-10 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-14:atom-11 minimal-even:goal:train:state-14
  (quote (minimal-even:goal minimal-even:goal:train:state-14 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-1 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-2 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-3 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-4 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-5 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-6 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-7 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-8 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-9 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-10 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-15:atom-11 minimal-even:goal:train:state-15
  (quote (minimal-even:goal minimal-even:goal:train:state-15 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-1 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-2 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-3 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-4 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-5 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-6 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-7 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-8 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-9 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-10 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-16:atom-11 minimal-even:goal:train:state-16
  (quote (minimal-even:goal minimal-even:goal:train:state-16 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-1 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-2 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-3 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-4 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-5 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-6 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-7 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-8 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-9 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-10 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-17:atom-11 minimal-even:goal:train:state-17
  (quote (minimal-even:goal minimal-even:goal:train:state-17 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-1 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-2 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-3 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-4 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-5 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-6 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-7 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-8 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-9 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-10 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-18:atom-11 minimal-even:goal:train:state-18
  (quote (minimal-even:goal minimal-even:goal:train:state-18 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-1 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-2 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-3 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-4 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-5 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-6 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-7 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-8 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-9 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-10 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-19:atom-11 minimal-even:goal:train:state-19
  (quote (minimal-even:goal minimal-even:goal:train:state-19 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-1 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-2 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-3 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-4 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-5 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-6 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-7 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-8 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-9 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-10 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-20:atom-11 minimal-even:goal:train:state-20
  (quote (minimal-even:goal minimal-even:goal:train:state-20 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-1 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-2 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-3 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-4 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-5 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-6 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-7 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-8 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-9 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-10 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-21:atom-11 minimal-even:goal:train:state-21
  (quote (minimal-even:goal minimal-even:goal:train:state-21 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-1 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-2 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-3 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-4 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-5 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-6 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-7 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-8 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-9 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-10 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-22:atom-11 minimal-even:goal:train:state-22
  (quote (minimal-even:goal minimal-even:goal:train:state-22 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-1 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-2 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-3 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-4 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-5 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-6 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-7 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-8 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-9 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-10 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-23:atom-11 minimal-even:goal:train:state-23
  (quote (minimal-even:goal minimal-even:goal:train:state-23 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-1 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-2 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-3 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-4 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-5 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-6 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-7 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-8 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-9 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-10 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-24:atom-11 minimal-even:goal:train:state-24
  (quote (minimal-even:goal minimal-even:goal:train:state-24 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-1 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-2 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-3 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-4 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-5 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-6 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-7 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-8 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-9 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-10 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-25:atom-11 minimal-even:goal:train:state-25
  (quote (minimal-even:goal minimal-even:goal:train:state-25 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-1 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-2 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-3 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-4 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-5 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-6 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-7 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-8 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-9 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-10 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-26:atom-11 minimal-even:goal:train:state-26
  (quote (minimal-even:goal minimal-even:goal:train:state-26 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-1 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-2 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-3 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-4 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-5 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-6 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-7 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-8 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-9 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-10 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-27:atom-11 minimal-even:goal:train:state-27
  (quote (minimal-even:goal minimal-even:goal:train:state-27 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-1 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-2 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-3 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-4 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-5 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-6 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-7 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-8 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-9 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-10 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-28:atom-11 minimal-even:goal:train:state-28
  (quote (minimal-even:goal minimal-even:goal:train:state-28 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-1 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-2 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-3 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-4 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-5 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-6 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-7 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-8 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-9 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-10 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-29:atom-11 minimal-even:goal:train:state-29
  (quote (minimal-even:goal minimal-even:goal:train:state-29 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-1 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-2 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-3 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-4 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-5 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-6 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-7 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-8 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-9 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-10 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-30:atom-11 minimal-even:goal:train:state-30
  (quote (minimal-even:goal minimal-even:goal:train:state-30 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-1 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-2 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-3 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-4 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-5 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-6 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-7 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-8 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-9 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-10 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-31:atom-11 minimal-even:goal:train:state-31
  (quote (minimal-even:goal minimal-even:goal:train:state-31 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-1 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-2 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-3 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-4 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-5 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-6 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-7 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-8 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-9 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-10 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-32:atom-11 minimal-even:goal:train:state-32
  (quote (minimal-even:goal minimal-even:goal:train:state-32 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-1 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-2 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-3 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-4 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-5 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-6 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-7 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-8 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-9 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-10 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-33:atom-11 minimal-even:goal:train:state-33
  (quote (minimal-even:goal minimal-even:goal:train:state-33 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-1 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-2 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-3 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-4 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-5 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-6 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-7 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-8 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-9 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-10 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-34:atom-11 minimal-even:goal:train:state-34
  (quote (minimal-even:goal minimal-even:goal:train:state-34 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-1 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-2 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-3 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-4 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-5 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-6 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-7 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-8 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-9 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-10 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-35:atom-11 minimal-even:goal:train:state-35
  (quote (minimal-even:goal minimal-even:goal:train:state-35 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-1 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-2 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-3 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-4 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-5 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-6 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-7 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-8 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-9 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-10 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-36:atom-11 minimal-even:goal:train:state-36
  (quote (minimal-even:goal minimal-even:goal:train:state-36 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-1 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-2 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-3 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-4 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-5 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-6 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-7 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-8 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-9 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-10 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-37:atom-11 minimal-even:goal:train:state-37
  (quote (minimal-even:goal minimal-even:goal:train:state-37 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-1 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-2 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-3 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-4 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-5 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-6 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-7 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-8 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-9 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-10 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-38:atom-11 minimal-even:goal:train:state-38
  (quote (minimal-even:goal minimal-even:goal:train:state-38 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-1 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-2 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-3 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-4 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-5 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-6 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-7 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-8 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-9 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-10 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-39:atom-11 minimal-even:goal:train:state-39
  (quote (minimal-even:goal minimal-even:goal:train:state-39 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-1 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-2 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-3 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-4 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-5 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-6 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-7 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-8 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-9 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-10 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-40:atom-11 minimal-even:goal:train:state-40
  (quote (minimal-even:goal minimal-even:goal:train:state-40 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-1 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-2 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-3 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-4 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-5 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-6 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-7 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-8 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-9 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-10 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-41:atom-11 minimal-even:goal:train:state-41
  (quote (minimal-even:goal minimal-even:goal:train:state-41 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-1 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-2 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-3 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-4 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-5 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-6 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-7 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-8 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-9 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-10 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:train:state-42:atom-11 minimal-even:goal:train:state-42
  (quote (minimal-even:goal minimal-even:goal:train:state-42 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-1 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-2 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-3 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-4 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-5 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-6 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-7 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-8 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-9 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-10 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-1:atom-11 minimal-even:goal:validate:state-1
  (quote (minimal-even:goal minimal-even:goal:validate:state-1 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-1 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-2 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-3 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-4 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-5 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-6 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-7 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-8 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-9 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-10 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-2:atom-11 minimal-even:goal:validate:state-2
  (quote (minimal-even:goal minimal-even:goal:validate:state-2 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-1 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-2 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-3 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-4 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-5 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-6 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-7 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-8 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-9 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-10 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-3:atom-11 minimal-even:goal:validate:state-3
  (quote (minimal-even:goal minimal-even:goal:validate:state-3 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-1 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-2 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-3 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-4 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-5 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-6 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-7 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-8 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-9 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-10 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-4:atom-11 minimal-even:goal:validate:state-4
  (quote (minimal-even:goal minimal-even:goal:validate:state-4 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-1 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-2 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-3 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-4 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-5 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-6 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-7 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-8 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-9 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-10 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-5:atom-11 minimal-even:goal:validate:state-5
  (quote (minimal-even:goal minimal-even:goal:validate:state-5 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-1 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-2 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-3 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-4 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-5 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-6 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-7 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-8 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-9 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-10 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-6:atom-11 minimal-even:goal:validate:state-6
  (quote (minimal-even:goal minimal-even:goal:validate:state-6 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-1 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-2 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-3 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-4 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-5 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-6 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-7 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-8 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-9 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-10 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-7:atom-11 minimal-even:goal:validate:state-7
  (quote (minimal-even:goal minimal-even:goal:validate:state-7 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-1 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-2 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-3 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-4 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-5 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-6 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-7 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-8 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-9 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-10 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-8:atom-11 minimal-even:goal:validate:state-8
  (quote (minimal-even:goal minimal-even:goal:validate:state-8 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-1 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-2 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-3 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-4 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-5 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-6 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-7 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-8 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-9 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-10 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-9:atom-11 minimal-even:goal:validate:state-9
  (quote (minimal-even:goal minimal-even:goal:validate:state-9 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-1 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-2 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-3 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-4 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-5 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-6 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-7 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-8 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-9 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-10 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:validate:state-10:atom-11 minimal-even:goal:validate:state-10
  (quote (minimal-even:goal minimal-even:goal:validate:state-10 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-1 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-2 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-3 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-4 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-5 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-6 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-7 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-8 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-9 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-10 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-1:atom-11 minimal-even:goal:test:state-1
  (quote (minimal-even:goal minimal-even:goal:test:state-1 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-1 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-2 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-3 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-4 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-5 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-6 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-7 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-8 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-9 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-10 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-2:atom-11 minimal-even:goal:test:state-2
  (quote (minimal-even:goal minimal-even:goal:test:state-2 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-1 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-2 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-3 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-4 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-5 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-6 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-7 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-8 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-9 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-10 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-3:atom-11 minimal-even:goal:test:state-3
  (quote (minimal-even:goal minimal-even:goal:test:state-3 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-1 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-2 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-3 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-4 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-5 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-6 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-7 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-8 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-9 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-10 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-4:atom-11 minimal-even:goal:test:state-4
  (quote (minimal-even:goal minimal-even:goal:test:state-4 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-1 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-2 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-3 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-4 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-5 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-6 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-7 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-8 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-9 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-10 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-5:atom-11 minimal-even:goal:test:state-5
  (quote (minimal-even:goal minimal-even:goal:test:state-5 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-1 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-2 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-3 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-4 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-5 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-6 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-7 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-8 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-9 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-10 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-6:atom-11 minimal-even:goal:test:state-6
  (quote (minimal-even:goal minimal-even:goal:test:state-6 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-1 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-2 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-3 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-4 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-5 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-6 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-7 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-8 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-9 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-10 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-7:atom-11 minimal-even:goal:test:state-7
  (quote (minimal-even:goal minimal-even:goal:test:state-7 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-1 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-2 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-3 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-4 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-5 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-6 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-7 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-8 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-9 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-10 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-8:atom-11 minimal-even:goal:test:state-8
  (quote (minimal-even:goal minimal-even:goal:test:state-8 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-1 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-2 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-3 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-4 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-5 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-6 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-7 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-8 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-9 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-10 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-9:atom-11 minimal-even:goal:test:state-9
  (quote (minimal-even:goal minimal-even:goal:test:state-9 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-1 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-2 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-3 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-4 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-5 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-6 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-7 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-8 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-9 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-10 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-10:atom-11 minimal-even:goal:test:state-10
  (quote (minimal-even:goal minimal-even:goal:test:state-10 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-1 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-2 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-3 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-4 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-5 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-6 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-7 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-8 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-9 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-10 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-11:atom-11 minimal-even:goal:test:state-11
  (quote (minimal-even:goal minimal-even:goal:test:state-11 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-1 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-2 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-3 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-4 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-5 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-6 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-7 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-8 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-9 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-10 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:goal:test:state-12:atom-11 minimal-even:goal:test:state-12
  (quote (minimal-even:goal minimal-even:goal:test:state-12 minimal-even:player minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-1 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-2 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-3 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-4 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-5 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-6 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-7 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-8 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-9 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-10 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-1:atom-11 minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-1 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-2 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-3 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-4 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-5 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-6 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-7 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-8 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-9 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-10 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-2:atom-11 minimal-even:legal:train:state-2
  (quote (minimal-even:legal minimal-even:legal:train:state-2 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-1 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-2 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-3 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-4 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-5 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-6 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-7 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-8 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-9 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-10 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-3:atom-11 minimal-even:legal:train:state-3
  (quote (minimal-even:legal minimal-even:legal:train:state-3 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-1 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-2 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-3 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-4 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-5 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-6 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-7 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-8 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-9 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-10 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-4:atom-11 minimal-even:legal:train:state-4
  (quote (minimal-even:legal minimal-even:legal:train:state-4 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-1 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-2 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-3 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-4 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-5 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-6 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-7 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-8 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-9 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-10 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-5:atom-11 minimal-even:legal:train:state-5
  (quote (minimal-even:legal minimal-even:legal:train:state-5 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-1 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-2 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-3 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-4 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-5 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-6 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-7 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-8 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-9 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-10 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-6:atom-11 minimal-even:legal:train:state-6
  (quote (minimal-even:legal minimal-even:legal:train:state-6 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-1 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-2 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-3 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-4 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-5 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-6 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-7 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-8 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-9 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-10 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-7:atom-11 minimal-even:legal:train:state-7
  (quote (minimal-even:legal minimal-even:legal:train:state-7 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-1 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-2 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-3 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-4 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-5 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-6 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-7 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-8 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-9 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-10 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-8:atom-11 minimal-even:legal:train:state-8
  (quote (minimal-even:legal minimal-even:legal:train:state-8 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-1 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-2 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-3 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-4 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-5 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-6 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-7 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-8 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-9 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-10 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-9:atom-11 minimal-even:legal:train:state-9
  (quote (minimal-even:legal minimal-even:legal:train:state-9 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-1 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-2 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-3 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-4 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-5 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-6 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-7 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-8 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-9 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-10 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-10:atom-11 minimal-even:legal:train:state-10
  (quote (minimal-even:legal minimal-even:legal:train:state-10 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-1 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-2 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-3 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-4 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-5 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-6 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-7 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-8 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-9 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-10 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-11:atom-11 minimal-even:legal:train:state-11
  (quote (minimal-even:legal minimal-even:legal:train:state-11 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-1 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-2 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-3 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-4 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-5 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-6 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-7 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-8 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-9 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-10 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-12:atom-11 minimal-even:legal:train:state-12
  (quote (minimal-even:legal minimal-even:legal:train:state-12 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-1 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-2 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-3 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-4 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-5 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-6 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-7 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-8 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-9 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-10 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-13:atom-11 minimal-even:legal:train:state-13
  (quote (minimal-even:legal minimal-even:legal:train:state-13 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-1 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-2 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-3 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-4 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-5 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-6 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-7 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-8 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-9 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-10 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-14:atom-11 minimal-even:legal:train:state-14
  (quote (minimal-even:legal minimal-even:legal:train:state-14 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-1 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-2 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-3 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-4 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-5 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-6 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-7 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-8 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-9 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-10 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-15:atom-11 minimal-even:legal:train:state-15
  (quote (minimal-even:legal minimal-even:legal:train:state-15 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-1 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-2 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-3 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-4 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-5 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-6 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-7 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-8 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-9 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-10 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-16:atom-11 minimal-even:legal:train:state-16
  (quote (minimal-even:legal minimal-even:legal:train:state-16 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-1 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-2 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-3 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-4 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-5 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-6 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-7 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-8 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-9 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-10 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-17:atom-11 minimal-even:legal:train:state-17
  (quote (minimal-even:legal minimal-even:legal:train:state-17 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-1 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-2 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-3 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-4 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-5 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-6 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-7 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-8 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-9 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-10 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-18:atom-11 minimal-even:legal:train:state-18
  (quote (minimal-even:legal minimal-even:legal:train:state-18 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-1 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-2 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-3 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-4 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-5 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-6 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-7 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-8 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-9 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-10 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-19:atom-11 minimal-even:legal:train:state-19
  (quote (minimal-even:legal minimal-even:legal:train:state-19 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-1 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-2 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-3 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-4 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-5 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-6 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-7 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-8 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-9 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-10 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-20:atom-11 minimal-even:legal:train:state-20
  (quote (minimal-even:legal minimal-even:legal:train:state-20 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-1 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-2 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-3 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-4 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-5 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-6 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-7 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-8 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-9 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-10 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-21:atom-11 minimal-even:legal:train:state-21
  (quote (minimal-even:legal minimal-even:legal:train:state-21 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-1 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-2 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-3 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-4 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-5 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-6 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-7 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-8 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-9 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-10 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-22:atom-11 minimal-even:legal:train:state-22
  (quote (minimal-even:legal minimal-even:legal:train:state-22 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-1 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-2 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-3 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-4 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-5 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-6 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-7 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-8 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-9 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-10 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-23:atom-11 minimal-even:legal:train:state-23
  (quote (minimal-even:legal minimal-even:legal:train:state-23 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-1 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-2 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-3 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-4 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-5 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-6 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-7 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-8 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-9 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-10 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-24:atom-11 minimal-even:legal:train:state-24
  (quote (minimal-even:legal minimal-even:legal:train:state-24 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-1 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-2 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-3 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-4 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-5 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-6 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-7 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-8 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-9 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-10 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-25:atom-11 minimal-even:legal:train:state-25
  (quote (minimal-even:legal minimal-even:legal:train:state-25 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-1 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-2 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-3 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-4 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-5 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-6 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-7 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-8 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-9 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-10 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-26:atom-11 minimal-even:legal:train:state-26
  (quote (minimal-even:legal minimal-even:legal:train:state-26 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-1 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-2 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-3 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-4 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-5 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-6 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-7 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-8 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-9 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-10 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-27:atom-11 minimal-even:legal:train:state-27
  (quote (minimal-even:legal minimal-even:legal:train:state-27 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-1 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-2 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-3 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-4 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-5 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-6 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-7 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-8 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-9 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-10 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-28:atom-11 minimal-even:legal:train:state-28
  (quote (minimal-even:legal minimal-even:legal:train:state-28 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-1 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-2 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-3 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-4 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-5 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-6 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-7 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-8 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-9 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-10 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-29:atom-11 minimal-even:legal:train:state-29
  (quote (minimal-even:legal minimal-even:legal:train:state-29 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-1 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-2 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-3 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-4 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-5 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-6 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-7 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-8 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-9 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-10 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-30:atom-11 minimal-even:legal:train:state-30
  (quote (minimal-even:legal minimal-even:legal:train:state-30 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-1 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-2 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-3 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-4 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-5 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-6 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-7 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-8 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-9 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-10 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-31:atom-11 minimal-even:legal:train:state-31
  (quote (minimal-even:legal minimal-even:legal:train:state-31 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-1 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-2 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-3 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-4 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-5 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-6 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-7 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-8 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-9 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-10 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-32:atom-11 minimal-even:legal:train:state-32
  (quote (minimal-even:legal minimal-even:legal:train:state-32 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-1 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-2 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-3 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-4 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-5 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-6 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-7 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-8 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-9 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-10 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-33:atom-11 minimal-even:legal:train:state-33
  (quote (minimal-even:legal minimal-even:legal:train:state-33 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-1 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-2 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-3 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-4 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-5 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-6 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-7 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-8 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-9 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-10 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-34:atom-11 minimal-even:legal:train:state-34
  (quote (minimal-even:legal minimal-even:legal:train:state-34 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-1 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-2 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-3 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-4 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-5 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-6 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-7 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-8 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-9 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-10 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-35:atom-11 minimal-even:legal:train:state-35
  (quote (minimal-even:legal minimal-even:legal:train:state-35 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-1 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-2 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-3 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-4 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-5 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-6 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-7 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-8 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-9 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-10 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-36:atom-11 minimal-even:legal:train:state-36
  (quote (minimal-even:legal minimal-even:legal:train:state-36 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-1 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-2 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-3 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-4 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-5 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-6 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-7 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-8 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-9 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-10 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-37:atom-11 minimal-even:legal:train:state-37
  (quote (minimal-even:legal minimal-even:legal:train:state-37 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-1 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-2 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-3 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-4 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-5 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-6 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-7 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-8 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-9 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-10 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-38:atom-11 minimal-even:legal:train:state-38
  (quote (minimal-even:legal minimal-even:legal:train:state-38 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-1 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-2 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-3 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-4 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-5 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-6 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-7 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-8 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-9 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-10 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-39:atom-11 minimal-even:legal:train:state-39
  (quote (minimal-even:legal minimal-even:legal:train:state-39 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-1 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-2 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-3 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-4 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-5 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-6 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-7 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-8 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-9 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-10 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-40:atom-11 minimal-even:legal:train:state-40
  (quote (minimal-even:legal minimal-even:legal:train:state-40 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-1 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-2 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-3 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-4 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-5 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-6 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-7 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-8 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-9 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-10 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-41:atom-11 minimal-even:legal:train:state-41
  (quote (minimal-even:legal minimal-even:legal:train:state-41 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-1 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-2 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-3 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-4 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-5 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-6 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-7 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-8 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-9 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-10 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:train:state-42:atom-11 minimal-even:legal:train:state-42
  (quote (minimal-even:legal minimal-even:legal:train:state-42 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-1 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-2 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-3 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-4 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-5 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-6 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-7 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-8 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-9 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-10 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-1:atom-11 minimal-even:legal:validate:state-1
  (quote (minimal-even:legal minimal-even:legal:validate:state-1 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-1 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-2 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-3 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-4 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-5 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-6 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-7 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-8 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-9 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-10 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-2:atom-11 minimal-even:legal:validate:state-2
  (quote (minimal-even:legal minimal-even:legal:validate:state-2 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-1 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-2 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-3 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-4 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-5 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-6 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-7 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-8 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-9 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-10 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-3:atom-11 minimal-even:legal:validate:state-3
  (quote (minimal-even:legal minimal-even:legal:validate:state-3 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-1 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-2 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-3 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-4 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-5 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-6 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-7 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-8 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-9 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-10 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-4:atom-11 minimal-even:legal:validate:state-4
  (quote (minimal-even:legal minimal-even:legal:validate:state-4 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-1 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-2 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-3 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-4 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-5 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-6 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-7 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-8 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-9 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-10 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-5:atom-11 minimal-even:legal:validate:state-5
  (quote (minimal-even:legal minimal-even:legal:validate:state-5 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-1 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-2 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-3 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-4 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-5 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-6 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-7 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-8 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-9 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-10 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-6:atom-11 minimal-even:legal:validate:state-6
  (quote (minimal-even:legal minimal-even:legal:validate:state-6 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-1 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-2 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-3 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-4 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-5 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-6 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-7 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-8 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-9 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-10 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-7:atom-11 minimal-even:legal:validate:state-7
  (quote (minimal-even:legal minimal-even:legal:validate:state-7 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-1 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-2 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-3 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-4 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-5 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-6 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-7 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-8 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-9 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-10 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-8:atom-11 minimal-even:legal:validate:state-8
  (quote (minimal-even:legal minimal-even:legal:validate:state-8 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-1 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-2 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-3 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-4 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-5 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-6 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-7 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-8 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-9 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-10 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-9:atom-11 minimal-even:legal:validate:state-9
  (quote (minimal-even:legal minimal-even:legal:validate:state-9 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-1 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-2 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-3 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-4 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-5 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-6 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-7 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-8 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-9 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-10 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:validate:state-10:atom-11 minimal-even:legal:validate:state-10
  (quote (minimal-even:legal minimal-even:legal:validate:state-10 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-1 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-2 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-3 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-4 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-5 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-6 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-7 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-8 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-9 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-10 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-1:atom-11 minimal-even:legal:test:state-1
  (quote (minimal-even:legal minimal-even:legal:test:state-1 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-1 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-2 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-3 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-4 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-5 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-6 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-7 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-8 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-9 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-10 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-2:atom-11 minimal-even:legal:test:state-2
  (quote (minimal-even:legal minimal-even:legal:test:state-2 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-1 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-2 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-3 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-4 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-5 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-6 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-7 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-8 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-9 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-10 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-3:atom-11 minimal-even:legal:test:state-3
  (quote (minimal-even:legal minimal-even:legal:test:state-3 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-1 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-2 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-3 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-4 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-5 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-6 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-7 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-8 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-9 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-10 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-4:atom-11 minimal-even:legal:test:state-4
  (quote (minimal-even:legal minimal-even:legal:test:state-4 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-1 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-2 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-3 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-4 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-5 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-6 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-7 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-8 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-9 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-10 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-5:atom-11 minimal-even:legal:test:state-5
  (quote (minimal-even:legal minimal-even:legal:test:state-5 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-1 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-2 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-3 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-4 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-5 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-6 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-7 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-8 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-9 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-10 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-6:atom-11 minimal-even:legal:test:state-6
  (quote (minimal-even:legal minimal-even:legal:test:state-6 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-1 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-2 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-3 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-4 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-5 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-6 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-7 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-8 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-9 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-10 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-7:atom-11 minimal-even:legal:test:state-7
  (quote (minimal-even:legal minimal-even:legal:test:state-7 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-1 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-2 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-3 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-4 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-5 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-6 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-7 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-8 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-9 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-10 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-8:atom-11 minimal-even:legal:test:state-8
  (quote (minimal-even:legal minimal-even:legal:test:state-8 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-1 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-2 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-3 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-4 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-5 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-6 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-7 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-8 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-9 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-10 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-9:atom-11 minimal-even:legal:test:state-9
  (quote (minimal-even:legal minimal-even:legal:test:state-9 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-1 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-2 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-3 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-4 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-5 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-6 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-7 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-8 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-9 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-10 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-10:atom-11 minimal-even:legal:test:state-10
  (quote (minimal-even:legal minimal-even:legal:test:state-10 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-1 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-2 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-3 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-4 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-5 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-6 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-7 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-8 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-9 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-10 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-11:atom-11 minimal-even:legal:test:state-11
  (quote (minimal-even:legal minimal-even:legal:test:state-11 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-1 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n0))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-2 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n1))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-3 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n10))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-4 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n2))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-5 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n3))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-6 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n4))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-7 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-8 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n6))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-9 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n7))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-10 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n8))))
!(iggp:minimal-even:classify minimal-even:legal:test:state-12:atom-11 minimal-even:legal:test:state-12
  (quote (minimal-even:legal minimal-even:legal:test:state-12 minimal-even:player (minimal-even:choose minimal-even:n9))))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-1 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-2 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-3 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-4 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-5 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-6 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-7 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-8 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-9 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-10 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-1:atom-11 minimal-even:next:train:state-1
  (quote (minimal-even:next-chosen minimal-even:next:train:state-1 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-1 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-2 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-3 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-4 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-5 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-6 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-7 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-8 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-9 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-10 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-2:atom-11 minimal-even:next:train:state-2
  (quote (minimal-even:next-chosen minimal-even:next:train:state-2 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-1 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-2 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-3 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-4 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-5 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-6 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-7 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-8 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-9 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-10 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-3:atom-11 minimal-even:next:train:state-3
  (quote (minimal-even:next-chosen minimal-even:next:train:state-3 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-1 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-2 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-3 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-4 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-5 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-6 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-7 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-8 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-9 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-10 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-4:atom-11 minimal-even:next:train:state-4
  (quote (minimal-even:next-chosen minimal-even:next:train:state-4 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-1 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-2 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-3 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-4 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-5 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-6 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-7 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-8 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-9 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-10 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-5:atom-11 minimal-even:next:train:state-5
  (quote (minimal-even:next-chosen minimal-even:next:train:state-5 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-1 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-2 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-3 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-4 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-5 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-6 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-7 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-8 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-9 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-10 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-6:atom-11 minimal-even:next:train:state-6
  (quote (minimal-even:next-chosen minimal-even:next:train:state-6 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-1 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-2 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-3 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-4 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-5 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-6 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-7 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-8 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-9 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-10 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-7:atom-11 minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-1 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-2 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-3 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-4 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-5 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-6 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-7 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-8 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-9 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-10 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-8:atom-11 minimal-even:next:train:state-8
  (quote (minimal-even:next-chosen minimal-even:next:train:state-8 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-1 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-2 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-3 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-4 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-5 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-6 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-7 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-8 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-9 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-10 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-9:atom-11 minimal-even:next:train:state-9
  (quote (minimal-even:next-chosen minimal-even:next:train:state-9 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-1 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-2 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-3 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-4 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-5 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-6 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-7 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-8 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-9 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-10 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-10:atom-11 minimal-even:next:train:state-10
  (quote (minimal-even:next-chosen minimal-even:next:train:state-10 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-1 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-2 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-3 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-4 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-5 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-6 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-7 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-8 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-9 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-10 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-11:atom-11 minimal-even:next:train:state-11
  (quote (minimal-even:next-chosen minimal-even:next:train:state-11 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-1 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-2 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-3 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-4 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-5 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-6 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-7 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-8 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-9 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-10 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-12:atom-11 minimal-even:next:train:state-12
  (quote (minimal-even:next-chosen minimal-even:next:train:state-12 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-1 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-2 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-3 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-4 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-5 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-6 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-7 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-8 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-9 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-10 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-13:atom-11 minimal-even:next:train:state-13
  (quote (minimal-even:next-chosen minimal-even:next:train:state-13 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-1 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-2 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-3 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-4 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-5 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-6 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-7 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-8 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-9 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-10 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-14:atom-11 minimal-even:next:train:state-14
  (quote (minimal-even:next-chosen minimal-even:next:train:state-14 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-1 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-2 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-3 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-4 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-5 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-6 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-7 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-8 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-9 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-10 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-15:atom-11 minimal-even:next:train:state-15
  (quote (minimal-even:next-chosen minimal-even:next:train:state-15 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-1 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-2 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-3 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-4 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-5 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-6 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-7 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-8 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-9 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-10 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-16:atom-11 minimal-even:next:train:state-16
  (quote (minimal-even:next-chosen minimal-even:next:train:state-16 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-1 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-2 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-3 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-4 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-5 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-6 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-7 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-8 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-9 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-10 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-17:atom-11 minimal-even:next:train:state-17
  (quote (minimal-even:next-chosen minimal-even:next:train:state-17 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-1 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-2 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-3 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-4 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-5 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-6 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-7 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-8 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-9 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-10 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-18:atom-11 minimal-even:next:train:state-18
  (quote (minimal-even:next-chosen minimal-even:next:train:state-18 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-1 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-2 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-3 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-4 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-5 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-6 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-7 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-8 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-9 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-10 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-19:atom-11 minimal-even:next:train:state-19
  (quote (minimal-even:next-chosen minimal-even:next:train:state-19 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-1 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-2 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-3 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-4 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-5 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-6 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-7 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-8 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-9 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-10 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-20:atom-11 minimal-even:next:train:state-20
  (quote (minimal-even:next-chosen minimal-even:next:train:state-20 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-1 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-2 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-3 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-4 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-5 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-6 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-7 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-8 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-9 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-10 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-21:atom-11 minimal-even:next:train:state-21
  (quote (minimal-even:next-chosen minimal-even:next:train:state-21 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-1 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-2 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-3 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-4 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-5 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-6 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-7 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-8 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-9 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-10 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-22:atom-11 minimal-even:next:train:state-22
  (quote (minimal-even:next-chosen minimal-even:next:train:state-22 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-1 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-2 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-3 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-4 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-5 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-6 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-7 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-8 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-9 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-10 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-23:atom-11 minimal-even:next:train:state-23
  (quote (minimal-even:next-chosen minimal-even:next:train:state-23 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-1 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-2 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-3 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-4 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-5 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-6 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-7 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-8 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-9 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-10 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-24:atom-11 minimal-even:next:train:state-24
  (quote (minimal-even:next-chosen minimal-even:next:train:state-24 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-1 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-2 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-3 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-4 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-5 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-6 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-7 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-8 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-9 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-10 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-25:atom-11 minimal-even:next:train:state-25
  (quote (minimal-even:next-chosen minimal-even:next:train:state-25 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-1 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-2 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-3 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-4 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-5 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-6 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-7 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-8 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-9 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-10 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-26:atom-11 minimal-even:next:train:state-26
  (quote (minimal-even:next-chosen minimal-even:next:train:state-26 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-1 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-2 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-3 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-4 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-5 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-6 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-7 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-8 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-9 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-10 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-27:atom-11 minimal-even:next:train:state-27
  (quote (minimal-even:next-chosen minimal-even:next:train:state-27 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-1 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-2 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-3 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-4 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-5 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-6 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-7 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-8 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-9 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-10 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-28:atom-11 minimal-even:next:train:state-28
  (quote (minimal-even:next-chosen minimal-even:next:train:state-28 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-1 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-2 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-3 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-4 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-5 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-6 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-7 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-8 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-9 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-10 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-29:atom-11 minimal-even:next:train:state-29
  (quote (minimal-even:next-chosen minimal-even:next:train:state-29 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-1 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-2 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-3 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-4 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-5 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-6 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-7 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-8 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-9 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-10 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-30:atom-11 minimal-even:next:train:state-30
  (quote (minimal-even:next-chosen minimal-even:next:train:state-30 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-1 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-2 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-3 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-4 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-5 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-6 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-7 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-8 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-9 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-10 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-31:atom-11 minimal-even:next:train:state-31
  (quote (minimal-even:next-chosen minimal-even:next:train:state-31 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-1 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-2 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-3 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-4 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-5 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-6 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-7 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-8 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-9 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-10 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-32:atom-11 minimal-even:next:train:state-32
  (quote (minimal-even:next-chosen minimal-even:next:train:state-32 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-1 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-2 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-3 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-4 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-5 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-6 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-7 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-8 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-9 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-10 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-33:atom-11 minimal-even:next:train:state-33
  (quote (minimal-even:next-chosen minimal-even:next:train:state-33 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-1 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-2 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-3 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-4 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-5 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-6 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-7 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-8 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-9 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-10 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-34:atom-11 minimal-even:next:train:state-34
  (quote (minimal-even:next-chosen minimal-even:next:train:state-34 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-1 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-2 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-3 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-4 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-5 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-6 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-7 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-8 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-9 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-10 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-35:atom-11 minimal-even:next:train:state-35
  (quote (minimal-even:next-chosen minimal-even:next:train:state-35 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-1 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-2 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-3 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-4 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-5 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-6 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-7 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-8 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-9 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-10 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-36:atom-11 minimal-even:next:train:state-36
  (quote (minimal-even:next-chosen minimal-even:next:train:state-36 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-1 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-2 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-3 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-4 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-5 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-6 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-7 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-8 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-9 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-10 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-37:atom-11 minimal-even:next:train:state-37
  (quote (minimal-even:next-chosen minimal-even:next:train:state-37 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-1 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-2 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-3 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-4 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-5 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-6 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-7 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-8 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-9 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-10 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-38:atom-11 minimal-even:next:train:state-38
  (quote (minimal-even:next-chosen minimal-even:next:train:state-38 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-1 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-2 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-3 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-4 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-5 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-6 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-7 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-8 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-9 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-10 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-39:atom-11 minimal-even:next:train:state-39
  (quote (minimal-even:next-chosen minimal-even:next:train:state-39 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-1 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-2 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-3 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-4 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-5 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-6 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-7 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-8 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-9 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-10 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-40:atom-11 minimal-even:next:train:state-40
  (quote (minimal-even:next-chosen minimal-even:next:train:state-40 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-1 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-2 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-3 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-4 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-5 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-6 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-7 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-8 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-9 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-10 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-41:atom-11 minimal-even:next:train:state-41
  (quote (minimal-even:next-chosen minimal-even:next:train:state-41 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-1 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-2 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-3 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-4 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-5 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-6 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-7 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-8 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-9 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-10 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-42:atom-11 minimal-even:next:train:state-42
  (quote (minimal-even:next-chosen minimal-even:next:train:state-42 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-1 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-2 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-3 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-4 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-5 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-6 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-7 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-8 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-9 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-10 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-43:atom-11 minimal-even:next:train:state-43
  (quote (minimal-even:next-chosen minimal-even:next:train:state-43 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-1 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-2 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-3 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-4 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-5 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-6 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-7 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-8 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-9 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-10 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-44:atom-11 minimal-even:next:train:state-44
  (quote (minimal-even:next-chosen minimal-even:next:train:state-44 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-1 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-2 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-3 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-4 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-5 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-6 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-7 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-8 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-9 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-10 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-45:atom-11 minimal-even:next:train:state-45
  (quote (minimal-even:next-chosen minimal-even:next:train:state-45 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-1 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-2 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-3 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-4 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-5 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-6 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-7 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-8 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-9 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-10 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-46:atom-11 minimal-even:next:train:state-46
  (quote (minimal-even:next-chosen minimal-even:next:train:state-46 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-1 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-2 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-3 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-4 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-5 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-6 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-7 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-8 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-9 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-10 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-47:atom-11 minimal-even:next:train:state-47
  (quote (minimal-even:next-chosen minimal-even:next:train:state-47 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-1 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-2 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-3 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-4 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-5 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-6 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-7 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-8 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-9 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-10 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-48:atom-11 minimal-even:next:train:state-48
  (quote (minimal-even:next-chosen minimal-even:next:train:state-48 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-1 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-2 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-3 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-4 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-5 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-6 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-7 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-8 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-9 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-10 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-49:atom-11 minimal-even:next:train:state-49
  (quote (minimal-even:next-chosen minimal-even:next:train:state-49 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-1 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-2 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-3 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-4 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-5 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-6 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-7 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-8 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-9 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-10 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-50:atom-11 minimal-even:next:train:state-50
  (quote (minimal-even:next-chosen minimal-even:next:train:state-50 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-1 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-2 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-3 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-4 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-5 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-6 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-7 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-8 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-9 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-10 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-51:atom-11 minimal-even:next:train:state-51
  (quote (minimal-even:next-chosen minimal-even:next:train:state-51 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-1 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-2 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-3 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-4 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-5 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-6 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-7 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-8 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-9 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-10 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-52:atom-11 minimal-even:next:train:state-52
  (quote (minimal-even:next-chosen minimal-even:next:train:state-52 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-1 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-2 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-3 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-4 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-5 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-6 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-7 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-8 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-9 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-10 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-53:atom-11 minimal-even:next:train:state-53
  (quote (minimal-even:next-chosen minimal-even:next:train:state-53 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-1 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-2 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-3 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-4 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-5 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-6 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-7 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-8 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-9 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-10 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-54:atom-11 minimal-even:next:train:state-54
  (quote (minimal-even:next-chosen minimal-even:next:train:state-54 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-1 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-2 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-3 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-4 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-5 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-6 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-7 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-8 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-9 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-10 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-55:atom-11 minimal-even:next:train:state-55
  (quote (minimal-even:next-chosen minimal-even:next:train:state-55 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-1 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-2 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-3 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-4 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-5 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-6 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-7 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-8 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-9 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-10 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-56:atom-11 minimal-even:next:train:state-56
  (quote (minimal-even:next-chosen minimal-even:next:train:state-56 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-1 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-2 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-3 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-4 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-5 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-6 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-7 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-8 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-9 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-10 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-57:atom-11 minimal-even:next:train:state-57
  (quote (minimal-even:next-chosen minimal-even:next:train:state-57 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-1 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-2 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-3 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-4 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-5 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-6 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-7 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-8 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-9 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-10 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-58:atom-11 minimal-even:next:train:state-58
  (quote (minimal-even:next-chosen minimal-even:next:train:state-58 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-1 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-2 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-3 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-4 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-5 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-6 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-7 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-8 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-9 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-10 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-59:atom-11 minimal-even:next:train:state-59
  (quote (minimal-even:next-chosen minimal-even:next:train:state-59 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-1 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-2 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-3 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-4 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-5 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-6 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-7 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-8 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-9 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-10 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-60:atom-11 minimal-even:next:train:state-60
  (quote (minimal-even:next-chosen minimal-even:next:train:state-60 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-1 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-2 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-3 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-4 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-5 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-6 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-7 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-8 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-9 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-10 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-61:atom-11 minimal-even:next:train:state-61
  (quote (minimal-even:next-chosen minimal-even:next:train:state-61 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-1 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-2 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-3 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-4 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-5 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-6 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-7 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-8 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-9 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-10 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-62:atom-11 minimal-even:next:train:state-62
  (quote (minimal-even:next-chosen minimal-even:next:train:state-62 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-1 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-2 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-3 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-4 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-5 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-6 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-7 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-8 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-9 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-10 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-63:atom-11 minimal-even:next:train:state-63
  (quote (minimal-even:next-chosen minimal-even:next:train:state-63 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-1 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-2 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-3 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-4 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-5 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-6 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-7 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-8 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-9 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-10 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-64:atom-11 minimal-even:next:train:state-64
  (quote (minimal-even:next-chosen minimal-even:next:train:state-64 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-1 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-2 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-3 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-4 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-5 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-6 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-7 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-8 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-9 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-10 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-65:atom-11 minimal-even:next:train:state-65
  (quote (minimal-even:next-chosen minimal-even:next:train:state-65 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-1 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-2 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-3 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-4 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-5 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-6 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-7 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-8 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-9 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-10 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-66:atom-11 minimal-even:next:train:state-66
  (quote (minimal-even:next-chosen minimal-even:next:train:state-66 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-1 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-2 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-3 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-4 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-5 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-6 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-7 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-8 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-9 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-10 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-67:atom-11 minimal-even:next:train:state-67
  (quote (minimal-even:next-chosen minimal-even:next:train:state-67 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-1 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-2 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-3 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-4 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-5 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-6 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-7 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-8 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-9 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-10 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-68:atom-11 minimal-even:next:train:state-68
  (quote (minimal-even:next-chosen minimal-even:next:train:state-68 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-1 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-2 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-3 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-4 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-5 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-6 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-7 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-8 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-9 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-10 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-69:atom-11 minimal-even:next:train:state-69
  (quote (minimal-even:next-chosen minimal-even:next:train:state-69 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-1 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-2 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-3 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-4 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-5 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-6 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-7 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-8 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-9 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-10 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-70:atom-11 minimal-even:next:train:state-70
  (quote (minimal-even:next-chosen minimal-even:next:train:state-70 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-1 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-2 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-3 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-4 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-5 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-6 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-7 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-8 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-9 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-10 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-71:atom-11 minimal-even:next:train:state-71
  (quote (minimal-even:next-chosen minimal-even:next:train:state-71 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-1 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-2 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-3 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-4 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-5 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-6 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-7 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-8 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-9 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-10 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-72:atom-11 minimal-even:next:train:state-72
  (quote (minimal-even:next-chosen minimal-even:next:train:state-72 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-1 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-2 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-3 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-4 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-5 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-6 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-7 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-8 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-9 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-10 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-73:atom-11 minimal-even:next:train:state-73
  (quote (minimal-even:next-chosen minimal-even:next:train:state-73 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-1 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-2 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-3 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-4 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-5 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-6 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-7 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-8 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-9 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-10 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-74:atom-11 minimal-even:next:train:state-74
  (quote (minimal-even:next-chosen minimal-even:next:train:state-74 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-1 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-2 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-3 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-4 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-5 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-6 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-7 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-8 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-9 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-10 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-75:atom-11 minimal-even:next:train:state-75
  (quote (minimal-even:next-chosen minimal-even:next:train:state-75 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-1 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-2 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-3 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-4 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-5 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-6 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-7 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-8 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-9 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-10 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-76:atom-11 minimal-even:next:train:state-76
  (quote (minimal-even:next-chosen minimal-even:next:train:state-76 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-1 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-2 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-3 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-4 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-5 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-6 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-7 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-8 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-9 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-10 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-77:atom-11 minimal-even:next:train:state-77
  (quote (minimal-even:next-chosen minimal-even:next:train:state-77 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-1 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-2 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-3 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-4 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-5 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-6 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-7 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-8 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-9 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-10 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:train:state-78:atom-11 minimal-even:next:train:state-78
  (quote (minimal-even:next-chosen minimal-even:next:train:state-78 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-1 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-2 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-3 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-4 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-5 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-6 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-7 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-8 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-9 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-10 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-1:atom-11 minimal-even:next:validate:state-1
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-1 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-1 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-2 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-3 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-4 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-5 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-6 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-7 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-8 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-9 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-10 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-2:atom-11 minimal-even:next:validate:state-2
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-2 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-1 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-2 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-3 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-4 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-5 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-6 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-7 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-8 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-9 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-10 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-3:atom-11 minimal-even:next:validate:state-3
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-3 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-1 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-2 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-3 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-4 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-5 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-6 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-7 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-8 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-9 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-10 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-4:atom-11 minimal-even:next:validate:state-4
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-4 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-1 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-2 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-3 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-4 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-5 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-6 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-7 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-8 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-9 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-10 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-5:atom-11 minimal-even:next:validate:state-5
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-5 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-1 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-2 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-3 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-4 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-5 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-6 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-7 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-8 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-9 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-10 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-6:atom-11 minimal-even:next:validate:state-6
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-6 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-1 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-2 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-3 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-4 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-5 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-6 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-7 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-8 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-9 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-10 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-7:atom-11 minimal-even:next:validate:state-7
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-7 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-1 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-2 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-3 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-4 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-5 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-6 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-7 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-8 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-9 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-10 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-8:atom-11 minimal-even:next:validate:state-8
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-8 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-1 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-2 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-3 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-4 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-5 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-6 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-7 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-8 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-9 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-10 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-9:atom-11 minimal-even:next:validate:state-9
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-9 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-1 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-2 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-3 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-4 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-5 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-6 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-7 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-8 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-9 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-10 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-10:atom-11 minimal-even:next:validate:state-10
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-10 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-1 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-2 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-3 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-4 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-5 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-6 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-7 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-8 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-9 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-10 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-11:atom-11 minimal-even:next:validate:state-11
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-11 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-1 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-2 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-3 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-4 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-5 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-6 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-7 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-8 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-9 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-10 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-12:atom-11 minimal-even:next:validate:state-12
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-12 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-1 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-2 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-3 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-4 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-5 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-6 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-7 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-8 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-9 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-10 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-13:atom-11 minimal-even:next:validate:state-13
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-13 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-1 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-2 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-3 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-4 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-5 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-6 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-7 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-8 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-9 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-10 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-14:atom-11 minimal-even:next:validate:state-14
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-14 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-1 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-2 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-3 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-4 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-5 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-6 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-7 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-8 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-9 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-10 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-15:atom-11 minimal-even:next:validate:state-15
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-15 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-1 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-2 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-3 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-4 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-5 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-6 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-7 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-8 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-9 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-10 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-16:atom-11 minimal-even:next:validate:state-16
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-16 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-1 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-2 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-3 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-4 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-5 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-6 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-7 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-8 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-9 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-10 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-17:atom-11 minimal-even:next:validate:state-17
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-17 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-1 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-2 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-3 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-4 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-5 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-6 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-7 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-8 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-9 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-10 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-18:atom-11 minimal-even:next:validate:state-18
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-18 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-1 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-2 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-3 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-4 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-5 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-6 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-7 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-8 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-9 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-10 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:validate:state-19:atom-11 minimal-even:next:validate:state-19
  (quote (minimal-even:next-chosen minimal-even:next:validate:state-19 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-1 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-2 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-3 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-4 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-5 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-6 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-7 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-8 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-9 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-10 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-1:atom-11 minimal-even:next:test:state-1
  (quote (minimal-even:next-chosen minimal-even:next:test:state-1 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-1 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-2 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-3 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-4 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-5 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-6 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-7 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-8 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-9 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-10 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-2:atom-11 minimal-even:next:test:state-2
  (quote (minimal-even:next-chosen minimal-even:next:test:state-2 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-1 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-2 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-3 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-4 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-5 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-6 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-7 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-8 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-9 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-10 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-3:atom-11 minimal-even:next:test:state-3
  (quote (minimal-even:next-chosen minimal-even:next:test:state-3 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-1 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-2 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-3 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-4 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-5 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-6 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-7 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-8 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-9 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-10 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-4:atom-11 minimal-even:next:test:state-4
  (quote (minimal-even:next-chosen minimal-even:next:test:state-4 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-1 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-2 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-3 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-4 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-5 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-6 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-7 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-8 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-9 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-10 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-5:atom-11 minimal-even:next:test:state-5
  (quote (minimal-even:next-chosen minimal-even:next:test:state-5 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-1 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-2 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-3 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-4 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-5 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-6 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-7 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-8 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-9 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-10 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-6:atom-11 minimal-even:next:test:state-6
  (quote (minimal-even:next-chosen minimal-even:next:test:state-6 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-1 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-2 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-3 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-4 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-5 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-6 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-7 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-8 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-9 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-10 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-7:atom-11 minimal-even:next:test:state-7
  (quote (minimal-even:next-chosen minimal-even:next:test:state-7 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-1 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-2 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-3 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-4 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-5 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-6 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-7 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-8 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-9 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-10 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-8:atom-11 minimal-even:next:test:state-8
  (quote (minimal-even:next-chosen minimal-even:next:test:state-8 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-1 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-2 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-3 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-4 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-5 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-6 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-7 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-8 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-9 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-10 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-9:atom-11 minimal-even:next:test:state-9
  (quote (minimal-even:next-chosen minimal-even:next:test:state-9 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-1 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-2 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-3 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-4 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-5 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-6 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-7 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-8 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-9 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-10 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-10:atom-11 minimal-even:next:test:state-10
  (quote (minimal-even:next-chosen minimal-even:next:test:state-10 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-1 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-2 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-3 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-4 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-5 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-6 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-7 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-8 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-9 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-10 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-11:atom-11 minimal-even:next:test:state-11
  (quote (minimal-even:next-chosen minimal-even:next:test:state-11 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-1 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-2 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-3 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-4 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-5 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-6 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-7 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-8 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-9 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-10 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-12:atom-11 minimal-even:next:test:state-12
  (quote (minimal-even:next-chosen minimal-even:next:test:state-12 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-1 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-2 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-3 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-4 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-5 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-6 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-7 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-8 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-9 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-10 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-13:atom-11 minimal-even:next:test:state-13
  (quote (minimal-even:next-chosen minimal-even:next:test:state-13 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-1 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-2 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-3 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-4 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-5 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-6 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-7 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-8 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-9 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-10 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-14:atom-11 minimal-even:next:test:state-14
  (quote (minimal-even:next-chosen minimal-even:next:test:state-14 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-1 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-2 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-3 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-4 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-5 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-6 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-7 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-8 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-9 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-10 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-15:atom-11 minimal-even:next:test:state-15
  (quote (minimal-even:next-chosen minimal-even:next:test:state-15 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-1 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-2 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-3 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-4 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-5 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-6 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-7 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-8 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-9 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-10 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-16:atom-11 minimal-even:next:test:state-16
  (quote (minimal-even:next-chosen minimal-even:next:test:state-16 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-1 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-2 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-3 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-4 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-5 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-6 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-7 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-8 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-9 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-10 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-17:atom-11 minimal-even:next:test:state-17
  (quote (minimal-even:next-chosen minimal-even:next:test:state-17 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-1 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-2 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-3 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-4 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-5 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-6 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-7 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-8 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-9 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-10 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-18:atom-11 minimal-even:next:test:state-18
  (quote (minimal-even:next-chosen minimal-even:next:test:state-18 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-1 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-2 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-3 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-4 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-5 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-6 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-7 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-8 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-9 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-10 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-19:atom-11 minimal-even:next:test:state-19
  (quote (minimal-even:next-chosen minimal-even:next:test:state-19 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-1 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n0)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-2 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n1)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-3 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n10)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-4 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n2)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-5 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n3)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-6 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n4)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-7 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n5)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-8 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n6)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-9 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n7)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-10 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n8)))
!(iggp:minimal-even:classify minimal-even:next:test:state-20:atom-11 minimal-even:next:test:state-20
  (quote (minimal-even:next-chosen minimal-even:next:test:state-20 minimal-even:n9)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-1:atom-1 minimal-even:terminal:train:state-1
  (quote (minimal-even:terminal minimal-even:terminal:train:state-1)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-2:atom-1 minimal-even:terminal:train:state-2
  (quote (minimal-even:terminal minimal-even:terminal:train:state-2)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-3:atom-1 minimal-even:terminal:train:state-3
  (quote (minimal-even:terminal minimal-even:terminal:train:state-3)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-4:atom-1 minimal-even:terminal:train:state-4
  (quote (minimal-even:terminal minimal-even:terminal:train:state-4)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-5:atom-1 minimal-even:terminal:train:state-5
  (quote (minimal-even:terminal minimal-even:terminal:train:state-5)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-6:atom-1 minimal-even:terminal:train:state-6
  (quote (minimal-even:terminal minimal-even:terminal:train:state-6)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-7:atom-1 minimal-even:terminal:train:state-7
  (quote (minimal-even:terminal minimal-even:terminal:train:state-7)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-8:atom-1 minimal-even:terminal:train:state-8
  (quote (minimal-even:terminal minimal-even:terminal:train:state-8)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-9:atom-1 minimal-even:terminal:train:state-9
  (quote (minimal-even:terminal minimal-even:terminal:train:state-9)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-10:atom-1 minimal-even:terminal:train:state-10
  (quote (minimal-even:terminal minimal-even:terminal:train:state-10)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-11:atom-1 minimal-even:terminal:train:state-11
  (quote (minimal-even:terminal minimal-even:terminal:train:state-11)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-12:atom-1 minimal-even:terminal:train:state-12
  (quote (minimal-even:terminal minimal-even:terminal:train:state-12)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-13:atom-1 minimal-even:terminal:train:state-13
  (quote (minimal-even:terminal minimal-even:terminal:train:state-13)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-14:atom-1 minimal-even:terminal:train:state-14
  (quote (minimal-even:terminal minimal-even:terminal:train:state-14)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-15:atom-1 minimal-even:terminal:train:state-15
  (quote (minimal-even:terminal minimal-even:terminal:train:state-15)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-16:atom-1 minimal-even:terminal:train:state-16
  (quote (minimal-even:terminal minimal-even:terminal:train:state-16)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-17:atom-1 minimal-even:terminal:train:state-17
  (quote (minimal-even:terminal minimal-even:terminal:train:state-17)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-18:atom-1 minimal-even:terminal:train:state-18
  (quote (minimal-even:terminal minimal-even:terminal:train:state-18)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-19:atom-1 minimal-even:terminal:train:state-19
  (quote (minimal-even:terminal minimal-even:terminal:train:state-19)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-20:atom-1 minimal-even:terminal:train:state-20
  (quote (minimal-even:terminal minimal-even:terminal:train:state-20)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-21:atom-1 minimal-even:terminal:train:state-21
  (quote (minimal-even:terminal minimal-even:terminal:train:state-21)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-22:atom-1 minimal-even:terminal:train:state-22
  (quote (minimal-even:terminal minimal-even:terminal:train:state-22)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-23:atom-1 minimal-even:terminal:train:state-23
  (quote (minimal-even:terminal minimal-even:terminal:train:state-23)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-24:atom-1 minimal-even:terminal:train:state-24
  (quote (minimal-even:terminal minimal-even:terminal:train:state-24)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-25:atom-1 minimal-even:terminal:train:state-25
  (quote (minimal-even:terminal minimal-even:terminal:train:state-25)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-26:atom-1 minimal-even:terminal:train:state-26
  (quote (minimal-even:terminal minimal-even:terminal:train:state-26)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-27:atom-1 minimal-even:terminal:train:state-27
  (quote (minimal-even:terminal minimal-even:terminal:train:state-27)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-28:atom-1 minimal-even:terminal:train:state-28
  (quote (minimal-even:terminal minimal-even:terminal:train:state-28)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-29:atom-1 minimal-even:terminal:train:state-29
  (quote (minimal-even:terminal minimal-even:terminal:train:state-29)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-30:atom-1 minimal-even:terminal:train:state-30
  (quote (minimal-even:terminal minimal-even:terminal:train:state-30)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-31:atom-1 minimal-even:terminal:train:state-31
  (quote (minimal-even:terminal minimal-even:terminal:train:state-31)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-32:atom-1 minimal-even:terminal:train:state-32
  (quote (minimal-even:terminal minimal-even:terminal:train:state-32)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-33:atom-1 minimal-even:terminal:train:state-33
  (quote (minimal-even:terminal minimal-even:terminal:train:state-33)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-34:atom-1 minimal-even:terminal:train:state-34
  (quote (minimal-even:terminal minimal-even:terminal:train:state-34)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-35:atom-1 minimal-even:terminal:train:state-35
  (quote (minimal-even:terminal minimal-even:terminal:train:state-35)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-36:atom-1 minimal-even:terminal:train:state-36
  (quote (minimal-even:terminal minimal-even:terminal:train:state-36)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-37:atom-1 minimal-even:terminal:train:state-37
  (quote (minimal-even:terminal minimal-even:terminal:train:state-37)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-38:atom-1 minimal-even:terminal:train:state-38
  (quote (minimal-even:terminal minimal-even:terminal:train:state-38)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-39:atom-1 minimal-even:terminal:train:state-39
  (quote (minimal-even:terminal minimal-even:terminal:train:state-39)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-40:atom-1 minimal-even:terminal:train:state-40
  (quote (minimal-even:terminal minimal-even:terminal:train:state-40)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-41:atom-1 minimal-even:terminal:train:state-41
  (quote (minimal-even:terminal minimal-even:terminal:train:state-41)))
!(iggp:minimal-even:classify minimal-even:terminal:train:state-42:atom-1 minimal-even:terminal:train:state-42
  (quote (minimal-even:terminal minimal-even:terminal:train:state-42)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-1:atom-1 minimal-even:terminal:validate:state-1
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-1)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-2:atom-1 minimal-even:terminal:validate:state-2
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-2)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-3:atom-1 minimal-even:terminal:validate:state-3
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-3)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-4:atom-1 minimal-even:terminal:validate:state-4
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-4)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-5:atom-1 minimal-even:terminal:validate:state-5
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-5)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-6:atom-1 minimal-even:terminal:validate:state-6
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-6)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-7:atom-1 minimal-even:terminal:validate:state-7
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-7)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-8:atom-1 minimal-even:terminal:validate:state-8
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-8)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-9:atom-1 minimal-even:terminal:validate:state-9
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-9)))
!(iggp:minimal-even:classify minimal-even:terminal:validate:state-10:atom-1 minimal-even:terminal:validate:state-10
  (quote (minimal-even:terminal minimal-even:terminal:validate:state-10)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-1:atom-1 minimal-even:terminal:test:state-1
  (quote (minimal-even:terminal minimal-even:terminal:test:state-1)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-2:atom-1 minimal-even:terminal:test:state-2
  (quote (minimal-even:terminal minimal-even:terminal:test:state-2)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-3:atom-1 minimal-even:terminal:test:state-3
  (quote (minimal-even:terminal minimal-even:terminal:test:state-3)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-4:atom-1 minimal-even:terminal:test:state-4
  (quote (minimal-even:terminal minimal-even:terminal:test:state-4)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-5:atom-1 minimal-even:terminal:test:state-5
  (quote (minimal-even:terminal minimal-even:terminal:test:state-5)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-6:atom-1 minimal-even:terminal:test:state-6
  (quote (minimal-even:terminal minimal-even:terminal:test:state-6)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-7:atom-1 minimal-even:terminal:test:state-7
  (quote (minimal-even:terminal minimal-even:terminal:test:state-7)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-8:atom-1 minimal-even:terminal:test:state-8
  (quote (minimal-even:terminal minimal-even:terminal:test:state-8)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-9:atom-1 minimal-even:terminal:test:state-9
  (quote (minimal-even:terminal minimal-even:terminal:test:state-9)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-10:atom-1 minimal-even:terminal:test:state-10
  (quote (minimal-even:terminal minimal-even:terminal:test:state-10)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-11:atom-1 minimal-even:terminal:test:state-11
  (quote (minimal-even:terminal minimal-even:terminal:test:state-11)))
!(iggp:minimal-even:classify minimal-even:terminal:test:state-12:atom-1 minimal-even:terminal:test:state-12
  (quote (minimal-even:terminal minimal-even:terminal:test:state-12)))

!(iggp:minimal-even:proof-shapes
  minimal-even:legal:train:state-1
  (quote (minimal-even:legal minimal-even:legal:train:state-1
    minimal-even:player (minimal-even:choose minimal-even:n5))))
!(iggp:minimal-even:proof-shapes
  minimal-even:next:train:state-7
  (quote (minimal-even:next-chosen minimal-even:next:train:state-7
    minimal-even:n9)))
