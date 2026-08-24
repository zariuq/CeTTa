; Exact qualification for all IGGP buttons_and_lights target tasks.
;
; Canonical GDL: games/buttons_and_lights.txt
; SHA-256: 701742477ee9b78648fb20e5705dc2a765c5259d7b344d9ea904b83aa9ac812b
; The excluded Prolog projection omits numeric successor transitions.
; Every train, validation, and test atom occurrence is classified.
; GDL negation consumes finite-state absence evidence, never failure.

!(import! &self ../../lib/ilp/iggp_finite_view_types.metta)
!(import! &self ../../lib/ilp/iggp_buttons_and_lights_types.metta)
!(import! &self ../../lib/ilp/iggp_buttons_and_lights_rules.metta)
!(import! &self ../../lib/ilp/iggp_benchmark_classify.metta)

(= (iggp:buttons:classify $name $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (buttons:package $episode) 32 2000000 256 (quote $goal))
    (iggp:classify-occurrences
      $name (quote $goal) $occurrences)))

(= (buttons:proof-shape
     (buttons:proof:goal-0-p $state $absence))
   (goal-zero-via buttons:p))
(= (buttons:proof-shape
     (buttons:proof:goal-0-q $state $absence))
   (goal-zero-via buttons:q))
(= (buttons:proof-shape
     (buttons:proof:goal-0-r $state $absence))
   (goal-zero-via buttons:r))
(= (buttons:proof-shape
     (buttons:proof:terminal-all-on $state $p $q $r))
   (terminal-via all-on))
(= (buttons:proof-shape
     (buttons:proof:terminal-step-seven $state $step))
   (terminal-via step-seven))
(= (buttons:absence-shape (unquote (quote $proof)))
   (buttons:absence-shape $proof))
(= (buttons:absence-shape
     (buttons:proof:not-true-p $state $q $r $step $view))
   (finite-state-absence buttons:p))
(= (buttons:proof-shape
     (buttons:proof:next-p-a-absent $state $does $absence))
   (next-p-via (buttons:absence-shape $absence)))
(= (iggp:buttons:proof-shapes $episode (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      $occurrences $metrics $revision)
    (compile:run
      (buttons:package $episode) 32 2000000 256 (quote $goal))
    (collapse
      (let (occurrence $proof-data) (superpose $occurrences)
        (let (quote $proof) $proof-data
          (buttons:proof-shape $proof))))))

