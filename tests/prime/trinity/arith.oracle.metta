(set:profile hol)

; Native arithmetic as an oracle with a specification, with the trust it
; needs placed in the open.  The positive binary numbers and their sum,
; product and comparison are declared as in arith.binary.metta; nothing
; about them is trusted until a declaration says so:
;   (set:numerals space type (kind constructor ...))  how a type writes numbers;
;   (set:oracle space op (equations op) (backend name native))
;                                                     who computes `op` natively.
; Admission checks both, and compares the backend with the equations on small
; calls.  Every answer the backend computed is followed by its trust record,
; which names two trusts apart: that the backend computes its native
; operation correctly, (trusted backend native); and that the native
; operation agrees with the declared equations, (spec-agreement ...), either
; proved by the Lean theorems of the agreement in the model
; (Trinity.Arith.Oracle) or only tested by the calls checked at admission.
; Last come the calls and the tests that check both.
!(set:inductive &self pos (u 0) (: one pos) (: bit0 (-> pos pos)) (: bit1 (-> pos pos)))
!(set:inductive &self nat (u 0) (: n0 nat) (: npos (-> pos nat)))
!(set:inductive &self ord (u 0) (: less ord) (: same ord) (: more ord))
!(set:define &self psucc (-> pos pos)
   (= (psucc one) (bit0 one))
   (= (psucc (bit0 $p)) (bit1 $p))
   (= (psucc (bit1 $p)) (bit0 (psucc $p))))
!(set:define &self phalf (-> pos nat)
   (= (phalf one) n0) (= (phalf (bit0 $p)) (npos $p)) (= (phalf (bit1 $p)) (npos $p)))
!(set:define &self nhalf (-> nat nat)
   (= (nhalf n0) n0) (= (nhalf (npos $p)) (phalf $p)))
!(set:define &self nsucc-pos (-> nat pos)
   (= (nsucc-pos n0) one) (= (nsucc-pos (npos $q)) (psucc $q)))
!(set:define &self low0-pos (-> pos pos pos)
   (= (low0-pos one $r) (bit1 $r))
   (= (low0-pos (bit0 $q) $r) (bit0 $r))
   (= (low0-pos (bit1 $q) $r) (bit1 $r)))
!(set:define &self low0 (-> nat pos pos)
   (= (low0 n0 $r) (bit0 $r)) (= (low0 (npos $q) $r) (low0-pos $q $r)))
!(set:define &self low1-pos (-> pos pos pos)
   (= (low1-pos one $r) (bit0 (psucc $r)))
   (= (low1-pos (bit0 $q) $r) (bit1 $r))
   (= (low1-pos (bit1 $q) $r) (bit0 (psucc $r))))
!(set:define &self low1 (-> nat pos pos)
   (= (low1 n0 $r) (bit1 $r)) (= (low1 (npos $q) $r) (low1-pos $q $r)))
!(set:define &self padd-nat (-> pos nat pos)
   (= (padd-nat one $m) (nsucc-pos $m))
   (= (padd-nat (bit0 $p) $m) (low0 $m (padd-nat $p (nhalf $m))))
   (= (padd-nat (bit1 $p) $m) (low1 $m (padd-nat $p (nhalf $m)))))
!(set:define &self padd (-> pos pos pos) (= (padd $p $q) (padd-nat $p (npos $q))))
!(set:define &self pmul (-> pos pos pos)
   (= (pmul one $q) $q)
   (= (pmul (bit0 $p) $q) (bit0 (pmul $p $q)))
   (= (pmul (bit1 $p) $q) (padd $q (bit0 (pmul $p $q)))))
!(set:define &self below-if-same (-> ord ord)
   (= (below-if-same less) less) (= (below-if-same same) less) (= (below-if-same more) more))
!(set:define &self above-if-same (-> ord ord)
   (= (above-if-same less) less) (= (above-if-same same) more) (= (above-if-same more) more))
!(set:define &self cmp-one-pos (-> pos ord)
   (= (cmp-one-pos one) same) (= (cmp-one-pos (bit0 $q)) less) (= (cmp-one-pos (bit1 $q)) less))
!(set:define &self cmp-one (-> nat ord)
   (= (cmp-one n0) more) (= (cmp-one (npos $q)) (cmp-one-pos $q)))
!(set:define &self cmp-low0-pos (-> pos ord ord)
   (= (cmp-low0-pos one $c) more)
   (= (cmp-low0-pos (bit0 $q) $c) $c)
   (= (cmp-low0-pos (bit1 $q) $c) (below-if-same $c)))
!(set:define &self cmp-low0 (-> nat ord ord)
   (= (cmp-low0 n0 $c) more) (= (cmp-low0 (npos $q) $c) (cmp-low0-pos $q $c)))
!(set:define &self cmp-low1-pos (-> pos ord ord)
   (= (cmp-low1-pos one $c) more)
   (= (cmp-low1-pos (bit0 $q) $c) (above-if-same $c))
   (= (cmp-low1-pos (bit1 $q) $c) $c))
!(set:define &self cmp-low1 (-> nat ord ord)
   (= (cmp-low1 n0 $c) more) (= (cmp-low1 (npos $q) $c) (cmp-low1-pos $q $c)))
!(set:define &self pcmp-nat (-> pos nat ord)
   (= (pcmp-nat one $m) (cmp-one $m))
   (= (pcmp-nat (bit0 $p) $m) (cmp-low0 $m (pcmp-nat $p (nhalf $m))))
   (= (pcmp-nat (bit1 $p) $m) (cmp-low1 $m (pcmp-nat $p (nhalf $m)))))
