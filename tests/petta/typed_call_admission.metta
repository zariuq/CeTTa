; A typed call runs its body once for each tuple of argument values, and
; every declaration that applies admits its results: a result stands when
; some applicable declaration accepts it.

; Two declarations admit the argument; the first refuses the result, the
; second accepts it.  The body, with its effect, runs once.
(: sharp (-> Number Bool))
(: sharp (-> Number Number))
(= (sharp $x) (let $_ (add-atom &log (ran $x)) 7))
!(Sharp (collapse (sharp 1)))
!(Ran (collapse (match &log (ran $x) $x)))

; Both refuse: no result stands, and the body still ran once.
(: refused (-> Number Bool))
(: refused (-> Number String))
(= (refused $x) (let $_ (add-atom &log2 (ran $x)) 7))
!(Refused (collapse (refused 2)))
!(Ran2 (collapse (match &log2 (ran $x) $x)))

; An open result is admitted once, not once per declaration.
(: openr (-> Number Number))
(: openr (-> Number String))
(= (openr $x) $r)
!(Open (collapse (openr 3)))

; Each result meets the union of the declarations: 1 and "a" stand, True
; does not.
(: pick (-> Number Number))
(: pick (-> Number String))
(= (pick $x) (superpose (1 "a" True)))
!(Pick (collapse (pick 0)))

; Distinct equations stay distinct answers.
(: two (-> Number Symbol))
(: two (-> Number Expression))
(= (two $x) left)
(= (two $x) right)
!(Two (collapse (two 1)))

; One declaration whose result check refuses: the profile classifies the
; failure.  Plain PeTTa filters; call contracts raise.
(: one (-> Number Number))
(= (one $x) wrong)
!(One (catch (collapse (one 1))))
