!(import! &self lib_atp)

!(bind! &indexed-kb (new-space))
!(bind! &rule-index (new-space))

!(add-atom &indexed-kb (: ph Type))
!(add-atom &indexed-kb (: ps Type))
!(add-atom &indexed-kb (: ch Type))
!(add-atom &indexed-kb (: impl (-> Type Type Type)))
!(add-atom &indexed-kb (: h1 ph))
!(add-atom &indexed-kb (: h2 ps))
!(add-atom &indexed-kb (: h3 (impl ph (impl ps ch))))
!(add-atom &rule-index (ATP.Index.Hypothesis ph 0 h1))
!(add-atom &rule-index (ATP.Index.Hypothesis ps 1 h2))
!(add-atom &rule-index (ATP.Index.Hypothesis impl 2 h3))
!(add-atom &indexed-kb (: ax-mp (-> $a (impl $a $b) $b)))
!(add-atom &indexed-kb (chaining-rule ax-mp))
!(add-atom &rule-index (ATP.Index.Rule ATP.AnyHead 3 ax-mp))

!(add-atom &indexed-kb (: A Type))
!(add-atom &indexed-kb (: B Type))
!(add-atom &indexed-kb (: C Type))
!(add-atom &indexed-kb (: D Type))
!(add-atom &indexed-kb (: pa A))
!(add-atom &indexed-kb (: pb B))
!(add-atom &indexed-kb (: pc C))
!(add-atom &rule-index (ATP.Index.Hypothesis A 4 pa))
!(add-atom &rule-index (ATP.Index.Hypothesis B 5 pb))
!(add-atom &rule-index (ATP.Index.Hypothesis C 6 pc))
!(add-atom &indexed-kb (: combine3 (-> A B C D)))
!(add-atom &indexed-kb (chaining-rule combine3))
!(add-atom &rule-index (ATP.Index.Rule D 7 combine3))

!(bind! &ranker (ATP.LinearPolicy 4096 2048 512 256 8 1 7))
!(bind! &policy
  (ATP.DirectSearch
    (ATP.CoarseToFine 4 4 4 &ranker &ranker)
    ATP.RightToLeft))

; The score is an inspectable dot product over the declared feature basis.
!(assertEqual
   (reify
     (let $candidate
          (atp:indexed:support-candidate
            &indexed-kb &rule-index 4 ATP.RightToLeft
            (atp:indexed:rule-candidate
              &indexed-kb &rule-index ch))
       (atp:policy:cost &ranker $candidate)))
   (6708))

; A head-specific rule ranks ahead of the generic modus-ponens scheme.  This
; avoids enumerating an unconstrained first premise when an exact conclusion
; shape is already available.
!(assertEqual
   (let (ATP.Candidate
          $_age $_generality $_misses $_direct-support
          $rule $_premises $_goal)
        (select
          (atp:indexed:ranked-candidate
            &indexed-kb &rule-index D &policy))
     $rule)
   combine3)

; Search reads only rule labels from the optional index, reconstructs each
; application from the typing space, and checks the resulting proof.
!(assertEqual
   (atp:indexed:first-checked
     &indexed-kb &rule-index ch 2 100000 &policy)
   (ATP.Checked (ax-mp h2 (ax-mp h1 h3)) ch (exact ch)))

!(assertEqual
   (atp:indexed:first-checked
     &indexed-kb &rule-index D 1 100000 &policy)
   (ATP.Checked (combine3 pa pb pc) D (exact D)))

; A stale hint naming no marked source rule cannot fabricate a candidate.
!(bind! &stale-index (new-space))
!(add-atom &stale-index (ATP.Index.Rule ATP.AnyHead 999 forged-rule))
!(add-atom &stale-index (ATP.Index.Fact ch 999 forged-fact))
!(add-atom &stale-index (ATP.Index.Hypothesis ch 999 forged-hypothesis))
!(assertEqual
   (reify
     (atp:indexed:prove-checked
       &indexed-kb &stale-index ch 2 100000 &policy))
   ())

; Ancestor regularity rejects a cyclic rule instead of repeatedly scheduling
; the same alpha-equivalent goal.
!(bind! &loop-kb (new-space))
!(bind! &loop-index (new-space))
!(add-atom &loop-kb (: Loop Type))
!(add-atom &loop-kb (: loop (-> Loop Loop)))
!(add-atom &loop-kb (chaining-rule loop))
!(add-atom &loop-index (ATP.Index.Rule Loop 0 loop))
!(bind! &loop-ranker (ATP.LinearPolicy 4096 2048 512 256 8 0 0))
!(bind! &loop-policy
  (ATP.DirectSearch
    (ATP.CoarseToFine 1 1 1 &loop-ranker &loop-ranker)
    ATP.RightToLeft))
!(assertEqual
   (reify
     (atp:indexed:prove-checked
       &loop-kb &loop-index Loop 8 100000 &loop-policy))
   ())

!(println! (ATPIndexedLibrarySummary 6 6 0))
