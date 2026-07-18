!(import! &self lib_atp)

!(bind! &agenda-kb (new-space))
!(bind! &agenda-index (new-space))

!(add-atom &agenda-kb (: ph Type))
!(add-atom &agenda-kb (: ps Type))
!(add-atom &agenda-kb (: ch Type))
!(add-atom &agenda-kb (: impl (-> Type Type Type)))
!(add-atom &agenda-kb (: neg (-> Type Type)))
!(add-atom &agenda-kb (: h1 ph))
!(add-atom &agenda-kb (: h2 ps))
!(add-atom &agenda-kb (: h3 (impl ph (impl ps ch))))
!(add-atom &agenda-index (ATP.Index.Hypothesis ph 0 h1))
!(add-atom &agenda-index (ATP.Index.Hypothesis ps 1 h2))
!(add-atom &agenda-index (ATP.Index.Hypothesis impl 2 h3))
!(add-atom &agenda-kb (: ax-mp (-> $a (impl $a $b) $b)))
!(add-atom &agenda-kb (chaining-rule ax-mp))
!(add-atom &agenda-index (ATP.Index.Rule ATP.AnyHead 3 ax-mp))

!(bind! &agenda-policy
  (ATP.AgendaSearch
    (ATP.AgendaPolicy 16 64 4096 1 1 0 256 32)
    (ATP.DirectSearch
      (ATP.TopK 16 (ATP.LinearPolicy 4096 2048 512 256 8 0 8))
      ATP.RightToLeft)))

; Positive: a whole-state best-first agenda finds and checks mp2 without the
; recursive evaluator retaining every sibling branch.
!(assertEqual
  (atp:agenda:checked-proof
    (atp:agenda:first-checked
      &agenda-kb &agenda-index ch 2 1000 100000 &agenda-policy))
  (ATP.Checked (ax-mp h2 (ax-mp h1 h3)) ch (exact ch)))

; Negative: an absent theorem is an explicitly bounded incomplete result.
!(assertEqual
  (let (ATP.AgendaSearchResult
         (ATP.AgendaIncomplete $reason $coverage) $stats)
       (atp:agenda:first-checked
         &agenda-kb &agenda-index Missing 2 100 100000 &agenda-policy)
    (Incomplete $reason $coverage))
  (Incomplete NoInhabitantWithinBounds TruncatedAgenda))

; Negative: an alpha-variant ancestor loop cannot keep rescheduling itself.
!(bind! &agenda-loop-kb (new-space))
!(bind! &agenda-loop-index (new-space))
!(add-atom &agenda-loop-kb (: Loop Type))
!(add-atom &agenda-loop-kb (: loop (-> Loop Loop)))
!(add-atom &agenda-loop-kb (chaining-rule loop))
!(add-atom &agenda-loop-index (ATP.Index.Rule Loop 0 loop))
!(assertEqual
  (let (ATP.AgendaSearchResult
         (ATP.AgendaIncomplete $reason $coverage) $stats)
       (atp:agenda:first-checked
         &agenda-loop-kb &agenda-loop-index
         Loop 8 100 100000 &agenda-policy)
    (Incomplete $reason $coverage))
  (Incomplete NoInhabitantWithinBounds TruncatedAgenda))

; Positive: the ja proof is rediscovered from its two rules and hypotheses.
!(add-atom &agenda-kb (: jph Type))
!(add-atom &agenda-kb (: jps Type))
!(add-atom &agenda-kb (: jch Type))
!(add-atom &agenda-kb (: jh1 (impl (neg jph) jch)))
!(add-atom &agenda-kb (: jh2 (impl jps jch)))
!(add-atom &agenda-index (ATP.Index.Hypothesis impl 5 jh1))
!(add-atom &agenda-index (ATP.Index.Hypothesis impl 6 jh2))
!(add-atom &agenda-kb
  (: pm2.61d1
    (-> (impl $ph (impl $ps $ch))
        (impl (neg $ps) $ch)
        (impl $ph $ch))))
!(add-atom &agenda-kb (chaining-rule pm2.61d1))
!(add-atom &agenda-index (ATP.Index.Rule impl 7 pm2.61d1))
!(add-atom &agenda-kb
  (: imim2i
    (-> (impl $ph $ps)
        (impl (impl $ch $ph) (impl $ch $ps)))))
!(add-atom &agenda-kb (chaining-rule imim2i))
!(add-atom &agenda-index (ATP.Index.Rule impl 8 imim2i))

!(assertEqual
  (atp:agenda:checked-proof
    (atp:agenda:first-checked
      &agenda-kb &agenda-index
      (impl (impl jph jps) jch)
      3 5000 100000 &agenda-policy))
  (ATP.Checked
    (pm2.61d1 (imim2i jh2) jh1)
    (impl (impl jph jps) jch)
    (exact (impl (impl jph jps) jch))))

; Negative: stale index entries still cannot fabricate a state transition.
!(bind! &agenda-stale (new-space))
!(add-atom &agenda-stale (ATP.Index.Rule ATP.AnyHead 999 forged))
!(assertEqual
  (let (ATP.AgendaSearchResult
         (ATP.AgendaIncomplete $reason $coverage) $stats)
       (atp:agenda:first-checked
         &agenda-kb &agenda-stale ch 2 100 100000 &agenda-policy)
    (Incomplete $reason $coverage))
  (Incomplete NoInhabitantWithinBounds TruncatedAgenda))

; Negative: a zero step budget is never reported as a refutation.
!(assertEqual
  (let (ATP.AgendaSearchResult
         (ATP.AgendaIncomplete $reason $coverage) $stats)
       (atp:agenda:first-checked
         &agenda-kb &agenda-index ch 2 0 100000 &agenda-policy)
    (Incomplete $reason $coverage))
  (Incomplete StepBound CompleteAgenda))

!(println! (ATPAgendaLibrarySummary 6 6 0))
