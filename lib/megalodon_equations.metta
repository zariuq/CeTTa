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
;   (eq:turned E NAME NEW)        the known NAME, quantifiers and premises
;                                 around L = R, proved with R = L in its
;                                 place and kept in the theory as NEW
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
;                           head are tried.  Where none applies although a
;                           left side matched (a premise was not proved),
;                           the term is left and (conv:undecided E CONTEXT)
;                           names the rule: that term is not known to be one
;                           no rule applies to.  (conv:declare E CONTEXT SET NAME)
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
;   (Cv.memo C)             C, with each answer kept and found again (in the
;                           space given by conv:keep-evidence-in, or the
;                           program's own)
;   (Cv.viewed SET VIEW PROVER DEFAULT)
;                           the rules of SET in the order declared, each as
;                           the term stands and, where it does not apply,
;                           again after the first subterm whose head differs
;                           from the head the rule's left side has there is
;                           converted by the conversion VIEW.  A view is an equation that shows a value
;                           in the form a left side asks for (a numeral as a
;                           successor); it is used only where the rule then
;                           applies, and the term is left as it is otherwise
;   (Cv.eval C)             the term's value under C: the arguments first,
;                           then C at the whole, then the value of what
;                           results.  Each answer is kept (as Cv.memo keeps
;                           them), so a term met again is one lookup, and
;                           (conv:origins E CONTEXT VALUE) says what a value
;                           was computed from.  An argument is evaluated as a whole when
;                           it is a name applied to all the arguments its
;                           type takes, with a result of the base type; the
;                           arguments of a shorter application are evaluated
;                           in place; the body of a binder is left as it is
;   (Cv.under C)            at a binder form H X (fun x => B) whose family
;                           lemma is declared (conv:declare-binder HEAD NAME,
;                           the known NAME stating: F x = G x for every x in
;                           X gives H X F = H X G): C on B at a variable of
;                           X, and the equation it makes generalized over the
;                           variable.  The body of a binder is computed by a
;                           theorem, not by the kernel's own conversion
;   (Cv.strong C)           the value under the evaluator C, and then the
;                           same inside every such binder form in it: two
;                           types or propositions that are equal by computing
;                           under their binders become the same term
;   (Cv.eval-needed SET C)  the same value, with only the arguments that a
;                           rule of the declared set SET needs in a
;                           particular form computed before C is tried; the
;                           others wait until their value is asked for.  The
;                           branch a conditional does not take is not
;                           computed
;
; Another file adds a conversion by giving
; (conv:run-with CONVERSION E CONTEXT TERM) for its own form of CONVERSION.
;
; A prover is data too, run by (conv:prove PROVER E CONTEXT PROPOSITION);
; no answer when it proves nothing.
;
;   Prover.none             proves nothing
;   (Prover.known NAME)     the known NAME, where it states the proposition
;   (Prover.tactic TACTIC)  a tactic of the megalodon library
;   (Prover.turned NAME)    the known NAME, premises -> L = R, where the
;                           proposition is its R = L; the premises are
;                           hypotheses in hand
;   (Prover.horn (lemmas NAME ..) DEPTH)
;                           a hypothesis, or backchaining through the named
;                           knowns, each read as premises -> conclusion and
;                           instantiated by matching its conclusion, at most
;                           DEPTH lemmas deep; a goal with binders is proved
;                           under them
;
; Another file adds a prover by giving (conv:prove-with PROVER E CONTEXT
; PROPOSITION) for its own form of PROVER, and with it
; (conv:witness-with PROVER E CONTEXT BODY K BINDS): the term for a
; quantifier the match left open, or None.
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

; The arguments of an application in order, the K-th of a list (from 0),
; and where a term stands in a list; None where there is none.
(= (mgt:spine-args $t) (case $t (((Tm.app $f $x) (mgt:snoc (mgt:spine-args $f) $x)) ($other ()))))
(= (mgt:nth $xs $k) (if (== $xs ()) None (if (== $k 0) (car-atom $xs) (mgt:nth (cdr-atom $xs) (- $k 1)))))
(= (mgt:position $x $xs $k)
   (if (== $xs ()) None (if (== (car-atom $xs) $x) $k (mgt:position $x (cdr-atom $xs) (+ $k 1)))))
; The first position, function part before argument, where the pattern and
; the term are headed by different names: (At PATH).  None where there is no
; such position, Differ where the two are applications of different lengths
; (no view of a part mends that).
(= (mgt:named-p $t) (case $t (((Tm.named $name) True) ($other False))))
(= (mgt:mismatch $pat $t $path)
   (case $pat
     (((Tm.db $j) None)
      ((Tm.lam $A $b) None)
      ($other
       (let* (($hp (mgt:head $pat)) ($ht (mgt:head $t)))
         (if (and (mgt:named-p $hp) (and (mgt:named-p $ht) (not (== $hp $ht))))
             (At $path)
             (case $pat
               (((Tm.app $pf $px)
                 (case $t
                   (((Tm.app $f $x)
                     (let $left (mgt:mismatch $pf $f (mgt:snoc $path fun))
                       (if (== $left None) (mgt:mismatch $px $x (mgt:snoc $path arg)) $left)))
                    ($else Differ))))
                ($atom (case $t (((Tm.app $f $x) Differ) ($same None))))))))))))

; Whether a term is a name applied to all the arguments its type takes, with
; a result of the base type.
(= (mgt:whole $E $x)
   (case (mgt:signature $E (mgt:head $x))
     (((Signature $n $base) (if $base (== $n (mgt:spine-length $x)) False))
      ($other False))))
(= (mgt:spine-length $t) (case $t (((Tm.app $f $x) (+ 1 (mgt:spine-length $f))) ($other 0))))
; (Signature N BASE) of a name: the arguments its type takes and whether the
; result is of the base type; kept once read.
(= (mgt:signature $E $head)
   (case $head
     (((Tm.named $id)
       (let $kept (collapse (match &self (TermSignature $id $n $base) (Signature $n $base)))
         (if (== $kept ())
             (let ($arity $is-base) (mgt:read-signature (mg:declaration (eq:theory $E) $id))
               (let $done (add-atom &self (TermSignature $id $arity $is-base)) (Signature $arity $is-base)))
             (car-atom $kept))))
      ($other None))))
(= (mgt:read-signature $declaration)
   (let* (($kind (car-atom $declaration))
          ($after-kind (cdr-atom $declaration))
          ($after-name (cdr-atom $after-kind))
          ($type (car-atom $after-name)))
     (if (or (== $kind Megalodon.parameter) (== $kind Megalodon.definition))
         (mgt:arrows $type 0)
         (0 False))))
(= (mgt:arrows $type $n)
   (case $type
     (((Tp.arr $a $b) (mgt:arrows $b (+ $n 1)))
      ((Tp.base $i) ($n True))
      ($other ($n False)))))

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

; The known NAME, quantifiers and premises around L = R, proved again with
; R = L in its place and kept in the theory under the name NEW.
(= (eq:turned $E $name $new)
   (let $claim (eq:turn $E (mg:proposition (mg:known (eq:theory $E) $name)))
     (mg:prove-by (eq:theory $E) $new $claim (Tac.intros (Tac.prove (eq:name $E) (Prover.turned $name))))))
(= (eq:turn $E $p)
   (case $p
     (((Tm.all $A $body) (let $b (eq:turn $E $body) (Tm.all $A $b)))
      ((Tm.imp $premise $rest) (let $r (eq:turn $E $rest) (Tm.imp $premise $r)))
      ($other (case (eq:sides $E $p) (((Sides $a $b) (eq:of $E $b $a)) ($none $p)))))))
; R = L in a context whose hypotheses are the known's premises.
(= (eq:turned-instance $E $ctx $name $p)
   (case (eq:sides $E $p)
     (((Sides $b $a)
       (let* (($known (mg:known-in (eq:theory $E) $ctx $name))
              ($statement (mg:proposition $known))
              ($shape (rule:conclusion $statement 0)))
         (case $shape
           (((Concludes $n $conclusion)
             (case (eq:sides $E $conclusion)
               (((Sides $left $right)
                 (let $binds (mgt:match (mgt:eta $right) $b $n 0 (mgt:match (mgt:eta $left) $a $n 0 ()))
                   (if (== $binds fail) (empty)
                       (case (rule:instantiate $E $ctx (Prover.tactic Tac.assumption) None $known $statement 0 $binds)
                         (((Instance $thm $stated) (eq:sym $E $ctx $a $b $thm))
                          ($other (empty)))))))
                ($other (empty)))))))))
      ($other (empty)))))

; ---- provers of premises -------------------------------------------------

(= (conv:prove $prover $E $ctx $p)
   (case $prover
     ((Prover.none (empty))
      ((Prover.known $name) (mg:exact (eq:theory $E) $ctx $p (mg:known-in (eq:theory $E) $ctx $name)))
      ((Prover.tactic $tactic) (mg:run $tactic (eq:theory $E) $ctx $p))
      ((Prover.turned $name) (eq:turned-instance $E $ctx $name $p))
      ((Prover.horn $lemmas $depth)
       (let $found (horn:prove $E $ctx (cdr-atom $lemmas) $depth $p)
         (if (== $found None) (empty) $found)))
      ($other (conv:prove-with $other $E $ctx $p)))))

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
; quantifier the match did not bind takes DEFAULT.  The answer is
; (Instance THEOREM PROPOSITION), or why there is none:
;   (Open PREMISE)   the prover gave no proof of this premise: an open goal.
;                    The instance exists for whoever proves or assumes it
;   (Unstated K)     quantifier K is not determined and has no default: the
;                    instance cannot be stated until a witness is given
;   None             the kernel refused a step
(= (rule:instantiate $E $ctx $prover $default $thm $p $k $binds)
   (case $p
     (((Tm.all $A $body)
       (let $bound (mgt:bound $k $binds)
         (let $term (if (== $bound None) (rule:witness $E $ctx $prover $default $body $k $binds) $bound)
           (if (== $term None) (Unstated $k)
               (case (mg:try-term-app $thm $term)
                 (((Error $call $why) None)
                  ($next (rule:instantiate $E $ctx $prover $default $next (mgt:inst $body $term) (+ $k 1) $binds))))))))
      ((Tm.imp $premise $rest)
       (let $proved (collapse (conv:prove $prover $E $ctx $premise))
         (if (== $proved ()) (Open $premise)
             (case (mg:try-proof-app $thm (car-atom $proved))
               (((Error $call $why) None)
                ($next (rule:instantiate $E $ctx $prover $default $next $rest $k $binds)))))))
      ($other (Instance $thm $p)))))

; The term for a quantifier the match did not bind: a witness the prover
; finds from the rest of the statement (a universe level read off the type
; of the thing a premise says lives at that level), else DEFAULT, else None.
; BODY is the statement after the quantifier, the quantified variable its
; variable 0; K is the quantifier's number, so that a later quantifier the
; match did bind is found in BINDS.
(= (rule:witness $E $ctx $prover $default $body $k $binds)
   (let $found (conv:witness $prover $E $ctx $body $k $binds)
     (if (== $found None) $default $found)))
(= (conv:witness $prover $E $ctx $body $k $binds)
   (case $prover
     ((Prover.none None)
      ((Prover.known $name) None)
      ((Prover.tactic $tactic) None)
      ((Prover.turned $name) None)
      ((Prover.horn $lemmas $depth) None)
      ($other (conv:witness-with $other $E $ctx $body $k $binds)))))

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
      ((Cv.viewed $set $views $prover $default) (conv:viewed $E $ctx $set $views $prover $default $t))
      ((Cv.eval $inner) (conv:eval $E $ctx (Cv.eval $inner) $t))
      ((Cv.eval-needed $set $inner) (conv:eval $E $ctx (Cv.eval-needed $set $inner) $t))
      ((Cv.under $inner) (conv:under $E $ctx $inner $t))
      ((Cv.strong $inner) (conv:strong $E $ctx $inner $t))
      ($other (conv:run-with $other $E $ctx $t)))))

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
             (if (== $left $right)
                 (Error (conv:declare $name) (MegalodonMalformed "the two sides of the known's equation are the same term"))
             (let* (($head (mgt:head (mgt:eta $left)))
                    ($before (conv:declared $set $head))
                    ($names (mgt:snoc $before $name))
                    ($gone (if (== $before ()) () (remove-atom &self (ConvRules $set $head $before))))
                    ($done (add-atom &self (ConvRules $set $head $names))))
               $head)))
            ($other (Error (conv:declare $name) (MegalodonMalformed "the known does not conclude with an equation"))))))))))
