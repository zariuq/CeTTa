; Equations on the Megalodon kernel: the laws of equality, conversions and
; rewriting at a stated position.  Import megalodon first, then this library:
;   !(import! &self megalodon)
;   !(import! &self (library megalodon_equations))
;
; Every theorem below is made by the native kernel (mg:known-in, mg:term-app,
; mg:proof-app, mg:prove); this file is search and bookkeeping, and nothing
; it does can make a theorem the kernel would refuse.
;
; An equational theory is (Equality THEORY EQ): a theory and the name of its
; equality at the base type, defined as Megalodon defines it (x = y is: every
; relation Q with Q x y has Q y x), with one type argument.  (eq:init E)
; proves the laws once in the theory; the other functions use them.
;
;   (eq:of E A B)                 the proposition A = B
;   (eq:refl E CONTEXT A)         A = A
;   (eq:sym E CONTEXT A B AB)     B = A from A = B
;   (eq:trans E CONTEXT A B C AB BC)
;   (eq:cong E CONTEXT F A B AB)  F A = F B, F a function of the base type
;   (eq:mp E CONTEXT P A B AB PB) P A from A = B and P B
;
; A conversion is data, run by (conv:run E CONTEXT CONVERSION TERM).  Its
; answer is (Conv TERM' THEOREM), with THEOREM stating TERM = TERM' in
; CONTEXT up to the kernel's conversion, or Same when the conversion leaves
; the term as it is.  An equation is used at the position asked for and
; nowhere else.
;
;   (Cv.rule NAME PROVER)   the known NAME, read as premises -> L = R, at a
;                           term its L matches; PROVER proves the premises
;   (Cv.rule-default NAME PROVER TERM)
;                           the same, with TERM for each quantifier that L
;                           does not determine
;   (Cv.equation THEOREM)   the theorem, stating L = R, at a term convertible
;                           with its L
;   (Cv.rules SET PROVER DEFAULT)
;                           the first rule of the declared set SET that
;                           applies; only the rules declared for the term's
;                           head are tried.  (conv:declare E CONTEXT SET NAME)
;                           declares the known NAME a rule of SET, under the
;                           head of its left side
;   (Cv.then C1 C2)         C1, then C2 on its result
;   (Cv.orelse C1 C2)       C1, or C2 where C1 leaves the term as it is
;   (Cv.path PATH C)        C at the subterm PATH leads to; PATH is a list of
;                           fun and arg, the two parts of an application
;   (Cv.top-down C)         C at the first position where it applies, the
;                           whole term before its parts, the function part
;                           before the argument
;   (Cv.repeat C)           C until it leaves the term as it is
;   (Cv.innermost C)        the parts of an application first, then C at the
;                           whole, and again on what results, until C applies
;                           nowhere: a value is computed before the function
;                           applied to it is.  The body of a binder is left
;                           as it is
;   (Cv.memo C)             C, with each answer kept and found again
;
; A prover is data too, run by (conv:prove PROVER E CONTEXT PROPOSITION);
; no answer when it proves nothing.
;
;   Prover.none             proves nothing
;   (Prover.known NAME)     the known NAME, where it states the proposition
;   (Prover.tactic TACTIC)  a tactic of the megalodon library
;   (Prover.horn (lemmas NAME ..) DEPTH)
;                           a hypothesis, or backchaining through the named
;                           knowns, each read as premises -> conclusion and
;                           instantiated by matching its conclusion, at most
;                           DEPTH lemmas deep; a goal with binders is proved
;                           under them
;
; Two tactics for mg:run (megalodon.metta hands them here):
;
;   (Tac.conv EQ PATH CONVERSION TACTIC)
;                           convert the goal's subterm at PATH, then prove
;                           the goal that results by TACTIC
;   (Tac.rewrite EQ SOURCE PATH TACTIC)
;                           the same with the equation SOURCE states
;   (Tac.prove EQ PROVER)   the goal by a prover

; ---- terms as data -------------------------------------------------------

; Free variables at or above CUT moved by BY.
(= (mgt:shift $t $by $cut)
   (case $t
     (((Tm.db $i) (if (< $i $cut) $t (Tm.db (+ $i $by))))
      ((Tm.app $f $x) (Tm.app (mgt:shift $f $by $cut) (mgt:shift $x $by $cut)))
      ((Tm.lam $A $b) (Tm.lam $A (mgt:shift $b $by (+ $cut 1))))
      ((Tm.all $A $b) (Tm.all $A (mgt:shift $b $by (+ $cut 1))))
      ((Tm.imp $p $q) (Tm.imp (mgt:shift $p $by $cut) (mgt:shift $q $by $cut)))
      ((Tm.typeApp $m $A) (Tm.typeApp (mgt:shift $m $by $cut) $A))
      ((Tm.typeLam $m) (Tm.typeLam (mgt:shift $m $by $cut)))
      ((Tm.typeAll $m) (Tm.typeAll (mgt:shift $m $by $cut)))
      ($other $t))))
; The body of a binder at the term S: variable K becomes S, the variables
; above it move down.
(= (mgt:inst $t $s) (mgt:inst-at $t $s 0))
(= (mgt:inst-at $t $s $k)
   (case $t
     (((Tm.db $i) (if (== $i $k) (mgt:shift $s $k 0) (if (> $i $k) (Tm.db (- $i 1)) $t)))
      ((Tm.app $f $x) (Tm.app (mgt:inst-at $f $s $k) (mgt:inst-at $x $s $k)))
      ((Tm.lam $A $b) (Tm.lam $A (mgt:inst-at $b $s (+ $k 1))))
      ((Tm.all $A $b) (Tm.all $A (mgt:inst-at $b $s (+ $k 1))))
      ((Tm.imp $p $q) (Tm.imp (mgt:inst-at $p $s $k) (mgt:inst-at $q $s $k)))
      ((Tm.typeApp $m $A) (Tm.typeApp (mgt:inst-at $m $s $k) $A))
      ((Tm.typeLam $m) (Tm.typeLam (mgt:inst-at $m $s $k)))
      ((Tm.typeAll $m) (Tm.typeAll (mgt:inst-at $m $s $k)))
      ($other $t))))
(= (mgt:mentions $t $k)
   (case $t
     (((Tm.db $i) (== $i $k))
      ((Tm.app $f $x) (if (mgt:mentions $f $k) True (mgt:mentions $x $k)))
      ((Tm.lam $A $b) (mgt:mentions $b (+ $k 1)))
      ((Tm.all $A $b) (mgt:mentions $b (+ $k 1)))
      ((Tm.imp $p $q) (if (mgt:mentions $p $k) True (mgt:mentions $q $k)))
      ((Tm.typeApp $m $A) (mgt:mentions $m $k))
      ((Tm.typeLam $m) (mgt:mentions $m $k))
      ((Tm.typeAll $m) (mgt:mentions $m $k))
      ($other False))))
; Beta redexes at the head contracted.
(= (mgt:head-beta $t)
   (case $t
     (((Tm.app $f $x)
       (let $g (mgt:head-beta $f)
         (case $g
           (((Tm.lam $A $b) (mgt:head-beta (mgt:inst $b $x)))
            ($other (Tm.app $g $x))))))
      ($other $t))))
; Beta redexes contracted everywhere.
(= (mgt:beta $t)
   (case $t
     (((Tm.app $f $x)
       (let $g (mgt:beta $f)
         (case $g
           (((Tm.lam $A $b) (mgt:beta (mgt:inst $b $x)))
            ($other (let $y (mgt:beta $x) (Tm.app $g $y)))))))
      ((Tm.lam $A $b) (let $c (mgt:beta $b) (Tm.lam $A $c)))
      ((Tm.all $A $b) (let $c (mgt:beta $b) (Tm.all $A $c)))
      ((Tm.imp $p $q) (let $a (mgt:beta $p) (let $c (mgt:beta $q) (Tm.imp $a $c))))
      ($other $t))))
; Eta redexes contracted everywhere: fun x => F x is F where x is not in F.
(= (mgt:eta $t)
   (case $t
     (((Tm.lam $A $b)
       (let $c (mgt:eta $b)
         (case $c
           (((Tm.app $f (Tm.db 0)) (if (mgt:mentions $f 0) (Tm.lam $A $c) (mgt:shift $f -1 0)))
            ($other (Tm.lam $A $c))))))
      ((Tm.app $f $x) (Tm.app (mgt:eta $f) (mgt:eta $x)))
      ((Tm.all $A $b) (Tm.all $A (mgt:eta $b)))
      ((Tm.imp $p $q) (Tm.imp (mgt:eta $p) (mgt:eta $q)))
      ($other $t))))

; The subterm a path leads to, or None; and the term with it replaced.
(= (mgt:subterm $t $path)
   (if (== $path ()) $t
       (case $t
         (((Tm.app $f $x)
           (if (== (car-atom $path) fun) (mgt:subterm $f (cdr-atom $path)) (mgt:subterm $x (cdr-atom $path))))
          ($other None)))))
(= (mgt:replace $t $path $new)
   (if (== $path ()) $new
       (case $t
         (((Tm.app $f $x)
           (if (== (car-atom $path) fun)
               (Tm.app (mgt:replace $f (cdr-atom $path) $new) $x)
               (Tm.app $f (mgt:replace $x (cdr-atom $path) $new))))
          ($other $t)))))
; The term as a function of its subterm at a path.
(= (mgt:context $t $path)
   (Tm.lam (Tp.base 0) (mgt:replace (mgt:shift $t 1 0) $path (Tm.db 0))))
(= (mgt:snoc $xs $x)
   (if (== $xs ()) (cons-atom $x ())
       (let $head (car-atom $xs)
         (let $tail (mgt:snoc (cdr-atom $xs) $x) (cons-atom $head $tail)))))

; A pattern is a term under N quantifiers, the K-th of them (from outside,
; from 0) written as the variable N - 1 - K.  The answer binds each pattern
; variable it meets: a list of (K TERM), or fail.  D is the number of
; binders passed inside the pattern.
(= (mgt:match $pat $t $n $d $binds)
   (if (== $binds fail) fail
       (case $pat
         (((Tm.db $j)
           (if (< $j $d)
               (if (== $t (Tm.db $j)) $binds fail)
               (if (< (- $j $d) $n)
                   (mgt:bind (- (- $n 1) (- $j $d)) (mgt:shift $t (- 0 $d) 0) $binds)
                   (if (== $t $pat) $binds fail))))
          ((Tm.app $pf $px)
           (case $t
             (((Tm.app $f $x) (mgt:match $px $x $n $d (mgt:match $pf $f $n $d $binds)))
              ($other fail))))
          ((Tm.lam $A $pb)
           (case $t
             (((Tm.lam $B $b) (if (== $A $B) (mgt:match $pb $b $n (+ $d 1) $binds) fail))
              ($other fail))))
          ((Tm.typeApp $pm $A)
           (case $t
             (((Tm.typeApp $m $B) (if (== $A $B) (mgt:match $pm $m $n $d $binds) fail))
              ($other fail))))
          ($other (if (== $pat $t) $binds fail))))))
(= (mgt:bind $k $t $binds)
   (let $found (mgt:bound $k $binds)
     (if (== $found None) (cons-atom ($k $t) $binds)
         (if (== $found $t) $binds fail))))
(= (mgt:bound $k $binds)
   (if (== $binds ()) None
       (let ($j $t) (car-atom $binds) (if (== $j $k) $t (mgt:bound $k (cdr-atom $binds))))))

; ---- equality ------------------------------------------------------------

(= (eq:theory (Equality $theory $eq)) $theory)
(= (eq:name (Equality $theory $eq)) $eq)
(= (eq:of $E $a $b)
   (Tm.app (Tm.app (Tm.typeApp (Tm.named (eq:name $E)) (Tp.base 0)) $a) $b))
; The two sides of an equation, (Sides A B), or None.
(= (eq:sides $E $p)
   (case $p
     (((Tm.app (Tm.app (Tm.typeApp (Tm.named $e) (Tp.base 0)) $a) $b)
       (if (== $e (eq:name $E)) (Sides $a $b) None))
      ($other None))))

; The laws, proved once in the theory from the definition of equality:
;   megalodon_eq_refl   x = x
;   megalodon_eq_sym    x = y -> y = x
;   megalodon_eq_mp     x = y -> P y -> P x
;   megalodon_eq_trans  x = y -> y = z -> x = z
;   megalodon_eq_cong   x = y -> f x = f y
(= (eq:init $E)
   (case (mg:try-known (eq:theory $E) "megalodon_eq_cong")
     (((Error $call $why) (eq:prove-laws $E (eq:theory $E) (Tp.base 0) (Tp.arr (Tp.base 0) (Tp.arr (Tp.base 0) (Tp.prop)))))
      ($known True))))
(= (eq:prove-laws $E $th $set $rel)
   (let* (($r (mg:prove $th "megalodon_eq_refl"
                (Tm.all $set (eq:of $E (Tm.db 0) (Tm.db 0)))
                (Pf.termLam $set (Pf.termLam $rel
                  (Pf.proofLam (Tm.app (Tm.app (Tm.db 0) (Tm.db 1)) (Tm.db 1)) (Pf.hyp 0))))))
          ($s (mg:prove $th "megalodon_eq_sym"
                (Tm.all $set (Tm.all $set (Tm.imp (eq:of $E (Tm.db 1) (Tm.db 0)) (eq:of $E (Tm.db 0) (Tm.db 1)))))
                (Pf.termLam $set (Pf.termLam $set (Pf.proofLam (eq:of $E (Tm.db 1) (Tm.db 0))
                  (Pf.termLam $rel (Pf.proofLam (Tm.app (Tm.app (Tm.db 0) (Tm.db 1)) (Tm.db 2))
                    (Pf.proofApp
                      (Pf.termApp (Pf.hyp 1)
                        (Tm.lam $set (Tm.lam $set (Tm.app (Tm.app (Tm.db 2) (Tm.db 0)) (Tm.db 1)))))
                      (Pf.hyp 0)))))))))
          ($m (mg:prove $th "megalodon_eq_mp"
                (Tm.all (Tp.arr $set (Tp.prop)) (Tm.all $set (Tm.all $set
                  (Tm.imp (eq:of $E (Tm.db 1) (Tm.db 0))
                    (Tm.imp (Tm.app (Tm.db 2) (Tm.db 0)) (Tm.app (Tm.db 2) (Tm.db 1)))))))
                (Pf.termLam (Tp.arr $set (Tp.prop)) (Pf.termLam $set (Pf.termLam $set
                  (Pf.proofLam (eq:of $E (Tm.db 1) (Tm.db 0))
                    (Pf.proofLam (Tm.app (Tm.db 2) (Tm.db 0))
                      (Pf.proofApp
                        (Pf.termApp (Pf.hyp 1)
                          (Tm.lam $set (Tm.lam $set (Tm.app (Tm.db 4) (Tm.db 0)))))
                        (Pf.hyp 0)))))))))
          ($t (mg:prove $th "megalodon_eq_trans"
                (Tm.all $set (Tm.all $set (Tm.all $set
                  (Tm.imp (eq:of $E (Tm.db 2) (Tm.db 1))
                    (Tm.imp (eq:of $E (Tm.db 1) (Tm.db 0)) (eq:of $E (Tm.db 2) (Tm.db 0)))))))
                (Pf.termLam $set (Pf.termLam $set (Pf.termLam $set
                  (Pf.proofLam (eq:of $E (Tm.db 2) (Tm.db 1))
                    (Pf.proofLam (eq:of $E (Tm.db 1) (Tm.db 0))
                      (Pf.proofApp
                        (Pf.proofApp
                          (Pf.termApp (Pf.termApp (Pf.termApp (Pf.known "megalodon_eq_mp")
                            (Tm.lam $set (eq:of $E (Tm.db 3) (Tm.db 0)))) (Tm.db 0)) (Tm.db 1))
                          (Pf.proofApp (Pf.termApp (Pf.termApp (Pf.known "megalodon_eq_sym") (Tm.db 1)) (Tm.db 0))
                            (Pf.hyp 0)))
                        (Pf.hyp 1)))))))))
          ($c (mg:prove $th "megalodon_eq_cong"
                (Tm.all (Tp.arr $set $set) (Tm.all $set (Tm.all $set
                  (Tm.imp (eq:of $E (Tm.db 1) (Tm.db 0))
                    (eq:of $E (Tm.app (Tm.db 2) (Tm.db 1)) (Tm.app (Tm.db 2) (Tm.db 0)))))))
                (Pf.termLam (Tp.arr $set $set) (Pf.termLam $set (Pf.termLam $set
                  (Pf.proofLam (eq:of $E (Tm.db 1) (Tm.db 0))
                    (Pf.proofApp
                      (Pf.proofApp
                        (Pf.termApp (Pf.termApp (Pf.termApp (Pf.known "megalodon_eq_mp")
                          (Tm.lam $set (eq:of $E (Tm.app (Tm.db 3) (Tm.db 0)) (Tm.app (Tm.db 3) (Tm.db 1)))))
                          (Tm.db 1)) (Tm.db 0))
                        (Pf.hyp 0))
                      (Pf.termApp (Pf.known "megalodon_eq_refl") (Tm.app (Tm.db 2) (Tm.db 0)))))))))))
     True))

(= (eq:law $E $ctx $name) (mg:known-in (eq:theory $E) $ctx $name))
(= (eq:refl $E $ctx $a) (mg:term-app (eq:law $E $ctx "megalodon_eq_refl") $a))
(= (eq:sym $E $ctx $a $b $ab)
   (mg:proof-app (mg:term-app (mg:term-app (eq:law $E $ctx "megalodon_eq_sym") $a) $b) $ab))
(= (eq:trans $E $ctx $a $b $c $ab $bc)
   (mg:proof-app
     (mg:proof-app
       (mg:term-app (mg:term-app (mg:term-app (eq:law $E $ctx "megalodon_eq_trans") $a) $b) $c) $ab)
     $bc))
(= (eq:cong $E $ctx $f $a $b $ab)
   (mg:proof-app
     (mg:term-app (mg:term-app (mg:term-app (eq:law $E $ctx "megalodon_eq_cong") $f) $a) $b) $ab))
(= (eq:mp $E $ctx $P $a $b $ab $pb)
   (mg:proof-app
     (mg:proof-app
       (mg:term-app (mg:term-app (mg:term-app (eq:law $E $ctx "megalodon_eq_mp") $P) $a) $b) $ab)
     $pb))

; ---- provers of premises -------------------------------------------------

(= (conv:prove $prover $E $ctx $p)
   (case $prover
     ((Prover.none (empty))
      ((Prover.known $name) (mg:exact (eq:theory $E) $ctx $p (mg:known-in (eq:theory $E) $ctx $name)))
      ((Prover.tactic $tactic) (mg:run $tactic (eq:theory $E) $ctx $p))
      ((Prover.horn $lemmas $depth)
       (let $found (horn:prove $E $ctx (cdr-atom $lemmas) $depth $p)
         (if (== $found None) (empty) $found)))
      ($other (Error (conv:prove $other) (MegalodonMalformed "unknown prover"))))))

; A hypothesis, or the first lemma whose conclusion matches the proposition
; and whose premises are proved in turn: a theorem stating the proposition,
; or None.  The proposition is taken in beta normal form (an instantiated
; premise applies its functions).  One that begins with a quantifier or an
; implication is proved under its binders, their hypotheses in hand: so a
; function is typed by typing its body at a variable of its domain.
(= (horn:prove $E $ctx $lemmas $depth $p)
   (if (< $depth 1) None
       (let $q (mgt:beta $p)
         (case $q
           (((Tm.all $A $body) (horn:under $E $ctx $lemmas $depth $q))
            ((Tm.imp $premise $rest) (horn:under $E $ctx $lemmas $depth $q))
            ($other
             (let $assumed (collapse (mg:assumption (eq:theory $E) $ctx $q))
               (if (== $assumed ()) (horn:try $E $ctx $lemmas $lemmas $depth $q) (car-atom $assumed)))))))))
(= (horn:under $E $ctx $lemmas $depth $q)
   (let $proved (collapse (mg:run (Tac.intros (Tac.prove (eq:name $E) (Prover.horn (cons-atom lemmas $lemmas) $depth)))
                                  (eq:theory $E) $ctx $q))
     (if (== $proved ()) None (car-atom $proved))))
(= (horn:try $E $ctx $all $rest $depth $p)
   (if (== $rest ()) None
       (let $found (horn:lemma $E $ctx $all $depth (car-atom $rest) $p)
         (if (== $found None) (horn:try $E $ctx $all (cdr-atom $rest) $depth $p) $found))))
(= (horn:lemma $E $ctx $all $depth $name $p)
   (let* (($known (mg:known-in (eq:theory $E) $ctx $name))
          ($statement (mg:proposition $known))
          ($shape (rule:conclusion $statement 0)))
     (case $shape
       (((Concludes $n $conclusion)
         (let $binds (mgt:match (mgt:eta $conclusion) $p $n 0 ())
           (if (== $binds fail) None
               (case (rule:instantiate $E $ctx (Prover.horn (cons-atom lemmas $all) (- $depth 1)) None
                       $known $statement 0 $binds)
                 (((Instance $thm $stated)
                   (let $checked (collapse (mg:exact (eq:theory $E) $ctx $p $thm))
                     (if (== $checked ()) None (car-atom $checked))))
                  ($other None))))))))))
; (Concludes N C): the number of quantifiers and the conclusion, under them.
(= (rule:conclusion $p $n)
   (case $p
     (((Tm.all $A $body) (rule:conclusion $body (+ $n 1)))
      ((Tm.imp $premise $rest) (rule:conclusion $rest $n))
      ($other (Concludes $n $p)))))
; A known instantiated: its statement is walked with the quantifiers already
; instantiated, so each premise met is the proposition to prove.  A
; quantifier the match did not bind takes DEFAULT, or there is no instance.
; (Instance THEOREM PROPOSITION), or None.
(= (rule:instantiate $E $ctx $prover $default $thm $p $k $binds)
   (case $p
     (((Tm.all $A $body)
       (let $bound (mgt:bound $k $binds)
         (let $term (if (== $bound None) $default $bound)
           (if (== $term None) None
               (case (mg:try-term-app $thm $term)
                 (((Error $call $why) None)
                  ($next (rule:instantiate $E $ctx $prover $default $next (mgt:inst $body $term) (+ $k 1) $binds))))))))
      ((Tm.imp $premise $rest)
       (let $proved (collapse (conv:prove $prover $E $ctx $premise))
         (if (== $proved ()) None
             (case (mg:try-proof-app $thm (car-atom $proved))
               (((Error $call $why) None)
                ($next (rule:instantiate $E $ctx $prover $default $next $rest $k $binds)))))))
      ($other (Instance $thm $p)))))

; ---- conversions ---------------------------------------------------------

(= (conv:run $E $ctx $c $t)
   (case $c
     (((Cv.rule $name $prover) (conv:rule $E $ctx $name $prover None $t))
      ((Cv.rule-default $name $prover $default) (conv:rule $E $ctx $name $prover $default $t))
      ((Cv.equation $theorem) (conv:equation $E $ctx $theorem $t))
      ((Cv.then $one $two) (conv:then $E $ctx $one $two $t))
      ((Cv.orelse $one $two)
       (let $r (conv:run $E $ctx $one $t) (if (== $r Same) (conv:run $E $ctx $two $t) $r)))
      ((Cv.path $path $inner) (conv:at $E $ctx $path $inner $t))
      ((Cv.top-down $inner) (conv:top-down $E $ctx $inner $t))
      ((Cv.repeat $inner) (conv:repeat $E $ctx $inner $t))
      ((Cv.innermost $inner) (conv:innermost $E $ctx $inner $t))
      ((Cv.rules $set $prover $default) (conv:rules $E $ctx $set $prover $default $t))
      ((Cv.memo $inner) (conv:memo $E $ctx $inner $t))
      ($other (Error (conv:run $other) (MegalodonMalformed "unknown conversion"))))))

; t = t2 and t2 = t3 joined.
(= (conv:join $E $ctx $t $first $second)
   (case $first
     ((Same $second)
      ((Conv $t2 $thm2)
       (case $second
         ((Same $first)
          ((Conv $t3 $thm3) (Conv $t3 (eq:trans $E $ctx $t $t2 $t3 $thm2 $thm3)))))))))
(= (conv:then $E $ctx $one $two $t)
   (let $first (conv:run $E $ctx $one $t)
     (case $first
       ((Same (conv:run $E $ctx $two $t))
        ((Conv $t2 $thm2) (conv:join $E $ctx $t $first (conv:run $E $ctx $two $t2)))))))
(= (conv:repeat $E $ctx $inner $t)
   (let $first (conv:run $E $ctx $inner $t)
     (case $first
       ((Same Same)
        ((Conv $t2 $thm2) (conv:join $E $ctx $t $first (conv:repeat $E $ctx $inner $t2)))))))

; An equation between subterms, as an equation between the terms around them.
(= (conv:lift $E $ctx $t $path $s $inner)
   (case $inner
     ((Same Same)
      ((Conv $s2 $thm)
       (if (== $path ()) $inner
           (Conv (mgt:replace $t $path $s2)
                 (eq:cong $E $ctx (mgt:context $t $path) $s $s2 $thm)))))))
(= (conv:at $E $ctx $path $inner $t)
   (let $s (mgt:subterm $t $path)
     (if (== $s None) Same (conv:lift $E $ctx $t $path $s (conv:run $E $ctx $inner $s)))))
; The first position, in the order whole term, function part, argument.
(= (conv:top-down $E $ctx $inner $t)
   (let $found (conv:find $E $ctx $inner $t ())
     (case $found
       ((None Same)
        ((At $path $s $r) (conv:lift $E $ctx $t $path $s $r))))))
(= (conv:find $E $ctx $inner $s $path)
   (let $r (conv:run $E $ctx $inner $s)
     (if (== $r Same)
         (case $s
           (((Tm.app $f $x)
             (let $left (conv:find $E $ctx $inner $f (mgt:snoc $path fun))
               (if (== $left None) (conv:find $E $ctx $inner $x (mgt:snoc $path arg)) $left)))
            ($other None)))
         (At $path $s $r))))

; The parts first: the innermost position where the conversion applies is
; the first one met when the parts of an application are searched before the
; whole.  Each step is an equation between whole terms, the step lifted from
; its position, and the steps are joined as they are made.
(= (conv:innermost $E $ctx $inner $t) (conv:steps $E $ctx $inner $t $t Same))
(= (conv:steps $E $ctx $inner $start $t $so-far)
   (case (conv:find-inner $E $ctx $inner $t ())
     ((None $so-far)
      ((At $path $s $r)
       (let $step (conv:lift $E $ctx $t $path $s $r)
         (conv:steps $E $ctx $inner $start (conv:result $t $step) (conv:join $E $ctx $start $so-far $step)))))))
(= (conv:find-inner $E $ctx $inner $s $path)
   (case $s
     (((Tm.app $f $x)
       (let $left (conv:find-inner $E $ctx $inner $f (mgt:snoc $path fun))
         (if (== $left None)
             (let $right (conv:find-inner $E $ctx $inner $x (mgt:snoc $path arg))
               (if (== $right None) (conv:here $E $ctx $inner $s $path) $right))
             $left)))
      ($other (conv:here $E $ctx $inner $s $path)))))
(= (conv:here $E $ctx $inner $s $path)
   (let $r (conv:run $E $ctx $inner $s) (if (== $r Same) None (At $path $s $r))))
(= (conv:result $t $answer) (case $answer ((Same $t) ((Conv $t2 $theorem) $t2))))

; Rule sets.  A rule is declared under the head of its left side, so that a
; term is offered only the rules that can match it, in the order they were
; declared.
(= (mgt:head $t) (case $t (((Tm.app $f $x) (mgt:head $f)) ((Tm.typeApp $m $A) (mgt:head $m)) ($other $t))))
(= (conv:declared $set $head)
   (let $kept (collapse (match &self (ConvRules $set $head $names) $names))
     (if (== $kept ()) () (car-atom $kept))))
(= (conv:declare $E $ctx $set $name)
   (let* (($known (mg:known-in (eq:theory $E) $ctx $name))
          ($shape (rule:conclusion (mg:proposition $known) 0)))
     (case $shape
       (((Concludes $n $conclusion)
         (case (eq:sides $E $conclusion)
           (((Sides $left $right)
             (let* (($head (mgt:head (mgt:eta $left)))
                    ($before (conv:declared $set $head))
                    ($names (mgt:snoc $before $name))
                    ($gone (if (== $before ()) () (remove-atom &self (ConvRules $set $head $before))))
                    ($done (add-atom &self (ConvRules $set $head $names))))
               $head))
            ($other (Error (conv:declare $name) (MegalodonMalformed "the known does not conclude with an equation"))))))))))
(= (conv:rules $E $ctx $set $prover $default $t)
   (let $head (mgt:head $t)
     (conv:first-rule $E $ctx (conv:declared $set $head) $prover $default $t)))
(= (conv:first-rule $E $ctx $names $prover $default $t)
   (if (== $names ()) Same
       (let $r (conv:rule $E $ctx (car-atom $names) $prover $default $t)
         (if (== $r Same) (conv:first-rule $E $ctx (cdr-atom $names) $prover $default $t) $r))))

; Answers kept: a conversion asked again of the same term in the same
; equational theory and context is one lookup.
(= (conv:memo $E $ctx $inner $t)
   (let $kept (collapse (match &self (ConvFact $E $ctx $inner $t $answer) $answer))
     (if (== $kept ())
         (let $answer (conv:run $E $ctx $inner $t)
           (let $done (add-atom &self (ConvFact $E $ctx $inner $t $answer)) $answer))
         (car-atom $kept))))

; A known read as a rule.  Its statement is quantifiers and premises around
; an equation L = R; where L matches the term, the quantifiers are
; instantiated in order and each premise is proved by the prover.  No answer
; of the prover for a premise, or no match, leaves the term as it is.
(= (conv:rule $E $ctx $name $prover $default $t)
   (let* (($known (mg:known-in (eq:theory $E) $ctx $name))
          ($statement (mg:proposition $known))
          ($shape (rule:conclusion $statement 0)))
     (case $shape
       (((Concludes $n $conclusion)
         (case (eq:sides $E $conclusion)
           (((Sides $left $right)
             (let $binds (mgt:match (mgt:eta $left) $t $n 0 ())
               (if (== $binds fail) Same
                   (case (rule:instantiate $E $ctx $prover $default $known $statement 0 $binds)
                     (((Instance $thm $stated)
                       (case (eq:sides $E $stated)
                         (((Sides $l $r) (Conv (mgt:head-beta $r) $thm))
                          ($other Same))))
                      ($other Same))))))
            ($other Same))))))))
; A theorem stating L = R, at a term the kernel finds convertible with L.
(= (conv:equation $E $ctx $theorem $t)
   (case (eq:sides $E (mg:proposition $theorem))
     (((Sides $left $right)
       (case (mg:try-convertible (eq:theory $E) $ctx $left $t)
         (((Error $call $why) Same)
          ($same (if $same (Conv $right $theorem) Same)))))
      ($other Same))))

; ---- tactics -------------------------------------------------------------

; The goal with its subterm at PATH converted; the goal itself follows from
; the converted one by the equation, as a property of that subterm.
(= (mg:convert $theory $context $goal $eq $path $conversion $tactic)
   (let* (($E (Equality $theory $eq))
          ($s (mgt:subterm $goal $path)))
     (if (== $s None) (empty)
         (case (conv:run $E $context $conversion $s)
           ((Same (mg:run $tactic $theory $context $goal))
            ((Conv $s2 $equation)
             (let $proved (collapse (mg:run $tactic $theory $context (mgt:replace $goal $path $s2)))
               (if (== $proved ()) (empty)
                   (eq:mp $E $context (mgt:context $goal $path) $s $s2 $equation (car-atom $proved)))))
            ($other $other))))))
; A prover as a tactic.
(= (mg:prove-with $theory $context $goal $eq $prover)
   (conv:prove $prover (Equality $theory $eq) $context $goal))
(= (mg:rewrite $theory $context $goal $eq $source $path $tactic)
   (let $equations (collapse (mg:source $theory $context $source))
     (if (== $equations ()) (empty)
         (mg:convert $theory $context $goal $eq $path (Cv.equation (car-atom $equations)) $tactic))))
