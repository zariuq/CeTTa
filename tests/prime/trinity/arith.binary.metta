; Arithmetic in the language: binary numbers and fractions as declared
; datatypes, on three faces.
;   pos  one | bit0 p | bit1 p      (bit0 p = 2p, bit1 p = 2p + 1)
;   nat  n0 | npos p                 int  z0 | zpos p | zneg p
;   rat  over z d                    (z over d, the denominator positive)
; Nothing is trusted: the numbers are terms, the operations are declared
; equations admitted by the existing routes, and each verdict names the Lean
; theorem it rests on.  The operations that inspect one argument at a time
; are structural recursions; the two that are not, division and the greatest
; common divisor, are admitted by a bound, as log2 is.  The fractions of the
; Hyperseed laboratory (chapter 5) are computed in normal form and compared
; with the values the Lean theorems `Laboratory.*` state.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:known-proposition &self num-ind)
!(pf:equality &self num refl@num subst@num)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $k)) (suc (add $n $k))))
!(set:define &self mul (-> num num num)
   (= (mul $n zero) zero)
   (= (mul $n (suc $k)) (add (mul $n $k) $n)))

; ---- The datatypes.  Each is admitted with its induction principle; the
; package with all of them has a set model (in Lean,
; Trinity.Arith.Binary.binary_model for the positives and the naturals).
!(set:inductive &self pos (u 0) (: one pos) (: bit0 (-> pos pos)) (: bit1 (-> pos pos)))
!(set:inductive &self nat (u 0) (: n0 nat) (: npos (-> pos nat)))
!(set:inductive &self int (u 0) (: z0 int) (: zpos (-> pos int)) (: zneg (-> pos int)))
!(set:inductive &self rat (u 0) (: over (-> int pos rat)))
!(set:inductive &self ord (u 0) (: less ord) (: same ord) (: more ord))
!(set:inductive &self divided (u 0) (: quot-rem (-> nat nat divided)))
!(set:known-proposition &self pos-ind)
!(set:known-proposition &self nat-ind)
!(set:known-proposition &self int-ind)

; ---- Addition, digit by digit.  Every definition below looks at its first
; argument only, one constructor deep, and calls itself only on a field of
; it, so the check for structural recursion admits it; where a second
; argument must be inspected, a helper looks at it.  The sum of p and m
; reads the last digit of m and the rest of m (half of it):
;   2p + m = 2 (p + m/2) + (m mod 2),   2p + 1 + m = 2 (p + m/2) + 1 + (m mod 2),
; the second with a carry when m is odd.
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
; The records: padd-nat recurses on field 0 of bit0 and of bit1, padd is one
; equation without recursion.
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self padd-nat)
   (quote ($termination $lean "Trinity.Arith.Binary.binaryProgram_admissible")))
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self padd)
   (quote ($termination $lean)))
; The same sum written with two arguments inspected at once is not read by
; the structural check, which names the route that reads it.
!(try (set:define &self padd-cases (-> pos pos pos)
   (= (padd-cases one $q) (psucc $q))
   (= (padd-cases (bit0 $p) one) (bit1 $p))
   (= (padd-cases (bit0 $p) (bit0 $q)) (bit0 (padd-cases $p $q)))))

; ---- Comparison, minimum and maximum.  p is compared with m through the
; comparison of p with half of m and the last digit of m.
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
!(set:define &self ncmp-zero (-> nat ord) (= (ncmp-zero n0) same) (= (ncmp-zero (npos $q)) less))
!(set:define &self ncmp (-> nat nat ord)
   (= (ncmp n0 $m) (ncmp-zero $m)) (= (ncmp (npos $p) $m) (pcmp-nat $p $m)))
!(set:define &self pick-min (-> ord pos pos pos)
   (= (pick-min less $p $q) $p) (= (pick-min same $p $q) $p) (= (pick-min more $p $q) $q))
!(set:define &self pick-max (-> ord pos pos pos)
   (= (pick-max less $p $q) $q) (= (pick-max same $p $q) $q) (= (pick-max more $p $q) $p))
!(set:define &self pmin (-> pos pos pos) (= (pmin $p $q) (pick-min (pcmp $p $q) $p $q)))
!(set:define &self pmax (-> pos pos pos) (= (pmax $p $q) (pick-max (pcmp $p $q) $p $q)))

; ---- The naturals.
!(set:define &self nsucc (-> nat nat) (= (nsucc n0) (npos one)) (= (nsucc (npos $p)) (npos (psucc $p))))
!(set:define &self nadd (-> nat nat nat)
   (= (nadd n0 $m) $m) (= (nadd (npos $p) $m) (npos (padd-nat $p $m))))
!(set:define &self nmul-pos (-> nat pos nat)
   (= (nmul-pos n0 $p) n0) (= (nmul-pos (npos $q) $p) (npos (pmul $p $q))))
!(set:define &self nmul (-> nat nat nat) (= (nmul n0 $m) n0) (= (nmul (npos $p) $m) (nmul-pos $m $p)))
!(set:define &self ndouble (-> nat nat) (= (ndouble n0) n0) (= (ndouble (npos $p)) (npos (bit0 $p))))
!(set:define &self nsucc-double (-> nat nat)
   (= (nsucc-double n0) (npos one)) (= (nsucc-double (npos $p)) (npos (bit1 $p))))

; ---- The integers and their subtraction.  p - m is computed from
; p - m/2 and the last digit of m: 2p - m = 2 (p - m/2) - (m mod 2), and
; 2p + 1 - m = 2 (p - m/2) + 1 - (m mod 2).
!(set:define &self pdouble-pred (-> pos pos)
   (= (pdouble-pred one) one)
   (= (pdouble-pred (bit0 $p)) (bit1 (pdouble-pred $p)))
   (= (pdouble-pred (bit1 $p)) (bit1 (bit0 $p))))
!(set:define &self zdouble (-> int int)
   (= (zdouble z0) z0) (= (zdouble (zpos $p)) (zpos (bit0 $p))) (= (zdouble (zneg $p)) (zneg (bit0 $p))))
!(set:define &self zsucc-double (-> int int)
   (= (zsucc-double z0) (zpos one))
   (= (zsucc-double (zpos $p)) (zpos (bit1 $p)))
   (= (zsucc-double (zneg $p)) (zneg (pdouble-pred $p))))
