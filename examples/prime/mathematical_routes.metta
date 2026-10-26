; Define once; inspect the declaration as a language; check its proposed steps.
(hol:profile base)
!(import! &self mathematical_workspace)
!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &math N (u 0) (: z N) (: s (-> N N)))
!(hol:define &math add (-> N N N)
   (= (add $x z) $x)
   (= (add $x (s $y)) (s (add $x $y))))

; The selected declarations supply both the native computation and the
; presentation. No second list of arithmetic equations is authored here.
!(bind! &route (let $wire (math:presentation &math (N) (z s add))
                   (langdef:load-language-value $wire)))
!(eval (add (s z) (s z)) (admitted &math))

; Summarizing a report does not mint a theorem. The complete value retains
; the exact source, target, proposed proof, verdict and operational trace.
(= (step-summary (Math:Steps $status $steps))
   (RouteResults $status (step-summaries $steps)))
(= (step-summaries $steps)
   (case $steps ((() ())
     ($_ (let ((Math:Step (Math:Conversion $source $target $proof ($status $detail)) $trace) $rest)
                (decons-atom $steps)
       (let $tail (step-summaries $rest)
         (cons-atom (Step $source $target $status) $tail)))))))

!(step-summary (math:via &math N &route "N" 8 100000 100000
   (add (s (z)) (z))))
; Contextual evaluation is an authored congruence step, with its own trace.
!(step-summary (math:via &math N &route "N" 8 100000 100000
   (s (add (s (z)) (z)))))
; Evidence from a proposal can enter the ordinary theorem workflow. The
; publication operation rechecks it; the report's status is not authority.
!(let (Math:Steps $complete
        ((Math:Step (Math:Conversion $source $target $proof $verdict) $trace)))
      (math:via &math N &route "N" 8 100000 100000 (add (s (z)) (z)))
   (type:theorem &math from-route (id N $source $target) $proof))
!(type:check &math from-route (id N (add (s (z)) (z)) (s (z))))
; No outgoing step, exhausted exploration, and exhausted checking differ.
!(step-summary (math:via &math N &route "N" 8 100000 100000 (z)))
!(step-summary (math:via &math N &route "N" 8 1 100000
   (add (s (z)) (z))))
!(step-summary (math:via &math N &route "N" 8 100000 1
   (add (s (z)) (z))))

; Programs can edit the presentation as data. Neither an altered equation nor
; duplicate paths change the target theory's authority.
(= (wrong-zero (GSLTLanguageDefWireV1 $name $types $terms $eqs
                 (LCons (RewriteRule $key $bindings $premises $left $right) $rest)))
   (GSLTLanguageDefWireV1 $name $types $terms $eqs
     (LCons (RewriteRule $key $bindings $premises $left (PApp "z" LNil)) $rest)))
(= (duplicate-first (GSLTLanguageDefWireV1 $name $types $terms $eqs (LCons $rule $rest)))
   (GSLTLanguageDefWireV1 $name $types $terms $eqs (LCons $rule (LCons $rule $rest))))
!(let* (($wire (math:presentation &math (N) (z s add)))
         ($changed (wrong-zero $wire)) ($route (langdef:load-language-value $changed))
         ($result (math:via &math N $route "N" 8 100000 100000 (add (s (z)) (z))))
         ($closed (langdef:close-language $route)))
   (step-summary $result))
!(let* (($wire (math:presentation &math (N) (z s add)))
         ($changed (duplicate-first $wire)) ($route (langdef:load-language-value $changed))
         ($result (math:via &math N $route "N" 8 100000 100000 (add (s (z)) (z))))
         ($closed (langdef:close-language $route)))
   (step-summary $result))

; A loaded presentation remains a snapshot; every mathematical crossing reads
; the current target scope and refuses a withdrawn dependency.
!(match &math (: s $type) (add-atom &self (saved-successor $type)))
!(match &self (saved-successor $type) (remove-atom &math (: s $type)))
!(step-summary (math:via &math N &route "N" 8 100000 100000 (add (s (z)) (z))))
!(match &self (saved-successor $type) (add-atom &math (: s $type)))
!(step-summary (math:via &math N &route "N" 8 100000 100000 (add (s (z)) (z))))
!(langdef:close-language &route)