!(set:define &self pcmp (-> pos pos ord) (= (pcmp $p $q) (pcmp-nat $p (npos $q))))
!(set:define &self pick-max (-> ord pos pos pos)
   (= (pick-max less $p $q) $q) (= (pick-max same $p $q) $q) (= (pick-max more $p $q) $p))
!(set:define &self pmax (-> pos pos pos) (= (pmax $p $q) (pick-max (pcmp $p $q) $p $q)))

; ---- Without a declaration the equations run: 7 * 9 = 63 in 41 firings,
; and no trust record follows.
!(cost:firings (pmul (bit1 (bit1 one)) (bit1 (bit0 (bit0 one)))))

; ---- Readings.  A reading needs the readings of its fields' types first,
; and must name the type's admitted constructors, in order, with the fields
; its kind reads.
!(try (set:numerals &self nat (natural n0 npos)))
!(try (set:numerals &self pos (positive one bit0)))
!(try (set:numerals &self pos (positive one bit1 bit2)))
!(try (set:numerals &self ord (positive less same more)))
!(set:numerals &self pos (positive one bit0 bit1))
!(set:numerals &self nat (natural n0 npos))
!(set:numerals &self ord (ordering less same more))
!(try (set:numerals &self pos (positive one bit0 bit1)))

; ---- Declarations that admission refuses.  The specification must be the
; operation's own equations; the backend's operation must have the
; operation's signature; and on small calls the backend must return what
; the equations compute: multiplication declared as addition fails at
; 1 * 1.  A definition written as raw equations has no admitted
; specification, and a backend not built is not one.
(: rawadd (-> pos pos pos))
(= (rawadd $p $q) (padd $p $q))
!(try (set:oracle &self padd (equations pmul) (backend gmp-mpz add)))
!(try (set:oracle &self pcmp (equations pcmp) (backend gmp-mpz add)))
!(try (set:oracle &self pmul (equations pmul) (backend gmp-mpz add)))
!(try (set:oracle &self rawadd (equations rawadd) (backend gmp-mpz add)))
!(try (set:oracle &self padd (equations padd) (backend abacus add)))

; ---- Declarations admitted.  The sum and the comparison by GMP, the product
; by 64-bit words, which decline any call whose numbers do not fit.  A second
; declaration of an operation is refused.  The record names the size of the
; specification (the definitions the equations reach), the backend, the
; signature and the agreement with the equations: the Lean theorems that
; prove it for the sum, and for the comparison the 9 calls that tested it.
!(set:oracle &self padd (equations padd) (backend gmp-mpz add))
!(set:oracle &self pcmp (equations pcmp) (backend gmp-mpz compare))
!(set:oracle &self pmul (equations pmul) (backend uint64 mul))
!(try (set:oracle &self padd (equations padd) (backend gmp-mpz add)))
!(match &self (set:oracle-of padd (equations padd $n $digest) $b $s $l) ($n $b $s $l))
!(match &self (set:oracle-of pcmp (equations pcmp $n $digest) $b $s $l) ($n $b $s $l))

; ---- Operational: the declared operations compute natively, in one firing,
; each answer followed by its trust record.  7 + 5 = 12, 7 * 9 = 63, and
; 3 < 6 makes the maximum of 3 and 6 the second.
!(padd (bit1 (bit1 one)) (bit1 (bit0 one)))
!(cost:firings (pmul (bit1 (bit1 one)) (bit1 (bit0 (bit0 one)))))
!(pmax (bit1 one) (bit0 (bit1 one)))
; (2^20 + 1)^4 does not fit in 64 bits: the word backend computes the
; square and declines the product of two squares, which is computed by its
; equations, with only the sums inside it native.
!(let $square (pmul (bit1 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 one))))))))))))))))))))
                  (bit1 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 (bit0 one)))))))))))))))))))))
   (let (quote ($values $n)) (lift (cost:firings (pmul $square $square)))
     (quote (product-by-equations (firings $n)))))
; A raw equation calling a declared operation: the call inside is native.
!(rawadd (bit1 (bit1 one)) (bit1 (bit0 one)))

; ---- Intensional: the kernel compares terms by the equations alone, so a
; judgment carries no trust record.
!(type:eq &self (padd (bit1 one) (bit1 one)) (bit0 (bit1 one)))
!(type:eq &self (padd (bit1 one) (bit1 one)) (bit1 (bit0 one)))

; ---- A raw record confers nothing.  Written into the space by hand, the
; record of a native sum for the maximum is not admitted, and the maximum of
; 3 and 6 is still 6, not 9.
!(add-atom &self (set:oracle-of pmax (equations pmax 5 "0") (backend gmp-mpz add) (signature pos pos pos) (spec-agreement (tested (admission-calls 9)))))
!(pmax (bit1 one) (bit0 (bit1 one)))


; ---- The specification is the admitted equations as they stand.  Removing
; the computation rule of the sum changes them: the oracle stops, and the
; call is left as written, as its equations now leave it.
!(remove-atom &self (type:rule padd 2 ((PVar 0) (PVar 1)) (App (App (DeclConst padd-nat) (PVar 0)) (App (DeclConst npos) (PVar 1)))))
!(padd (bit1 (bit1 one)) (bit1 (bit0 one)))
