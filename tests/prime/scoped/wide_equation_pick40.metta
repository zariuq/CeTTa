; An equation with 40 pattern variables: its telescope is held at its own
; size, so the definition is admitted and computes, and conversion with
; anything but its reduct is refuted.
(set:profile hol)
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &s pick40 (-> nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat nat)
   (= (pick40 $a1 $a2 $a3 $a4 $a5 $a6 $a7 $a8 $a9 $a10 $a11 $a12 $a13 $a14 $a15 $a16 $a17 $a18 $a19 $a20 $a21 $a22 $a23 $a24 $a25 $a26 $a27 $a28 $a29 $a30 $a31 $a32 $a33 $a34 $a35 $a36 $a37 $a38 $a39 $a40) $a40))
!(type:eq &s (pick40 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) (succ zero))
!(type:eq &s (pick40 zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero zero (succ zero)) zero)
