; A typed Prime application must return the same witnesses whether its head
; is written directly or supplied through a binding, inside relational search
; as well as at top level.  Non-callable and ill-typed heads keep their own
; behaviors, and held or quoted values never become calls.

(: step (-> u0 (-> u0 u1)))
(: bad-ret (-> u0 u0))
(: ctor (-> u0 u0))
(: tag u0)
(: a u0)
(: b u0)
(: c u0)
(: wrong u1)

(row a)
(row wrong)

(= (step a) (witness b p1))
(= (step a) (witness c p2))
(= (bad-ret $x) wrong)

(= (lookup-head) step)

(= (apply-direct $x) (match &self (row $x) (step $x)))
(= (apply-bound $fn $x) (match &self (row $x) (let $h $fn ($h $x))))
(= (apply-indirect $x) (match &self (row $x) (let $h (lookup-head) ($h $x))))
(= (apply-ctor-direct $x) (match &self (row $x) (ctor $x)))
(= (apply-ctor-bound $fn $x) (match &self (row $x) (let $h $fn ($h $x))))

; Same admissible application, written head vs bound head vs indirect lookup.
!(assertEqualToResult
   (step a)
   ((witness b p1) (witness c p2)))
!(assertEqualToResult
   (apply-direct a)
   ((witness b p1) (witness c p2)))
!(assertEqualToResult
   (apply-bound step a)
   ((witness b p1) (witness c p2)))
!(assertEqualToResult
   (apply-indirect a)
   ((witness b p1) (witness c p2)))
!(assertEqualToResult
   (apply-ctor-direct a)
   ((ctor a)))
!(assertEqualToResult
   (apply-ctor-bound ctor a)
   ((ctor a)))

; Negative controls: ill-typed arguments and ill-typed return values fault
; identically through written and bound heads, rather than disappearing.
!(assertEqualToResult
   (apply-direct wrong)
   ((Error (step wrong) (BadArgType 1 u0 u1))))
!(assertEqualToResult
   (apply-bound step wrong)
   ((Error (step wrong) (BadArgType 1 u0 u1))))
!(assertEqualToResult
   (apply-bound bad-ret a)
   ((Error wrong (BadType u0 u1))))
!(assertEqualToResult
   (apply-ctor-direct wrong)
   ((Error (ctor wrong) (BadArgType 1 u0 u1))))
!(assertEqualToResult
   (apply-ctor-bound ctor wrong)
   ((Error (ctor wrong) (BadArgType 1 u0 u1))))

; Negative controls: non-callable heads and quoted/held values stay inert data.
!(assertEqualToResult
   (apply-bound unknown-head a)
   ((unknown-head a)))
!(assertEqualToResult
   (apply-bound tag a)
   ((tag a)))
!(assertEqualToResult
   (apply-bound 42 a)
   ((42 a)))
!(assertEqualToResult
   (let $q (quote (step a)) $q)
   ((quote (step a))))
!(assertEqualToResult
   (apply-bound (quote step) a)
   (((quote step) a)))
