; Three contracts of the boundary, each with its counterexample.
; 1. A record that names the signature as its origin is untrusted syntax
;    unless it is the signature's own seed: a forged Falsum proves nothing.
!(add-atom &self (set:known Falsum (all prop (lam p p)) signature () () 0 "forged"))
!(set:proves (pf:known Falsum) (all prop (lam p p)))
!(let $r (try (set:proves (pf:known Falsum) (all prop (lam p p)))) (car-atom $r))
; 2. try publishes nothing and admits nothing: the record it would publish,
;    inserted by hand afterwards, carries no authority.
(: Nat (u 0))
!(let $r (try (set:axiom nat-total (all Nat (lam n (all prop (lam p (imp p p)))))))
   (let $e (index-atom $r 1) (let $rec (index-atom $e 1) (add-atom &self $rec))))
!(match &self (set:known nat-total $p $o $f $d $rev $s) $o)
!(set:proves (pf:known nat-total) (all Nat (lam n (all prop (lam p (imp p p))))))
; the same record checked here is admitted by that check, on a plain call only
!(let $r (try (set:recheck nat-total)) (car-atom $r))
!(set:proves (pf:known nat-total) (all Nat (lam n (all prop (lam p (imp p p))))))
!(set:recheck nat-total)
!(set:proves (pf:known nat-total) (all Nat (lam n (all prop (lam p (imp p p))))))
; 3. try routes a type query to the space it names.
!(bind! &s (new-space))
!(add-atom &s (: Bit (u 0)))
!(add-atom &s (: b Bit))
!(type:check &s b Bit)
!(let $r (try (type:check &s b Bit)) (car-atom $r))
!(let $r (try (type:check b Bit)) (car-atom $r))