(= (conv:rules $E $ctx $set $prover $default $t)
   (let $head (mgt:head $t)
     (conv:first-rule $E $ctx (conv:declared $set $head) $prover $default $t)))
(= (conv:first-rule $E $ctx $names $prover $default $t) (conv:rules-from $E $ctx $names $prover $default $t None))
; The rules in order.  A rule whose left side matches while its instance was
; not made is passed over (its premise may be a guard that a later rule's
; premise answers the other way); but where no rule applies and one was
; passed over, the term is not a term no rule applies to.  It is left as it
; is, and the evidence keeps the first such rule with its reason:
; (conv:undecided E CONTEXT) lists (Undecided TERM NAME WHY).  An open
; premise there is an open goal: (conv:assuming ..) below computes on with
; it as a hypothesis.
(= (conv:rules-from $E $ctx $names $prover $default $t $passed)
   (if (== $names ()) (conv:nothing-applied $E $ctx $t $passed)
       (case (conv:rule-try $E $ctx (car-atom $names) $prover $default $t)
         ((Same (conv:rules-from $E $ctx (cdr-atom $names) $prover $default $t $passed))
          ((Undecided $why)
           (conv:rules-from $E $ctx (cdr-atom $names) $prover $default $t
                            (if (== $passed None) (Passed (car-atom $names) $why) $passed)))
          ($answer $answer)))))
