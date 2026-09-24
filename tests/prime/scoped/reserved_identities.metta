; The identities that proof packages generate are reserved: no program
; declares them, and a declaration written for one confers nothing.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(try (set:define &self __cetta_proof_mine (-> num num)
        (= (__cetta_proof_mine $n) $n)))
!(try (set:inductive &self __cetta_holds_type (u 0)
        (: holdsIntro __cetta_holds_type)))
!(try (set:inductive &self fresh (u 0) (: __cetta_proof_ctor fresh)))
; A raw declaration of a reserved name is not a kernel declaration; the same
; declaration of an ordinary name is.
(: __cetta_proof_raw num)
(: plain_raw num)
!(type:kernel-query (type:eq &self __cetta_proof_raw __cetta_proof_raw))
!(type:eq &self __cetta_proof_raw __cetta_proof_raw)
!(type:eq &self plain_raw plain_raw)
; Ordinary names are admitted as before.
!(set:define &self ident (-> num num) (= (ident $n) $n))
!(type:eq &self (ident zero) zero)
