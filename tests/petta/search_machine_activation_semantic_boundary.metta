; Clause-body activation is an experimental derived Bindings view.  These
; cases protect the authoritative materialized path at contextual binders,
; authored translator rules, and deep recursive chains.

(: apply (-> Atom %Undefined% %Undefined%))
(= (apply (lambda $var $body) $arg)
   (eval (let $var $arg $body)))
(= (activation-lambda)
   (apply (lambda $x (+ $x 1)) 2))
!(test (activation-lambda) 3)

(: activation-for (-> Atom Atom Atom %Undefined%))
(= (activation-for $var $collection $body)
   (quote (let $var (superpose $collection) $body)))
!(add-translator-rule! activation-for)
(= (activation-parity $items)
   (activation-for $x $items
     (if (== (% $x 2) 0) (even $x) (odd $x))))
!(test (collapse (activation-parity (3 4)))
       ((odd 3) (even 4)))

!(import! &self ../lib/lib_he)
(= (activation-div $x $y $accum)
   (chain (eval (- $x $y)) $difference
     (chain (eval (< $difference 0)) $finished
       (chain (unify $finished True
                $accum
                (chain (eval (+ 1 $accum)) $next
                  (chain (eval (activation-div $difference $y $next))
                         $answer $answer)))
              $result $result))))
!(test (chain (eval (activation-div 350 5 0)) $answer $answer) 70)
