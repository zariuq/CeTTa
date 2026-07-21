; A bounded contrast between equal-hazard and demand-weighted agenda policy.
; Both policies enumerate the same rule occurrences and both accept results
; only through the ordinary Prime checker.  This is a scheduler pre-experiment,
; not yet a stochastic MAM implementation.

!(import! &self atp:weighted_agenda)

!(bind! &prism-agenda-kb (new-space))
!(bind! &prism-agenda-index (new-space))

!(add-atom &prism-agenda-kb (: base-type Type))
!(add-atom &prism-agenda-kb (: goal-type Type))
!(add-atom &prism-agenda-kb (: base base-type))
!(add-atom &prism-agenda-index
  (ATP.Index.Hypothesis base-type 0 base))

; Four source-ordered dead ends share the exact goal conclusion but ask for
; an unsupported premise.  They make scheduling policy observable without
; changing theoremhood.
!(add-atom &prism-agenda-kb (: dead0 Type))
!(add-atom &prism-agenda-kb (: dead1 Type))
!(add-atom &prism-agenda-kb (: dead2 Type))
!(add-atom &prism-agenda-kb (: dead3 Type))

!(add-atom &prism-agenda-kb (: decoy0 (-> dead0 goal-type)))
!(add-atom &prism-agenda-kb (: decoy1 (-> dead1 goal-type)))
!(add-atom &prism-agenda-kb (: decoy2 (-> dead2 goal-type)))
!(add-atom &prism-agenda-kb (: decoy3 (-> dead3 goal-type)))
!(add-atom &prism-agenda-kb (chaining-rule decoy0))
!(add-atom &prism-agenda-kb (chaining-rule decoy1))
!(add-atom &prism-agenda-kb (chaining-rule decoy2))
!(add-atom &prism-agenda-kb (chaining-rule decoy3))
!(add-atom &prism-agenda-index (ATP.Index.Rule goal-type 1 decoy0))
!(add-atom &prism-agenda-index (ATP.Index.Rule goal-type 2 decoy1))
!(add-atom &prism-agenda-index (ATP.Index.Rule goal-type 3 decoy2))
!(add-atom &prism-agenda-index (ATP.Index.Rule goal-type 4 decoy3))

!(add-atom &prism-agenda-kb (: useful (-> base-type goal-type)))
!(add-atom &prism-agenda-kb (chaining-rule useful))
!(add-atom &prism-agenda-index (ATP.Index.Rule goal-type 5 useful))

!(bind! &prism-search-policy
  (ATP.AgendaSearch
    (ATP.AgendaPolicy 16 64 4096 1 0 0 256 32)
    (ATP.DirectSearch
      (ATP.TopK 32 (ATP.LinearPolicy 0 0 0 0 0 0 0))
      ATP.LeftToRight)))

!(bind! &prism-uniform-hazard
  (atp:weighted-agenda:policy
    1 atp:weighted-agenda:uniform))

!(bind! &prism-demand-hazard
  (atp:weighted-agenda:policy
    1 (atp:weighted-agenda:demand 4300 1)))

!(assertEqual
  (atp:weighted-agenda:valid-policy
    (atp:weighted-agenda:policy
      0 atp:weighted-agenda:uniform))
  (atp:weighted-agenda:invalid-baseline 0))

!(assertEqual
  (atp:weighted-agenda:valid-policy
    (atp:weighted-agenda:policy
      1 (atp:weighted-agenda:demand 4300 -1)))
  (atp:weighted-agenda:invalid-scale -1))

(= (prism:agenda:checked
     (ATP.AgendaSearchResult
       (ATP.AgendaChecked $proof $goal $verdict) $stats))
   True)

(= (prism:agenda:checked
     (ATP.AgendaSearchResult
       (ATP.AgendaIncomplete $reason $coverage) $stats))
   False)

(= (prism:agenda:checked
     (atp:weighted-agenda:run-result $result $seed))
   (prism:agenda:checked $result))

(= (prism:agenda:summary $label
     (ATP.AgendaSearchResult
       (ATP.AgendaChecked $proof $goal $verdict)
       (ATP.AgendaStats $expanded $seen)))
   (prism:agenda:trial $label checked $expanded $seen))

(= (prism:agenda:summary $label
     (ATP.AgendaSearchResult
       (ATP.AgendaIncomplete $reason $coverage)
       (ATP.AgendaStats $expanded $seen)))
   (prism:agenda:trial
     $label (incomplete $reason $coverage) $expanded $seen))

(= (prism:agenda:summary $label
     (atp:weighted-agenda:run-result $result $seed))
   (let (prism:agenda:trial
          $inner-label $status $expanded $seen)
        (prism:agenda:summary $label $result)
     (prism:agenda:trial
       $inner-label $status $expanded $seen $seed)))

(= (prism:agenda:compare $seed)
   (let $uniform-result
        (atp:weighted-agenda:first-checked
          &prism-agenda-kb &prism-agenda-index
          goal-type
          2 20 100000 &prism-search-policy
          &prism-uniform-hazard $seed)
     (let $demand-result
          (atp:weighted-agenda:first-checked
            &prism-agenda-kb &prism-agenda-index
            goal-type
            2 20 100000 &prism-search-policy
            &prism-demand-hazard $seed)
       (prism:agenda:comparison
         $seed
         (prism:agenda:summary uniform $uniform-result)
         (prism:agenda:summary demand $demand-result)))))

!(prism:agenda:compare 1)
!(prism:agenda:compare 7)
!(prism:agenda:compare 19)
!(prism:agenda:compare 42)
