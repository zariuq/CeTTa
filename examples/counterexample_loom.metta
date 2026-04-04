; Counterexample Loom — CeTTa working version
; =============================================
; MeTTa searches over subsets of MM2 rules.
; MM2 (MORK) acts as saturating oracle per candidate.
;
; CeTTa quirk: exec atoms contain (+ ...) and (- ...) which get
; evaluated if passed through equations. Work around by doing
; match→add-atom directly inside the MORK space setup.

!(import! &self mork)

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
  (match &self (rule $label $atom) (mork:add-atom $ws $atom)))

; Load substrate for a scenario
(= (load-substrate! $ws solar)
  (let* (($_ (mork:add-atom $ws (essence ember))) ($_ (mork:add-atom $ws (essence dew)))
         ($_ (mork:add-atom $ws (essence seed))) ($_ (mork:add-atom $ws (essence echo)))) ()))
(= (load-substrate! $ws lunar)
  (let* (($_ (mork:add-atom $ws (essence dew))) ($_ (mork:add-atom $ws (essence echo)))
         ($_ (mork:add-atom $ws (essence seed))) ($_ (mork:add-atom $ws (essence spark)))) ()))
(= (load-substrate! $ws sterile)
  (let* (($_ (mork:add-atom $ws (essence ember))) ($_ (mork:add-atom $ws (essence spark)))) ()))
(= (load-substrate! $ws chorus-guard)
  (let* (($_ (mork:add-atom $ws (essence seed))) ($_ (mork:add-atom $ws (essence echo)))) ()))

; Stable candidate catalog.
; This keeps the "loom" idea visible while keeping live MorkSpace creation
; explicit at top level instead of hiding it inside equation-local search.
(= (candidate-bundle solar-only)
  (bundle
    solar-starter solar-middle solar-terminal
    skip skip skip
    skip skip))
(= (candidate-bundle chorus-trap)
  (bundle
    solar-starter solar-middle solar-terminal
    lunar-starter lunar-middle lunar-terminal
    decoy-chorus skip))
(= (candidate-bundle balanced)
  (bundle
    solar-starter solar-middle solar-terminal
    lunar-starter lunar-middle lunar-terminal
    skip skip))

(= (load-bundle! $ws $name)
  (let (bundle $a $b $c $d $e $f $g $h) (candidate-bundle $name)
    (let* (($_ (maybe-load! $ws $a)) ($_ (maybe-load! $ws $b))
           ($_ (maybe-load! $ws $c)) ($_ (maybe-load! $ws $d))
           ($_ (maybe-load! $ws $e)) ($_ (maybe-load! $ws $f))
           ($_ (maybe-load! $ws $g)) ($_ (maybe-load! $ws $h)))
      ())))

(= (prepare-trial! $ws $name $scenario)
  (let* (($_ (load-bundle! $ws $name))
         ($_ (load-substrate! $ws $scenario))
         ($_ (mork:step! $ws 64)))
    ()))

; Export the current visible atom set into an ordinary CeTTa space for
; straightforward host-side inspection.
(= (export-visible! $dst $src)
  (let $atoms (collapse (mork:get-atoms $src))
    (add-atoms $dst $atoms)))

; Check whether a specific atom exists in an exported ordinary space.
(= (view-has? $space $atom)
  (let $hits (collapse (match $space $atom yes))
    (if (== (size-atom $hits) 0) False True)))

(= (forbidden-absent? $space $atom)
  (if (view-has? $space $atom) False True))

(= (winner-if-ok $name $solar $lunar $sterile $chorus)
  (if (== $solar True)
    (if (== $lunar True)
      (if (== $sterile True)
        (if (== $chorus True)
          (winner $name (candidate-bundle $name))
          (empty))
        (empty))
      (empty))
    (empty)))

!(println! "=== Counterexample Loom ===")

!(bind! &solarOnlySolar (mork:new-space))
!(prepare-trial! &solarOnlySolar solar-only solar)
!(bind! &solarOnlyLunar (mork:new-space))
!(prepare-trial! &solarOnlyLunar solar-only lunar)
!(bind! &solarOnlySterile (mork:new-space))
!(prepare-trial! &solarOnlySterile solar-only sterile)
!(bind! &solarOnlyChorus (mork:new-space))
!(prepare-trial! &solarOnlyChorus solar-only chorus-guard)

