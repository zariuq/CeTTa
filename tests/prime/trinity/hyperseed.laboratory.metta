; Hyperseed's d-calculus concepts as Prime programs over declared data, and
; the finite laboratory of chapter 5 recomputed from them.  Source: Goertzel,
; "Hyperseed in the d-Calculus", chapters 3 and 5; HS3.* and HS5.* are the
; chapters' identifiers.  The Lean names printed below are in
; Mettapedia.Cybernetics.DistinctionCalculus: Laboratory.* and
; EvidenceExamples.* are its two example namespaces, DistinctionCalculus.*
; names the rest.
;
; This file is not run alone.  It uses the binary numbers, fractions and
; decimal notation of arith.binary.metta, whose definitions
; tests/support/check_prime_hyperseed_laboratory.py places before it, and it
; runs twice: on the declared equations alone (hyperseed.laboratory.expected),
; and with the native arithmetic of arith.binary.oracle-declarations.metta
; declared in between (hyperseed.laboratory.oracle.expected).  The answers are
; the same both ways; the firing counts differ, and with the oracle every
; answer it computed is followed by its trust record.
;
; Nothing here is a closed formula for three objects: every quantity is a
; finite sum, maximum or iteration over declared lists, by structural
; recursion, and the laboratory is one input to it.

; ---- Decisions and fraction helpers.
!(set:inductive &self decision (u 0) (: yes decision) (: no decision))
!(set:define &self yes-and (-> decision decision decision)
   (= (yes-and yes $b) $b) (= (yes-and no $b) no))
!(set:define &self le-of (-> ord decision)
   (= (le-of less) yes) (= (le-of same) yes) (= (le-of more) no))
!(set:define &self same-of (-> ord decision)
   (= (same-of less) no) (= (same-of same) yes) (= (same-of more) no))
!(set:define &self qle (-> rat rat decision) (= (qle $x $y) (le-of (qcmp $x $y))))
!(set:define &self qsame (-> rat rat decision) (= (qsame $x $y) (same-of (qcmp $x $y))))
!(set:define &self in-unit (-> rat decision)
   (= (in-unit $x) (yes-and (qle (whole n0) $x) (qle $x (whole (npos one))))))
!(set:define &self qsquare (-> rat rat) (= (qsquare $x) (qmul $x $x)))
!(set:define &self qinv-over (-> int pos rat)
   (= (qinv-over z0 $d) (over z0 one))
   (= (qinv-over (zpos $p) $d) (over (zpos $d) $p))
   (= (qinv-over (zneg $p) $d) (over (zneg $d) $p)))
!(set:define &self qinv (-> rat rat) (= (qinv (over $z $d)) (qinv-over $z $d)))

; ---- HS3.Evidence.  A p-bit (p, n) is a pair of fractions, support and
; opposition to one claim, each in [0, 1], with no constraint p + n = 1.  The
; knowledge order is componentwise, the truth order reverses opposition,
; negation swaps the pair, and the four corners are T = (1, 0), F = (0, 1),
; B = (1, 1) and N = (0, 0).  Evidence accumulation is the componentwise
; maximum (the join of the knowledge order); the gain product scales each
; channel; truth disjunction is (max p p', min n n').
!(set:inductive &self pbit (u 0) (: pb (-> rat rat pbit)))
!(set:known-proposition &self pbit-ind)
!(pf:equality &self pbit refl@pbit subst@pbit)
!(set:define &self support (-> pbit rat) (= (support (pb $p $n)) $p))
!(set:define &self opposition (-> pbit rat) (= (opposition (pb $p $n)) $n))
!(set:define &self k-le (-> pbit pbit decision)
   (= (k-le (pb $p $n) $w) (yes-and (qle $p (support $w)) (qle $n (opposition $w)))))
!(set:define &self t-le (-> pbit pbit decision)
   (= (t-le (pb $p $n) $w) (yes-and (qle $p (support $w)) (qle (opposition $w) $n))))
!(set:define &self neg (-> pbit pbit) (= (neg (pb $p $n)) (pb $n $p)))
!(set:inductive &self corner (u 0)
   (: supported corner) (: opposed corner) (: conflicted corner) (: unknown corner))
!(set:define &self corner-value (-> corner pbit)
   (= (corner-value supported) (pb (whole (npos one)) (whole n0)))
   (= (corner-value opposed) (pb (whole n0) (whole (npos one))))
   (= (corner-value conflicted) (pb (whole (npos one)) (whole (npos one))))
   (= (corner-value unknown) (pb (whole n0) (whole n0))))
!(set:define &self k-join (-> pbit pbit pbit)
   (= (k-join (pb $p $n) $w) (pb (qmax $p (support $w)) (qmax $n (opposition $w)))))
!(set:define &self gain (-> pbit pbit pbit)
   (= (gain (pb $p $n) $w) (pb (qmul $p (support $w)) (qmul $n (opposition $w)))))
!(set:define &self truth-or (-> pbit pbit pbit)
   (= (truth-or (pb $p $n) $w) (pb (qmax $p (support $w)) (qmin $n (opposition $w)))))
; A pair of fractions is admitted as a p-bit only inside the square.
!(set:inductive &self square-check (u 0)
   (: in-square (-> pbit square-check)) (: outside-square (-> rat rat square-check)))
!(set:define &self square-verdict (-> decision rat rat square-check)
   (= (square-verdict yes $p $n) (in-square (pb $p $n)))
   (= (square-verdict no $p $n) (outside-square $p $n)))
!(set:define &self p-bit (-> rat rat square-check)
   (= (p-bit $p $n) (square-verdict (yes-and (in-unit $p) (in-unit $n)) $p $n)))
!(set:define &self pb-of (-> nat nat nat nat pbit) (= (pb-of $a $b $c $e) (pb (frac $a $b) (frac $c $e))))

; Negation is an involution, N (N v) = v, on all three faces: it runs, the
; kernel judges it (and does not judge N T to be T), and it is proved for
; every p-bit by induction (one constructor, so by its two fields).
!(neg (pb-of (num1 d1) (num1 d2) (num1 d1) (num1 d3)))
!(neg (neg (pb-of (num1 d1) (num1 d2) (num1 d1) (num1 d3))))
!(type:eq &self (neg (neg (corner-value supported))) (corner-value supported))
!(type:eq &self (neg (corner-value supported)) (corner-value supported))
!(pf:theorem &self neg-neg (all pbit (lam v (eq pbit (neg (neg v)) v)))
   (pf:induction pbit-ind (lam v (eq pbit (neg (neg v)) v))
     (pf:fix p (pf:fix n (pf:refl pbit (pb p n))))))