(= (conv:nothing-applied $E $ctx $t $passed)
   (case $passed
     ((None Same)
      ((Passed $name $why) (conv:undecided-at $E $ctx $t $name $why)))))
(= (conv:undecided-at $E $ctx $t $name $why)
   (let* (($evidence (conv:evidence-for $ctx)) ($key (eq:name $E))
          ($known (collapse (match $evidence (ConvUndecided $key $ctx $t $name $why) known)))
          ($kept (if (== $known ()) (add-atom $evidence (ConvUndecided $key $ctx $t $name $why)) ())))
     Same))
(= (conv:undecided $E $ctx)
   (let* (($evidence (conv:evidence-for $ctx)) ($key (eq:name $E)))
     (collapse (match $evidence (ConvUndecided $key $ctx $t $name $why) (Undecided $t $name $why)))))

; Open goals as hypotheses.  A premise no one proved is not a refusal: the
; rule holds for whoever has the premise.  So a computation that stopped at
; open premises is run again in a context that assumes them, and what it
; proves there depends on the premises themselves and on nothing else (not on
; a rule, not on a proof that was looked for).  An assumed premise can let
; the computation reach further open premises: the caller repeats until a
; round opens none.
;   (conv:open-premises E CONTEXT)       the premises left open in CONTEXT,
;                                        each once, in the order met
;   (conv:assuming E CONTEXT PREMISES)   CONTEXT with the premises as
;                                        hypotheses, the first outermost
;   (conv:close-over E ANSWER PREMISES)  ANSWER, made under those hypotheses,
;                                        with its theorem closed over them:
;                                        P1 -> .. -> Pk -> T = T2
(= (conv:open-premises $E $ctx) (conv:premises-of (conv:undecided $E $ctx) ()))
(= (conv:premises-of $rows $found)
   (if (== $rows ()) $found
       (case (car-atom $rows)
         (((Undecided $t $name (Open $premise))
           (conv:premises-of (cdr-atom $rows)
                             (if (== (mgt:position $premise $found 0) None) (mgt:snoc $found $premise) $found)))
          ($other (conv:premises-of (cdr-atom $rows) $found))))))
(= (conv:assuming $E $ctx $premises)
   (if (== $premises ()) $ctx
       (case (mg:try-proof-binder (eq:theory $E) $ctx (car-atom $premises))
         (((Error $call $why) (Error (conv:assuming (car-atom $premises)) $why))
          ($inner (conv:assuming $E $inner (cdr-atom $premises)))))))
