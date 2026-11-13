; The builtins' declared positions (lib/presentation/position_roles.metta),
; checked against this lane's behaviour: a call written where a position is
; declared Atom is kept as data; written where a position is declared any
; other type, or where nothing is declared, it runs.  The presentations print
; the first as data, f{a}, and the second as a call, f(a).
!(import! &self (library lib_flow))
(= (twice $x) (* 2 $x))
!(bind! &s (new-space))

; add-atom and remove-atom: (-> SpaceType Atom (->)): the atom is held.
!(add-atom &s (twice 3))
!(collapse (get-atoms &s))
!(remove-atom &s (twice 3))
!(collapse (get-atoms &s))
; quote: (-> Atom Atom): held.
!(quote (twice 3))
; import!: (-> SpaceType Atom (->)): the module is named by data, here the held
; call (modname) itself, not its value.
(= (modname) no-such-module-anywhere)
!(catch (import! &self (modname)))
; bind!: (-> Atom %Undefined% (->)): the value runs (here it makes a space).
!(bind! &w (new-space))
!(add-atom &w kept)
!(collapse (get-atoms &w))
; match: (-> SpaceType Atom %Undefined% %Undefined%): the pattern is held
; (it meets the stored (twice 3) as it is), the template runs.
!(add-atom &s (mark (twice 3)))
!(match &s (mark (twice 3)) (twice 5))
; unify: (-> Atom Atom %Undefined% %Undefined% %Undefined%): both held.
!(unify (twice 3) (twice $y) $y no)
; let: (-> Atom %Undefined% %Undefined% %Undefined%): the value runs.
!(let $v (twice 1) (got $v))
; chain: (-> %Undefined% Atom %Undefined% %Undefined%): the first runs.
!(chain (twice 2) $v (got $v))
; |->: (-> Atom %Undefined% %Undefined%): the body runs when applied.
!((|-> ($x) (twice $x)) 5)
; transform: the sources are patterns; without a destination the instances
; are evaluated, routed into a destination they are made as data.
!(bind! &t (new-space))
!(add-atom &t (item 1))
!(collapse (transform &t (item $m) (twice 7)))
!(transform &t (item $m) (made (twice 1)) &t)
!(collapse (match &t (made $x) (kept $x)))
; flow:for-match: the pattern is held, the body runs.
!(flow:for-match &s (mark (twice 3)) (println! (body (twice 6))))
; superpose, a special form: its list is held (never a call of its first
; item) and each item runs.
!(collapse (superpose ((twice 1) b)))

; Declared nowhere, so they run their argument here: (twice 3) is 6.
!(get-metatype (twice 3))
!(get-type (twice 3))
!(no-such-function (twice 3))