!(pf:proves &self (pf:by neg-neg) (eq pbit (neg (neg (corner-value opposed))) (corner-value opposed)))
; Negation swaps T and F and fixes B and N.
!(neg (corner-value supported))
!(neg (corner-value conflicted))
!(neg (corner-value unknown))
; The orders: N is below B in the knowledge order and F below T in the truth
; order, but F is not below T in the knowledge order.
!(k-le (corner-value unknown) (corner-value conflicted))
!(t-le (corner-value opposed) (corner-value supported))
!(k-le (corner-value opposed) (corner-value supported))
; Chapter 3's correction of its source: material implication N v or w sends
; T => F to F, not to B.
!(let $agree (== (truth-or (neg (corner-value supported)) (corner-value opposed)) (corner-value opposed))
   (quote ("DistinctionCalculus.PBit.materialImpl_supported_opposed" $agree)))
; Admission: (0.9, 0.8) is a p-bit; (3/2, 0) and (-1/10, 1/2) are refused.
!(p-bit (frac (num1 d9) (num2 d1 d0)) (frac (num1 d4) (num1 d5)))
!(p-bit (frac (num1 d3) (num1 d2)) (whole n0))
!(p-bit (qsub (whole n0) (frac (num1 d1) (num2 d1 d0))) (frac (num1 d1) (num1 d2)))

