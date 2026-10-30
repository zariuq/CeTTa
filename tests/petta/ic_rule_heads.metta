!(import! &self ../../examples/petta/ic_pln_poc.metta)

(= (rule-test:check $name $condition) (println! (RuleCheck $name (== $condition True))))
(= (rule-test:count $text)
   (library-stack:site-count (ic:prepare-library RuleHeads (syntax:read $text))))
(= (RuleBlocked $x) (println! ConstructorBodyMustNotRun))
(: RuleTyped (-> Atom Atom))
(: (RuleTypedApplication $x) Number)

!(rule-test:check PatternAdmitted
   (> (rule-test:count "(= (rule (Box $x $y)) (+ $x $y))") 0))
!(rule-test:check OldGateRejectsPattern
   (== (library-stack:site-count (ic:prepare-library Deferred
      (syntax:read "(= (rule (Box $x $y)) (+ $x $y))"))) 0))
!(rule-test:check LeanPLNHeadCounterexample
   (== (library-stack:site-count (ic:prepare-library Deferred
     (syntax:read "(= (Truth_ModusPonens (stv $s1 $c1) (stv $s2 $c2)) (+ $s1 $s2))"))) 0))
!(rule-test:check PLNHeadNowAdmitted
   (> (rule-test:count "(= (Truth_ModusPonens (stv $s1 $c1) (stv $s2 $c2)) (+ $s1 $s2))") 0))
!(rule-test:check LocalConstructorRefused
   (== (rule-test:count "(= (Box $x) $x) (= (rule (Box $x)) (+ $x $x))") 0))
!(rule-test:check AmbientConstructorRefused
   (== (rule-test:count "(= (rule (RuleBlocked $x)) (+ $x $x))") 0))
!(rule-test:check TypedConstructorRefused
   (== (rule-test:count "(= (rule (RuleTyped $x)) (+ $x $x))") 0))
!(rule-test:check TypedConstructorApplicationRefused
   (== (rule-test:count "(= (rule (RuleTypedApplication $x)) (+ $x $x))") 0))
!(rule-test:check BindingPatternRefused
   (== (rule-test:count "(= (rule (quote $x)) (+ $x $x))") 0))
!(rule-test:check VariableHeadRefused
   (== (rule-test:count "(= (rule ($head $x)) (+ $x $x))") 0))
!(rule-test:check FreeCaptureRefused
   (== (rule-test:count "(= (rule (Box $x)) (+ $x $free))") 0))
!(rule-test:check TypedRuleRefused
   (== (rule-test:count "(: rule (-> Atom Atom)) (= (rule (Box $x)) (+ $x $x))") 0))
!(rule-test:check EvaluationFormBlocksInference
   (== (rule-test:count "!(unknown-command) (= (rule (Box $x)) (+ $x $x))") 0))

; A controlled witness for two distinct features. It tests the mechanism;
; the separate PLN application gate still requires its own two improvements.
(= (rule-test:source)
   "(= (rule-a (stv $x $cx) (stv $y $cy) (stv $z $cz)) (/ (* (+ 1.0 (/ $x $y)) $z) (+ 1.0 $z)))
(= (rule-b (stv $u $cu) (stv $v $cv) (stv $w $cw)) (/ (* (+ 1.0 (/ $u $v)) $w) (+ 1.0 $w)))
(= (rule-c (stv $a $ca) (stv $b $cb) (stv $c $cc)) (/ (* (+ 1.0 (/ $a $b)) $c) (+ 1.0 $c)))
(= (rule-d (stv $x $c)) (if (< $x 0) (- 0 $x) (+ (* $x $x) (* $x $x))))
(= (rule-e (stv $y $c)) (if (< $y 0) (- 0 $y) (+ (* $y $y) (* $y $y))))
(= (rule-f (stv $z $c)) (if (< $z 0) (- 0 $z) (+ (* $z $z) (* $z $z))))
(= (rule-alias (Box $x $x)) (if (< $x 0) (- 0 $x) (+ (* $x $x) (* $x $x))))")
(= (rule-test:second $source $original $first (PLN:Search $second $stats $reason $fault))
   (case (library-stack:view $second)
     (((Library:StackValue $layers $document $restored)
       (let* (($a (rule-test:check FirstImproves (< (pln-poc:cost $first) (pln-poc:cost $original))))
              ($b (rule-test:check SecondImproves (< (pln-poc:cost $second) (pln-poc:cost $first))))
              ($c (rule-test:check TwoDefinitions (== (size-atom $layers) 2)))
              ($d (rule-test:check Reconstructs (== $restored $source)))
              ($e (rule-test:check Exhausted (and (== $reason RegionExhausted) (== $fault IC:NoFault))))
              ($original-text (fs:write "runtime/ic-rule-heads-control/original.metta" (syntax:write $source)))
              ($rewritten (fs:write "runtime/ic-rule-heads-control/rewritten.metta" (syntax:write $document))))
         (println! (RuleSteps (pln-poc:cost $original) (pln-poc:cost $first) (pln-poc:cost $second)))))
      ($bad (println! (RuleCheck Expansion False))))))
(= (rule-test:first $source $original (PLN:Search $first $stats $reason $fault))
   (rule-test:second $source $original $first (pln-poc:search $first "rule-second" 0)))
!(let* (($source (syntax:read (rule-test:source))) ($original (ic:source-pack $source)))
   (rule-test:first $source $original (pln-poc:search $original "rule-first" 0)))
