; Faithful ground-truth qualification for Popper's synthesis-length task.
;
; Source: logic-and-learning-lab/Popper at
; 5c608d3ec87e4975866042fe326e9a317277e021.
; Source file digests (SHA-256):
;   bias.pl 47b140c5a05128921ba1d6527b44ac09ecb2f9a82ad9ef0e7a3a0469448a2c94
;   bk.pl   ccd924be3546104bd81abd974737ea3df3e56e409ffd2efe3fdf8dec27c8df97
;   exs.pl  a7fb2b1a45daa3d912095fd19105f184e517dc78e4fe1a8b07c806dcddf27245
;
; The two target clauses are the task's authored best program.  They remain
; ordinary proof-relevant rules: the generic rule machine constructs finite
; derivation occurrences, including the recursive premise.  This fixture
; qualifies the known program against all six positive and three negative
; examples; it does not claim that CeTTa learned the program yet.
;
; Runtime lists are written in the native List constructor vocabulary.  This
; avoids baking the finite examples into background tail facts: the one tail
; rule works for every constructor-shaped list.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(import! &self ../../lib/ilp/popper_synthesis_length_types.metta)
!(import! &self ../../lib/ilp/popper_synthesis_length_rules.metta)

; A complete run with exactly zero or one proof occurrence is the task oracle.
; `not-derived` is deliberately not a Prime type-theoretic Refuted verdict.
; Any resource-incomplete run or unexpected multiplicity remains visibly
; unevaluated and therefore fails the golden gate.
(= (popper:length:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences) $metrics $revision)
    (compile:run
      (popper:length:package) 24 1000000 100 (quote $goal))
    (popper:case $name not-derived 0)))

(= (popper:length:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences (occurrence $proof-data)) $metrics $revision)
    (compile:run
      (popper:length:package) 24 1000000 100 (quote $goal))
    (popper:case $name derived 1
      (type:check (unquote $proof-data) $goal))))

!(popper:length:classify pos-1
  (quote
    (target:length
      (list:cons element e1 (list:nil element)) n1)))
!(popper:length:classify pos-2
  (quote
    (target:length
      (list:cons element e2
        (list:cons element e2 (list:nil element)))
      n2)))
!(popper:length:classify pos-3
  (quote
    (target:length
      (list:cons element e3
        (list:cons element e3
          (list:cons element e1 (list:nil element))))
      n3)))
!(popper:length:classify pos-4
  (quote
    (target:length
      (list:cons element e4 (list:nil element)) n1)))
!(popper:length:classify pos-5
  (quote
    (target:length
      (list:cons element e4
        (list:cons element e3 (list:nil element)))
      n2)))
!(popper:length:classify pos-6
  (quote
    (target:length
      (list:cons element e4
        (list:cons element e3
          (list:cons element e2
            (list:cons element e2
              (list:cons element e3
                (list:cons element e5
                  (list:cons element e2
                    (list:cons element e7 (list:nil element)))))))))
      n8)))

!(popper:length:classify neg-1
  (quote
    (target:length
      (list:cons element e3
        (list:cons element e3
          (list:cons element e1 (list:nil element))))
      n2)))
!(popper:length:classify neg-2
  (quote
    (target:length
      (list:cons element e4 (list:nil element)) n0)))
!(popper:length:classify neg-3
  (quote
    (target:length
      (list:cons element e4 (list:nil element)) n2)))

; One complete proof tree pins recursive premise order and provenance rather
; than checking endpoint truth alone.
!(compile:run (popper:length:package) 24 1000000 100
  (quote
    (target:length
      (list:cons element e3
        (list:cons element e3
          (list:cons element e1 (list:nil element))))
      n3)))

; The same three-step derivation, elaborated from the occurrence tree into
; ordinary Prime constructor applications, inhabits its exact indexed goal.
; The generic C ingress independently checks the dynamically produced
; occurrence.  This literal witness keeps the surface qualification readable.
!(type:check
  (length-proof:step
    (list:cons element e3
      (list:cons element e3
        (list:cons element e1 (list:nil element))))
    (list:cons element e3
      (list:cons element e1 (list:nil element)))
    n2 n3
    (length-proof:tail e3
      (list:cons element e3
        (list:cons element e1 (list:nil element))))
    (length-proof:step
      (list:cons element e3
        (list:cons element e1 (list:nil element)))
      (list:cons element e1 (list:nil element))
      n1 n2
      (length-proof:tail e3
        (list:cons element e1 (list:nil element)))
      (length-proof:step
        (list:cons element e1 (list:nil element))
        (list:nil element)
        n0 n1
        (length-proof:tail e1 (list:nil element))
        (length-proof:base
          (list:nil element) n0
          length-proof:zero length-proof:empty)
        length-proof:succ-0-1)
      length-proof:succ-1-2)
    length-proof:succ-2-3)
  (target:length
    (list:cons element e3
      (list:cons element e3
        (list:cons element e1 (list:nil element))))
    n3))