; ---- HS3.Context: observers on a declared finite carrier.  The tokens are a
; declared datatype; an edge between two tokens is a token too (chapter 3
; allows pair-tokens, and chapter 5's probes are the unordered pairs).  A
; carrier is a declared list of tokens; a probability vector is a list of
; fractions in the order of the carrier; a kernel is a table, a list of rows
; in that order.
!(set:inductive &self token (u 0)
   (: a token) (: b token) (: c token) (: d token) (: q token) (: edge (-> token token token)))
!(set:inductive &self carrier (u 0) (: no-token carrier) (: token-and (-> token carrier carrier)))
!(set:inductive &self rats (u 0) (: qnil rats) (: qcons (-> rat rats rats)))
!(set:inductive &self table (u 0) (: tnil table) (: trow (-> rats table table)))
!(set:inductive &self reports (u 0) (: pnil reports) (: pcons (-> pbit reports reports)))
!(set:define &self q-head (-> rats rat) (= (q-head qnil) (whole n0)) (= (q-head (qcons $x $xs)) $x))
!(set:define &self q-tail (-> rats rats) (= (q-tail qnil) qnil) (= (q-tail (qcons $x $xs)) $xs))
!(set:define &self t-head (-> table rats) (= (t-head tnil) qnil) (= (t-head (trow $r $rs)) $r))
!(set:define &self t-tail (-> table table) (= (t-tail tnil) tnil) (= (t-tail (trow $r $rs)) $rs))
!(set:define &self p-head (-> reports pbit)
   (= (p-head pnil) (pb (whole n0) (whole n0))) (= (p-head (pcons $v $vs)) $v))
!(set:define &self p-tail (-> reports reports) (= (p-tail pnil) pnil) (= (p-tail (pcons $v $vs)) $vs))
!(set:define &self q-nth (-> num rats rat)
   (= (q-nth zero $l) (q-head $l)) (= (q-nth (suc $k) $l) (q-nth $k (q-tail $l))))
!(set:define &self t-nth (-> num table rats)
   (= (t-nth zero $t) (t-head $t)) (= (t-nth (suc $k) $t) (t-nth $k (t-tail $t))))
!(set:define &self p-nth (-> num reports pbit)
   (= (p-nth zero $l) (p-head $l)) (= (p-nth (suc $k) $l) (p-nth $k (p-tail $l))))
!(set:define &self carrier-size (-> carrier num)
   (= (carrier-size no-token) zero) (= (carrier-size (token-and $x $xs)) (suc (carrier-size $xs))))
!(set:define &self q-length (-> rats num)
   (= (q-length qnil) zero) (= (q-length (qcons $x $xs)) (suc (q-length $xs))))
!(set:define &self t-length (-> table num)
   (= (t-length tnil) zero) (= (t-length (trow $r $rs)) (suc (t-length $rs))))
!(set:define &self q-sum (-> rats rat)
   (= (q-sum qnil) (whole n0)) (= (q-sum (qcons $x $xs)) (qadd $x (q-sum $xs))))
!(set:define &self q-scale (-> rats rat rats)
   (= (q-scale qnil $s) qnil) (= (q-scale (qcons $x $xs) $s) (qcons (qmul $s $x) (q-scale $xs $s))))
!(set:define &self rats-same (-> rats rats decision)
   (= (rats-same qnil $l) yes)
   (= (rats-same (qcons $x $xs) $l) (yes-and (qsame $x (q-head $l)) (rats-same $xs (q-tail $l)))))
!(set:define &self table-same (-> table table decision)
   (= (table-same tnil $t) yes)
   (= (table-same (trow $r $rs) $t) (yes-and (rats-same $r (t-head $t)) (table-same $rs (t-tail $t)))))
!(set:define &self is-zero (-> num decision) (= (is-zero zero) yes) (= (is-zero (suc $n)) no))
!(set:define &self is-positive (-> num decision) (= (is-positive zero) no) (= (is-positive (suc $n)) yes))
!(set:define &self num-pred (-> num num) (= (num-pred zero) zero) (= (num-pred (suc $n)) $n))
!(set:define &self num-same (-> num num decision)
   (= (num-same zero $n) (is-zero $n))
   (= (num-same (suc $m) $n) (yes-and (is-positive $n) (num-same $m (num-pred $n)))))
!(set:define &self nat-of-num (-> num nat)
   (= (nat-of-num zero) n0) (= (nat-of-num (suc $k)) (nsucc (nat-of-num $k))))
!(set:define &self ones (-> carrier rats)
   (= (ones no-token) qnil) (= (ones (token-and $x $xs)) (qcons (whole (npos one)) (ones $xs))))
; The uniform measure of a carrier, 1/|X| at every token.
!(set:define &self uniform (-> carrier rats)
   (= (uniform $X) (q-scale (ones $X) (qinv (whole (nat-of-num (carrier-size $X)))))))

; Admission of an observer (X, mu, alpha) checks, by computation, that mu and
; alpha have the carrier's shape, that mu is nonnegative and sums to 1, and
; that alpha has entries in [0, 1], is reflexive (alpha(x, x) = 1) and is
; symmetric.  A refusal names the first defect and the tokens where it is.
!(set:inductive &self defect (u 0)
   (: wrong-shape defect)
   (: negative-mass (-> token defect))
   (: mass-not-one (-> rat defect))
   (: outside-unit (-> token token defect))
   (: not-reflexive (-> token defect))
   (: not-symmetric (-> token token defect)))
!(set:inductive &self finding (u 0) (: no-defect finding) (: found (-> defect finding)))
!(set:define &self first-finding (-> finding finding finding)
   (= (first-finding no-defect $g) $g) (= (first-finding (found $e) $g) (found $e)))
!(set:define &self unless-holds (-> decision defect finding)
   (= (unless-holds yes $e) no-defect) (= (unless-holds no $e) (found $e)))
!(set:define &self rows-fit (-> table num decision)
   (= (rows-fit tnil $n) yes)
   (= (rows-fit (trow $r $rs) $n) (yes-and (num-same (q-length $r) $n) (rows-fit $rs $n))))
!(set:define &self shape-finding (-> carrier rats table finding)
   (= (shape-finding $X $mu $alpha)
      (unless-holds
        (yes-and (num-same (q-length $mu) (carrier-size $X))
          (yes-and (num-same (t-length $alpha) (carrier-size $X)) (rows-fit $alpha (carrier-size $X))))
        wrong-shape)))
!(set:define &self mass-findings (-> carrier rats finding)
   (= (mass-findings no-token $mu) no-defect)
   (= (mass-findings (token-and $x $xs) $mu)
      (first-finding (unless-holds (qle (whole n0) (q-head $mu)) (negative-mass $x))
        (mass-findings $xs (q-tail $mu)))))
!(set:define &self measure-finding (-> carrier rats finding)
   (= (measure-finding $X $mu)
      (first-finding (mass-findings $X $mu)
        (unless-holds (qsame (q-sum $mu) (whole (npos one))) (mass-not-one (q-sum $mu))))))
!(set:define &self row-findings (-> carrier rats num token num table finding)
   (= (row-findings no-token $row $j $x $i $alpha) no-defect)
   (= (row-findings (token-and $y $ys) $row $j $x $i $alpha)
      (first-finding (unless-holds (in-unit (q-head $row)) (outside-unit $x $y))
        (first-finding
          (unless-holds (qsame (q-head $row) (q-nth $i (t-nth $j $alpha))) (not-symmetric $x $y))
          (row-findings $ys (q-tail $row) (suc $j) $x $i $alpha)))))
!(set:define &self kernel-findings (-> carrier table num carrier table finding)
   (= (kernel-findings no-token $rows $i $Y $alpha) no-defect)
   (= (kernel-findings (token-and $x $xs) $rows $i $Y $alpha)
      (first-finding
        (unless-holds (qsame (q-nth $i (t-head $rows)) (whole (npos one))) (not-reflexive $x))
        (first-finding (row-findings $Y (t-head $rows) zero $x $i $alpha)
          (kernel-findings $xs (t-tail $rows) (suc $i) $Y $alpha)))))
!(set:inductive &self observer (u 0) (: observer-of (-> carrier rats table observer)))
!(set:inductive &self observer-check (u 0)
   (: admitted-observer (-> observer observer-check)) (: refused-observer (-> defect observer-check)))
!(set:define &self observer-verdict (-> finding carrier rats table observer-check)
   (= (observer-verdict no-defect $X $mu $alpha) (admitted-observer (observer-of $X $mu $alpha)))
   (= (observer-verdict (found $e) $X $mu $alpha) (refused-observer $e)))
!(set:define &self admitted (-> observer-check decision)
   (= (admitted (admitted-observer $o)) yes) (= (admitted (refused-observer $e)) no))
!(set:define &self observe (-> carrier rats table observer-check)
   (= (observe $X $mu $alpha)
      (observer-verdict
        (first-finding (shape-finding $X $mu $alpha)
          (first-finding (measure-finding $X $mu) (kernel-findings $X $alpha zero $X $alpha)))
        $X $mu $alpha)))
!(set:define &self measure-check (-> carrier rats finding)
   (= (measure-check $X $mu)
      (first-finding (unless-holds (num-same (q-length $mu) (carrier-size $X)) wrong-shape)
        (measure-finding $X $mu))))
; A kernel on three tokens from its pair entries ab, bc, ac, with unit diagonal.
!(set:define &self table3 (-> rat rat rat table)
   (= (table3 $ab $bc $ac)
      (trow (qcons (whole (npos one)) (qcons $ab (qcons $ac qnil)))
        (trow (qcons $ab (qcons (whole (npos one)) (qcons $bc qnil)))
          (trow (qcons $ac (qcons $bc (qcons (whole (npos one)) qnil))) tnil)))))
!(set:define &self carrier1 (-> token carrier) (= (carrier1 $x) (token-and $x no-token)))
!(set:define &self carrier3 (-> token token token carrier)
   (= (carrier3 $x $y $z) (token-and $x (token-and $y (carrier1 $z)))))
!(set:define &self carrier4 (-> token token token token carrier)
   (= (carrier4 $w $x $y $z) (token-and $w (carrier3 $x $y $z))))
!(set:define &self rats3 (-> rat rat rat rats)
   (= (rats3 $x $y $z) (qcons $x (qcons $y (qcons $z qnil)))))

; Admitted: the uniform measure on a, b, c with the kernel ab = bc = 4/5,
; ac = 1/10 (chapter 3's raw sorites).
!(observe (carrier3 a b c) (uniform (carrier3 a b c))
   (table3 (frac (num1 d4) (num1 d5)) (frac (num1 d4) (num1 d5)) (frac (num1 d1) (num2 d1 d0))))
; Refused: a kernel whose entry at a c is not its entry at c a; one whose
; diagonal at b is 9/10; one with an entry above 1; a measure summing to 7/8;
; a measure on two tokens for a carrier of three.
!(observe (carrier3 a b c) (uniform (carrier3 a b c))
   (trow (rats3 (whole (npos one)) (frac (num1 d4) (num1 d5)) (frac (num1 d1) (num2 d1 d0)))
     (trow (rats3 (frac (num1 d4) (num1 d5)) (whole (npos one)) (frac (num1 d4) (num1 d5)))
       (trow (rats3 (frac (num1 d1) (num1 d5)) (frac (num1 d4) (num1 d5)) (whole (npos one))) tnil))))
!(observe (carrier3 a b c) (uniform (carrier3 a b c))
   (trow (rats3 (whole (npos one)) (frac (num1 d4) (num1 d5)) (frac (num1 d1) (num2 d1 d0)))
     (trow (rats3 (frac (num1 d4) (num1 d5)) (frac (num1 d9) (num2 d1 d0)) (frac (num1 d4) (num1 d5)))
       (trow (rats3 (frac (num1 d1) (num2 d1 d0)) (frac (num1 d4) (num1 d5)) (whole (npos one))) tnil))))
!(observe (carrier3 a b c) (uniform (carrier3 a b c))
   (table3 (frac (num1 d4) (num1 d5)) (frac (num1 d3) (num1 d2)) (frac (num1 d1) (num2 d1 d0))))
!(observe (carrier3 a b c) (rats3 (frac (num1 d1) (num1 d2)) (frac (num1 d1) (num1 d4)) (frac (num1 d1) (num1 d8)))
   (table3 (frac (num1 d4) (num1 d5)) (frac (num1 d4) (num1 d5)) (frac (num1 d1) (num2 d1 d0))))
!(observe (carrier3 a b c) (qcons (frac (num1 d1) (num1 d2)) (qcons (frac (num1 d1) (num1 d2)) qnil))
   (table3 (frac (num1 d4) (num1 d5)) (frac (num1 d4) (num1 d5)) (frac (num1 d1) (num2 d1 d0))))

; ---- HS3.Bracket.  <mu|K|nu> = sum over x, y of mu(x) K(x, y) nu(y), a double
; sum over the carrier's lists; graphtropy g = <mu|alpha|mu> and logical
; entropy h = 1 - g.
!(set:define &self dot (-> rats rats rat)
   (= (dot qnil $nu) (whole n0))
   (= (dot (qcons $k $ks) $nu) (qadd (qmul $k (q-head $nu)) (dot $ks (q-tail $nu)))))
!(set:define &self bracket (-> rats table rats rat)
   (= (bracket qnil $K $nu) (whole n0))
   (= (bracket (qcons $m $ms) $K $nu) (qadd (qmul $m (dot (t-head $K) $nu)) (bracket $ms (t-tail $K) $nu))))
!(set:define &self graphtropy (-> rats table rat) (= (graphtropy $mu $alpha) (bracket $mu $alpha $mu)))
!(set:define &self logical-entropy (-> rats table rat)
   (= (logical-entropy $mu $alpha) (qsub (whole (npos one)) (graphtropy $mu $alpha))))
!(set:define &self graphtropy-of (-> observer rat)
   (= (graphtropy-of (observer-of $X $mu $alpha)) (graphtropy $mu $alpha)))
!(set:define &self observer-graphtropy (-> observer-check rat)
   (= (observer-graphtropy (admitted-observer $o)) (graphtropy-of $o))
   (= (observer-graphtropy (refused-observer $e)) (whole n0)))
; The identity kernel of a carrier, and 1 - K entrywise.
!(set:define &self zeros (-> carrier rats)
   (= (zeros no-token) qnil) (= (zeros (token-and $x $xs)) (qcons (whole n0) (zeros $xs))))
!(set:define &self shift-rows (-> table table)
   (= (shift-rows tnil) tnil)
   (= (shift-rows (trow $r $rs)) (trow (qcons (whole n0) $r) (shift-rows $rs))))
!(set:define &self identity-table (-> carrier table)
   (= (identity-table no-token) tnil)
   (= (identity-table (token-and $x $xs))
      (trow (qcons (whole (npos one)) (zeros $xs)) (shift-rows (identity-table $xs)))))
!(set:define &self complement-row (-> rats rats)
   (= (complement-row qnil) qnil)
   (= (complement-row (qcons $x $xs)) (qcons (qsub (whole (npos one)) $x) (complement-row $xs))))
!(set:define &self complement (-> table table)
   (= (complement tnil) tnil) (= (complement (trow $r $rs)) (trow (complement-row $r) (complement $rs))))
!(type:of &self graphtropy)
; The sorites of chapter 3 (uniform measure, ab = bc = 4/5, ac = 1/10):
; g = 32/45, and h = 13/45 both as 1 - g and as <mu|1 - alpha|mu>.
!(let $sorites (table3 (frac (num1 d4) (num1 d5)) (frac (num1 d4) (num1 d5)) (frac (num1 d1) (num2 d1 d0)))
   (let $g (observer-graphtropy (observe (carrier3 a b c) (uniform (carrier3 a b c)) $sorites))
     (let $h (bracket (uniform (carrier3 a b c)) (complement $sorites) (uniform (carrier3 a b c)))
       (let $agree (== ($g $h (logical-entropy (uniform (carrier3 a b c)) $sorites))
                       ((written (num2 d3 d2) (num2 d4 d5)) (written (num2 d1 d3) (num2 d4 d5))
                        (written (num2 d1 d3) (num2 d4 d5))))
         (quote ("EvidenceExamples.sorites_cost" "DistinctionCalculus.Distribution.distinction_eq_one_sub" $agree))))))
; Any finite carrier: on four tokens with the uniform measure, the identity
; observer has g = sum of mu(x)^2 = 1/4, the total observer g = 1.
!(set:define &self ones-table (-> carrier carrier table)
   (= (ones-table no-token $Y) tnil) (= (ones-table (token-and $x $xs) $Y) (trow (ones $Y) (ones-table $xs $Y))))
!(let $X (carrier4 a b c d)
   (let $agree (== ((graphtropy (uniform $X) (identity-table $X)) (graphtropy (uniform $X) (ones-table $X $X)))
                   ((written (num1 d1) (num1 d4)) (written (num1 d1) (num1 d1))))
     (quote ("DistinctionCalculus.graphtropy_discrete" $agree))))

; ---- HS3.ReportDistance.  The squared report distance of tokens x, y on
; probes with weights rho: 1/2 sum over q of rho(q) [(p_x(q) - p_y(q))^2 +
; (n_x(q) - n_y(q))^2].  It stays squared, so it stays a fraction; the
; distance is its root (DistinctionCalculus.reportDistance_sq).  A token's
; reports are a list over the probes; a profile lists them over the carrier.
!(set:define &self pbit-dist-sq (-> pbit pbit rat)
   (= (pbit-dist-sq (pb $p $n) $w)
      (qmul (frac (num1 d1) (num1 d2)) (qadd (qsquare (qsub $p (support $w))) (qsquare (qsub $n (opposition $w)))))))
!(set:define &self report-dist-sq (-> rats reports reports rat)
   (= (report-dist-sq qnil $s $t) (whole n0))
   (= (report-dist-sq (qcons $r $rho) $s $t)
      (qadd (qmul $r (pbit-dist-sq (p-head $s) (p-head $t))) (report-dist-sq $rho (p-tail $s) (p-tail $t)))))
!(set:inductive &self profiles (u 0) (: no-profile profiles) (: profile-and (-> reports profiles profiles)))
!(set:define &self report-row (-> profiles rats reports rats)
   (= (report-row no-profile $rho $ex) qnil)
   (= (report-row (profile-and $ey $es) $rho $ex)
      (qcons (qsub (whole (npos one)) (report-dist-sq $rho $ex $ey)) (report-row $es $rho $ex))))
; The table 1 - d^2 of a profile (DistinctionCalculus.squaredReportTolerance).
!(set:define &self squared-report-table (-> profiles rats profiles table)
   (= (squared-report-table no-profile $rho $all) tnil)
   (= (squared-report-table (profile-and $ex $es) $rho $all)
      (trow (report-row $all $rho $ex) (squared-report-table $es $rho $all))))
!(set:define &self one-probe (-> pbit reports) (= (one-probe $v) (pcons $v pnil)))
!(set:define &self profile3 (-> pbit pbit pbit profiles)
   (= (profile3 $u $v $w)
      (profile-and (one-probe $u) (profile-and (one-probe $v) (profile-and (one-probe $w) no-profile)))))
; Positive: equal reports at distinct tokens.  a and b both report N on the
; probe q: their squared distance is 0, so the observer the reports generate
; on a, b is admitted with alpha(a, b) = 1; two tokens, one point of the
; report quotient.
!(let $e (profile-and (one-probe (corner-value unknown)) (profile-and (one-probe (corner-value unknown)) no-profile))
   (let $alpha (squared-report-table $e (uniform (carrier1 q)) $e)
     (let $agree (== ((report-dist-sq (uniform (carrier1 q)) (one-probe (corner-value unknown)) (one-probe (corner-value unknown)))
                      (q-nth (suc zero) (t-head $alpha)))
                     ((written n0 (num1 d1)) (written (num1 d1) (num1 d1))))
       (let $admitted (admitted (observe (token-and a (carrier1 b)) (uniform (token-and a (carrier1 b))) $alpha))
         (quote ((tokens a b) (distance-squared 0) (admitted $admitted) $agree
                 "EvidenceExamples.equal_reports_distinct_tokens"))))))
; Positive: the three reports T, (1/2, 1/2), F have squared distances 1/4,
; 1/4 and 1.
!(let $e (profile3 (corner-value supported) (pb-of (num1 d1) (num1 d2) (num1 d1) (num1 d2)) (corner-value opposed))
   (let $agree (== (squared-report-table $e (uniform (carrier1 q)) $e)
                   (table3 (frac (num1 d3) (num1 d4)) (frac (num1 d3) (num1 d4)) (whole n0)))
     (quote ("EvidenceExamples.threeReports_distSq" $agree))))

; ---- HS5.03, HS5.04, HS3.Accumulate, HS3.08: path closures.  A path law
; composes a gain with the signal it carries: Lukasiewicz max(0, s + t - 1),
; product s t, minimum min(s, t).  One accumulation step is
; F_A(C) = C or (A . C), with (A . C)(x, y) = max over z of law(A(x, z), C(z, y));
; the closure starts from the identity and takes |carrier| - 1 steps, a
; recursion on that number, so it is structural.
!(set:inductive &self path-law (u 0) (: lukasiewicz path-law) (: product path-law) (: bottleneck path-law))
!(set:define &self law-op (-> path-law rat rat rat)
   (= (law-op lukasiewicz $s $t) (qmax (whole n0) (qsub (qadd $s $t) (whole (npos one)))))
   (= (law-op product $s $t) (qmul $s $t))
   (= (law-op bottleneck $s $t) (qmin $s $t)))
!(set:define &self scale-row (-> rats path-law rat rats)
   (= (scale-row qnil $L $s) qnil)
   (= (scale-row (qcons $t $ts) $L $s) (qcons (law-op $L $s $t) (scale-row $ts $L $s))))
!(set:define &self row-max (-> rats rats rats)
   (= (row-max qnil $r) $r)
   (= (row-max (qcons $x $xs) $r) (qcons (qmax $x (q-head $r)) (row-max $xs (q-tail $r)))))
!(set:define &self transmit (-> rats path-law table rats)
   (= (transmit qnil $L $C) qnil)
   (= (transmit (qcons $s $ss) $L $C) (row-max (transmit $ss $L (t-tail $C)) (scale-row (t-head $C) $L $s))))
!(set:define &self accumulate-rows (-> table path-law table table table)
   (= (accumulate-rows tnil $L $C $rest) tnil)
   (= (accumulate-rows (trow $arow $arows) $L $C $rest)
      (trow (row-max (t-head $rest) (transmit $arow $L $C)) (accumulate-rows $arows $L $C (t-tail $rest)))))
!(set:define &self accumulate (-> path-law table table table)
   (= (accumulate $L $A $C) (accumulate-rows $A $L $C $C)))
!(set:define &self iterate (-> num path-law table table table)
   (= (iterate zero $L $A $C) $C)
   (= (iterate (suc $k) $L $A $C) (iterate $k $L $A (accumulate $L $A $C))))
!(set:define &self steps (-> carrier num)
   (= (steps no-token) zero) (= (steps (token-and $x $xs)) (carrier-size $xs)))
!(set:define &self path-closure (-> path-law carrier table table)
   (= (path-closure $L $X $A) (iterate (steps $X) $L $A (identity-table $X))))
!(type:of &self path-closure)
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self iterate) (quote $termination))
; Shortest paths on d = 1 - alpha, by the same bounded relaxation with
; minimum and sum: the Lukasiewicz closure is 1 minus them.
!(set:define &self plus-row (-> rats rat rats)
   (= (plus-row qnil $s) qnil) (= (plus-row (qcons $t $ts) $s) (qcons (qadd $s $t) (plus-row $ts $s))))