!(set:define &self zpred-double (-> int int)
   (= (zpred-double z0) (zneg one))
   (= (zpred-double (zpos $p)) (zpos (pdouble-pred $p)))
   (= (zpred-double (zneg $p)) (zneg (bit1 $p))))
!(set:define &self one-minus-pos (-> pos int)
   (= (one-minus-pos one) z0)
   (= (one-minus-pos (bit0 $q)) (zneg (pdouble-pred $q)))
   (= (one-minus-pos (bit1 $q)) (zneg (bit0 $q))))
!(set:define &self one-minus (-> nat int)
   (= (one-minus n0) (zpos one)) (= (one-minus (npos $q)) (one-minus-pos $q)))
!(set:define &self zlow0-pos (-> pos int int)
   (= (zlow0-pos one $z) (zpred-double $z))
   (= (zlow0-pos (bit0 $q) $z) (zdouble $z))
   (= (zlow0-pos (bit1 $q) $z) (zpred-double $z)))
!(set:define &self zlow0 (-> nat int int)
   (= (zlow0 n0 $z) (zdouble $z)) (= (zlow0 (npos $q) $z) (zlow0-pos $q $z)))
!(set:define &self zlow1-pos (-> pos int int)
   (= (zlow1-pos one $z) (zdouble $z))
   (= (zlow1-pos (bit0 $q) $z) (zsucc-double $z))
   (= (zlow1-pos (bit1 $q) $z) (zdouble $z)))
!(set:define &self zlow1 (-> nat int int)
   (= (zlow1 n0 $z) (zsucc-double $z)) (= (zlow1 (npos $q) $z) (zlow1-pos $q $z)))
!(set:define &self psub-nat (-> pos nat int)
   (= (psub-nat one $m) (one-minus $m))
   (= (psub-nat (bit0 $p) $m) (zlow0 $m (psub-nat $p (nhalf $m))))
   (= (psub-nat (bit1 $p) $m) (zlow1 $m (psub-nat $p (nhalf $m)))))
!(set:define &self psub (-> pos pos int) (= (psub $p $q) (psub-nat $p (npos $q))))
!(set:define &self zopp (-> int int)
   (= (zopp z0) z0) (= (zopp (zpos $p)) (zneg $p)) (= (zopp (zneg $p)) (zpos $p)))
!(set:define &self zadd-pos (-> int pos int)
   (= (zadd-pos z0 $p) (zpos $p))
   (= (zadd-pos (zpos $q) $p) (zpos (padd $p $q)))
   (= (zadd-pos (zneg $q) $p) (psub $p $q)))
!(set:define &self zadd (-> int int int)
   (= (zadd z0 $w) $w)
   (= (zadd (zpos $p) $w) (zadd-pos $w $p))
   (= (zadd (zneg $p) $w) (zopp (zadd-pos (zopp $w) $p))))
!(set:define &self zsub (-> int int int) (= (zsub $a $b) (zadd $a (zopp $b))))
!(set:define &self zmul-pos (-> int pos int)
   (= (zmul-pos z0 $p) z0)
   (= (zmul-pos (zpos $q) $p) (zpos (pmul $p $q)))
   (= (zmul-pos (zneg $q) $p) (zneg (pmul $p $q))))
!(set:define &self zmul (-> int int int)
   (= (zmul z0 $w) z0) (= (zmul (zpos $p) $w) (zmul-pos $w $p)) (= (zmul (zneg $p) $w) (zopp (zmul-pos $w $p))))
!(set:define &self zsign (-> int ord)
   (= (zsign z0) same) (= (zsign (zpos $p)) more) (= (zsign (zneg $p)) less))
!(set:define &self zcmp (-> int int ord) (= (zcmp $a $b) (zsign (zsub $a $b))))
!(set:define &self int-of-nat (-> nat int) (= (int-of-nat n0) z0) (= (int-of-nat (npos $p)) (zpos $p)))
!(set:define &self nat-of-int (-> int nat)
   (= (nat-of-int z0) n0) (= (nat-of-int (zpos $p)) (npos $p)) (= (nat-of-int (zneg $p)) n0))
!(set:define &self nsub (-> nat nat nat)
   (= (nsub $a $b) (nat-of-int (zsub (int-of-nat $a) (int-of-nat $b)))))
!(set:define &self pos-of-int (-> int pos)
   (= (pos-of-int z0) one) (= (pos-of-int (zpos $p)) $p) (= (pos-of-int (zneg $p)) one))
!(set:define &self pos-of-nat (-> nat pos) (= (pos-of-nat n0) one) (= (pos-of-nat (npos $p)) $p))
; The distance from a smaller positive number, as a positive number (one
; where there is none).
!(set:define &self pdiff (-> pos pos pos) (= (pdiff $p $q) (pos-of-int (psub $p $q))))

; ---- The value maps into the unary numbers, and the number of digits.
!(set:define &self pval (-> pos num)
   (= (pval one) (suc zero))
   (= (pval (bit0 $p)) (add (pval $p) (pval $p)))
   (= (pval (bit1 $p)) (suc (add (pval $p) (pval $p)))))
!(set:define &self nval (-> nat num) (= (nval n0) zero) (= (nval (npos $q)) (pval $q)))
!(set:define &self pos-digits (-> pos num)
   (= (pos-digits one) (suc zero))
   (= (pos-digits (bit0 $p)) (suc (pos-digits $p)))
   (= (pos-digits (bit1 $p)) (suc (pos-digits $p))))
!(set:define &self nat-digits (-> nat num) (= (nat-digits n0) zero) (= (nat-digits (npos $p)) (pos-digits $p)))

; ---- Division with remainder, by a bound.  The long division of a natural
; number shifts in its digits from the first: the quotient and remainder of
; 2p (and of 2p + 1) come from those of p.  The call at npos p under
; npos (bit0 p) is two constructors down, not on a field, so the structural
; check does not read the equations (it names the bound); they are admitted
; by the bound "number of digits of the dividend", whose obligations hold by
; computation: the digits of p are one fewer than those of bit0 p.
!(set:define &self divided-quot (-> divided nat) (= (divided-quot (quot-rem $q $r)) $q))
!(set:define &self div-pick (-> ord nat nat pos divided)
   (= (div-pick less $q $r $d) (quot-rem $q $r))
   (= (div-pick same $q $r $d) (quot-rem (nsucc $q) (nsub $r (npos $d))))
   (= (div-pick more $q $r $d) (quot-rem (nsucc $q) (nsub $r (npos $d)))))
