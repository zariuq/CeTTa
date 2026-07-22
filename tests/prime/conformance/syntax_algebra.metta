!(import! &self syn)

; Identity-sensitive syntax equality and alpha-equivalence are distinct.
!(assertEqual (syn:syntactic-equal (+ 1 2) (+ 1 2)) True)
!(assertEqual (syn:syntactic-equal (+ 1 2) 3) False)
!(assertEqual (syn:syntactic-equal (f $x $x) (f $y $y)) False)
!(assertEqual (syn:syntactic-equal (f $x $y) (f $x $y)) True)
!(assertEqual (syn:syntactic-equal (f $x $y) (f $y $x)) False)
!(assertEqual (=alpha (f $x $x) (f $y $y)) True)
!(assertEqual
    (syn:syntactic-equal (f $x $y) (f $y $x))
    (noreduce-eq (f $x $y) (f $y $x)))

; Compact and expanded source forms lower to the same syntax.
!(assertAlphaEqual
    (parse "$@(hole 0)")
    (parse "(syn:var (quote (hole 0)))"))
!(assertAlphaEqual
    (parse "$x")
    (parse "(syn:var x)"))
!(assertEqual
    (parse "&@(agent zar)")
    (parse "(syn:ref (quote (agent zar)))"))
!(assertEqual
    (parse "&self")
    (parse "(syn:ref self)"))
!(assertEqual
    (parse "@(agent zar)")
    (parse "(quote (agent zar))"))
!(assertEqual
    (parse "*@(agent zar)")
    (parse "(unquote (quote (agent zar)))"))

; Reader variable scope preserves sharing and distinguishes different names.
!(let $same (parse "(pair $@(hole 0) $@(hole 0))")
    (assertEqual (=alpha $same (pair $x $x)) True))
!(let $different (parse "(pair $@(hole 0) $@(hole 1))")
    (assertEqual (=alpha $different (pair $x $y)) True))
!(let $different (parse "(pair $@(hole 0) $@(hole 1))")
    (assertEqual (=alpha $different (pair $x $x)) False))

; Separate reads allocate separate variable identities even when their source
; names agree. Alpha-equivalence deliberately forgets that allocation detail.
!(let $left (parse "(pair $x $x)")
    (let $right (parse "(pair $x $x)")
      (assertEqual (syn:syntactic-equal $left $right) False)))
!(let $left (parse "(pair $x $x)")
    (let $right (parse "(pair $x $x)")
      (assertEqual (=alpha $left $right) True)))

; repr is the sole public printer and is an alpha-inverse of parse on admitted
; syntax. Expanded forms remain ordinary inputs to the same parse operation.
!(assertEqual
    (repr (quote (agent zar)))
    "@(agent zar)")
!(let $ref (parse "&@(agent zar)")
    (assertEqual (repr $ref) "&@(agent zar)"))
!(let $var (parse "$@(hole 0)")
    (assertEqual (repr $var) "$@(hole 0)"))
!(let $pattern (parse "(rule $@(hole 0) (pair $@(hole 0) $x))")
    (let $text (repr $pattern)
      (assertAlphaEqual (parse $text) $pattern)))
!(let $pattern
    (parse
      "(rule (syn:var (quote (hole 0))) (pair (syn:var (quote (hole 0))) (syn:var x)))")
    (let $text (repr $pattern)
      (assertAlphaEqual (parse $text) $pattern)))
!(let $ground (parse "(f a \"text\" 3)")
    (let $text (repr $ground)
      (assertEqual (parse $text) $ground)))
!(assertEqual (repr (+ 1 2)) "(+ 1 2)")
!(assertEqual
    (parse (repr (quote (agent zar))))
    (quote (agent zar)))
!(assertAlphaEqual
    (parse (repr (pair $x $x)))
    (pair $y $y))

; Top-level execution has a normal S-expression spelling.
(syn:exec (assertEqual (+ 20 22) 42))
!(let $form (parse "!(+ 1 2)")
    (assertEqual (repr $form) "!(+ 1 2)"))
!(assertEqual
    (parse "!(+ 1 2)")
    (parse "(syn:exec (+ 1 2))"))

; Failure modes are explicit and preserve the existing strict-reader contract.
!(assertEqualToResult
    (parse "A B")
    ((Error (parse "A B") ParseFailed)))
!(assertEqual (parse "A ; trailing comment") A)
!(assertEqualToResult
    (parse " ; comment only")
    ((Error (parse " ; comment only") ParseFailed)))
!(assertEqualToResult
    (parse "(A B")
    ((Error (parse "(A B") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:var)")
    ((Error (parse "(syn:var)") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:var (compound x))")
    ((Error (parse "(syn:var (compound x))") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:var (quote $open))")
    ((Error (parse "(syn:var (quote $open))") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:ref)")
    ((Error (parse "(syn:ref)") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:ref (compound x))")
    ((Error (parse "(syn:ref (compound x))") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:ref (quote $open))")
    ((Error (parse "(syn:ref (quote $open))") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:exec)")
    ((Error (parse "(syn:exec)") ParseFailed)))
!(assertEqualToResult
    (parse "(syn:exec a b)")
    ((Error (parse "(syn:exec a b)") ParseFailed)))
!(assertEqualToResult
    (parse 42)
    ((Error (parse 42) (BadArgType 1 String Number))))