!(set:define &self row-min (-> rats rats rats)
   (= (row-min qnil $r) $r)
   (= (row-min (qcons $x $xs) $r) (qcons (qmin $x (q-head $r)) (row-min $xs (q-tail $r)))))
!(set:define &self relax (-> rats table rats)
   (= (relax qnil $D) qnil)
   (= (relax (qcons $s $ss) $D) (row-min (relax $ss (t-tail $D)) (plus-row (t-head $D) $s))))
!(set:define &self relax-rows (-> table table table table)
   (= (relax-rows tnil $D $rest) tnil)
   (= (relax-rows (trow $drow $drows) $D $rest)
      (trow (row-min (t-head $rest) (relax $drow $D)) (relax-rows $drows $D (t-tail $rest)))))
!(set:define &self shortest (-> num table table table)
   (= (shortest zero $d $D) $D)
   (= (shortest (suc $k) $d $D) (shortest $k $d (relax-rows $d $D $D))))
!(set:define &self shortest-paths (-> carrier table table)
   (= (shortest-paths $X $d) (shortest (steps $X) $d $d)))
; A tolerance is metric exactly when its Lukasiewicz closure leaves it as it
; is: the closure is the least metric tolerance above it.
!(set:define &self metric (-> carrier table decision)
   (= (metric $X $alpha) (table-same (path-closure lukasiewicz $X $alpha) $alpha)))