!(set:define &self div-settle (-> nat nat pos divided)
   (= (div-settle $q $r $d) (div-pick (ncmp $r (npos $d)) $q $r $d)))
!(set:define &self div-step0 (-> divided pos divided)
   (= (div-step0 (quot-rem $q $r) $d) (div-settle (ndouble $q) (ndouble $r) $d)))
!(set:define &self div-step1 (-> divided pos divided)
   (= (div-step1 (quot-rem $q $r) $d) (div-settle (ndouble $q) (nsucc-double $r) $d)))
!(try (set:define &self ndivmod-plain (-> nat pos divided)
   (= (ndivmod-plain n0 $d) (quot-rem n0 n0))
   (= (ndivmod-plain (npos one) $d) (div-step1 (quot-rem n0 n0) $d))
   (= (ndivmod-plain (npos (bit0 $p)) $d) (div-step0 (ndivmod-plain (npos $p) $d) $d))
   (= (ndivmod-plain (npos (bit1 $p)) $d) (div-step1 (ndivmod-plain (npos $p) $d) $d))))
!(set:define &self ndivmod (-> nat pos divided)
   (by bound num (lam a (lam d (nat-digits a)))
     (pf:fix p (pf:fix d (pf:fix R (pf:assume base (pf:assume step (pf:all-elim base (pos-digits p)))))))
     (pf:fix p (pf:fix d (pf:fix R (pf:assume base (pf:assume step (pf:all-elim base (pos-digits p))))))))
   (= (ndivmod n0 $d) (quot-rem n0 n0))
   (= (ndivmod (npos one) $d) (div-step1 (quot-rem n0 n0) $d))
   (= (ndivmod (npos (bit0 $p)) $d) (div-step0 (ndivmod (npos $p) $d) $d))
   (= (ndivmod (npos (bit1 $p)) $d) (div-step1 (ndivmod (npos $p) $d) $d)))
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self ndivmod)
   (let (bound $type $measure $obligations) $termination
     (let $count (size-atom $obligations) (quote ((Bound $type $measure $count) $lean)))))
!(set:define &self pquot (-> pos pos pos)
   (= (pquot $p $g) (pos-of-nat (divided-quot (ndivmod (npos $p) $g)))))

; ---- The greatest common divisor, by a bound.  Binary gcd: two even numbers
; share a 2, one even number loses its 2, and of two odd numbers 2x+1 and
; 2y+1 with x < y the larger becomes y - x (their difference halved).  The
; comparison of the two arguments is passed along, so the equations can tell
; the three odd cases apart.  Its calls go down the bound "digits of the
; first plus digits of the second", the unary sum; the obligations are
; proved from the facts below, all about unary numbers and digit counts.
!(set:define &self below (-> num num prop)
   (= (below $m $n)
      (all (-> num num prop) (lam R
        (imp (all num (lam x (R x (suc x))))
             (imp (all num (lam l (all num (lam x (imp (R l x) (R l (suc x)))))))
                  (R $m $n)))))))
!(set:define &self le (-> num num prop)
   (= (le $m $n)
      (all (-> num num prop) (lam R
        (imp (all num (lam x (R x x)))
             (imp (all num (lam l (all num (lam x (imp (R l x) (R l (suc x)))))))
                  (R $m $n)))))))
!(pf:theorem &self le-refl (all num (lam x (le x x)))
   (pf:fix x (pf:fix R (pf:assume rf (pf:assume st (pf:all-elim rf x))))))
!(pf:theorem &self le-step (all num (lam a (all num (lam b (imp (le a b) (le a (suc b)))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:fix R (pf:assume rf (pf:assume st
     (pf:imp-elim (pf:all-elim (pf:all-elim st a) b)
       (pf:imp-elim (pf:imp-elim (pf:all-elim h R) rf) st)))))))))
!(pf:theorem &self le-trans
   (all num (lam a (all num (lam b (all num (lam c (imp (le a b) (imp (le b c) (le a c)))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume h1 (pf:assume h2
     (pf:imp-elim
       (pf:imp-elim
         (pf:imp-elim (pf:all-elim h2 (lam u (lam v (imp (le a u) (le a v)))))
           (pf:fix x (pf:assume k k)))
         (pf:fix l (pf:fix x (pf:assume g (pf:assume k
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-step) a) x) (pf:imp-elim g k)))))))
       h1)))))))
!(pf:theorem &self le-suc (all num (lam a (all num (lam b (imp (le a b) (le (suc a) (suc b)))))))
   (pf:fix a (pf:fix b (pf:assume h
     (pf:imp-elim
       (pf:imp-elim (pf:all-elim h (lam u (lam v (le (suc u) (suc v)))))
         (pf:fix x (pf:all-elim (pf:known le-refl) (suc x))))
       (pf:fix l (pf:fix x (pf:all-elim (pf:all-elim (pf:known le-step) (suc l)) (suc x)))))))))
!(pf:theorem &self le-zero (all num (lam n (le zero n)))
   (pf:induction num-ind (lam n (le zero n))
     (pf:all-elim (pf:known le-refl) zero)
     (pf:fix n (pf:all-elim (pf:all-elim (pf:known le-step) zero) n))))
!(pf:theorem &self below-later (all num (lam a (all num (lam b (imp (below a b) (below a (suc b)))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:fix R (pf:assume base (pf:assume step
     (pf:imp-elim (pf:all-elim (pf:all-elim step a) b)
       (pf:imp-elim (pf:imp-elim (pf:all-elim h R) base) step)))))))))
!(pf:theorem &self le-below (all num (lam a (all num (lam b (imp (le a b) (below a (suc b)))))))
   (pf:fix a (pf:fix b (pf:assume h
     (pf:imp-elim
       (pf:imp-elim (pf:all-elim h (lam u (lam v (below u (suc v)))))
         (pf:fix x (pf:fix R (pf:assume base (pf:assume step (pf:all-elim base x))))))
       (pf:fix l (pf:fix x (pf:all-elim (pf:all-elim (pf:known below-later) l) (suc x)))))))))
