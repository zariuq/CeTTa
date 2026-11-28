; unify and capture are HE's builtins, which the builtin policy offers to
; PeTTa in the extended profile only.  Plain PeTTa defines neither (the
; reference registers no such function; its lib_he defines unify by
; equations), so there an expression they head is data whose arguments are
; evaluated, at a directive's root as nested and in an equation body.  The
; extended profile runs both.  The same holds for every name the policy
; offers in the extended profile only, such as count-atoms.
(fact 1)
!(unify (f $x) (f 1) $x no)
!(unify (f 1) (f 2) yes no)
!(unify &self (fact $x) $x none)
!(capture (+ 1 2))
!(get-metatype (capture &self))
!(count-atoms &self)
!(let $r (unify (f $x) (f 1) $x no) $r)
!(collapse (capture (+ 1 2)))
(= (unify-in-body $v) (unify (f $x) $v $x no))
!(unify-in-body (f 1))
(= (capture-in-body) (capture (+ 1 2)))
!(capture-in-body)
; lib_he defines unify by equations: after the import, plain PeTTa's unify
; is that function, and capture stays data.
!(import! &self (library lib_he))
!(unify (f $x) (f 1) $x no)
!(unify (f 1) (f 2) yes no)
!(capture (+ 1 2))