; The sorites of chapter 3 again.  Metric closure raises ac from 1/10 to 3/5,
; which is 1 minus the shortest path a b c, 1/5 + 1/5; the raw kernel is not
; metric (its closure moves it) and the closure is; the closure costs 1/9 of
; expected indistinction; the minimum closure raises ac to 4/5, with g = 13/15.
!(let $sorites (table3 (frac (num1 d4) (num1 d5)) (frac (num1 d4) (num1 d5)) (frac (num1 d1) (num2 d1 d0)))
   (let $closed (path-closure lukasiewicz (carrier3 a b c) $sorites)
     (let $expected (table3 (frac (num1 d4) (num1 d5)) (frac (num1 d4) (num1 d5)) (frac (num1 d3) (num1 d5)))
       (let $agree-closure (== ($closed (complement (shortest-paths (carrier3 a b c) (complement $sorites)))) ($expected $expected))
         (let $raw-metric (table-same $closed $sorites)
           (let $closed-metric (metric (carrier3 a b c) $closed)
             (let $agree-g (== ((qsub (graphtropy (uniform (carrier3 a b c)) $closed) (graphtropy (uniform (carrier3 a b c)) $sorites))
                                (graphtropy (uniform (carrier3 a b c)) (path-closure bottleneck (carrier3 a b c) $sorites)))
                               ((written (num1 d1) (num1 d9)) (written (num2 d1 d3) (num2 d1 d5))))
               (quote ((closure-ac 3/5 $agree-closure "EvidenceExamples.sorites_closure" "DistinctionCalculus.lukasiewicz_closure_eq")
                       (metric (raw $raw-metric) (closed $closed-metric) "DistinctionCalculus.metric_closure")
                       (cost 1/9 minimum-closure-g 13/15 $agree-g "EvidenceExamples.sorites_cost" "Laboratory.sorites_ultrametric"))))))))))