(= (conv:close-over $E $answer $premises)
   (case $answer
     ((Same Same)
      ((Conv $t2 $theorem)
       (let $closed (conv:discharge $theorem (conv:reversed $premises ()))
         (if (== $closed None) None (Conv $t2 $closed)))))))
(= (conv:reversed $xs $acc) (if (== $xs ()) $acc (let $next (cons-atom (car-atom $xs) $acc) (conv:reversed (cdr-atom $xs) $next))))
(= (conv:discharge $theorem $innermost-first)
   (if (== $innermost-first ()) $theorem
       (case (mg:try-proof-lam-of $theorem (car-atom $innermost-first))
         (((Error $call $why) None)
          ($closed (conv:discharge $closed (cdr-atom $innermost-first)))))))

; Evidence is not knowledge.  The answers a conversion kept, and where each
; value came from, record what was done; rules and declarations say what
; holds.  Evidence is kept in the program's own space unless a space is
; given for it, (conv:keep-evidence-in SPACE); in a space of its own it does
; not change what a query of the program's space answers.  A row is kept
; under the NAME of the theory's equality, not under the theory: a sealed
; object inside a query cannot be looked up by an index, and every stored
; row would be read.  So one evidence space serves one theory.
(= (conv:evidence)
   (let $given (collapse (match &self (ConvEvidence space $space) $space))
     (if (== $given ()) &self (car-atom $given))))
(= (conv:keep-evidence-in $space)
   (let $before (collapse (match &self (ConvEvidence space $old) $old))
     (if (== $before ()) (add-atom &self (ConvEvidence space $space))
         (let* (($old (car-atom $before)) ($gone (remove-atom &self (ConvEvidence space $old))))
           (add-atom &self (ConvEvidence space $space))))))
; A row must not hold a theorem.  A space cannot look up by an index the
; rows that hold a sealed object: it reads every such row at every lookup.
; So a theorem made in the empty context is put in its theory under a name
; of its own (megalodon_kept_N) and the row holds (Named NAME); the theory
; is where theorems are kept.  A theorem made under binders or hypotheses
; cannot be a known of the theory: its row holds it, (Held THEOREM), and
; such rows go to a second space so that they slow only one another.
(= (conv:in-empty-context $ctx) (let $empty (mg:empty-context) (== $ctx $empty)))
(= (conv:seal-out $E $ctx $theorem)
   (case (conv:close-in $theorem $ctx)
     (((Error $call $why) (Held $theorem))
      ($closed
       (let $name (conv:next-name)
         (case (mg:try-publish (eq:theory $E) $closed $name)
           (((Error $call2 $why2) (Held $theorem))
            ($published (Named $name)))))))))
(= (conv:seal-in $E $ctx $kept)
   (case $kept
     (((Named $name) (conv:open-in $E $ctx (mg:known-in (eq:theory $E) $ctx $name)))
      ((Held $theorem) $theorem))))
; A theorem of a context with variables and hypotheses is kept closed over
; them: every hypothesis discharged, innermost first, then every variable
; generalized.  That is a theorem of the empty context, which the theory
; keeps under a name; opened in the context again (at its variables, by its
; hypotheses) it is the theorem it was.  So the rows of every context are
; plain, and none is read that a lookup does not ask for.
(= (conv:close-in $theorem (Megalodon.context $depth $types $hypotheses))
   (if (== $depth 0)
       (conv:close-variables (conv:close-hypotheses $theorem (cdr-atom $hypotheses)) (cdr-atom $types))
       (Error (conv:close-in $depth) "a context under a type binder")))
(= (conv:close-hypotheses $theorem $hypotheses)
   (if (== $hypotheses ()) $theorem
       (case (mg:try-proof-lam $theorem)
         (((Error $call $why) (Error $call $why))
          ($closed (conv:close-hypotheses $closed (cdr-atom $hypotheses)))))))
(= (conv:close-variables $theorem $types)
   (case $theorem
     (((Error $call $why) (Error $call $why))
      ($open
       (if (== $types ()) $open
           (case (mg:try-term-lam $open)
             (((Error $call2 $why2) (Error $call2 $why2))
              ($closed (conv:close-variables $closed (cdr-atom $types))))))))))
(= (conv:open-in $E $ctx $closed)
   (let (Megalodon.context $depth $types $hypotheses) $ctx
     (conv:open-hypotheses $E $ctx (conv:open-variables $closed (mgt:count (cdr-atom $types)))
                           (mgt:count (cdr-atom $hypotheses)))))
(= (mgt:count $xs) (if (== $xs ()) 0 (+ 1 (mgt:count (cdr-atom $xs)))))
(= (conv:open-variables $theorem $n)
   (if (== $n 0) $theorem
       (case (mg:try-term-app $theorem (Tm.db (- $n 1)))
         (((Error $call $why) (Error $call $why))
          ($opened (conv:open-variables $opened (- $n 1)))))))
(= (conv:open-hypotheses $E $ctx $theorem $k)
   (case $theorem
     (((Error $call $why) (Error $call $why))
      ($open
       (if (== $k 0) $open
           (case (mg:try-proof-app $open (mg:hyp (eq:theory $E) $ctx (- $k 1)))
             (((Error $call2 $why2) (Error $call2 $why2))
              ($opened (conv:open-hypotheses $E $ctx $opened (- $k 1))))))))))
