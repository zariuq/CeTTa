; A lambda binder is a name key: a symbol x, or a structured name @K. The body
; refers to the bound value through the bare x or the drop *@K. A quotation is
; sealed: @x and @K are literal names even under a binder of that key, and
; substitution enters no quotation.
!((lam x x) payload)
!((lam x *@x) payload)
!((lam x @x) payload)
!((lam @(var moo) *@(var moo)) payload)
!((lam @(var moo) @(var moo)) payload)
!((lam x @(f x)) payload)
!((lam @(var a) (pair *@(var a) @(var b))) payload)

; Beta does not observe how its argument was written: an argument and its
; reduct give the same result.
!((lam x @x) ((lam y y) payload))
!((lam x (pair x @x)) ((lam y y) payload))

; The drop of a quoted drop of a name is the drop of that name.
!((lam x *@*@x) payload)

; The kernel types the bound value; a name has no type in its fragment.
!(type:check (lam @(var moo) *@(var moo)) (-> u0 u0))
!(type:check (lam x *@x) (-> u0 u0))
!(type:check (lam @(var moo) @(var moo)) (-> u0 u0))
!(type:check (lam x @x) (-> u0 u0))

; An inner binder with the same name shadows the outer one.
!(((lam @(var a) (lam @(var a) *@(var a))) outer) inner)

; A sealed name passed as an argument stays literal under a binder of the
; same key; only references are bound.
!(((lam @(var a) (lam @(var b) (pair *@(var a) *@(var b)))) @(var b)) inner)
!(((lam a (lam x (pair *@a @x))) @x) inner)

; A different structural name is not bound: its drop is the drop of a free
; name, which runs the quoted term.
!((lam @(var a) *@(var b)) payload)