; Negative: without the root, report distances are not metric.  The reports
; N, (1/2, 1/2), B have squared distances 1/4, 1/4, 1, and 1 > 1/4 + 1/4.
!(let $e (profile3 (corner-value unknown) (pb-of (num1 d1) (num1 d2) (num1 d1) (num1 d2)) (corner-value conflicted))
   (let $verdict (metric (carrier3 a b c) (squared-report-table $e (uniform (carrier1 q)) $e))
     (quote (squared-report-metric $verdict "EvidenceExamples.squaredReport_not_metric"))))
; The stopping bound: on the chain a - b - c - d with links 9/10, the product
; closure needs |carrier| - 1 = 3 steps to reach a d = 729/1000.  After 2
; steps the table is not yet stable; after 3 one more step changes nothing.
!(set:define &self chain4 (-> rat table)
   (= (chain4 $s)
      (trow (qcons (whole (npos one)) (rats3 $s (whole n0) (whole n0)))
        (trow (qcons $s (rats3 (whole (npos one)) $s (whole n0)))
          (trow (qcons (whole n0) (rats3 $s (whole (npos one)) $s))
            (trow (qcons (whole n0) (rats3 (whole n0) $s (whole (npos one)))) tnil))))))
!(let $A (chain4 (frac (num1 d9) (num2 d1 d0)))
   (let $early (iterate (suc (suc zero)) product $A (identity-table (carrier4 a b c d)))
     (let $closed (path-closure product (carrier4 a b c d) $A)
       (let $agree (== (q-nth (suc (suc (suc zero))) (t-head $closed)) (written (num3 d7 d2 d9) (num4 d1 d0 d0 d0)))
         (let $after-2 (table-same (accumulate product $A $early) $early)
           (let $after-3 (table-same (accumulate product $A $closed) $closed)
             (quote ((stable-after-2 $after-2) (stable-after-3 $after-3)
                     (a-d 729/1000 $agree) "DistinctionCalculus.iterate_stable"
                     "DistinctionCalculus.accumulate_propagationClosure"))))))))

; ---- The laboratory of chapter 5.  Objects a, b, c; probes the unordered
; pairs ab, bc, ac in that order; the source context C1 and the initial
; receiver C2^0 report on the probes; importance (1, 4/5, 3/5); the transfer
; gain (0.9, 0.9).  Under the positive-support policy the observer of a
; context has alpha(x, x) = 1 and alpha(x, y) = the support of its report on
; the pair.
!(set:inductive &self context (u 0) (: source context) (: receiver context))
!(set:define &self objects carrier (= objects (carrier3 a b c)))
!(set:define &self probes carrier
   (= probes (carrier3 (edge a b) (edge b c) (edge a c))))
