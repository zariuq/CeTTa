; Faithful ground-truth qualification for Popper's synthesis-filter task.
;
; Source: logic-and-learning-lab/Popper revision
; 5c608d3ec87e4975866042fe326e9a317277e021.
; Source file digests (SHA-256):
;   bias.pl 5dded3ecce672170067181d8c6d5c648a3360b5ccefd9925a5e7f2e8dd48a750
;   bk.pl   5b5ca157afc6b0accf7f3e451e39b1f4c7d799304191846b8ec8d07f0de99d5b
;   exs.pl  a62b958fdb6a625afbb745aea9b8185efe03fd1c2bad4eceb62eb66b7f28c0ed
;
; This is the complete authored background and three-clause reference
; program, not a finite-example approximation.  Every produced occurrence is
; checked at its exact indexed target; absence of a proof is not Refuted.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(import! &self ../../lib/ilp/popper_native_list_relations.metta)
!(import! &self ../../lib/ilp/popper_synthesis_filter_types.metta)
!(import! &self ../../lib/ilp/popper_synthesis_filter_rules.metta)

(= (popper:filter:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences) $metrics $revision)
    (compile:run
      (popper:filter:package) 64 1000000 100 (quote $goal))
    (popper:case $name not-derived 0)))

(= (popper:filter:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences (occurrence $proof-data)) $metrics $revision)
    (compile:run
      (popper:filter:package) 64 1000000 100 (quote $goal))
    (popper:case $name derived 1
      (type:check (unquote $proof-data) $goal))))

!(popper:filter:classify pos-1
  (quote
    (filter:target
      (list:cons filter:element filter:n1
        (list:cons filter:element filter:n2
          (list:cons filter:element filter:n3
            (list:cons filter:element filter:n4
              (list:nil filter:element)))))
      (list:cons filter:element filter:n2
        (list:cons filter:element filter:n4
          (list:nil filter:element))))))
!(popper:filter:classify pos-2
  (quote
    (filter:target
      (list:cons filter:element filter:n1
        (list:cons filter:element filter:n1
          (list:cons filter:element filter:n1
            (list:nil filter:element))))
      (list:nil filter:element))))
!(popper:filter:classify pos-3
  (quote
    (filter:target
      (list:cons filter:element filter:n3
        (list:cons filter:element filter:n2
          (list:cons filter:element filter:n1
            (list:nil filter:element))))
      (list:cons filter:element filter:n2
        (list:nil filter:element)))))
!(popper:filter:classify pos-4
  (quote
    (filter:target
      (list:cons filter:element filter:n2
        (list:nil filter:element))
      (list:cons filter:element filter:n2
        (list:nil filter:element)))))
!(popper:filter:classify pos-5
  (quote
    (filter:target
      (list:cons filter:element filter:n10
        (list:cons filter:element filter:n1
          (list:cons filter:element filter:n4
            (list:nil filter:element))))
      (list:cons filter:element filter:n10
        (list:cons filter:element filter:n4
          (list:nil filter:element))))))

!(popper:filter:classify neg-1
  (quote
    (filter:target
      (list:cons filter:element filter:n1
        (list:cons filter:element filter:n2
          (list:cons filter:element filter:n3
            (list:nil filter:element))))
      (list:cons filter:element filter:n2
        (list:cons filter:element filter:n3
          (list:cons filter:element filter:n7
            (list:nil filter:element)))))))
!(popper:filter:classify neg-2
  (quote
    (filter:target
      (list:cons filter:element filter:n1
        (list:cons filter:element filter:n1
          (list:cons filter:element filter:n1
            (list:nil filter:element))))
      (list:cons filter:element filter:n1
        (list:cons filter:element filter:n1
          (list:cons filter:element filter:n1
            (list:nil filter:element)))))))
!(popper:filter:classify neg-3
  (quote
    (filter:target
      (list:cons filter:element filter:n4
        (list:cons filter:element filter:n4
          (list:nil filter:element)))
      (list:cons filter:element filter:n4
        (list:nil filter:element)))))
!(popper:filter:classify neg-4
  (quote
    (filter:target
      (list:cons filter:element filter:n10
        (list:nil filter:element))
      (list:nil filter:element))))
!(popper:filter:classify neg-5
  (quote
    (filter:target
      (list:cons filter:element filter:n10
        (list:cons filter:element filter:n1
          (list:cons filter:element filter:n4
            (list:nil filter:element))))
      (list:cons filter:element filter:n9
        (list:cons filter:element filter:n5
          (list:nil filter:element))))))
