; The ATP-guided lane changes only search order.  Both lanes must return a
; proof term accepted by the same Prime typing checker.
(: ph Type)
(: ps Type)
(: ch Type)
(: impl (-> Type Type Type))
(: h1 ph)
(: h2 ps)
(: h3 (impl ph (impl ps ch)))
(: ax-mp (-> $a (impl $a $b) $b))
(chaining-rule ax-mp)

!(search-first-inhabitant
   &self ch 3 100000
   (search-policy atp-guided-inhabitation))
!(search-first-inhabitant &self ch 3 100000)

; A named but unimplemented lane cannot silently fall back to another search.
!(search-first-inhabitant
   &self ch 3 100000
   (search-policy atp-saturation))

; Exact rigid-fact support is a search-order observation, not authority.  The
; two-premise rule is chosen ahead of a cheaper one-premise alternative because
; both of its premises join the current facts under one substitution; the
; returned term is still checked by the ordinary Prime boundary.
!(assertEqual
  (let $kb (new-space)
    (let $i0 (add-atom $kb (: P Type))
    (let $i1 (add-atom $kb (: Q Type))
    (let $i2 (add-atom $kb (: L Type))
    (let $i3 (add-atom $kb (: Goal Type))
    (let $i4 (add-atom $kb (: hp P))
    (let $i5 (add-atom $kb (: hq Q))
    (let $i6 (add-atom $kb (: hl L))
    (let $i7 (add-atom $kb (: bad (-> L Goal)))
    (let $i8 (add-atom $kb (chaining-rule bad))
    (let $i9 (add-atom $kb (: good (-> P Q Goal)))
    (let $i10 (add-atom $kb (chaining-rule good))
      (case
        (search-first-inhabitant
          $kb Goal 2 10000
          (search-policy atp-guided-inhabitation))
        (((he-accept
            (typed-answer-v2 (: (good hp hq) Goal) $substitution))
          True)
         ($_ False)))))))))))))))
  True)

; Positive pruning case: the only rule asks for the alpha-variant ancestor
; goal, so bounded guided search rejects the loop instead of exhausting fuel.
!(bind! &guided-loop-kb (new-space))
!(add-atom &guided-loop-kb (: Loop Type))
!(add-atom &guided-loop-kb (: self (-> Loop Loop)))
!(add-atom &guided-loop-kb (chaining-rule self))
!(assertEqual
  (search-first-inhabitant
    &guided-loop-kb Loop 100 200
    (search-policy atp-guided-inhabitation))
  (he-reject (no-inhabitant-at-depth)))

; Negative pruning case: a different child goal remains searchable and its
; ordinary checked proof is returned.
!(bind! &guided-step-kb (new-space))
!(add-atom &guided-step-kb (: Base Type))
!(add-atom &guided-step-kb (: Goal2 Type))
!(add-atom &guided-step-kb (: base Base))
!(add-atom &guided-step-kb (: step (-> Base Goal2)))
!(add-atom &guided-step-kb (chaining-rule step))
!(assertEqual
  (case
    (search-first-inhabitant
      &guided-step-kb Goal2 2 1000
      (search-policy atp-guided-inhabitation))
    (((he-accept
        (typed-answer-v2 (: (step base) Goal2) $substitution))
      True)
     ($_ False)))
  True)