(= (conv:next-name)
   (let* (($before (collapse (match &self (ConvKeptCount count $n) $n)))
          ($n (if (== $before ()) 0 (car-atom $before)))
          ($gone (if (== $before ()) () (remove-atom &self (ConvKeptCount count $n))))
          ($m (+ $n 1))
          ($done (add-atom &self (ConvKeptCount count $m))))
     (format-args "megalodon_kept_{}" ($m))))
; The space for rows about CONTEXT: the evidence space, or for a context
; with binders or hypotheses the space of held theorems.
(= (conv:evidence-for $ctx)
   (if (conv:in-empty-context $ctx) (conv:evidence)
       (let $given (collapse (match &self (ConvEvidence held $space) $space))
         (if (== $given ())
             (let* (($space (new-space)) ($kept (add-atom &self (ConvEvidence held $space)))) $space)
             (car-atom $given)))))
(= (conv:answer-out $E $ctx $answer)
   (case $answer ((Same Same) ((Conv $value $theorem) (let $kept (conv:seal-out $E $ctx $theorem) (Conv $value $kept))))))
(= (conv:answer-in $E $ctx $row)
   (case $row ((Same Same) ((Conv $value $kept) (let $theorem (conv:seal-in $E $ctx $kept) (Conv $value $theorem))))))
(= (conv:answers-in $E $ctx $rows)
   (if (== $rows ()) ()
       (let* (($first (conv:answer-in $E $ctx (car-atom $rows))) ($rest (conv:answers-in $E $ctx (cdr-atom $rows))))
         (cons-atom $first $rest))))
(= (conv:kept $E $ctx $conversion $t)
   (let* (($evidence (conv:evidence-for $ctx)) ($key (eq:name $E)))
     (conv:answers-in $E $ctx (collapse (match $evidence (ConvFact $key $ctx $conversion $t $answer) $answer)))))
(= (conv:keep $E $ctx $conversion $t $answer)
   (let* (($evidence (conv:evidence-for $ctx)) ($key (eq:name $E)) ($row (conv:answer-out $E $ctx $answer)))
     (add-atom $evidence (ConvFact $key $ctx $conversion $t $row))))
(= (conv:keep-origin $E $ctx $value $t $theorem)
   (let* (($evidence (conv:evidence-for $ctx)) ($key (eq:name $E)) ($kept (conv:seal-out $E $ctx $theorem)))
     (add-atom $evidence (ConvOrigin $key $ctx $value $t $kept))))

; Answers kept: a conversion asked again of the same term in the same
; equational theory and context is one lookup.
(= (conv:memo $E $ctx $inner $t)
   (let $kept (conv:kept $E $ctx $inner $t)
     (if (== $kept ())
         (let $answer (conv:run $E $ctx $inner $t)
           (let $done (conv:keep $E $ctx $inner $t $answer) $answer))
         (car-atom $kept))))

; A known read as a rule.  Its statement is quantifiers and premises around
; an equation L = R; where L matches the term, the quantifiers are
; instantiated in order and each premise is proved by the prover.  No answer
; of the prover for a premise, or no match, leaves the term as it is; so
; does an instance whose R is the term itself (a name the kernel holds to
; be another name of the same definition unfolds to itself).
(= (conv:rule $E $ctx $name $prover $default $t)
   (case (conv:rule-try $E $ctx $name $prover $default $t) (((Undecided $why) Same) ($answer $answer))))
; The same with a third answer: (Undecided WHY) where the left side matches
; the term and the instance was not made; WHY is (Open PREMISE),
; (Unstated K) or Refused, as rule:instantiate says.  Same is then "this
; rule does not apply here".
(= (conv:rule-try $E $ctx $name $prover $default $t)
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
                         (((Sides $l $r)
                           (let $t2 (mgt:head-beta $r) (if (== $t2 $t) Same (Conv $t2 $thm))))
                          ($other Same))))
                      ((Open $premise) (Undecided (Open $premise)))
                      ((Unstated $quantifier) (Undecided (Unstated $quantifier)))
                      ($other (Undecided Refused)))))))
            ($other Same))))))))
; A theorem stating L = R, at a term the kernel finds convertible with L.
(= (conv:equation $E $ctx $theorem $t)
   (case (eq:sides $E (mg:proposition $theorem))
     (((Sides $left $right)
       (case (mg:try-convertible (eq:theory $E) $ctx $left $t)
         (((Error $call $why) Same)
          ($same (if $same (Conv $right $theorem) Same)))))
      ($other Same))))

; Rules with views: each rule of the set in the order declared, as the term
; stands and then after views, so that an earlier rule that needs a view is
; preferred to a later one that needs none.
(= (conv:viewed $E $ctx $set $views $prover $default $t)
   (conv:view-first $E $ctx (conv:declared $set (mgt:head $t)) $views $prover $default $t))
(= (conv:view-first $E $ctx $names $views $prover $default $t)
   (conv:view-from $E $ctx $names $views $prover $default $t None))
(= (conv:view-from $E $ctx $names $views $prover $default $t $passed)
   (if (== $names ()) (conv:nothing-applied $E $ctx $t $passed)
       (case (conv:rule-try $E $ctx (car-atom $names) $prover $default $t)
         ((Same
           (case (conv:view-rule $E $ctx (car-atom $names) $views $prover $default $t ())
             ((Same (conv:view-from $E $ctx (cdr-atom $names) $views $prover $default $t $passed))
              ((Undecided $reason)
               (conv:view-from $E $ctx (cdr-atom $names) $views $prover $default $t
                               (if (== $passed None) (Passed (car-atom $names) $reason) $passed)))
              ($viewed $viewed))))
          ((Undecided $why)
           (conv:view-from $E $ctx (cdr-atom $names) $views $prover $default $t
                           (if (== $passed None) (Passed (car-atom $names) $why) $passed)))
          ($answer $answer)))))
