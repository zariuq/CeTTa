; Control APIs keep their original non-strict behavior as one alternative.
(= (if $condition $yes $no) extra-if)
!(collapse (if True 1 (undefined-branch)))
!(std:if True 1 (undefined-branch))
(= (let $pattern $value $body) extra-let)
!(collapse (let $x 2 (+ $x 1)))
!(std:let $x 2 (+ $x 1))
!(collapse (match &self (: quote $type) $type))
(= (quote $body) extra-quote)
!(collapse (quote (+ 1 2)))
!(std:quote (+ 1 2))
; Replacing the public demand changes the parameter's denotation. The
; retained original keeps its own syntax parameter.
!(remove-atom &self (: quote (-> Atom Atom)))
(: quote (-> Proc Atom))
!(collapse (std:let (quote $body) (quote (+ 1 2)) $body))
!(std:quote (+ 1 2))