!(iggp:buttons:classify buttons:goal:train:state-1:atom-1 buttons:goal:train:state-1
  (quote (buttons:goal buttons:goal:train:state-1 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-1:atom-2 buttons:goal:train:state-1
  (quote (buttons:goal buttons:goal:train:state-1 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-2:atom-1 buttons:goal:train:state-2
  (quote (buttons:goal buttons:goal:train:state-2 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-2:atom-2 buttons:goal:train:state-2
  (quote (buttons:goal buttons:goal:train:state-2 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-3:atom-1 buttons:goal:train:state-3
  (quote (buttons:goal buttons:goal:train:state-3 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-3:atom-2 buttons:goal:train:state-3
  (quote (buttons:goal buttons:goal:train:state-3 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-4:atom-1 buttons:goal:train:state-4
  (quote (buttons:goal buttons:goal:train:state-4 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-4:atom-2 buttons:goal:train:state-4
  (quote (buttons:goal buttons:goal:train:state-4 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-5:atom-1 buttons:goal:train:state-5
  (quote (buttons:goal buttons:goal:train:state-5 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-5:atom-2 buttons:goal:train:state-5
  (quote (buttons:goal buttons:goal:train:state-5 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-6:atom-1 buttons:goal:train:state-6
  (quote (buttons:goal buttons:goal:train:state-6 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-6:atom-2 buttons:goal:train:state-6
  (quote (buttons:goal buttons:goal:train:state-6 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-7:atom-1 buttons:goal:train:state-7
  (quote (buttons:goal buttons:goal:train:state-7 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-7:atom-2 buttons:goal:train:state-7
  (quote (buttons:goal buttons:goal:train:state-7 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-8:atom-1 buttons:goal:train:state-8
  (quote (buttons:goal buttons:goal:train:state-8 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-8:atom-2 buttons:goal:train:state-8
  (quote (buttons:goal buttons:goal:train:state-8 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-9:atom-1 buttons:goal:train:state-9
  (quote (buttons:goal buttons:goal:train:state-9 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-9:atom-2 buttons:goal:train:state-9
  (quote (buttons:goal buttons:goal:train:state-9 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-10:atom-1 buttons:goal:train:state-10
  (quote (buttons:goal buttons:goal:train:state-10 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-10:atom-2 buttons:goal:train:state-10
  (quote (buttons:goal buttons:goal:train:state-10 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-11:atom-1 buttons:goal:train:state-11
  (quote (buttons:goal buttons:goal:train:state-11 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-11:atom-2 buttons:goal:train:state-11
  (quote (buttons:goal buttons:goal:train:state-11 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-12:atom-1 buttons:goal:train:state-12
  (quote (buttons:goal buttons:goal:train:state-12 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-12:atom-2 buttons:goal:train:state-12
  (quote (buttons:goal buttons:goal:train:state-12 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-13:atom-1 buttons:goal:train:state-13
  (quote (buttons:goal buttons:goal:train:state-13 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-13:atom-2 buttons:goal:train:state-13
  (quote (buttons:goal buttons:goal:train:state-13 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-14:atom-1 buttons:goal:train:state-14
  (quote (buttons:goal buttons:goal:train:state-14 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-14:atom-2 buttons:goal:train:state-14
  (quote (buttons:goal buttons:goal:train:state-14 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-15:atom-1 buttons:goal:train:state-15
  (quote (buttons:goal buttons:goal:train:state-15 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-15:atom-2 buttons:goal:train:state-15
  (quote (buttons:goal buttons:goal:train:state-15 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-16:atom-1 buttons:goal:train:state-16
  (quote (buttons:goal buttons:goal:train:state-16 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-16:atom-2 buttons:goal:train:state-16
  (quote (buttons:goal buttons:goal:train:state-16 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-17:atom-1 buttons:goal:train:state-17
  (quote (buttons:goal buttons:goal:train:state-17 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-17:atom-2 buttons:goal:train:state-17
  (quote (buttons:goal buttons:goal:train:state-17 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-18:atom-1 buttons:goal:train:state-18
  (quote (buttons:goal buttons:goal:train:state-18 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-18:atom-2 buttons:goal:train:state-18
  (quote (buttons:goal buttons:goal:train:state-18 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-19:atom-1 buttons:goal:train:state-19
  (quote (buttons:goal buttons:goal:train:state-19 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-19:atom-2 buttons:goal:train:state-19
  (quote (buttons:goal buttons:goal:train:state-19 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-20:atom-1 buttons:goal:train:state-20
  (quote (buttons:goal buttons:goal:train:state-20 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-20:atom-2 buttons:goal:train:state-20
  (quote (buttons:goal buttons:goal:train:state-20 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-21:atom-1 buttons:goal:train:state-21
  (quote (buttons:goal buttons:goal:train:state-21 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-21:atom-2 buttons:goal:train:state-21
  (quote (buttons:goal buttons:goal:train:state-21 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:train:state-22:atom-1 buttons:goal:train:state-22
  (quote (buttons:goal buttons:goal:train:state-22 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:train:state-22:atom-2 buttons:goal:train:state-22
  (quote (buttons:goal buttons:goal:train:state-22 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:validate:state-1:atom-1 buttons:goal:validate:state-1
  (quote (buttons:goal buttons:goal:validate:state-1 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:validate:state-1:atom-2 buttons:goal:validate:state-1
  (quote (buttons:goal buttons:goal:validate:state-1 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:validate:state-2:atom-1 buttons:goal:validate:state-2
  (quote (buttons:goal buttons:goal:validate:state-2 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:validate:state-2:atom-2 buttons:goal:validate:state-2
  (quote (buttons:goal buttons:goal:validate:state-2 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:validate:state-3:atom-1 buttons:goal:validate:state-3
  (quote (buttons:goal buttons:goal:validate:state-3 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:validate:state-3:atom-2 buttons:goal:validate:state-3
  (quote (buttons:goal buttons:goal:validate:state-3 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:validate:state-4:atom-1 buttons:goal:validate:state-4
  (quote (buttons:goal buttons:goal:validate:state-4 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:validate:state-4:atom-2 buttons:goal:validate:state-4
  (quote (buttons:goal buttons:goal:validate:state-4 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:validate:state-5:atom-1 buttons:goal:validate:state-5
  (quote (buttons:goal buttons:goal:validate:state-5 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:validate:state-5:atom-2 buttons:goal:validate:state-5
  (quote (buttons:goal buttons:goal:validate:state-5 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:test:state-1:atom-1 buttons:goal:test:state-1
  (quote (buttons:goal buttons:goal:test:state-1 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:test:state-1:atom-2 buttons:goal:test:state-1
  (quote (buttons:goal buttons:goal:test:state-1 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:test:state-2:atom-1 buttons:goal:test:state-2
  (quote (buttons:goal buttons:goal:test:state-2 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:test:state-2:atom-2 buttons:goal:test:state-2
  (quote (buttons:goal buttons:goal:test:state-2 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:test:state-3:atom-1 buttons:goal:test:state-3
  (quote (buttons:goal buttons:goal:test:state-3 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:test:state-3:atom-2 buttons:goal:test:state-3
  (quote (buttons:goal buttons:goal:test:state-3 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:test:state-4:atom-1 buttons:goal:test:state-4
  (quote (buttons:goal buttons:goal:test:state-4 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:test:state-4:atom-2 buttons:goal:test:state-4
  (quote (buttons:goal buttons:goal:test:state-4 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:goal:test:state-5:atom-1 buttons:goal:test:state-5
  (quote (buttons:goal buttons:goal:test:state-5 buttons:robot buttons:n0)))
!(iggp:buttons:classify buttons:goal:test:state-5:atom-2 buttons:goal:test:state-5
  (quote (buttons:goal buttons:goal:test:state-5 buttons:robot buttons:n100)))
!(iggp:buttons:classify buttons:legal:train:state-1:atom-1 buttons:legal:train:state-1
  (quote (buttons:legal buttons:legal:train:state-1 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-1:atom-2 buttons:legal:train:state-1
  (quote (buttons:legal buttons:legal:train:state-1 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-1:atom-3 buttons:legal:train:state-1
  (quote (buttons:legal buttons:legal:train:state-1 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-2:atom-1 buttons:legal:train:state-2
  (quote (buttons:legal buttons:legal:train:state-2 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-2:atom-2 buttons:legal:train:state-2
  (quote (buttons:legal buttons:legal:train:state-2 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-2:atom-3 buttons:legal:train:state-2
  (quote (buttons:legal buttons:legal:train:state-2 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-3:atom-1 buttons:legal:train:state-3
  (quote (buttons:legal buttons:legal:train:state-3 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-3:atom-2 buttons:legal:train:state-3
  (quote (buttons:legal buttons:legal:train:state-3 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-3:atom-3 buttons:legal:train:state-3
  (quote (buttons:legal buttons:legal:train:state-3 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-4:atom-1 buttons:legal:train:state-4
  (quote (buttons:legal buttons:legal:train:state-4 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-4:atom-2 buttons:legal:train:state-4
  (quote (buttons:legal buttons:legal:train:state-4 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-4:atom-3 buttons:legal:train:state-4
  (quote (buttons:legal buttons:legal:train:state-4 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-5:atom-1 buttons:legal:train:state-5
  (quote (buttons:legal buttons:legal:train:state-5 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-5:atom-2 buttons:legal:train:state-5
  (quote (buttons:legal buttons:legal:train:state-5 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-5:atom-3 buttons:legal:train:state-5
  (quote (buttons:legal buttons:legal:train:state-5 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-6:atom-1 buttons:legal:train:state-6
  (quote (buttons:legal buttons:legal:train:state-6 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-6:atom-2 buttons:legal:train:state-6
  (quote (buttons:legal buttons:legal:train:state-6 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-6:atom-3 buttons:legal:train:state-6
  (quote (buttons:legal buttons:legal:train:state-6 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-7:atom-1 buttons:legal:train:state-7
  (quote (buttons:legal buttons:legal:train:state-7 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-7:atom-2 buttons:legal:train:state-7
  (quote (buttons:legal buttons:legal:train:state-7 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-7:atom-3 buttons:legal:train:state-7
  (quote (buttons:legal buttons:legal:train:state-7 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-8:atom-1 buttons:legal:train:state-8
  (quote (buttons:legal buttons:legal:train:state-8 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-8:atom-2 buttons:legal:train:state-8
  (quote (buttons:legal buttons:legal:train:state-8 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-8:atom-3 buttons:legal:train:state-8
  (quote (buttons:legal buttons:legal:train:state-8 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-9:atom-1 buttons:legal:train:state-9
  (quote (buttons:legal buttons:legal:train:state-9 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-9:atom-2 buttons:legal:train:state-9
  (quote (buttons:legal buttons:legal:train:state-9 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-9:atom-3 buttons:legal:train:state-9
  (quote (buttons:legal buttons:legal:train:state-9 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-10:atom-1 buttons:legal:train:state-10
  (quote (buttons:legal buttons:legal:train:state-10 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-10:atom-2 buttons:legal:train:state-10
  (quote (buttons:legal buttons:legal:train:state-10 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-10:atom-3 buttons:legal:train:state-10
  (quote (buttons:legal buttons:legal:train:state-10 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-11:atom-1 buttons:legal:train:state-11
  (quote (buttons:legal buttons:legal:train:state-11 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-11:atom-2 buttons:legal:train:state-11
  (quote (buttons:legal buttons:legal:train:state-11 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-11:atom-3 buttons:legal:train:state-11
  (quote (buttons:legal buttons:legal:train:state-11 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-12:atom-1 buttons:legal:train:state-12
  (quote (buttons:legal buttons:legal:train:state-12 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-12:atom-2 buttons:legal:train:state-12
  (quote (buttons:legal buttons:legal:train:state-12 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-12:atom-3 buttons:legal:train:state-12
  (quote (buttons:legal buttons:legal:train:state-12 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-13:atom-1 buttons:legal:train:state-13
  (quote (buttons:legal buttons:legal:train:state-13 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-13:atom-2 buttons:legal:train:state-13
  (quote (buttons:legal buttons:legal:train:state-13 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-13:atom-3 buttons:legal:train:state-13
  (quote (buttons:legal buttons:legal:train:state-13 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-14:atom-1 buttons:legal:train:state-14
  (quote (buttons:legal buttons:legal:train:state-14 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-14:atom-2 buttons:legal:train:state-14
  (quote (buttons:legal buttons:legal:train:state-14 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-14:atom-3 buttons:legal:train:state-14
  (quote (buttons:legal buttons:legal:train:state-14 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-15:atom-1 buttons:legal:train:state-15
  (quote (buttons:legal buttons:legal:train:state-15 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-15:atom-2 buttons:legal:train:state-15
  (quote (buttons:legal buttons:legal:train:state-15 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-15:atom-3 buttons:legal:train:state-15
  (quote (buttons:legal buttons:legal:train:state-15 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-16:atom-1 buttons:legal:train:state-16
  (quote (buttons:legal buttons:legal:train:state-16 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-16:atom-2 buttons:legal:train:state-16
  (quote (buttons:legal buttons:legal:train:state-16 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-16:atom-3 buttons:legal:train:state-16
  (quote (buttons:legal buttons:legal:train:state-16 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-17:atom-1 buttons:legal:train:state-17
  (quote (buttons:legal buttons:legal:train:state-17 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-17:atom-2 buttons:legal:train:state-17
  (quote (buttons:legal buttons:legal:train:state-17 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-17:atom-3 buttons:legal:train:state-17
  (quote (buttons:legal buttons:legal:train:state-17 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-18:atom-1 buttons:legal:train:state-18
  (quote (buttons:legal buttons:legal:train:state-18 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-18:atom-2 buttons:legal:train:state-18
  (quote (buttons:legal buttons:legal:train:state-18 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-18:atom-3 buttons:legal:train:state-18
  (quote (buttons:legal buttons:legal:train:state-18 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-19:atom-1 buttons:legal:train:state-19
  (quote (buttons:legal buttons:legal:train:state-19 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-19:atom-2 buttons:legal:train:state-19
  (quote (buttons:legal buttons:legal:train:state-19 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-19:atom-3 buttons:legal:train:state-19
  (quote (buttons:legal buttons:legal:train:state-19 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-20:atom-1 buttons:legal:train:state-20
  (quote (buttons:legal buttons:legal:train:state-20 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-20:atom-2 buttons:legal:train:state-20
  (quote (buttons:legal buttons:legal:train:state-20 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-20:atom-3 buttons:legal:train:state-20
  (quote (buttons:legal buttons:legal:train:state-20 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:train:state-21:atom-1 buttons:legal:train:state-21
  (quote (buttons:legal buttons:legal:train:state-21 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:train:state-21:atom-2 buttons:legal:train:state-21
  (quote (buttons:legal buttons:legal:train:state-21 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:train:state-21:atom-3 buttons:legal:train:state-21
  (quote (buttons:legal buttons:legal:train:state-21 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:validate:state-1:atom-1 buttons:legal:validate:state-1
  (quote (buttons:legal buttons:legal:validate:state-1 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:validate:state-1:atom-2 buttons:legal:validate:state-1
  (quote (buttons:legal buttons:legal:validate:state-1 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:validate:state-1:atom-3 buttons:legal:validate:state-1
  (quote (buttons:legal buttons:legal:validate:state-1 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:validate:state-2:atom-1 buttons:legal:validate:state-2
  (quote (buttons:legal buttons:legal:validate:state-2 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:validate:state-2:atom-2 buttons:legal:validate:state-2
  (quote (buttons:legal buttons:legal:validate:state-2 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:validate:state-2:atom-3 buttons:legal:validate:state-2
  (quote (buttons:legal buttons:legal:validate:state-2 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:validate:state-3:atom-1 buttons:legal:validate:state-3
  (quote (buttons:legal buttons:legal:validate:state-3 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:validate:state-3:atom-2 buttons:legal:validate:state-3
  (quote (buttons:legal buttons:legal:validate:state-3 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:validate:state-3:atom-3 buttons:legal:validate:state-3
  (quote (buttons:legal buttons:legal:validate:state-3 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:validate:state-4:atom-1 buttons:legal:validate:state-4
  (quote (buttons:legal buttons:legal:validate:state-4 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:validate:state-4:atom-2 buttons:legal:validate:state-4
  (quote (buttons:legal buttons:legal:validate:state-4 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:validate:state-4:atom-3 buttons:legal:validate:state-4
  (quote (buttons:legal buttons:legal:validate:state-4 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:validate:state-5:atom-1 buttons:legal:validate:state-5
  (quote (buttons:legal buttons:legal:validate:state-5 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:validate:state-5:atom-2 buttons:legal:validate:state-5
  (quote (buttons:legal buttons:legal:validate:state-5 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:validate:state-5:atom-3 buttons:legal:validate:state-5
  (quote (buttons:legal buttons:legal:validate:state-5 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:test:state-1:atom-1 buttons:legal:test:state-1
  (quote (buttons:legal buttons:legal:test:state-1 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:test:state-1:atom-2 buttons:legal:test:state-1
  (quote (buttons:legal buttons:legal:test:state-1 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:test:state-1:atom-3 buttons:legal:test:state-1
  (quote (buttons:legal buttons:legal:test:state-1 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:test:state-2:atom-1 buttons:legal:test:state-2
  (quote (buttons:legal buttons:legal:test:state-2 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:test:state-2:atom-2 buttons:legal:test:state-2
  (quote (buttons:legal buttons:legal:test:state-2 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:test:state-2:atom-3 buttons:legal:test:state-2
  (quote (buttons:legal buttons:legal:test:state-2 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:test:state-3:atom-1 buttons:legal:test:state-3
  (quote (buttons:legal buttons:legal:test:state-3 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:test:state-3:atom-2 buttons:legal:test:state-3
  (quote (buttons:legal buttons:legal:test:state-3 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:test:state-3:atom-3 buttons:legal:test:state-3
  (quote (buttons:legal buttons:legal:test:state-3 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:test:state-4:atom-1 buttons:legal:test:state-4
  (quote (buttons:legal buttons:legal:test:state-4 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:test:state-4:atom-2 buttons:legal:test:state-4
  (quote (buttons:legal buttons:legal:test:state-4 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:test:state-4:atom-3 buttons:legal:test:state-4
  (quote (buttons:legal buttons:legal:test:state-4 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:test:state-5:atom-1 buttons:legal:test:state-5
  (quote (buttons:legal buttons:legal:test:state-5 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:test:state-5:atom-2 buttons:legal:test:state-5
  (quote (buttons:legal buttons:legal:test:state-5 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:test:state-5:atom-3 buttons:legal:test:state-5
  (quote (buttons:legal buttons:legal:test:state-5 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:legal:test:state-6:atom-1 buttons:legal:test:state-6
  (quote (buttons:legal buttons:legal:test:state-6 buttons:robot buttons:a)))
!(iggp:buttons:classify buttons:legal:test:state-6:atom-2 buttons:legal:test:state-6
  (quote (buttons:legal buttons:legal:test:state-6 buttons:robot buttons:b)))
!(iggp:buttons:classify buttons:legal:test:state-6:atom-3 buttons:legal:test:state-6
  (quote (buttons:legal buttons:legal:test:state-6 buttons:robot buttons:c)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-1 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-2 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-3 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-4 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-5 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-6 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-7 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-8 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-9 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-1:atom-10 buttons:next:train:state-1
  (quote (buttons:next buttons:next:train:state-1 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-1 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-2 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-3 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-4 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-5 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-6 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-7 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-8 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-9 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-2:atom-10 buttons:next:train:state-2
  (quote (buttons:next buttons:next:train:state-2 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-1 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-2 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-3 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-4 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-5 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-6 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-7 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-8 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-9 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-3:atom-10 buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-1 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-2 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-3 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-4 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-5 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-6 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-7 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-8 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-9 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-4:atom-10 buttons:next:train:state-4
  (quote (buttons:next buttons:next:train:state-4 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-1 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-2 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-3 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-4 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-5 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-6 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-7 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-8 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-9 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-5:atom-10 buttons:next:train:state-5
  (quote (buttons:next buttons:next:train:state-5 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-1 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-2 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-3 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-4 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-5 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-6 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-7 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-8 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-9 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-6:atom-10 buttons:next:train:state-6
  (quote (buttons:next buttons:next:train:state-6 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-1 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-2 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-3 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-4 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-5 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-6 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-7 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-8 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-9 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-7:atom-10 buttons:next:train:state-7
  (quote (buttons:next buttons:next:train:state-7 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-1 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-2 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-3 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-4 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-5 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-6 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-7 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-8 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-9 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-8:atom-10 buttons:next:train:state-8
  (quote (buttons:next buttons:next:train:state-8 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-1 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-2 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-3 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-4 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-5 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-6 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-7 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-8 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-9 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-9:atom-10 buttons:next:train:state-9
  (quote (buttons:next buttons:next:train:state-9 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-1 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-2 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-3 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-4 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-5 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-6 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-7 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-8 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-9 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-10:atom-10 buttons:next:train:state-10
  (quote (buttons:next buttons:next:train:state-10 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-1 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-2 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-3 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-4 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-5 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-6 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-7 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-8 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-9 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-11:atom-10 buttons:next:train:state-11
  (quote (buttons:next buttons:next:train:state-11 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-1 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-2 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-3 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-4 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-5 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-6 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-7 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-8 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-9 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-12:atom-10 buttons:next:train:state-12
  (quote (buttons:next buttons:next:train:state-12 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-1 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-2 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-3 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-4 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-5 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-6 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-7 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-8 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-9 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-13:atom-10 buttons:next:train:state-13
  (quote (buttons:next buttons:next:train:state-13 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-1 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-2 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-3 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-4 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-5 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-6 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-7 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-8 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-9 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-14:atom-10 buttons:next:train:state-14
  (quote (buttons:next buttons:next:train:state-14 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-1 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-2 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-3 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-4 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-5 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-6 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-7 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-8 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-9 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-15:atom-10 buttons:next:train:state-15
  (quote (buttons:next buttons:next:train:state-15 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-1 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-2 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-3 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-4 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-5 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-6 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-7 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-8 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-9 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-16:atom-10 buttons:next:train:state-16
  (quote (buttons:next buttons:next:train:state-16 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-1 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-2 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-3 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-4 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-5 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-6 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-7 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-8 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-9 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-17:atom-10 buttons:next:train:state-17
  (quote (buttons:next buttons:next:train:state-17 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-1 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-2 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-3 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-4 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-5 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-6 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-7 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-8 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-9 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-18:atom-10 buttons:next:train:state-18
  (quote (buttons:next buttons:next:train:state-18 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-1 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-2 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-3 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-4 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-5 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-6 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-7 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-8 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-9 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-19:atom-10 buttons:next:train:state-19
  (quote (buttons:next buttons:next:train:state-19 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-1 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-2 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-3 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-4 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-5 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-6 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-7 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-8 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-9 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-20:atom-10 buttons:next:train:state-20
  (quote (buttons:next buttons:next:train:state-20 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-1 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-2 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-3 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-4 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-5 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-6 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-7 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-8 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-9 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-21:atom-10 buttons:next:train:state-21
  (quote (buttons:next buttons:next:train:state-21 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-1 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-2 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-3 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-4 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-5 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-6 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-7 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-8 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-9 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-22:atom-10 buttons:next:train:state-22
  (quote (buttons:next buttons:next:train:state-22 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-1 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-2 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-3 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-4 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-5 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-6 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-7 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-8 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-9 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-23:atom-10 buttons:next:train:state-23
  (quote (buttons:next buttons:next:train:state-23 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-1 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-2 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-3 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-4 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-5 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-6 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-7 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-8 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-9 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-24:atom-10 buttons:next:train:state-24
  (quote (buttons:next buttons:next:train:state-24 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-1 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-2 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-3 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-4 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-5 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-6 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-7 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-8 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-9 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-25:atom-10 buttons:next:train:state-25
  (quote (buttons:next buttons:next:train:state-25 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-1 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-2 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-3 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-4 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-5 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-6 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-7 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-8 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-9 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-26:atom-10 buttons:next:train:state-26
  (quote (buttons:next buttons:next:train:state-26 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-1 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-2 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-3 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-4 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-5 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-6 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-7 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-8 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-9 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-27:atom-10 buttons:next:train:state-27
  (quote (buttons:next buttons:next:train:state-27 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-1 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-2 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-3 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-4 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-5 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-6 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-7 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-8 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-9 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-28:atom-10 buttons:next:train:state-28
  (quote (buttons:next buttons:next:train:state-28 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-1 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-2 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-3 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-4 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-5 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-6 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-7 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-8 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-9 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-29:atom-10 buttons:next:train:state-29
  (quote (buttons:next buttons:next:train:state-29 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-1 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-2 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-3 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-4 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-5 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-6 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-7 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-8 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-9 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-30:atom-10 buttons:next:train:state-30
  (quote (buttons:next buttons:next:train:state-30 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-1 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-2 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-3 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-4 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-5 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-6 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-7 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-8 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-9 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-31:atom-10 buttons:next:train:state-31
  (quote (buttons:next buttons:next:train:state-31 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-1 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-2 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-3 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-4 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-5 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-6 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-7 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-8 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-9 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-32:atom-10 buttons:next:train:state-32
  (quote (buttons:next buttons:next:train:state-32 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-1 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-2 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-3 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-4 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-5 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-6 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-7 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-8 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-9 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-33:atom-10 buttons:next:train:state-33
  (quote (buttons:next buttons:next:train:state-33 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-1 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-2 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-3 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-4 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-5 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-6 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-7 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-8 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-9 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-34:atom-10 buttons:next:train:state-34
  (quote (buttons:next buttons:next:train:state-34 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-1 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-2 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-3 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-4 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-5 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-6 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-7 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-8 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-9 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-35:atom-10 buttons:next:train:state-35
  (quote (buttons:next buttons:next:train:state-35 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-1 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-2 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-3 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-4 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-5 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-6 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-7 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-8 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-9 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-36:atom-10 buttons:next:train:state-36
  (quote (buttons:next buttons:next:train:state-36 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-1 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-2 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-3 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-4 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-5 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-6 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-7 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-8 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-9 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-37:atom-10 buttons:next:train:state-37
  (quote (buttons:next buttons:next:train:state-37 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-1 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-2 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-3 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-4 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-5 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-6 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-7 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-8 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-9 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-38:atom-10 buttons:next:train:state-38
  (quote (buttons:next buttons:next:train:state-38 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-1 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-2 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-3 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-4 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-5 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-6 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-7 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-8 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-9 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-39:atom-10 buttons:next:train:state-39
  (quote (buttons:next buttons:next:train:state-39 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-1 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-2 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-3 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-4 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-5 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-6 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-7 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-8 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-9 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-40:atom-10 buttons:next:train:state-40
  (quote (buttons:next buttons:next:train:state-40 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-1 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-2 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-3 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-4 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-5 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-6 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-7 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-8 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-9 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-41:atom-10 buttons:next:train:state-41
  (quote (buttons:next buttons:next:train:state-41 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-1 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-2 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-3 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-4 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-5 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-6 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-7 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-8 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-9 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-42:atom-10 buttons:next:train:state-42
  (quote (buttons:next buttons:next:train:state-42 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-1 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-2 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-3 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-4 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-5 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-6 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-7 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-8 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-9 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-43:atom-10 buttons:next:train:state-43
  (quote (buttons:next buttons:next:train:state-43 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-1 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-2 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-3 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-4 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-5 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-6 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-7 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-8 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-9 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-44:atom-10 buttons:next:train:state-44
  (quote (buttons:next buttons:next:train:state-44 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-1 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-2 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-3 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-4 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-5 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-6 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-7 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-8 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-9 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-45:atom-10 buttons:next:train:state-45
  (quote (buttons:next buttons:next:train:state-45 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-1 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-2 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-3 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-4 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-5 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-6 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-7 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-8 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-9 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-46:atom-10 buttons:next:train:state-46
  (quote (buttons:next buttons:next:train:state-46 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-1 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-2 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-3 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-4 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-5 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-6 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-7 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-8 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-9 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-47:atom-10 buttons:next:train:state-47
  (quote (buttons:next buttons:next:train:state-47 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-1 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-2 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-3 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-4 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-5 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-6 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-7 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-8 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-9 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-48:atom-10 buttons:next:train:state-48
  (quote (buttons:next buttons:next:train:state-48 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-1 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-2 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-3 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-4 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-5 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-6 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-7 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-8 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-9 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-49:atom-10 buttons:next:train:state-49
  (quote (buttons:next buttons:next:train:state-49 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-1 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-2 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-3 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-4 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-5 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-6 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-7 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-8 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-9 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-50:atom-10 buttons:next:train:state-50
  (quote (buttons:next buttons:next:train:state-50 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-1 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-2 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-3 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-4 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-5 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-6 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-7 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-8 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-9 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-51:atom-10 buttons:next:train:state-51
  (quote (buttons:next buttons:next:train:state-51 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-1 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-2 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-3 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-4 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-5 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-6 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-7 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-8 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-9 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-52:atom-10 buttons:next:train:state-52
  (quote (buttons:next buttons:next:train:state-52 buttons:r)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-1 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:n1)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-2 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:n2)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-3 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:n3)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-4 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:n4)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-5 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:n5)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-6 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:n6)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-7 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:n7)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-8 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:p)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-9 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:q)))
!(iggp:buttons:classify buttons:next:train:state-53:atom-10 buttons:next:train:state-53
  (quote (buttons:next buttons:next:train:state-53 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-1 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-2 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-3 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-4 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-5 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-6 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-7 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-8 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-9 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-1:atom-10 buttons:next:validate:state-1
  (quote (buttons:next buttons:next:validate:state-1 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-1 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-2 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-3 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-4 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-5 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-6 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-7 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-8 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-9 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-2:atom-10 buttons:next:validate:state-2
  (quote (buttons:next buttons:next:validate:state-2 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-1 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-2 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-3 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-4 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-5 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-6 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-7 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-8 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-9 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-3:atom-10 buttons:next:validate:state-3
  (quote (buttons:next buttons:next:validate:state-3 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-1 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-2 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-3 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-4 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-5 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-6 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-7 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-8 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-9 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-4:atom-10 buttons:next:validate:state-4
  (quote (buttons:next buttons:next:validate:state-4 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-1 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-2 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-3 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-4 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-5 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-6 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-7 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-8 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-9 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-5:atom-10 buttons:next:validate:state-5
  (quote (buttons:next buttons:next:validate:state-5 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-1 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-2 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-3 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-4 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-5 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-6 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-7 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-8 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-9 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-6:atom-10 buttons:next:validate:state-6
  (quote (buttons:next buttons:next:validate:state-6 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-1 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-2 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-3 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-4 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-5 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-6 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-7 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-8 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-9 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-7:atom-10 buttons:next:validate:state-7
  (quote (buttons:next buttons:next:validate:state-7 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-1 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-2 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-3 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-4 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-5 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-6 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-7 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-8 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-9 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-8:atom-10 buttons:next:validate:state-8
  (quote (buttons:next buttons:next:validate:state-8 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-1 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-2 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-3 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-4 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-5 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-6 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-7 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-8 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-9 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-9:atom-10 buttons:next:validate:state-9
  (quote (buttons:next buttons:next:validate:state-9 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-1 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-2 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-3 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-4 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-5 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-6 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-7 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-8 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-9 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-10:atom-10 buttons:next:validate:state-10
  (quote (buttons:next buttons:next:validate:state-10 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-1 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-2 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-3 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-4 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-5 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-6 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-7 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-8 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-9 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-11:atom-10 buttons:next:validate:state-11
  (quote (buttons:next buttons:next:validate:state-11 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-1 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-2 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-3 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-4 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-5 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-6 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-7 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-8 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-9 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-12:atom-10 buttons:next:validate:state-12
  (quote (buttons:next buttons:next:validate:state-12 buttons:r)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-1 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:n1)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-2 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:n2)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-3 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:n3)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-4 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:n4)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-5 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:n5)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-6 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:n6)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-7 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:n7)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-8 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:p)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-9 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:q)))
!(iggp:buttons:classify buttons:next:validate:state-13:atom-10 buttons:next:validate:state-13
  (quote (buttons:next buttons:next:validate:state-13 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-1 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-2 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-3 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-4 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-5 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-6 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-7 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-8 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-9 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-1:atom-10 buttons:next:test:state-1
  (quote (buttons:next buttons:next:test:state-1 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-1 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-2 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-3 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-4 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-5 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-6 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-7 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-8 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-9 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-2:atom-10 buttons:next:test:state-2
  (quote (buttons:next buttons:next:test:state-2 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-1 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-2 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-3 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-4 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-5 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-6 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-7 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-8 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-9 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-3:atom-10 buttons:next:test:state-3
  (quote (buttons:next buttons:next:test:state-3 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-1 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-2 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-3 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-4 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-5 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-6 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-7 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-8 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-9 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-4:atom-10 buttons:next:test:state-4
  (quote (buttons:next buttons:next:test:state-4 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-1 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-2 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-3 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-4 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-5 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-6 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-7 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-8 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-9 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-5:atom-10 buttons:next:test:state-5
  (quote (buttons:next buttons:next:test:state-5 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-1 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-2 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-3 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-4 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-5 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-6 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-7 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-8 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-9 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-6:atom-10 buttons:next:test:state-6
  (quote (buttons:next buttons:next:test:state-6 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-1 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-2 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-3 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-4 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-5 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-6 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-7 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-8 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-9 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-7:atom-10 buttons:next:test:state-7
  (quote (buttons:next buttons:next:test:state-7 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-1 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-2 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-3 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-4 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-5 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-6 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-7 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-8 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-9 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-8:atom-10 buttons:next:test:state-8
  (quote (buttons:next buttons:next:test:state-8 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-1 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-2 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-3 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-4 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-5 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-6 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-7 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-8 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-9 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-9:atom-10 buttons:next:test:state-9
  (quote (buttons:next buttons:next:test:state-9 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-1 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-2 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-3 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-4 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-5 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-6 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-7 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-8 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-9 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-10:atom-10 buttons:next:test:state-10
  (quote (buttons:next buttons:next:test:state-10 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-1 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-2 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-3 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-4 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-5 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-6 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-7 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-8 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-9 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-11:atom-10 buttons:next:test:state-11
  (quote (buttons:next buttons:next:test:state-11 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-1 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-2 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-3 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-4 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-5 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-6 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-7 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-8 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-9 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-12:atom-10 buttons:next:test:state-12
  (quote (buttons:next buttons:next:test:state-12 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-1 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-2 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-3 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-4 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-5 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-6 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-7 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-8 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-9 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-13:atom-10 buttons:next:test:state-13
  (quote (buttons:next buttons:next:test:state-13 buttons:r)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-1 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:n1)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-2 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:n2)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-3 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:n3)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-4 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:n4)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-5 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:n5)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-6 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:n6)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-7 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:n7)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-8 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:p)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-9 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:q)))
!(iggp:buttons:classify buttons:next:test:state-14:atom-10 buttons:next:test:state-14
  (quote (buttons:next buttons:next:test:state-14 buttons:r)))
!(iggp:buttons:classify buttons:terminal:train:state-1:atom-1 buttons:terminal:train:state-1
  (quote (buttons:terminal buttons:terminal:train:state-1)))
!(iggp:buttons:classify buttons:terminal:train:state-2:atom-1 buttons:terminal:train:state-2
  (quote (buttons:terminal buttons:terminal:train:state-2)))
!(iggp:buttons:classify buttons:terminal:train:state-3:atom-1 buttons:terminal:train:state-3
  (quote (buttons:terminal buttons:terminal:train:state-3)))
!(iggp:buttons:classify buttons:terminal:train:state-4:atom-1 buttons:terminal:train:state-4
  (quote (buttons:terminal buttons:terminal:train:state-4)))
!(iggp:buttons:classify buttons:terminal:train:state-5:atom-1 buttons:terminal:train:state-5
  (quote (buttons:terminal buttons:terminal:train:state-5)))
!(iggp:buttons:classify buttons:terminal:train:state-6:atom-1 buttons:terminal:train:state-6
  (quote (buttons:terminal buttons:terminal:train:state-6)))
!(iggp:buttons:classify buttons:terminal:train:state-7:atom-1 buttons:terminal:train:state-7
  (quote (buttons:terminal buttons:terminal:train:state-7)))
!(iggp:buttons:classify buttons:terminal:train:state-8:atom-1 buttons:terminal:train:state-8
  (quote (buttons:terminal buttons:terminal:train:state-8)))
!(iggp:buttons:classify buttons:terminal:train:state-9:atom-1 buttons:terminal:train:state-9
  (quote (buttons:terminal buttons:terminal:train:state-9)))
!(iggp:buttons:classify buttons:terminal:train:state-10:atom-1 buttons:terminal:train:state-10
  (quote (buttons:terminal buttons:terminal:train:state-10)))
!(iggp:buttons:classify buttons:terminal:train:state-11:atom-1 buttons:terminal:train:state-11
  (quote (buttons:terminal buttons:terminal:train:state-11)))
!(iggp:buttons:classify buttons:terminal:train:state-12:atom-1 buttons:terminal:train:state-12
  (quote (buttons:terminal buttons:terminal:train:state-12)))
!(iggp:buttons:classify buttons:terminal:train:state-13:atom-1 buttons:terminal:train:state-13
  (quote (buttons:terminal buttons:terminal:train:state-13)))
!(iggp:buttons:classify buttons:terminal:train:state-14:atom-1 buttons:terminal:train:state-14
  (quote (buttons:terminal buttons:terminal:train:state-14)))
!(iggp:buttons:classify buttons:terminal:train:state-15:atom-1 buttons:terminal:train:state-15
  (quote (buttons:terminal buttons:terminal:train:state-15)))
!(iggp:buttons:classify buttons:terminal:train:state-16:atom-1 buttons:terminal:train:state-16
  (quote (buttons:terminal buttons:terminal:train:state-16)))
!(iggp:buttons:classify buttons:terminal:train:state-17:atom-1 buttons:terminal:train:state-17
  (quote (buttons:terminal buttons:terminal:train:state-17)))
!(iggp:buttons:classify buttons:terminal:train:state-18:atom-1 buttons:terminal:train:state-18
  (quote (buttons:terminal buttons:terminal:train:state-18)))
!(iggp:buttons:classify buttons:terminal:train:state-19:atom-1 buttons:terminal:train:state-19
  (quote (buttons:terminal buttons:terminal:train:state-19)))
!(iggp:buttons:classify buttons:terminal:train:state-20:atom-1 buttons:terminal:train:state-20
  (quote (buttons:terminal buttons:terminal:train:state-20)))
!(iggp:buttons:classify buttons:terminal:train:state-21:atom-1 buttons:terminal:train:state-21
  (quote (buttons:terminal buttons:terminal:train:state-21)))
!(iggp:buttons:classify buttons:terminal:validate:state-1:atom-1 buttons:terminal:validate:state-1
  (quote (buttons:terminal buttons:terminal:validate:state-1)))
!(iggp:buttons:classify buttons:terminal:validate:state-2:atom-1 buttons:terminal:validate:state-2
  (quote (buttons:terminal buttons:terminal:validate:state-2)))
!(iggp:buttons:classify buttons:terminal:validate:state-3:atom-1 buttons:terminal:validate:state-3
  (quote (buttons:terminal buttons:terminal:validate:state-3)))
!(iggp:buttons:classify buttons:terminal:validate:state-4:atom-1 buttons:terminal:validate:state-4
  (quote (buttons:terminal buttons:terminal:validate:state-4)))
!(iggp:buttons:classify buttons:terminal:validate:state-5:atom-1 buttons:terminal:validate:state-5
  (quote (buttons:terminal buttons:terminal:validate:state-5)))
!(iggp:buttons:classify buttons:terminal:test:state-1:atom-1 buttons:terminal:test:state-1
  (quote (buttons:terminal buttons:terminal:test:state-1)))
!(iggp:buttons:classify buttons:terminal:test:state-2:atom-1 buttons:terminal:test:state-2
  (quote (buttons:terminal buttons:terminal:test:state-2)))
!(iggp:buttons:classify buttons:terminal:test:state-3:atom-1 buttons:terminal:test:state-3
  (quote (buttons:terminal buttons:terminal:test:state-3)))
!(iggp:buttons:classify buttons:terminal:test:state-4:atom-1 buttons:terminal:test:state-4
  (quote (buttons:terminal buttons:terminal:test:state-4)))
!(iggp:buttons:classify buttons:terminal:test:state-5:atom-1 buttons:terminal:test:state-5
  (quote (buttons:terminal buttons:terminal:test:state-5)))
!(iggp:buttons:classify buttons:terminal:test:state-6:atom-1 buttons:terminal:test:state-6
  (quote (buttons:terminal buttons:terminal:test:state-6)))

!(iggp:buttons:proof-shapes
  buttons:goal:train:state-1
  (quote (buttons:goal buttons:goal:train:state-1 buttons:robot buttons:n0)))

!(iggp:buttons:proof-shapes
  buttons:terminal:train:state-4
  (quote (buttons:terminal buttons:terminal:train:state-4)))

!(iggp:buttons:proof-shapes
  buttons:next:train:state-3
  (quote (buttons:next buttons:next:train:state-3 buttons:p)))
