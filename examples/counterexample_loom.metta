; Counterexample Loom — CeTTa working version
; =============================================
; MeTTa searches over subsets of MM2 rules.
; MM2 (MORK) acts as saturating oracle per candidate.
;
; CeTTa quirk: exec atoms contain (+ ...) and (- ...) which get
; evaluated if passed through equations. Work around by doing
; match→add-atom directly inside the MORK space setup.

; Rule catalog as data atoms
(rule solar-starter (exec (0 solar-starter) (, (essence ember) (essence dew)) (O (+ (essence mist)) (- (essence ember)) (- (essence dew)))))
(rule solar-middle (exec (1 solar-middle) (, (essence mist) (essence seed)) (O (+ (artifact sun-bloom)) (- (essence mist)) (- (essence seed)))))
(rule solar-terminal (exec (2 solar-terminal) (, (artifact sun-bloom) (essence echo)) (O (+ (artifact star-lantern)) (- (artifact sun-bloom)) (- (essence echo)))))
(rule lunar-starter (exec (0 lunar-starter) (, (essence dew) (essence echo)) (O (+ (essence dream-mist)) (- (essence dew)) (- (essence echo)))))
(rule lunar-middle (exec (1 lunar-middle) (, (essence dream-mist) (essence seed)) (O (+ (artifact moon-veil)) (- (essence dream-mist)) (- (essence seed)))))
(rule lunar-terminal (exec (2 lunar-terminal) (, (artifact moon-veil) (essence spark)) (O (+ (artifact star-lantern)) (- (artifact moon-veil)) (- (essence spark)))))
(rule decoy-chorus (exec (0 decoy-chorus) (, (essence seed) (essence echo)) (O (+ (artifact chorus)) (- (essence seed)) (- (essence echo)))))
(rule decoy-ash (exec (0 decoy-ash) (, (essence ember) (essence spark)) (O (+ (artifact ash)) (- (essence ember)) (- (essence spark)))))

; Conditionally load a rule: if label is skip, do nothing; else load from catalog
(= (maybe-load! $ws skip) ())
(= (maybe-load! $ws $label)
  (match &self (rule $label $atom) (add-atom $ws $atom)))

; Load substrate for a scenario
(= (load-substrate! $ws solar)
  (let* (($_ (add-atom $ws (essence ember))) ($_ (add-atom $ws (essence dew)))
         ($_ (add-atom $ws (essence seed))) ($_ (add-atom $ws (essence echo)))) ()))
(= (load-substrate! $ws lunar)
  (let* (($_ (add-atom $ws (essence dew))) ($_ (add-atom $ws (essence echo)))
         ($_ (add-atom $ws (essence seed))) ($_ (add-atom $ws (essence spark)))) ()))
(= (load-substrate! $ws sterile)
  (let* (($_ (add-atom $ws (essence ember))) ($_ (add-atom $ws (essence spark)))) ()))
(= (load-substrate! $ws chorus-guard)
  (let* (($_ (add-atom $ws (essence seed))) ($_ (add-atom $ws (essence echo)))) ()))

; Check if a specific artifact exists in a MORK space
(= (has? $ws $name)
  (let $r (collapse (match $ws (artifact $name) yes))
    (if (== (size-atom $r) 0) False True)))

; Run a scenario: fresh MORK space, load 8 maybe-rules, load substrate, step, judge
(= (run-trial $a $b $c $d $e $f $g $h $scenario)
  (let $ws (new-space mork)
    (let* (($_ (maybe-load! $ws $a)) ($_ (maybe-load! $ws $b))
           ($_ (maybe-load! $ws $c)) ($_ (maybe-load! $ws $d))
           ($_ (maybe-load! $ws $e)) ($_ (maybe-load! $ws $f))
           ($_ (maybe-load! $ws $g)) ($_ (maybe-load! $ws $h))
           ($_ (load-substrate! $ws $scenario))
           ($_ (step! $ws 64)))
      (has? $ws star-lantern))))

; Negative scenario: must NOT have a specific artifact
(= (run-neg-trial $a $b $c $d $e $f $g $h $scenario $forbidden)
  (let $ws (new-space mork)
    (let* (($_ (maybe-load! $ws $a)) ($_ (maybe-load! $ws $b))
           ($_ (maybe-load! $ws $c)) ($_ (maybe-load! $ws $d))
           ($_ (maybe-load! $ws $e)) ($_ (maybe-load! $ws $f))
           ($_ (maybe-load! $ws $g)) ($_ (maybe-load! $ws $h))
           ($_ (load-substrate! $ws $scenario))
           ($_ (step! $ws 64)))
      (if (has? $ws $forbidden) False True))))

; Full suite check — 4 scenarios
(= (suite-ok $a $b $c $d $e $f $g $h)
  (if (== (run-trial $a $b $c $d $e $f $g $h solar) True)
    (if (== (run-trial $a $b $c $d $e $f $g $h lunar) True)
      (if (== (run-neg-trial $a $b $c $d $e $f $g $h sterile ash) True)
        (if (== (run-neg-trial $a $b $c $d $e $f $g $h chorus-guard chorus) True)
          True False) False) False) False))

; Nondeterministic subset: each of 8 rules is either included or skipped
(= (pick $label) (superpose ($label skip)))

; Search: enumerate all 2^8 subsets, keep those passing the suite
!(println! "=== Counterexample Loom ===")

!(let* (($a (pick solar-starter))
        ($b (pick solar-middle))
        ($c (pick solar-terminal))
        ($d (pick lunar-starter))
        ($e (pick lunar-middle))
        ($f (pick lunar-terminal))
        ($g (pick decoy-chorus))
        ($h (pick decoy-ash)))
  (if (suite-ok $a $b $c $d $e $f $g $h)
    (println! (WINNER $a $b $c $d $e $f $g $h))
    (empty)))