!(pf:theorem &self le-suc-add (all num (lam a (all num (lam b (le (suc (add a b)) (add (suc a) b))))))
   (pf:fix a (pf:induction num-ind (lam b (le (suc (add a b)) (add (suc a) b)))
     (pf:all-elim (pf:known le-refl) (suc a))
     (pf:fix b (pf:assume ih
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) (suc (add a b))) (add (suc a) b)) ih))))))
!(pf:theorem &self le-add-right
   (all num (lam c (all num (lam a (all num (lam b (imp (le a b) (le (add c a) (add c b)))))))))
   (pf:fix c (pf:fix a (pf:fix b (pf:assume h
     (pf:imp-elim
       (pf:imp-elim (pf:all-elim h (lam u (lam v (le (add c u) (add c v)))))
         (pf:fix x (pf:all-elim (pf:known le-refl) (add c x))))
       (pf:fix l (pf:fix x (pf:all-elim (pf:all-elim (pf:known le-step) (add c l)) (add c x))))))))))
!(pf:theorem &self le-add-left
   (all num (lam a (all num (lam b (imp (le a b) (all num (lam c (le (add a c) (add b c)))))))))
   (pf:fix a (pf:fix b (pf:assume h
     (pf:induction num-ind (lam c (le (add a c) (add b c)))
       h
       (pf:fix c (pf:assume ih
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) (add a c)) (add b c)) ih))))))))
!(pf:theorem &self one-le (all num (lam n (le (suc zero) (suc n))))
   (pf:fix n (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) zero) n)
     (pf:all-elim (pf:known le-zero) n))))
; The digits of a difference are at most those of the larger number: by
; induction on it, through the doubling helpers of the subtraction.
!(pf:theorem &self pdouble-pred-digits
   (all pos (lam r (le (pos-digits (pdouble-pred r)) (suc (pos-digits r)))))
   (pf:induction pos-ind (lam r (le (pos-digits (pdouble-pred r)) (suc (pos-digits r))))
     (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-step) (suc zero)) (suc zero))
       (pf:all-elim (pf:known le-refl) (suc zero)))
     (pf:fix p (pf:assume ih
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) (pos-digits (pdouble-pred p))) (suc (pos-digits p))) ih)))
     (pf:fix p (pf:assume ih (pf:all-elim (pf:known le-refl) (suc (suc (pos-digits p))))))))
!(pf:theorem &self zdouble-digits
   (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zdouble z))) (suc n)))))))
   (pf:induction int-ind
     (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zdouble z))) (suc n))))))
     (pf:fix n (pf:assume h (pf:all-elim (pf:known one-le) n)))
     (pf:fix r (pf:fix n (pf:assume h (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) (pos-digits r)) n) h))))
     (pf:fix r (pf:fix n (pf:assume h (pf:all-elim (pf:known one-le) n))))))
!(pf:theorem &self zsucc-double-digits
   (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zsucc-double z))) (suc n)))))))
   (pf:induction int-ind
     (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zsucc-double z))) (suc n))))))
     (pf:fix n (pf:assume h (pf:all-elim (pf:known one-le) n)))
     (pf:fix r (pf:fix n (pf:assume h (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) (pos-digits r)) n) h))))
     (pf:fix r (pf:fix n (pf:assume h (pf:all-elim (pf:known one-le) n))))))
!(pf:theorem &self zpred-double-digits
   (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zpred-double z))) (suc n)))))))
   (pf:induction int-ind
     (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zpred-double z))) (suc n))))))
     (pf:fix n (pf:assume h (pf:all-elim (pf:known one-le) n)))
     (pf:fix r (pf:fix n (pf:assume h
       (pf:imp-elim
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:all-elim (pf:known le-trans) (pos-digits (pdouble-pred r))) (suc (pos-digits r))) (suc n))
           (pf:all-elim (pf:known pdouble-pred-digits) r))
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) (pos-digits r)) n) h)))))
     (pf:fix r (pf:fix n (pf:assume h (pf:all-elim (pf:known one-le) n))))))
!(pf:theorem &self zlow0-digits
   (all nat (lam m (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zlow0 m z))) (suc n)))))))))
   (pf:induction nat-ind
     (lam m (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zlow0 m z))) (suc n))))))))
     (pf:known zdouble-digits)
     (pf:fix q (pf:all-elim
       (pf:induction pos-ind
         (lam q (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zlow0-pos q z))) (suc n))))))))
         (pf:known zpred-double-digits)
         (pf:fix q (pf:assume ih (pf:known zdouble-digits)))
         (pf:fix q (pf:assume ih (pf:known zpred-double-digits))))
       q))))
!(pf:theorem &self zlow1-digits
   (all nat (lam m (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zlow1 m z))) (suc n)))))))))
   (pf:induction nat-ind
     (lam m (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zlow1 m z))) (suc n))))))))
     (pf:known zsucc-double-digits)
     (pf:fix q (pf:all-elim
       (pf:induction pos-ind
         (lam q (all int (lam z (all num (lam n (imp (le (pos-digits (pos-of-int z)) n) (le (pos-digits (pos-of-int (zlow1-pos q z))) (suc n))))))))
         (pf:known zdouble-digits)
         (pf:fix q (pf:assume ih (pf:known zsucc-double-digits)))
         (pf:fix q (pf:assume ih (pf:known zdouble-digits))))
       q))))
!(pf:theorem &self one-minus-digits
   (all nat (lam m (le (pos-digits (pos-of-int (one-minus m))) (suc zero))))
   (pf:induction nat-ind (lam m (le (pos-digits (pos-of-int (one-minus m))) (suc zero)))
     (pf:all-elim (pf:known le-refl) (suc zero))
     (pf:fix q (pf:all-elim
       (pf:induction pos-ind (lam q (le (pos-digits (pos-of-int (one-minus-pos q))) (suc zero)))
         (pf:all-elim (pf:known le-refl) (suc zero))
         (pf:fix q (pf:assume ih (pf:all-elim (pf:known le-refl) (suc zero))))
         (pf:fix q (pf:assume ih (pf:all-elim (pf:known le-refl) (suc zero)))))
       q))))
!(pf:theorem &self sub-digits
   (all pos (lam y (all nat (lam m (le (pos-digits (pos-of-int (psub-nat y m))) (pos-digits y))))))
   (pf:induction pos-ind (lam y (all nat (lam m (le (pos-digits (pos-of-int (psub-nat y m))) (pos-digits y)))))
     (pf:known one-minus-digits)
     (pf:fix y (pf:assume ih (pf:fix m
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known zlow0-digits) m) (psub-nat y (nhalf m))) (pos-digits y))
         (pf:all-elim ih (nhalf m))))))
     (pf:fix y (pf:assume ih (pf:fix m
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known zlow1-digits) m) (psub-nat y (nhalf m))) (pos-digits y))
         (pf:all-elim ih (nhalf m))))))))
