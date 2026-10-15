; An equation with 24 pattern variables: its telescope is held at its own
; size, so the definition is admitted and computes, and conversion with
; anything but its reduct is refuted.
(hol:profile base)
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile base)) $profile-space)))
!(hol:inductive &s nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &s pick24 (-> nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat)
   (= (pick24 $a1 $a2 $a3 $a4 $a5 $a6 $a7 $a8 $a9 $a10 $a11 $a12 $a13 $a14 $a15 $a16 $a17 $a18 $a19 $a20 $a21 $a22 $a23 $a24) $a24))
!(type:eq &s (pick24 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) (succ zero))
!(type:eq &s (pick24 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) zero)