; One rule after views.  SEEN holds the terms already viewed on the way, so
; that views which lead back to a term stop there.  A rule that matches after
; a view while its instance was not made is passed over as one that matches
; as the term stands: (Undecided WHY).
(= (conv:view-rule $E $ctx $name $views $prover $default $t $seen)
   (let* (($known (mg:known-in (eq:theory $E) $ctx $name))
          ($shape (rule:conclusion (mg:proposition $known) 0)))
     (case $shape
       (((Concludes $n $conclusion)
         (case (eq:sides $E $conclusion)
           (((Sides $left $right)
             (case (mgt:mismatch (mgt:eta $left) $t ())
               (((At $path)
                 (let $viewed (conv:at $E $ctx $path $views $t)
                   (case $viewed
                     ((Same Same)
                      ((Conv $t2 $thm2)
                       (if (conv:view-seen $t2 $seen) Same
                           (case (conv:rule-try $E $ctx $name $prover $default $t2)
                             ((Same
                               (case (conv:view-rule $E $ctx $name $views $prover $default $t2 (cons-atom $t $seen))
                                 ((Same Same)
                                  ((Undecided $reason) (Undecided $reason))
                                  ($more (conv:join $E $ctx $t $viewed $more)))))
                              ((Undecided $why) (Undecided $why))
                              ($after (conv:join $E $ctx $t $viewed $after))))))))))
                ($other Same))))
            ($other Same))))))))
(= (conv:view-seen $t $seen)
   (if (== $seen ()) False (if (== (car-atom $seen) $t) True (conv:view-seen $t (cdr-atom $seen)))))

; Evaluation, each answer kept under the conversion and the term.
;
; Cv.eval computes every argument before the whole.  Cv.eval-needed computes
; first only the arguments that some rule of SET needs in a particular form:
; those where a rule's left side has something other than a variable, or a
; variable it has twice.  The rest are computed when their value is asked
; for, or, where no rule applies with the needed ones in hand, before one
; more try.  So the branches of a conditional wait for the condition, and a
; recursive call in the branch not taken is never made.  A head with no rule
; in SET, or declared strict (conv:declare-strict), takes all its arguments
; computed.
(= (conv:eval-step $self) (case $self (((Cv.eval $inner) $inner) ((Cv.eval-needed $set $inner) $inner))))
(= (conv:eval $E $ctx $self $t)
   (case $t
     (((Tm.app $f $x)
       (let $kept (conv:kept $E $ctx $self $t)
         (if (== $kept ())
             (conv:eval-keep $E $ctx $self $t (conv:eval-fresh $E $ctx $self $t))
             (car-atom $kept))))
      ((Tm.named $id)
       (let $kept (conv:kept $E $ctx $self $t)
         (if (== $kept ())
             (conv:eval-keep $E $ctx $self $t (conv:eval-root $E $ctx $self $t all))
             (car-atom $kept))))
      ($other Same))))
; An answer is kept under its term, and a value also under itself with the
; term it came from, so that the origins of a value are found by the value.
(= (conv:eval-keep $E $ctx $self $t $answer)
   (case $answer
     ((Same (let $done (conv:keep $E $ctx $self $t Same) Same))
      ((Conv $value $theorem)
       (let* (($evidence (conv:evidence-for $ctx)) ($key (eq:name $E))
              ($kept (conv:seal-out $E $ctx $theorem))
              ($done (add-atom $evidence (ConvFact $key $ctx $self $t (Conv $value $kept))))
              ($origin (add-atom $evidence (ConvOrigin $key $ctx $value $t $kept))))
         $answer)))))
; The terms a value was computed from in this theory and context, each with
; the theorem TERM = VALUE: a list of (Origin TERM THEOREM).
(= (conv:origins $E $ctx $value)
   (let* (($evidence (conv:evidence-for $ctx)) ($key (eq:name $E)))
     (conv:origins-in $E $ctx (collapse (match $evidence (ConvOrigin $key $ctx $value $term $kept) (Origin $term $kept))))))
(= (conv:origins-in $E $ctx $rows)
   (if (== $rows ()) ()
       (let* (((Origin $term $kept) (car-atom $rows))
              ($theorem (conv:seal-in $E $ctx $kept))
              ($rest (conv:origins-in $E $ctx (cdr-atom $rows))))
         (cons-atom (Origin $term $theorem) $rest))))
(= (conv:eval-fresh $E $ctx $self $t)
   (let* (($flags (conv:eval-flags $E $ctx $self (mgt:head $t)))
          ($parts (conv:eval-parts $E $ctx $self $t $t () Same $flags True)))
     (case $parts
       ((Same (conv:eval-root $E $ctx $self $t $flags))
        ((Conv $t1 $thm) (conv:join $E $ctx $t $parts (conv:eval $E $ctx $self $t1)))))))
(= (conv:eval-root $E $ctx $self $t $flags)
   (let $stepped (conv:run $E $ctx (conv:eval-step $self) $t)
     (case $stepped
       ((Same (if (== $flags all) Same (conv:eval-rest $E $ctx $self $t $flags)))
        ((Conv $t2 $thm) (conv:join $E $ctx $t $stepped (conv:eval $E $ctx $self $t2)))))))