!(pf:theorem &self pdiff-digits (all pos (lam y (all pos (lam x (le (pos-digits (pdiff y x)) (pos-digits y))))))
   (pf:fix y (pf:fix x (pf:all-elim (pf:all-elim (pf:known sub-digits) y) (npos x)))))
; Without a clause the equations are not read: the odd case calls gcd at a
; difference, which is not a field of either argument.
!(try (set:define &self pgcd-plain (-> ord pos pos pos)
   (= (pgcd-plain $c one $y) one)
   (= (pgcd-plain less (bit1 $x) (bit1 $y)) (pgcd-plain (pcmp (bit1 $x) (pdiff $y $x)) (bit1 $x) (pdiff $y $x)))))
; Five calls, five obligations, in the order of the equations.
!(set:define &self pgcd-by (-> ord pos pos pos)
   (by bound num (lam c (lam x (lam y (add (pos-digits x) (pos-digits y)))))
     (pf:fix c (pf:fix x (pf:fix y
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-below) (add (pos-digits x) (pos-digits y))) (add (suc (pos-digits x)) (pos-digits y)))
         (pf:all-elim
           (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-add-left) (pos-digits x)) (suc (pos-digits x)))
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-step) (pos-digits x)) (pos-digits x))
               (pf:all-elim (pf:known le-refl) (pos-digits x))))
           (pos-digits y))))))
     (pf:fix c (pf:fix x (pf:fix y
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-below) (suc (add (pos-digits x) (pos-digits y)))) (add (suc (pos-digits x)) (pos-digits y)))
         (pf:all-elim (pf:all-elim (pf:known le-suc-add) (pos-digits x)) (pos-digits y))))))
     (pf:fix c (pf:fix x (pf:fix y
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-below) (add (suc (pos-digits x)) (pos-digits y))) (add (suc (pos-digits x)) (pos-digits y)))
         (pf:all-elim (pf:known le-refl) (add (suc (pos-digits x)) (pos-digits y)))))))
     (pf:fix x (pf:fix y
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-below) (add (suc (pos-digits x)) (pos-digits (pdiff y x)))) (add (suc (pos-digits x)) (pos-digits y)))
         (pf:imp-elim
           (pf:all-elim (pf:all-elim (pf:all-elim (pf:known le-add-right) (suc (pos-digits x))) (pos-digits (pdiff y x))) (pos-digits y))
           (pf:all-elim (pf:all-elim (pf:known pdiff-digits) y) x)))))
     (pf:fix x (pf:fix y
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-below) (suc (add (pos-digits (pdiff x y)) (pos-digits y)))) (add (suc (pos-digits x)) (pos-digits y)))
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim (pf:all-elim (pf:all-elim (pf:known le-trans) (suc (add (pos-digits (pdiff x y)) (pos-digits y)))) (suc (add (pos-digits x) (pos-digits y)))) (add (suc (pos-digits x)) (pos-digits y)))
             (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-suc) (add (pos-digits (pdiff x y)) (pos-digits y))) (add (pos-digits x) (pos-digits y)))
               (pf:all-elim
                 (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known le-add-left) (pos-digits (pdiff x y))) (pos-digits x))
                   (pf:all-elim (pf:all-elim (pf:known pdiff-digits) x) y))
                 (pos-digits y))))
           (pf:all-elim (pf:all-elim (pf:known le-suc-add) (pos-digits x)) (pos-digits y)))))))
   (= (pgcd-by $c one $y) one)
   (= (pgcd-by $c (bit0 $x) one) one)
   (= (pgcd-by $c (bit1 $x) one) one)
   (= (pgcd-by $c (bit0 $x) (bit0 $y)) (bit0 (pgcd-by (pcmp $x $y) $x $y)))
   (= (pgcd-by $c (bit0 $x) (bit1 $y)) (pgcd-by (pcmp $x (bit1 $y)) $x (bit1 $y)))
   (= (pgcd-by $c (bit1 $x) (bit0 $y)) (pgcd-by (pcmp (bit1 $x) $y) (bit1 $x) $y))
   (= (pgcd-by same (bit1 $x) (bit1 $y)) (bit1 $x))
   (= (pgcd-by less (bit1 $x) (bit1 $y)) (pgcd-by (pcmp (bit1 $x) (pdiff $y $x)) (bit1 $x) (pdiff $y $x)))
   (= (pgcd-by more (bit1 $x) (bit1 $y)) (pgcd-by (pcmp (pdiff $x $y) (bit1 $y)) (pdiff $x $y) (bit1 $y))))
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self pgcd-by)
   (let (bound $type $measure $obligations) $termination
     (let $count (size-atom $obligations) (quote ((Bound $type $measure $count) $lean)))))
!(set:define &self pgcd (-> pos pos pos) (= (pgcd $x $y) (pgcd-by (pcmp $x $y) $x $y)))

; ---- Fractions in normal form: the numerator and the denominator divided
; by their gcd, the denominator positive by its type.
!(set:define &self qreduce (-> pos pos pos rat)
   (= (qreduce $g $p $d) (over (zpos (pquot $p $g)) (pquot $d $g))))
!(set:define &self qopp (-> rat rat) (= (qopp (over $a $b)) (over (zopp $a) $b)))
!(set:define &self qmake (-> int pos rat)
   (= (qmake z0 $d) (over z0 one))
   (= (qmake (zpos $p) $d) (qreduce (pgcd $p $d) $p $d))
   (= (qmake (zneg $p) $d) (qopp (qreduce (pgcd $p $d) $p $d))))
!(set:define &self qadd-over (-> rat int pos rat)
   (= (qadd-over (over $c $d) $a $b) (qmake (zadd (zmul $a (zpos $d)) (zmul $c (zpos $b))) (pmul $b $d))))
!(set:define &self qadd (-> rat rat rat) (= (qadd (over $a $b) $y) (qadd-over $y $a $b)))
!(set:define &self qsub (-> rat rat rat) (= (qsub $x $y) (qadd $x (qopp $y))))
!(set:define &self qmul-over (-> rat int pos rat)
   (= (qmul-over (over $c $d) $a $b) (qmake (zmul $a $c) (pmul $b $d))))
