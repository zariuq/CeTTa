; Fuel-bounded operational control for examples using the generic derivation
; word machine.  This module executes a supplied LanguageDef and relation-row
; environment; it contains no TPTP calculus, proof search, or logical
; verification policy.

!(import! &self lib_tptp)

(= (dwm-example:execute $language $rows $fuel $term)
   (dwm-example:run
     $language $rows $fuel $term
     (dwm-example:traces-nil) 0))

(= (dwm-example:run
      $language $rows $fuel $term $traces $steps)
   (case $term
     (((dwm:halted $outcome $nodes)
        (dwm-example:complete
          (dwm:halted $outcome $nodes) $traces $steps))
      ($_
        (dwm-example:run-active
          $language $rows $fuel $term $traces $steps)))))

; A halted machine is recognized before this fuel test.  Reaching a terminal
; state on the final permitted step therefore succeeds without replaying it.
(= (dwm-example:run-active
      $language $rows $fuel $term $traces $steps)
   (case (== $fuel 0)
     ((True
        (dwm-example:fuel-exhausted $term $traces $steps))
      (False
        (dwm-example:transition
          $language $rows $fuel $term $traces $steps)))))

(= (dwm-example:transition
      $language $rows $fuel $term $traces $steps)
   (case
     (langdef:rewrite-term-at
       $language Config Config 1 200000 $rows $term)
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult $target $trace))
        (dwm-example:run
          $language $rows (- $fuel 1) $target
          (dwm-example:traces-cons $trace $traces)
          (+ $steps 1)))
      ((LangDef:ContextualTermResults LangDef:ContextComplete)
        (dwm-example:stuck $term $traces $steps))
      ($result
        (dwm-example:transition-fault
          $result $term $traces $steps)))))