!(bind! &chorusTrapSolar (mork:new-space))
!(prepare-trial! &chorusTrapSolar chorus-trap solar)
!(bind! &chorusTrapLunar (mork:new-space))
!(prepare-trial! &chorusTrapLunar chorus-trap lunar)
!(bind! &chorusTrapSterile (mork:new-space))
!(prepare-trial! &chorusTrapSterile chorus-trap sterile)
!(bind! &chorusTrapChorus (mork:new-space))
!(prepare-trial! &chorusTrapChorus chorus-trap chorus-guard)

!(bind! &balancedSolar (mork:new-space))
!(prepare-trial! &balancedSolar balanced solar)
!(bind! &balancedLunar (mork:new-space))
!(prepare-trial! &balancedLunar balanced lunar)
!(bind! &balancedSterile (mork:new-space))
!(prepare-trial! &balancedSterile balanced sterile)
!(bind! &balancedChorus (mork:new-space))
!(prepare-trial! &balancedChorus balanced chorus-guard)

!(bind! &solarOnlySolarView (new-space))
!(export-visible! &solarOnlySolarView &solarOnlySolar)
!(bind! &solarOnlyLunarView (new-space))
!(export-visible! &solarOnlyLunarView &solarOnlyLunar)
!(bind! &solarOnlySterileView (new-space))
!(export-visible! &solarOnlySterileView &solarOnlySterile)
!(bind! &solarOnlyChorusView (new-space))
!(export-visible! &solarOnlyChorusView &solarOnlyChorus)

!(bind! &chorusTrapSolarView (new-space))
!(export-visible! &chorusTrapSolarView &chorusTrapSolar)
!(bind! &chorusTrapLunarView (new-space))
!(export-visible! &chorusTrapLunarView &chorusTrapLunar)
!(bind! &chorusTrapSterileView (new-space))
!(export-visible! &chorusTrapSterileView &chorusTrapSterile)
!(bind! &chorusTrapChorusView (new-space))
!(export-visible! &chorusTrapChorusView &chorusTrapChorus)

!(bind! &balancedSolarView (new-space))
!(export-visible! &balancedSolarView &balancedSolar)
!(bind! &balancedLunarView (new-space))
!(export-visible! &balancedLunarView &balancedLunar)
!(bind! &balancedSterileView (new-space))
!(export-visible! &balancedSterileView &balancedSterile)
!(bind! &balancedChorusView (new-space))
!(export-visible! &balancedChorusView &balancedChorus)

!(println! (candidate-check solar-only
             solar (view-has? &solarOnlySolarView (artifact star-lantern))
             lunar (view-has? &solarOnlyLunarView (artifact star-lantern))
             sterile (forbidden-absent? &solarOnlySterileView (artifact ash))
             chorus-guard (forbidden-absent? &solarOnlyChorusView (artifact chorus))))
!(println! (candidate-check chorus-trap
             solar (view-has? &chorusTrapSolarView (artifact star-lantern))
             lunar (view-has? &chorusTrapLunarView (artifact star-lantern))
             sterile (forbidden-absent? &chorusTrapSterileView (artifact ash))
             chorus-guard (forbidden-absent? &chorusTrapChorusView (artifact chorus))))
!(println! (candidate-check balanced
             solar (view-has? &balancedSolarView (artifact star-lantern))
             lunar (view-has? &balancedLunarView (artifact star-lantern))
             sterile (forbidden-absent? &balancedSterileView (artifact ash))
             chorus-guard (forbidden-absent? &balancedChorusView (artifact chorus))))

!(let $winner
     (select
       (superpose (
         (winner-if-ok solar-only
           (view-has? &solarOnlySolarView (artifact star-lantern))
           (view-has? &solarOnlyLunarView (artifact star-lantern))
           (forbidden-absent? &solarOnlySterileView (artifact ash))
           (forbidden-absent? &solarOnlyChorusView (artifact chorus)))
         (winner-if-ok chorus-trap
           (view-has? &chorusTrapSolarView (artifact star-lantern))
           (view-has? &chorusTrapLunarView (artifact star-lantern))
           (forbidden-absent? &chorusTrapSterileView (artifact ash))
           (forbidden-absent? &chorusTrapChorusView (artifact chorus)))
         (winner-if-ok balanced
           (view-has? &balancedSolarView (artifact star-lantern))
           (view-has? &balancedLunarView (artifact star-lantern))
           (forbidden-absent? &balancedSterileView (artifact ash))
           (forbidden-absent? &balancedChorusView (artifact chorus))))))
   (println! (if (== $winner Empty)
               (catalog-result no-shared-bundle)
               $winner)))