!(set:define &self qmul (-> rat rat rat) (= (qmul (over $a $b) $y) (qmul-over $y $a $b)))
!(set:define &self qcmp-over (-> rat int pos ord)
   (= (qcmp-over (over $c $d) $a $b) (zcmp (zmul $a (zpos $d)) (zmul $c (zpos $b)))))
!(set:define &self qcmp (-> rat rat ord) (= (qcmp (over $a $b) $y) (qcmp-over $y $a $b)))
!(set:define &self qpick-min (-> ord rat rat rat)
   (= (qpick-min less $x $y) $x) (= (qpick-min same $x $y) $x) (= (qpick-min more $x $y) $y))
!(set:define &self qpick-max (-> ord rat rat rat)
   (= (qpick-max less $x $y) $y) (= (qpick-max same $x $y) $y) (= (qpick-max more $x $y) $x))
!(set:define &self qmin (-> rat rat rat) (= (qmin $x $y) (qpick-min (qcmp $x $y) $x $y)))
!(set:define &self qmax (-> rat rat rat) (= (qmax $x $y) (qpick-max (qcmp $x $y) $x $y)))

; ---- Decimal notation, read in the language: a digit is a constructor, and
; (num3 d5 d4 d7) is 547.
!(set:inductive &self digit (u 0) (: d0 digit) (: d1 digit) (: d2 digit) (: d3 digit) (: d4 digit)
   (: d5 digit) (: d6 digit) (: d7 digit) (: d8 digit) (: d9 digit))
!(set:define &self digit-value (-> digit nat)
   (= (digit-value d0) n0)
   (= (digit-value d1) (npos one))
   (= (digit-value d2) (npos (bit0 one)))
   (= (digit-value d3) (npos (bit1 one)))
   (= (digit-value d4) (npos (bit0 (bit0 one))))
   (= (digit-value d5) (npos (bit1 (bit0 one))))
   (= (digit-value d6) (npos (bit0 (bit1 one))))
   (= (digit-value d7) (npos (bit1 (bit1 one))))
   (= (digit-value d8) (npos (bit0 (bit0 (bit0 one)))))
   (= (digit-value d9) (npos (bit1 (bit0 (bit0 one))))))
!(set:define &self dec (-> nat digit nat)
   (= (dec $high $d) (nadd (nmul (npos (bit0 (bit1 (bit0 one)))) $high) (digit-value $d))))
!(set:define &self num1 (-> digit nat) (= (num1 $a) (digit-value $a)))
!(set:define &self num2 (-> digit digit nat) (= (num2 $a $b) (dec (digit-value $a) $b)))
!(set:define &self num3 (-> digit digit digit nat) (= (num3 $a $b $c) (dec (num2 $a $b) $c)))
!(set:define &self num4 (-> digit digit digit digit nat) (= (num4 $a $b $c $d) (dec (num3 $a $b $c) $d)))
!(set:define &self num5 (-> digit digit digit digit digit nat)
   (= (num5 $a $b $c $d $e) (dec (num4 $a $b $c $d) $e)))
; n / d in normal form, and a whole number as a fraction.
!(set:define &self frac (-> nat nat rat) (= (frac $n $d) (qmake (int-of-nat $n) (pos-of-nat $d))))
!(set:define &self whole (-> nat rat) (= (whole $n) (over (int-of-nat $n) one)))
; The fraction n / d written directly in normal form, without a gcd: what
; the computed values are compared with.
!(set:define &self written (-> nat nat rat) (= (written $n $d) (over (int-of-nat $n) (pos-of-nat $d))))

; ---- Operational: they run.
; 7 + 5 = 12, 15 * 15 = 225, 7 - 12 = -5, 13 = 4 * 3 + 1, gcd 12 18 = 6.
!(padd (bit1 (bit1 one)) (bit1 (bit0 one)))
!(pmul (bit1 (bit1 (bit1 one))) (bit1 (bit1 (bit1 one))))
!(zsub (zpos (bit1 (bit1 one))) (zpos (bit0 (bit0 (bit1 one)))))
!(ndivmod (npos (bit1 (bit0 (bit1 one)))) (bit1 one))
!(pgcd (bit0 (bit0 (bit1 one))) (bit0 (bit1 (bit0 (bit0 one)))))
; min 9 4 = 4, max 9 4 = 9.
!(pmin (pos-of-nat (num1 d9)) (pos-of-nat (num1 d4)))
!(pmax (pos-of-nat (num1 d9)) (pos-of-nat (num1 d4)))
; 1/6 + 1/3 = 1/2, in normal form; 2/4 is 1/2; 1/2 is more than 1/3.
!(qadd (frac (num1 d1) (num1 d6)) (frac (num1 d1) (num1 d3)))
!(frac (num1 d2) (num1 d4))
!(qcmp (frac (num1 d1) (num1 d2)) (frac (num1 d1) (num1 d3)))
!(qmax (frac (num1 d1) (num1 d2)) (frac (num1 d1) (num1 d3)))
;
; ---- Intensional: the operations are typed, and equal by computation at
; closed numbers.
!(type:of &self padd)
!(type:of &self pgcd)
!(type:of &self qadd)
!(type:of &self (qmul (frac (num1 d1) (num1 d2)) (frac (num1 d2) (num1 d3))))
; 3 + 3 is 6 and not 5.
!(type:eq &self (padd (bit1 one) (bit1 one)) (bit0 (bit1 one)))
!(type:eq &self (padd (bit1 one) (bit1 one)) (bit1 (bit0 one)))
; 1/6 + 1/3 is 1/2, computed by the kernel through the gcd's bound-admitted
; equations.
!(type:eq &self (qadd (frac (num1 d1) (num1 d6)) (frac (num1 d1) (num1 d3))) (written (num1 d1) (num1 d2)))

; ---- Extensional: the value is the set the equations name.
!(set:eq &self (pmul (bit1 one) (bit1 one)) (bit1 (bit0 (bit0 one))))
!(set:eq &self (qadd (frac (num1 d1) (num1 d6)) (frac (num1 d1) (num1 d3))) (written (num1 d1) (num1 d2)))
; A fact proved once for every positive number: the digits of a difference
; are at most those of the number it is taken from.
!(pf:proves &self (pf:by pdiff-digits)
   (le (pos-digits (pdiff (bit0 (bit1 one)) (bit1 one))) (pos-digits (bit0 (bit1 one)))))

