(hol:profile base)

; Three contracts of the boundary, each with its counterexample.
; 1. A record that names the signature as its origin is untrusted syntax
;    unless it is the signature's own seed: a forged Falsum proves nothing.
!(add-atom &self (hol:known Falsum (all prop (lam p p)) signature () () 0 "forged"))
!(hol:proves (pf:known Falsum) (all prop (lam p p)))
!(let $r (try (hol:proves (pf:known Falsum) (all prop (lam p p)))) (car-atom $r))
; 2. try publishes nothing and admits nothing: the record it would publish,
;    inserted by hand afterwards, carries no authority.
(: Nat (u 0))
!(let $r (try (hol:axiom nat-total (all Nat (lam n (all prop (lam p (imp p p)))))))
   (let $e (index-atom $r 1) (let $rec (index-atom $e 1) (add-atom &self $rec))))
!(match &self (hol:known nat-total $p $o $f $d $rev $s) $o)
!(hol:proves (pf:known nat-total) (all Nat (lam n (all prop (lam p (imp p p))))))
; the same record checked here is admitted by that check, on a plain call only
!(let $r (try (hol:recheck nat-total)) (car-atom $r))
!(hol:proves (pf:known nat-total) (all Nat (lam n (all prop (lam p (imp p p))))))
!(hol:recheck nat-total)
!(hol:proves (pf:known nat-total) (all Nat (lam n (all prop (lam p (imp p p))))))
; 3. try routes a type query to the space it names.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile base)) $profile-space)))
!(add-atom &s (: Bit (u 0)))
!(add-atom &s (: b Bit))
!(type:check &s b Bit)
!(let $r (try (type:check &s b Bit)) (car-atom $r))
!(let $r (try (type:check b Bit)) (car-atom $r))