!(set:define &self importance rats
   (= importance (rats3 (whole (npos one)) (frac (num1 d4) (num1 d5)) (frac (num1 d3) (num1 d5)))))
!(set:define &self reports-of (-> context reports)
   (= (reports-of source)
      (pcons (pb-of (num1 d9) (num2 d1 d0) (num1 d4) (num1 d5))
        (pcons (pb-of (num2 d1 d7) (num2 d2 d0) (num1 d1) (num1 d5))
          (pcons (pb-of (num1 d1) (num2 d1 d0) (num1 d7) (num2 d1 d0)) pnil))))
   (= (reports-of receiver)
      (pcons (pb-of (num1 d1) (num1 d5) (num1 d7) (num2 d1 d0))
        (pcons (pb-of (num1 d3) (num2 d1 d0) (num1 d3) (num1 d5))
          (pcons (pb-of (num1 d1) (num2 d1 d0) (num1 d4) (num1 d5)) pnil)))))
!(set:define &self transfer-gain pbit
   (= transfer-gain (pb-of (num1 d9) (num2 d1 d0) (num1 d9) (num2 d1 d0))))
; The declared measure normalizes the importance.
!(set:define &self normalize (-> rats rats) (= (normalize $w) (q-scale $w (qinv (q-sum $w)))))
!(set:define &self measure rats (= measure (normalize importance)))
!(set:define &self positive-policy (-> reports table)
   (= (positive-policy $r)
      (table3 (support (p-nth zero $r)) (support (p-nth (suc zero) $r)) (support (p-nth (suc (suc zero)) $r)))))
; The fixed-source gate (HS5.06): G(t)_e = t_e joined with k s_e, per probe.
!(set:define &self gate (-> reports pbit reports reports)
   (= (gate pnil $k $t) pnil)
   (= (gate (pcons $s $ss) $k $t) (pcons (k-join (p-head $t) (gain $k $s)) (gate $ss $k (p-tail $t)))))
!(set:define &self transported reports
   (= transported (gate (reports-of source) transfer-gain (reports-of receiver))))
; The weight of the pair x_i x_j, 2 mu(x_i) mu(x_j).
!(set:define &self pair-weight (-> rats num num rat)
   (= (pair-weight $mu $i $j) (qmul (whole (npos (bit0 one))) (qmul (q-nth $i $mu) (q-nth $j $mu)))))
!(set:define &self pair-weights (-> rats rats)
   (= (pair-weights $mu)
      (rats3 (pair-weight $mu zero (suc zero)) (pair-weight $mu (suc zero) (suc (suc zero)))
             (pair-weight $mu zero (suc (suc zero))))))
; The observers of the laboratory are admitted.
!(observe objects measure (positive-policy (reports-of source)))
!(observe objects measure (positive-policy (reports-of receiver)))
!(observe objects measure (positive-policy transported))