; ---- The value maps: binary computations agree with the unary ones.  In
; Lean the value of a sum is the sum of the values and the value of a
; product the product of the values, in the set model of the binary package
; (Trinity.Arith.Binary.padd_value, Trinity.Arith.Binary.pmul_value).
; 5 is five successors; 6 + 7 and 3 * 5 give the unary sum and product.
!(pval (bit1 (bit0 one)))
!(let $agree (== (pval (padd (bit0 (bit1 one)) (bit1 (bit1 one))))
                 (add (pval (bit0 (bit1 one))) (pval (bit1 (bit1 one)))))
   (quote ("Trinity.Arith.Binary.padd_value" $agree)))
!(let $agree (== (pval (pmul (bit1 one) (bit1 (bit0 one))))
                 (mul (pval (bit1 one)) (pval (bit1 (bit0 one)))))
   (quote ("Trinity.Arith.Binary.pmul_value" $agree)))
!(type:eq &self (pval (pmul (bit1 one) (bit1 (bit0 one)))) (mul (pval (bit1 one)) (pval (bit1 (bit0 one)))))
; 13 = 4 * 3 + 1 in unary too.
!(let (quot-rem $q $r) (ndivmod (npos (bit1 (bit0 (bit1 one)))) (bit1 one))
   (== (add (mul (nval $q) (suc (suc (suc zero)))) (nval $r)) (pval (bit1 (bit0 (bit1 one))))))
; A wrong carry, the last digit of 1 + 1 left as 1 (bit1 r for bit0 (psucc r)),
; is caught by the value map: its 3 + 1 is 3.
!(set:define &self low1-pos-wrong (-> pos pos pos)
   (= (low1-pos-wrong one $r) (bit1 $r))
   (= (low1-pos-wrong (bit0 $q) $r) (bit1 $r))
   (= (low1-pos-wrong (bit1 $q) $r) (bit0 (psucc $r))))
!(== (pval (low1-pos-wrong one one)) (add (pval (bit1 one)) (pval one)))

; ---- The laboratory of chapter 5.  The graphtropy of a positive policy on
; three objects is (25 + 20 ab + 12 bc + 15 ac) / 72
; (Laboratory.graphtropy_positivePolicy).  Each value is computed from its
; data in decimal, counted in firings of the written equations, and compared
; with the fraction the Lean theorem states, written in normal form.
!(set:define &self graphtropy (-> rat rat rat rat)
   (= (graphtropy $ab $bc $ac)
      (qmul (frac (num1 d1) (num2 d7 d2))
        (qadd (whole (num2 d2 d5))
          (qadd (qmul (whole (num2 d2 d0)) $ab)
            (qadd (qmul (whole (num2 d1 d2)) $bc) (qmul (whole (num2 d1 d5)) $ac)))))))
