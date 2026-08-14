; Prime reflective-name syntax.  The reader lowers @/* to the existing
; quote/unquote pair while $@ carries a closed structural key into VarId-based
; matching.  No expression-to-string conversion participates in identity.

!(assertEqual @doc (quote doc))
!(assertEqual @"moo" (quote "moo"))
!(assertEqual @(agent Max Botnick) (quote (agent Max Botnick)))

!(assertEqual *@doc doc)
!(assertEqual *@"moo" "moo")
!(assertEqual *@(agent Max Botnick) (agent Max Botnick))

!(assertEqual (== $@(mm-var "ph") $@(mm-var "ph")) True)
!(assertEqual (== $@(mm-var "ph") $@(mm-var "ps")) False)
!(assertEqual (== $@"ph" $@"ph") True)
!(assertEqual (== $@"ph" $@"ps") False)

(= (named-id $@(mm-var "ph")) $@(mm-var "ph"))
!(assertEqual (named-id payload) payload)

(= (named-pair $@(left) $@(right))
   (Pair $@(left) $@(right)))
!(assertEqual (named-pair A B) (Pair A B))

; Structural binders remain distinct through the evaluator's fold transport.
!(assertEqual
    (fold (superpose (10)) 1
          $@(accumulator) $@(item)
          (Pair $@(accumulator) $@(item)))
    (Pair 1 10))

; Bare * remains the ordinary prefix operator; only adjacent *name is unquote.
!(assertEqual (* 6 7) 42)

!(println! (PrimeUniversalNameSyntaxSummary 14 14 0))
