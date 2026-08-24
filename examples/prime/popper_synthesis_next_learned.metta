; Qualification of the size-8 program learned by pinned Popper for
; synthesis-next.  The source bias has no authored reference clauses, so this
; file says `learned` rather than `ground-truth`.
;
; Source: logic-and-learning-lab/Popper revision
; 5c608d3ec87e4975866042fe326e9a317277e021.
; Source file digests (SHA-256):
;   bias.pl db620e2f0072241463098f24438e85e860b3d20842c228788dd0ac88f42aa7bc
;   bk.pl   7ac8eb6bc75fec943a499a16785a3c689c4d7aef5606a769195746841dfd67e4
;   exs.pl  3db69b6936526865a02227c49014f94d49c4a3e649ebe15002234d4e5441c87f
;
; Popper reports Precision 1.00, Recall 1.00, TP 7, FN 0, TN 4, FP 0,
; and size 8.  This independent Prime execution rechecks every example and
; every complete proof occurrence at its exact dependent target.

!(import! &self ../../lib/ilp/prime_native_list_types.metta)
!(import! &self ../../lib/ilp/popper_native_list_relations.metta)
!(import! &self ../../lib/ilp/popper_synthesis_next_types.metta)
!(import! &self ../../lib/ilp/popper_synthesis_next_rules.metta)

(= (popper:next:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences) $metrics $revision)
    (compile:run
      (popper:next:package) 64 1000000 100 (quote $goal))
    (popper:case $name not-derived 0)))

(= (popper:next:classify $name (quote $goal))
  (let
    (compile-result proof-occurrence-bag
      (occurrences (occurrence $proof-data)) $metrics $revision)
    (compile:run
      (popper:next:package) 64 1000000 100 (quote $goal))
    (popper:case $name derived 1
      (type:check (unquote $proof-data) $goal))))

!(popper:next:classify pos-1
  (quote
    (next:target
      (list:cons next:element next:i
        (list:cons next:element next:f
          (list:cons next:element next:x
            (list:cons next:element next:c
              (list:cons next:element next:d
                (list:cons next:element next:c
                  (list:nil next:element)))))))
      next:c)))
!(popper:next:classify pos-2
  (quote
    (next:target
      (list:cons next:element next:g
        (list:cons next:element next:b
          (list:cons next:element next:c
            (list:cons next:element next:d
              (list:cons next:element next:x
                (list:cons next:element next:b
                  (list:cons next:element next:i
                    (list:cons next:element next:j
                      (list:nil next:element)))))))))
      next:b)))
!(popper:next:classify pos-3
  (quote
    (next:target
      (list:cons next:element next:b
        (list:cons next:element next:d
          (list:cons next:element next:h
            (list:cons next:element next:j
              (list:cons next:element next:x
                (list:cons next:element next:b
                  (list:nil next:element)))))))
      next:b)))
!(popper:next:classify pos-4
  (quote
    (next:target
      (list:cons next:element next:h
        (list:cons next:element next:e
          (list:cons next:element next:g
            (list:cons next:element next:h
              (list:cons next:element next:j
                (list:cons next:element next:c
                  (list:cons next:element next:x
                    (list:cons next:element next:g
                      (list:nil next:element)))))))))
      next:g)))
!(popper:next:classify pos-5
  (quote
    (next:target
      (list:cons next:element next:a
        (list:cons next:element next:f
          (list:cons next:element next:k
            (list:cons next:element next:d
              (list:cons next:element next:e
                (list:cons next:element next:x
                  (list:cons next:element next:d
                    (list:cons next:element next:a
                      (list:cons next:element next:b
                        (list:cons next:element next:c
                          (list:nil next:element)))))))))))
      next:d)))
!(popper:next:classify pos-6
  (quote
    (next:target
      (list:cons next:element next:a
        (list:cons next:element next:e
          (list:cons next:element next:d
            (list:cons next:element next:x
              (list:cons next:element next:f
                (list:cons next:element next:a
                  (list:nil next:element)))))))
      next:f)))
!(popper:next:classify pos-7
  (quote
    (next:target
      (list:cons next:element next:d
        (list:cons next:element next:e
          (list:cons next:element next:f
            (list:cons next:element next:x
              (list:cons next:element next:h
                (list:cons next:element next:c
                  (list:cons next:element next:a
                    (list:nil next:element))))))))
      next:h)))

!(popper:next:classify neg-1
  (quote
    (next:target
      (list:cons next:element next:c
        (list:cons next:element next:d
          (list:cons next:element next:i
            (list:cons next:element next:j
              (list:nil next:element)))))
      next:d)))
!(popper:next:classify neg-2
  (quote
    (next:target
      (list:cons next:element next:a
        (list:cons next:element next:j
          (list:cons next:element next:c
            (list:cons next:element next:e
              (list:cons next:element next:b
                (list:cons next:element next:c
                  (list:cons next:element next:d
                    (list:nil next:element))))))))
      next:b)))
!(popper:next:classify neg-3
  (quote
    (next:target
      (list:cons next:element next:c
        (list:cons next:element next:e
          (list:cons next:element next:i
            (list:cons next:element next:a
              (list:cons next:element next:b
                (list:cons next:element next:b
                  (list:cons next:element next:a
                    (list:nil next:element))))))))
      next:e)))
!(popper:next:classify neg-4
  (quote
    (next:target
      (list:cons next:element next:c
        (list:cons next:element next:b
          (list:cons next:element next:c
            (list:cons next:element next:d
              (list:cons next:element next:c
                (list:cons next:element next:d
                  (list:cons next:element next:c
                    (list:cons next:element next:f
                      (list:nil next:element)))))))))
      next:d)))