; The source's positive policy (0.9, 0.85, 0.1): 547/720.
!(let $expected (written (num3 d5 d4 d7) (num3 d7 d2 d0))
   (let (($value) $n) (cost:firings
        (graphtropy (frac (num1 d9) (num2 d1 d0)) (frac (num2 d1 d7) (num2 d2 d0)) (frac (num1 d1) (num2 d1 d0))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.initial_graphtropy" (fraction 547 720) $agree (firings $n))))))
; The receiver's (0.2, 0.3, 0.1): 341/720.
!(let $expected (written (num3 d3 d4 d1) (num3 d7 d2 d0))
   (let (($value) $n) (cost:firings
        (graphtropy (frac (num1 d1) (num1 d5)) (frac (num1 d3) (num2 d1 d0)) (frac (num1 d1) (num2 d1 d0))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.initial_graphtropy" (fraction 341 720) $agree (firings $n))))))
; The first as it is computed: numerator 1000100011 (547) and denominator
; 1011010000 (720) in binary, last digit outermost.
!(graphtropy (frac (num1 d9) (num2 d1 d0)) (frac (num2 d1 d7) (num2 d2 d0)) (frac (num1 d1) (num2 d1 d0)))
; The weight of the pair a c, 2 (5/12) (1/4) = 5/24 (Laboratory.measure_and_weights).
!(let $agree (== (qmul (whole (num1 d2)) (qmul (frac (num1 d5) (num2 d1 d2)) (frac (num1 d1) (num1 d4))))
                 (written (num1 d5) (num2 d2 d4)))
   (quote ("Laboratory.measure_and_weights" (fraction 5 24) $agree)))
; After transport the receiver is (0.81, 0.765, 0.1).  Its completions at a c:
; the metric one, max(0.1, 0.81 + 0.765 - 1) = 23/40, and the product one,
; max(0.1, 0.81 * 0.765) = 12393/20000 (Laboratory.transport_and_completion).
!(set:define &self metric-ac (-> rat rat rat rat)
   (= (metric-ac $ab $bc $ac)
      (qmax $ac (qmax (whole n0) (qsub (qadd $ab $bc) (whole (num1 d1)))))))
!(set:define &self product-ac (-> rat rat rat rat)
   (= (product-ac $ab $bc $ac) (qmax $ac (qmul $ab $bc))))
!(let $agree (== (metric-ac (frac (num2 d8 d1) (num3 d1 d0 d0)) (frac (num3 d1 d5 d3) (num3 d2 d0 d0)) (frac (num1 d1) (num2 d1 d0)))
                 (written (num2 d2 d3) (num2 d4 d0)))
   (quote ("Laboratory.transport_and_completion" (fraction 23 40) $agree)))
!(let $agree (== (product-ac (frac (num2 d8 d1) (num3 d1 d0 d0)) (frac (num3 d1 d5 d3) (num3 d2 d0 d0)) (frac (num1 d1) (num2 d1 d0)))
                 (written (num5 d1 d2 d3 d9 d3) (num5 d2 d0 d0 d0 d0)))
   (quote ("Laboratory.transport_and_completion" (fraction 12393 20000) $agree)))
; The balance: the metric completion of the transported receiver sits
; 4981/14400 above the initial receiver, and the product completion another
; 893/96000 above it (Laboratory.completion_balance), each a difference of
; two graphtropies.
!(let $expected (written (num4 d4 d9 d8 d1) (num5 d1 d4 d4 d0 d0))
   (let (($value) $n) (cost:firings
        (qsub (graphtropy (frac (num2 d8 d1) (num3 d1 d0 d0)) (frac (num3 d1 d5 d3) (num3 d2 d0 d0)) (frac (num2 d2 d3) (num2 d4 d0)))
              (graphtropy (frac (num1 d1) (num1 d5)) (frac (num1 d3) (num2 d1 d0)) (frac (num1 d1) (num2 d1 d0)))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.completion_balance" (fraction 4981 14400) $agree (firings $n))))))
!(let $expected (written (num3 d8 d9 d3) (num5 d9 d6 d0 d0 d0))
   (let (($value) $n) (cost:firings
        (qsub (graphtropy (frac (num2 d8 d1) (num3 d1 d0 d0)) (frac (num3 d1 d5 d3) (num3 d2 d0 d0)) (frac (num5 d1 d2 d3 d9 d3) (num5 d2 d0 d0 d0 d0)))
              (graphtropy (frac (num2 d8 d1) (num3 d1 d0 d0)) (frac (num3 d1 d5 d3) (num3 d2 d0 d0)) (frac (num2 d2 d3) (num2 d4 d0)))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.completion_balance" (fraction 893 96000) $agree (firings $n))))))
; The same balance as the source adds it: the transport's own gain 889/3600
; plus the completion's 5/24 (23/40 - 1/10) (Laboratory.transport_and_completion).
!(let $agree (== (qadd (frac (num3 d8 d8 d9) (num4 d3 d6 d0 d0))
                       (qmul (frac (num1 d5) (num2 d2 d4)) (qsub (frac (num2 d2 d3) (num2 d4 d0)) (frac (num1 d1) (num2 d1 d0)))))
                 (written (num4 d4 d9 d8 d1) (num5 d1 d4 d4 d0 d0)))
   (quote ("Laboratory.transport_and_completion" (fraction 4981 14400) $agree)))
!(let $agree (== (qmul (frac (num1 d5) (num2 d2 d4)) (qsub (frac (num5 d1 d2 d3 d9 d3) (num5 d2 d0 d0 d0 d0)) (frac (num2 d2 d3) (num2 d4 d0))))
                 (written (num3 d8 d9 d3) (num5 d9 d6 d0 d0 d0)))
   (quote ("Laboratory.transport_and_completion" (fraction 893 96000) $agree)))

; ---- Cost.  Unary multiplication takes a firing for each unit of the
; product; binary takes a number of firings that grows with the digits.
; 7 * 9 = 63 both ways.
!(let ($values $n) (cost:firings (mul (suc (suc (suc (suc (suc (suc (suc zero)))))))
                                       (suc (suc (suc (suc (suc (suc (suc (suc (suc zero)))))))))))
   (quote (unary 7 9 $n)))
!(let ($values $n) (cost:firings (pmul (bit1 (bit1 one)) (bit1 (bit0 (bit0 one)))))
   (quote (binary 7 9 $n)))
; 9 * 9 = 81: unary grows with the value, binary with the digits.  (A unary
; product much larger than this is already too deep a term to run here.)
!(let ($values $n) (cost:firings (mul (suc (suc (suc (suc (suc (suc (suc (suc (suc zero)))))))))
                                      (suc (suc (suc (suc (suc (suc (suc (suc (suc zero)))))))))))
   (quote (unary 9 9 $n)))
!(let ($values $n) (cost:firings (pmul (bit1 (bit0 (bit0 one))) (bit1 (bit0 (bit0 one)))))
   (quote (binary 9 9 $n)))
; (2^k - 1)^2 for k = 2, 4, 8, 16 digits.
!(let ($values $n) (cost:firings (pmul (bit1 one) (bit1 one))) (quote (binary-digits 2 $n)))
!(let ($values $n) (cost:firings (pmul (bit1 (bit1 (bit1 one))) (bit1 (bit1 (bit1 one)))))
   (quote (binary-digits 4 $n)))
!(let ($values $n) (cost:firings (pmul (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 one)))))))
                                       (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 one)))))))))
   (quote (binary-digits 8 $n)))
!(let ($values $n) (cost:firings (pmul (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 one)))))))))))))))
                                       (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 (bit1 one)))))))))))))))))
   (quote (binary-digits 16 $n)))

; ---- Negative controls.
; A fraction with denominator zero is not a fraction: the denominator is a
; positive number, and n0 is not one.  The call is refused with a type error,
; the kernel finds no type, and the type check says no.
!(over (zpos one) n0)
!(try (type:of &self (over (zpos one) n0)))
!(type:check &self (over (zpos one) n0) rat)
!(type:check &self (over (zpos one) (bit0 one)) rat)
; A recursion on an argument that is not smaller: Undetermined, with the
; call that repeats the left side and the routes that could admit it.
!(try (set:define &self spin (-> pos pos)
   (= (spin one) one)
   (= (spin (bit0 $p)) (spin (bit0 $p)))
   (= (spin (bit1 $p)) (spin $p))))
; A gcd that swaps its arguments: the calls do not go down the bound, and
; without proofs of the obligations the verdict states them, one per call.
!(let $r (try (set:define &self gcd-swap (-> pos pos pos)
   (by bound num (lam x (lam y (add (pos-digits x) (pos-digits y)))))
   (= (gcd-swap one $y) one)
   (= (gcd-swap (bit0 $x) $y) (gcd-swap $y (bit0 $x)))
   (= (gcd-swap (bit1 $x) $y) (gcd-swap $y (bit1 $x)))))
   (let $reason (index-atom $r 1) ((car-atom $r) (car-atom $reason) (size-atom (index-atom $reason 2)))))
; The obligation of the gcd's odd case given the proof of another: not admitted.
!(let $r (try (set:define &self gcd-short (-> ord pos pos pos)
   (by bound num (lam c (lam x (lam y (add (pos-digits x) (pos-digits y)))))
     (pf:known le-refl))
   (= (gcd-short $c one $y) one)
   (= (gcd-short less (bit1 $x) (bit1 $y)) (gcd-short (pcmp (bit1 $x) (pdiff $y $x)) (bit1 $x) (pdiff $y $x)))))
   (let $reason (index-atom $r 1) ((car-atom $r) (car-atom $reason))))