; No rule applies with the needed arguments in hand: the others are
; computed, and the step is tried once more.
(= (conv:eval-rest $E $ctx $self $t $flags)
   (let $parts (conv:eval-parts $E $ctx $self $t $t () Same $flags False)
     (case $parts
       ((Same Same)
        ((Conv $t1 $thm)
         (let $stepped (conv:run $E $ctx (conv:eval-step $self) $t1)
           (case $stepped
             ((Same $parts)
              ((Conv $t2 $thm2)
               (conv:join $E $ctx $t $parts (conv:join $E $ctx $t1 $stepped (conv:eval $E $ctx $self $t2))))))))))))
; The arguments along the spine of NODE, a part of T at PATH, whose flag is
; WANTED; ACC is the answer for T so far.
(= (conv:eval-parts $E $ctx $self $t $node $path $acc $flags $wanted)
   (case $node
     (((Tm.app $f $x)
       (let $acc1 (conv:eval-parts $E $ctx $self $t $f (mgt:snoc $path fun) $acc $flags $wanted)
         (if (== (conv:eval-flag $flags (mgt:spine-length $f)) $wanted)
             (conv:eval-arg $E $ctx $self $t $x (mgt:snoc $path arg) $acc1)
             $acc1)))
      ($other $acc))))
(= (conv:eval-flag $flags $index)
   (if (== $flags all) True
       (if (== $flags ()) True
           (if (== $index 0) (car-atom $flags) (conv:eval-flag (cdr-atom $flags) (- $index 1))))))
(= (conv:eval-arg $E $ctx $self $t $x $path $acc)
   (case $x
     (((Tm.app $g $y)
       (if (mgt:whole $E $x)
           (let $r (conv:eval $E $ctx $self $x)
             (case $r
               ((Same $acc)
                ((Conv $x2 $thm)
                 (conv:join $E $ctx $t $acc (conv:lift $E $ctx (conv:result $t $acc) $path $x $r))))))
           (conv:eval-parts $E $ctx $self $t $x $path $acc all True)))
      ((Tm.named $id)
       (let $r (conv:eval $E $ctx $self $x)
         (case $r
           ((Same $acc)
            ((Conv $x2 $thm)
             (if (mgt:whole $E $x)
                 (conv:join $E $ctx $t $acc (conv:lift $E $ctx (conv:result $t $acc) $path $x $r))
                 $acc))))))
      ($other $acc))))
; Which arguments a head needs computed first: all, or a flag for each.
(= (conv:eval-flags $E $ctx $self $head)
   (case $self
     (((Cv.eval $inner) all)
      ((Cv.eval-needed $set $inner) (conv:needed $E $ctx $set $head)))))
(= (conv:declare-strict $set $head) (add-atom &self (ConvNeeded $set $head all)))
(= (conv:needed $E $ctx $set $head)
   (let $kept (collapse (match &self (ConvNeeded $set $head $flags) $flags))
     (if (== $kept ())
         (let $flags (conv:needed-by $E $ctx (conv:declared $set $head) all)
           (let $done (add-atom &self (ConvNeeded $set $head $flags)) $flags))
         (car-atom $kept))))
(= (conv:needed-by $E $ctx $names $so-far)
   (if (== $names ()) $so-far
       (let $one (conv:rule-needs $E $ctx (car-atom $names))
         (conv:needed-by $E $ctx (cdr-atom $names) (if (== $so-far all) $one (conv:flags-or $so-far $one))))))
(= (conv:flags-or $a $b)
   (if (== $a ()) $b
       (if (== $b ()) $a
           (let* (($x (car-atom $a)) ($y (car-atom $b)) ($rest (conv:flags-or (cdr-atom $a) (cdr-atom $b)))
                  ($either (or $x $y)))
             (cons-atom $either $rest)))))
(= (conv:rule-needs $E $ctx $name)
   (case (rule:conclusion (mg:proposition (mg:known-in (eq:theory $E) $ctx $name)) 0)
     (((Concludes $n $conclusion)
       (case (eq:sides $E $conclusion)
         (((Sides $left $right) (let $args (mgt:spine-args (mgt:eta $left)) (conv:args-need $args $args 0)))
          ($other ()))))
      ($other ()))))
(= (conv:args-need $args $all $i)
   (if (== $args ()) ()
       (let* (($arg (car-atom $args))
              ($rest (conv:args-need (cdr-atom $args) $all (+ $i 1)))
              ($flag (case $arg (((Tm.db $j) (conv:variable-elsewhere $j $all $i 0)) ($other True)))))
         (cons-atom $flag $rest))))
(= (conv:variable-elsewhere $j $all $i $k)
   (if (== $all ()) False
       (if (== $k $i) (conv:variable-elsewhere $j (cdr-atom $all) $i (+ $k 1))
           (if (mgt:mentions (car-atom $all) $j) True
               (conv:variable-elsewhere $j (cdr-atom $all) $i (+ $k 1))))))

; Under a binder.  The kernel converts under a lambda by beta, eta and
; unfolding only; an equation made by a theorem is carried under the binder
; by the family lemma of the binder form: with a new variable x and the
; hypothesis that x is in X, the body is converted; the equation is
; generalized over x; the lemma gives H X F = H X G.
(= (conv:declare-binder $head $name) (add-atom &self (ConvBinder $head $name)))
(= (conv:under $E $ctx $c $t)
   (case $t
     (((Tm.app (Tm.app $h $X) $F)
       (let $lemmas (collapse (match &self (ConvBinder $h $name) $name))
         (if (== $lemmas ()) Same (conv:under-with $E $ctx $c $h $X $F (car-atom $lemmas)))))
      ($other Same))))