; Each value below is computed by the definitions above, counted in firings of
; the declared equations, and compared with the fraction the Lean theorem
; states, written in normal form.  The data a value starts from (the measure,
; the transported reports and the closure tables) are computed once, by the
; same definitions, and bound to a name, so that each count is of the value's
; own computation and the counts can be added.
!(bind! &mu measure)
; g(alpha_1) = 547/720 and g(alpha_2^0) = 341/720.
!(let $expected (written (num3 d5 d4 d7) (num3 d7 d2 d0))
   (let (($value) $n) (cost:firings (graphtropy &mu (positive-policy (reports-of source))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.initial_graphtropy" (fraction 547 720) $agree (firings $n))))))
!(let $expected (written (num3 d3 d4 d1) (num3 d7 d2 d0))
   (let (($value) $n) (cost:firings (graphtropy &mu (positive-policy (reports-of receiver))))
     (let $agree (== $value $expected)
       (let $bound (bind! &g-receiver $value)
         (quote ("Laboratory.initial_graphtropy" (fraction 341 720) $agree (firings $n)))))))
; The measure (5/12, 1/3, 1/4) is the importance normalized; the diagonal mass
; 25/72 is g of the identity observer; the pair weights are (5/18, 1/6, 5/24);
; and the conditional pair distribution omega = (20/47, 12/47, 15/47), the
; weights divided by 1 - 25/72, is a probability vector on the probes.
!(let $expected ((rats3 (written (num1 d5) (num2 d1 d2)) (written (num1 d1) (num1 d3)) (written (num1 d1) (num1 d4)))
                 (written (num2 d2 d5) (num2 d7 d2))
                 (rats3 (written (num1 d5) (num2 d1 d8)) (written (num1 d1) (num1 d6)) (written (num1 d5) (num2 d2 d4)))
                 (rats3 (written (num2 d2 d0) (num2 d4 d7)) (written (num2 d1 d2) (num2 d4 d7)) (written (num2 d1 d5) (num2 d4 d7)))
                 no-defect)
   (let (($value) $n)
        (cost:firings
          (let $mu (normalize importance)
            (let $diagonal (graphtropy $mu (identity-table objects))
              (let $omega (q-scale (pair-weights $mu) (qinv (qsub (whole (npos one)) $diagonal)))
                ($mu $diagonal (pair-weights $mu) $omega (measure-check probes $omega))))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.measure_and_weights" (measure 5/12 1/3 1/4) (diagonal 25/72) (pairs 5/18 1/6 5/24)
               (omega 20/47 12/47 15/47) $agree (firings $n))))))
; Transport: the gate gives the receiver C2^1, ab = (0.81, 0.72),
; bc = (0.765, 0.60), ac = (0.10, 0.80).
!(let $expected (pcons (pb (written (num2 d8 d1) (num3 d1 d0 d0)) (written (num2 d1 d8) (num2 d2 d5)))
                  (pcons (pb (written (num3 d1 d5 d3) (num3 d2 d0 d0)) (written (num1 d3) (num1 d5)))
                    (pcons (pb (written (num1 d1) (num2 d1 d0)) (written (num1 d4) (num1 d5))) pnil)))
   (let (($value) $n) (cost:firings transported)
     (let $agree (== $value $expected)
       (let $bound (bind! &transported $value)
         (quote ("Laboratory.transport_exact" $agree (firings $n)))))))
; After transport g = 1297/1800, 889/3600 above the initial receiver.  The
; three closures of the transported observer complete ac to 23/40
; (Lukasiewicz), 12393/20000 (product) and 153/200 (minimum).
!(let $expected ((written (num4 d1 d2 d9 d7) (num4 d1 d8 d0 d0)) (written (num3 d8 d8 d9) (num4 d3 d6 d0 d0)))
   (let (($value) $n)
        (cost:firings (let $after (graphtropy &mu (positive-policy &transported)) ($after (qsub $after &g-receiver))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.transport_and_completion" (fractions 1297/1800 889/3600) $agree (firings $n))))))
!(let $expected (written (num2 d2 d3) (num2 d4 d0))
   (let (($table) $n) (cost:firings (path-closure lukasiewicz objects (positive-policy &transported)))
     (let $agree (== (q-nth (suc (suc zero)) (t-head $table)) $expected)
       (let $bound (bind! &metric-completion $table)
         (quote ("Laboratory.transport_and_completion" (lukasiewicz-ac 23/40) $agree (firings $n)))))))
!(let $expected (written (num5 d1 d2 d3 d9 d3) (num5 d2 d0 d0 d0 d0))
   (let (($table) $n) (cost:firings (path-closure product objects (positive-policy &transported)))
     (let $agree (== (q-nth (suc (suc zero)) (t-head $table)) $expected)
       (let $bound (bind! &product-completion $table)
         (quote ("Laboratory.transport_and_completion" (product-ac 12393/20000) $agree (firings $n)))))))
!(let $expected (written (num3 d1 d5 d3) (num3 d2 d0 d0))
   (let (($table) $n) (cost:firings (path-closure bottleneck objects (positive-policy &transported)))
     (let $agree (== (q-nth (suc (suc zero)) (t-head $table)) $expected)
       (quote ("Laboratory.transport_and_completion" (minimum-ac 153/200) $agree (firings $n))))))
; Each completion is a tolerance above the transported observer, and they are
; nested: alpha <= Lukasiewicz <= product <= minimum, entry by entry.
!(set:define &self rats-le (-> rats rats decision)
   (= (rats-le qnil $l) yes)
   (= (rats-le (qcons $x $xs) $l) (yes-and (qle $x (q-head $l)) (rats-le $xs (q-tail $l)))))
!(set:define &self table-le (-> table table decision)
   (= (table-le tnil $t) yes)
   (= (table-le (trow $r $rs) $t) (yes-and (rats-le $r (t-head $t)) (table-le $rs (t-tail $t)))))
!(let $minimum-completion (path-closure bottleneck objects (positive-policy &transported))
   (let $nested ((table-le (positive-policy &transported) &metric-completion)
                 (table-le &metric-completion &product-completion)
                 (table-le &product-completion $minimum-completion))
     (let $admitted (admitted (observe objects &mu &product-completion))
       (quote ((nested $nested) (product-completion-admitted $admitted) "DistinctionCalculus.nested_completions")))))
; The balance (HS5.05).  What a completion costs in expected indistinction is
; the bracket of the similarity it adds (HS3.04, HS5.01): the metric
; completion of the transported receiver sits 4981/14400 above the initial
; receiver, and the product completion 893/96000 above the metric one.
!(set:define &self row-diff (-> rats rats rats)
   (= (row-diff qnil $r) qnil)
   (= (row-diff (qcons $x $xs) $r) (qcons (qsub $x (q-head $r)) (row-diff $xs (q-tail $r)))))
!(set:define &self table-diff (-> table table table)
   (= (table-diff tnil $t) tnil)
   (= (table-diff (trow $r $rs) $t) (trow (row-diff $r (t-head $t)) (table-diff $rs (t-tail $t)))))
!(let $expected (written (num4 d4 d9 d8 d1) (num5 d1 d4 d4 d0 d0))
   (let (($value) $n)
        (cost:firings (bracket &mu (table-diff &metric-completion (positive-policy (reports-of receiver))) &mu))
     (let $agree (== $value $expected)
       (quote ("Laboratory.completion_balance" (fraction 4981 14400) $agree (firings $n))))))
!(let $expected (written (num3 d8 d9 d3) (num5 d9 d6 d0 d0 d0))
   (let (($value) $n) (cost:firings (bracket &mu (table-diff &product-completion &metric-completion) &mu))
     (let $agree (== $value $expected)
       (quote ("Laboratory.completion_balance" (fraction 893 96000) $agree (firings $n))))))
; The same balances as the Lean theorem states them, differences of the
; graphtropies of the closure tables.
!(let $g-metric (graphtropy &mu &metric-completion)
   (let $agree (== ((qsub $g-metric &g-receiver) (qsub (graphtropy &mu &product-completion) $g-metric))
                   ((written (num4 d4 d9 d8 d1) (num5 d1 d4 d4 d0 d0)) (written (num3 d8 d9 d3) (num5 d9 d6 d0 d0 d0))))
     (quote ("Laboratory.completion_balance" (as-differences 4981/14400 893/96000) $agree "Laboratory.accounting"))))
; The squared report distances to the source over the probes weighted by
; omega: 157/940 before transport, 24467/940000 after; at ac alone 1/200.
!(bind! &omega (rats3 (frac (num2 d2 d0) (num2 d4 d7)) (frac (num2 d1 d2) (num2 d4 d7)) (frac (num2 d1 d5) (num2 d4 d7))))
!(let $expected (written (num3 d1 d5 d7) (num3 d9 d4 d0))
   (let (($value) $n) (cost:firings (report-dist-sq &omega (reports-of source) (reports-of receiver)))
     (let $agree (== $value $expected)
       (quote ("Laboratory.report_distances" (fraction 157 940) $agree (firings $n))))))
!(let $expected ((written (num5 d2 d4 d4 d6 d7) (nmul (num3 d9 d4 d0) (num4 d1 d0 d0 d0))) (written (num1 d1) (num3 d2 d0 d0)))
   (let (($value) $n)
        (cost:firings
          ((report-dist-sq &omega (reports-of source) &transported)
           (pbit-dist-sq (p-nth (suc (suc zero)) (reports-of source)) (p-nth (suc (suc zero)) &transported))))
     (let $agree (== $value $expected)
       (quote ("Laboratory.report_distances" (fractions 24467/940000 1/200) $agree (firings $n))))))
