; Faithful ground-truth qualification for Popper's synthesis-reverse task.
;
; Source: logic-and-learning-lab/Popper revision
; 5c608d3ec87e4975866042fe326e9a317277e021.
; Source file digests (SHA-256):
;   bias.pl 2ddb25e2862d050cf813d9bf639569b1f8c79f576f8bea5bfc620fbc6c926dd5
;   bk.pl   159f4ed83c43ee83bba5816691f16dbaec34f2a259e1795834285a25f73edaaf
;   exs.pl  483f98f0d7bc4841f198d4c94ec98df3e14babea2070b4f6f76f3b02ea91742c
;
; The two target clauses and all background relations are represented over
; native list constructors.  This fixture checks the authored program against
; all four positive and five negative examples.  It does not claim that CeTTa
; learned the program yet.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(import! &self ../../lib/ilp/popper_native_list_relations.metta)
!(import! &self ../../lib/ilp/popper_synthesis_reverse_types.metta)
!(import! &self ../../lib/ilp/popper_synthesis_reverse_rules.metta)

(= (popper:reverse:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences) $metrics $revision)
    (compile:run
      (popper:reverse:package) 64 1000000 100 (quote $goal))
    (popper:case $name not-derived 0)))

(= (popper:reverse:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences (occurrence $proof-data)) $metrics $revision)
    (compile:run
      (popper:reverse:package) 64 1000000 100 (quote $goal))
    (popper:case $name derived 1
      (type:check (unquote $proof-data) $goal))))

!(popper:reverse:classify pos-1
  (quote
    (reverse:target
      (list:cons reverse:element reverse:n1
        (list:cons reverse:element reverse:n2
          (list:cons reverse:element reverse:n3
            (list:cons reverse:element reverse:n4
              (list:nil reverse:element)))))
      (list:cons reverse:element reverse:n4
        (list:cons reverse:element reverse:n3
          (list:cons reverse:element reverse:n2
            (list:cons reverse:element reverse:n1
              (list:nil reverse:element))))))))
!(popper:reverse:classify pos-2
  (quote
    (reverse:target
      (list:cons reverse:element reverse:n1
        (list:cons reverse:element reverse:n2
          (list:cons reverse:element reverse:n3
            (list:cons reverse:element reverse:n4
              (list:cons reverse:element reverse:n1
                (list:nil reverse:element))))))
      (list:cons reverse:element reverse:n1
        (list:cons reverse:element reverse:n4
          (list:cons reverse:element reverse:n3
            (list:cons reverse:element reverse:n2
              (list:cons reverse:element reverse:n1
                (list:nil reverse:element)))))))))
!(popper:reverse:classify pos-3
  (quote
    (reverse:target
      (list:cons reverse:element reverse:a
        (list:cons reverse:element reverse:b
          (list:cons reverse:element reverse:c
            (list:nil reverse:element))))
      (list:cons reverse:element reverse:c
        (list:cons reverse:element reverse:b
          (list:cons reverse:element reverse:a
            (list:nil reverse:element)))))))
!(popper:reverse:classify pos-4
  (quote
    (reverse:target
      (list:cons reverse:element reverse:a
        (list:cons reverse:element reverse:b
          (list:cons reverse:element reverse:c
            (list:cons reverse:element reverse:d
              (list:cons reverse:element reverse:e
                (list:cons reverse:element reverse:f
                  (list:nil reverse:element)))))))
      (list:cons reverse:element reverse:f
        (list:cons reverse:element reverse:e
          (list:cons reverse:element reverse:d
            (list:cons reverse:element reverse:c
              (list:cons reverse:element reverse:b
                (list:cons reverse:element reverse:a
                  (list:nil reverse:element))))))))))

!(popper:reverse:classify neg-1
  (quote
    (reverse:target
      (list:cons reverse:element reverse:a
        (list:cons reverse:element reverse:b
          (list:cons reverse:element reverse:c
            (list:nil reverse:element))))
      (list:cons reverse:element reverse:a
        (list:cons reverse:element reverse:b
          (list:cons reverse:element reverse:c
            (list:nil reverse:element)))))))
!(popper:reverse:classify neg-2
  (quote
    (reverse:target
      (list:cons reverse:element reverse:a
        (list:cons reverse:element reverse:b
          (list:cons reverse:element reverse:c
            (list:nil reverse:element))))
      (list:nil reverse:element))))
!(popper:reverse:classify neg-3
  (quote
    (reverse:target
      (list:cons reverse:element reverse:a
        (list:cons reverse:element reverse:b
          (list:nil reverse:element)))
      (list:cons reverse:element reverse:a
        (list:nil reverse:element)))))
!(popper:reverse:classify neg-4
  (quote
    (reverse:target
      (list:cons reverse:element reverse:a
        (list:cons reverse:element reverse:b
          (list:cons reverse:element reverse:c
            (list:nil reverse:element))))
      (list:cons reverse:element reverse:b
        (list:cons reverse:element reverse:c
          (list:nil reverse:element))))))
!(popper:reverse:classify neg-5
  (quote
    (reverse:target
      (list:cons reverse:element reverse:n1
        (list:cons reverse:element reverse:n2
          (list:cons reverse:element reverse:n3
            (list:cons reverse:element reverse:n4
              (list:nil reverse:element)))))
      (list:cons reverse:element reverse:n1
        (list:cons reverse:element reverse:n4
          (list:cons reverse:element reverse:n3
            (list:cons reverse:element reverse:n2
              (list:nil reverse:element))))))))