(= (conv:under-with $E $ctx $c $h $X $F $lemma)
   (case (mg:try-term-binder (eq:theory $E) $ctx (Tp.base 0))
     (((Error $call $why) Same)
      ($inner
       (let* (($known (mg:known-in (eq:theory $E) $ctx $lemma))
              ($member (conv:binder-membership (mg:proposition $known)))
              ($hypothesis (Tm.app (Tm.app $member (Tm.db 0)) (mgt:shift $X 1 0)))
              ($body (mgt:beta (Tm.app (mgt:shift $F 1 0) (Tm.db 0)))))
         (case (mg:try-proof-binder (eq:theory $E) $inner $hypothesis)
           (((Error $call2 $why2) Same)
            ($inner2
             (case (conv:run $E $inner2 $c $body)
               (((Conv $body2 $theorem) (conv:under-close $E $h $X $F $body2 $hypothesis $theorem $known))
                ($other Same)))))))))))
; The membership relation the lemma speaks of, read from its premise.
(= (conv:binder-membership $statement)
   (case $statement
     (((Tm.all $A $body) (conv:binder-membership $body))
      ((Tm.imp (Tm.all $B (Tm.imp (Tm.app (Tm.app $member $x) $X) $equation)) $conclusion) $member)
      ($other None))))
(= (conv:under-close $E $h $X $F $body2 $hypothesis $theorem $known)
   (case (mg:try-proof-lam-of $theorem $hypothesis)
     (((Error $call $why) Same)
      ($over-hypothesis
       (case (mg:try-term-lam-at $over-hypothesis (Tp.base 0))
         (((Error $call2 $why2) Same)
          ($over-variable
           (let $G (Tm.lam (Tp.base 0) $body2)
             (case (mg:try-term-app (mg:term-app (mg:term-app $known $X) $F) $G)
               (((Error $call3 $why3) Same)
                ($instance
                 (case (mg:try-proof-app $instance $over-variable)
                   (((Error $call4 $why4) Same)
                    ($equation (Conv (Tm.app (Tm.app $h $X) $G) $equation)))))))))))))))
(= (conv:strong $E $ctx $c $t)
   (let $kept (conv:kept $E $ctx (Cv.strong $c) $t)
     (if (== $kept ())
         (let* (($value (conv:run $E $ctx $c $t))
                ($inside (conv:strong-inside $E $ctx $c (conv:result $t $value)))
                ($answer (conv:join $E $ctx $t $value $inside))
                ($done (conv:keep $E $ctx (Cv.strong $c) $t $answer)))
           $answer)
         (car-atom $kept))))
; The binder forms in a term that is already a value: in its arguments
; first, then at the term itself.
(= (conv:strong-inside $E $ctx $c $t)
   (let* (($parts (conv:strong-args $E $ctx $c $t $t () Same))
          ($root (conv:under $E $ctx (Cv.strong $c) (conv:result $t $parts))))
     (conv:join $E $ctx $t $parts $root)))
(= (conv:strong-args $E $ctx $c $t $node $path $acc)
   (case $node
     (((Tm.app $f $x)
       (let $acc1 (conv:strong-args $E $ctx $c $t $f (mgt:snoc $path fun) $acc)
         (conv:strong-arg $E $ctx $c $t $x (mgt:snoc $path arg) $acc1)))
      ($other $acc))))
(= (conv:strong-arg $E $ctx $c $t $x $path $acc)
   (case $x
     (((Tm.app $g $y)
       (if (mgt:whole $E $x)
           (let $r (conv:strong-inside $E $ctx $c $x)
             (case $r
               ((Same $acc)
                ((Conv $x2 $theorem)
                 (conv:join $E $ctx $t $acc (conv:lift $E $ctx (conv:result $t $acc) $path $x $r))))))
           (conv:strong-args $E $ctx $c $t $x $path $acc)))
      ($other $acc))))
; Whether a rule of the set for this head has one variable twice on its left
; side: such a rule compares two parts of the term, and they may be equal
; only as values under their binders.
(= (conv:nonlinear $E $ctx $set $head)
   (let $kept (collapse (match &self (ConvNonlinear $set $head $answer) $answer))
     (if (== $kept ())
         (let $answer (conv:any-nonlinear $E $ctx (conv:declared $set $head))
           (let $done (add-atom &self (ConvNonlinear $set $head $answer)) $answer))
         (car-atom $kept))))
(= (conv:any-nonlinear $E $ctx $names)
   (if (== $names ()) False
       (if (conv:rule-nonlinear $E $ctx (car-atom $names)) True (conv:any-nonlinear $E $ctx (cdr-atom $names)))))
(= (conv:rule-nonlinear $E $ctx $name)
   (case (rule:conclusion (mg:proposition (mg:known-in (eq:theory $E) $ctx $name)) 0)
     (((Concludes $n $conclusion)
       (case (eq:sides $E $conclusion)
         (((Sides $left $right) (let $args (mgt:spine-args (mgt:eta $left)) (conv:repeats $args $args 0)))
          ($other False))))
      ($other False))))
(= (conv:repeats $args $all $i)
   (if (== $args ()) False
       (let $here (case (car-atom $args) (((Tm.db $j) (conv:variable-elsewhere $j $all $i 0)) ($other False)))
         (if $here True (conv:repeats (cdr-atom $args) $all (+ $i 1))))))

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
