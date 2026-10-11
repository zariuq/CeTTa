; nuPLN over lambda hypotheses.
;
; Nil Geisweiller's nuPLN backward chainer with its combinators replaced by
; lambdas.  A predicate is (lam $x body); a statement about an individual is
; the predicate's body at that individual; the only binder is lam.  An
; existential over a finite domain A is (exists A (lam $y body)) and a
; recursive predicate is (fix (lam $p body)), so lam is their binder too.
; The connectives are Nil's: ∧, ∨ and ¬.
;
; Every piece of syntax inside a judgment is code (@t), and syntax is taken
; apart only by unification against code patterns.  Nothing in a hypothesis
; runs while it is typed or evaluated, even when its predicates and
; connectives are ordinary functions of the program.  A binder is the fresh
; variable of the rule activation that introduces it, so two hypotheses
; never share a binder and no renaming is written by hand.  Applying a
; hypothesis to an individual opens its binder there: the instance is a new
; code value and the hypothesis keeps its binder.  Application is
; conversion, so a statement about (h c) is proven by proving it about the
; instance.  A learned program is activated with `*` and then runs as
; ordinary code.
;
; Judgments, each returning (MkSized size judgment) as in nuPLN:
;   (nu:type $kb $ctx $budget (: @t T))        t has type T in context ctx
;   (nu:eval $kb $budget (: proof (≞ @s tv)))  closed statement s has value tv
; A context is nil or (ctx (ov $x A) rest).  Truth values are ⊤ and ⊥.
; The knowledge base declares the domains binders range over, (nu:domain A),
; and the constructs hypotheses may use: (nu:bias ∧), (nu:bias ∨),
; (nu:bias ¬), (nu:bias exists), (nu:bias fix).

;;;;;;;;;;;;;;
;; Contexts ;;
;;;;;;;;;;;;;;

;; Binder types are kept in a context with their arrows written (nu:fn A B):
;; a context that carries an arrow type loses the identity of its variables
;; on this runtime.
(= (nu:enc $t)
   (unify $t (-> $a $b)
     (let $ea (nu:enc $a) (let $eb (nu:enc $b) (nu:fn $ea $eb)){$eb}){$ea}
     $t))
(= (nu:dec $t)
   (unify $t (nu:fn $a $b)
     (let $da (nu:dec $a) (let $db (nu:dec $b) (-> $da $db)){$db}){$da}
     $t))

;; An entry with its type decoded.
(= (nu:decoded $entry)
   (unify $entry (ov $x $e) (let $t (nu:dec $e) (ov $x $t)){$t} $entry))

;; The entry of a variable in scope, found by identity, or none.  One walk
;; returns the whole entry.
(= (nu:ctx-lookup nil $v) none)
(= (nu:ctx-lookup (ctx $e $rest) $v)
   (unify $e (ov $x $t)
     (if (noreduce-eq $x $v) $e (nu:ctx-lookup $rest $v))
     none))

(= (nu:ctx-has $ctx $v) (if (== (nu:ctx-lookup $ctx $v) none) False True))

;; Choosing a variable in scope, outermost first.
(= (nu:ctx-choose nil) (empty))
(= (nu:ctx-choose (ctx $e $rest)) (nu:ctx-choose $rest))
(= (nu:ctx-choose (ctx $e $rest)) (nu:decoded $e))

;; An argument of an atom.  A variable in scope keeps its identity; a fresh
;; unknown becomes one of the variables in scope; a constant, which only a
;; given term contains, is typed by the knowledge base.
(= (nu:arg $kb $ctx $u)
   (let $entry (nu:ctx-lookup $ctx $u)
     (if (== $entry none)
         (if (== (get-metatype $u) Variable)
             (nu:ctx-choose $ctx)
             (match $kb (: $u $t) (ov $u $t)){$u $t})
         (nu:decoded $entry))))

;; The type of a binder: the given one, or else a domain the knowledge base
;; declares.  A binder is typed when it is introduced.
(= (nu:binder-type $kb $a)
   (if (== (get-metatype $a) Variable)
       (match $kb (nu:domain $d) $d){$d}
       $a))

;; Whether a lambda's body uses its binder: opening it at a probe changes
;; the body exactly when the binder occurs.  Code values are compared as
;; written.
(= (nu:uses-binder $lam)
   (unify $lam (quote (lam $x $body))
     (let $opened (nu:open $lam @nu:probe)
       (if (noreduce-eq $opened @$body) False True)){$opened}
     False))

;; The metatype of a term held as written.
(: nu:metatype (-> (Data Atom) $r))
(= (nu:metatype $t) (get-metatype $t))

;; Whether a code value is a metavariable: the whole term is still unknown.
(= (nu:unknown $code)
   (unify $code (quote $t) (== (nu:metatype $t) Variable) False))

;; Whether a code value is an application whose head is a metavariable, such
;; as ($Q $x $y) in a metarule.  Only the atom rules may fill such a head.
(= (nu:head-unknown $code)
   (if (nu:unknown $code)
       False
       (unify $code (quote ($h $a))
         (nu:unknown @$h)
         (unify $code (quote ($h $a $b))
           (nu:unknown @$h)
           False))))

;; A construct rule applies to a term that is unknown or that has the
;; construct's own head.
(= (nu:construct-goal $code) (if (nu:head-unknown $code) False True))

;;;;;;;;;;;;
;; Typing ;;
;;;;;;;;;;;;

;; The head of an atom: a predicate variable in scope, or a predicate the
;; knowledge base declares.  An unknown head may be either.
(= (nu:type-head $kb $ctx $budget (: (quote $p) $t))
   (if (<= 1 $budget)
       (let $entry (nu:ctx-lookup $ctx $p)
         (if (== $entry none)
           (if (== (get-metatype $p) Variable)
               (superpose
                 ((match $kb (: $p $t) (MkSized 1 (: @$p $t))){$p $t}
                  (let (ov $p $t) (nu:ctx-choose $ctx) (MkSized 1 (: @$p $t))){$p $t}))
               (match $kb (: $p $t) (MkSized 1 (: @$p $t))){$p $t})
           (let (ov $q $t) (nu:decoded $entry) (MkSized 1 (: @$p $t))){$q $t})){$entry}
       (empty)))

;; A given constant, predicate or individual the knowledge base declares, as
;; in nuPLN's base case.  Unknown terms are generated only as lambdas and
;; their bodies.
(= (nu:type $kb $ctx $budget (: $term $t))
   (if (nu:unknown $term)
       (empty)
       (unify $term (quote $c)
         (if (<= 1 $budget)
             (match $kb (: $c $t) (MkSized 1 (: @$c $t))){$c $t}
             (empty))
         (empty))))

;; Abstraction.  The binder $x is this activation's fresh variable.
(= (nu:type $kb $ctx $budget (: $term (-> $a $r)))
   (if (nu:construct-goal $term)
     (unify $term (quote (lam $x $body))
       (if (<= 2 $budget)
           (let $dom (nu:binder-type $kb $a)
             (let $stored (nu:enc $dom)
               (let (MkSized $bs (: (quote $b) $r))
                    (nu:type $kb (ctx (ov $x $stored) $ctx) (- $budget 1) (: @$body $r))
                 (MkSized (+ 1 $bs) (: @(lam $x $b) (-> $dom $r)))){$bs $b $r}){$stored}){$dom}
           (empty))
       (empty))
     (empty)))

;; A unary atom (P u).
(= (nu:type $kb $ctx $budget (: $term Bool))
   (unify $term (quote ($p $u))
     (if (<= 3 $budget)
         (let (MkSized $ps (: (quote $p2) (-> $a Bool)))
              (nu:type-head $kb $ctx (- $budget 2) (: @$p (-> $a Bool)))
           (let (ov $u2 $a) (nu:arg $kb $ctx $u)
             (MkSized (+ 2 $ps)
               (: (lift let $u-slot (lift $u2) @($p2 $u-slot)) Bool))){$u2 $a}){$ps $p2 $a}
         (empty))
     (empty)))

;; A binary atom (R u v), R curried.
(= (nu:type $kb $ctx $budget (: $term Bool))
   (unify $term (quote ($r $u $v))
     (if (<= 4 $budget)
         (let (MkSized $rs (: (quote $r2) (-> $a (-> $b Bool))))
              (nu:type-head $kb $ctx (- $budget 3) (: @$r (-> $a (-> $b Bool))))
           (let (ov $u2 $a) (nu:arg $kb $ctx $u)
             (let (ov $v2 $b) (nu:arg $kb $ctx $v)
               (MkSized (+ 3 $rs)
                 (: (lift let $u-slot (lift $u2)
                      (lift let $v-slot (lift $v2) @($r2 $u-slot $v-slot)))
                    Bool))){$v2 $b}){$u2 $a}){$rs $r2 $a $b}
         (empty))
     (empty)))

;; Conjunction ∧ and disjunction ∨: 1 for the connective, and both parts.
(= (nu:type $kb $ctx $budget (: $term Bool))
   (if (nu:construct-goal $term)
     (unify $term (quote (∧ $p $q))
       (match $kb (nu:bias ∧)
         (if (<= 3 $budget)
             (let* (((MkSized $ps (: (quote $p2) Bool))
                     (nu:type $kb $ctx (- $budget 2) (: @$p Bool)))
                    ((MkSized $qs (: (quote $q2) Bool))
                     (nu:type $kb $ctx (- $budget (+ $ps 1)) (: @$q Bool))))
               (MkSized (+ 1 (+ $ps $qs)) (: @(∧ $p2 $q2) Bool))){$ps $p2 $qs $q2}
             (empty)))
       (empty))
     (empty)))

(= (nu:type $kb $ctx $budget (: $term Bool))
   (if (nu:construct-goal $term)
     (unify $term (quote (∨ $p $q))
       (match $kb (nu:bias ∨)
         (if (<= 3 $budget)
             (let* (((MkSized $ps (: (quote $p2) Bool))
                     (nu:type $kb $ctx (- $budget 2) (: @$p Bool)))
                    ((MkSized $qs (: (quote $q2) Bool))
                     (nu:type $kb $ctx (- $budget (+ $ps 1)) (: @$q Bool))))
               (MkSized (+ 1 (+ $ps $qs)) (: @(∨ $p2 $q2) Bool))){$ps $p2 $qs $q2}
             (empty)))
       (empty))
     (empty)))

;; Negation ¬.
(= (nu:type $kb $ctx $budget (: $term Bool))
   (if (nu:construct-goal $term)
     (unify $term (quote (¬ $p))
       (match $kb (nu:bias ¬)
         (if (<= 2 $budget)
             (let (MkSized $ps (: (quote $p2) Bool))
                  (nu:type $kb $ctx (- $budget 1) (: @$p Bool))
               (MkSized (+ 1 $ps) (: @(¬ $p2) Bool))){$ps $p2}
             (empty)))
       (empty))
     (empty)))

;; Existential over a domain A.  A vacuous binder is not generated.
(= (nu:type $kb $ctx $budget (: $term Bool))
   (if (nu:construct-goal $term)
     (unify $term (quote (exists $a (lam $y $body)))
       (match $kb (nu:bias exists)
         (if (<= 3 $budget)
             (let $dom (nu:binder-type $kb $a)
               (let (MkSized $ls (: (quote (lam $y2 $b)) (-> $dom Bool)))
                    (nu:type $kb $ctx (- $budget 1) (: @(lam $y $body) (-> $dom Bool)))
                 (if (nu:uses-binder @(lam $y2 $b))
                     (MkSized (+ 1 $ls)
                       (: (lift let $dom-slot (lift $dom) @(exists $dom-slot (lam $y2 $b)))
                          Bool))
                     (empty))){$ls $b $dom}){$dom}
             (empty)))
       (empty))
     (empty)))

;; Recursion: (fix (lam $p body)) has type T when body has type T with the
;; recursive predicate $p : T in scope.  A body that never calls $p is not
;; generated.
(= (nu:type $kb $ctx $budget (: $term $t))
   (if (nu:construct-goal $term)
     (unify $term (quote (fix (lam $p $body)))
       (match $kb (nu:bias fix)
         (if (<= 3 $budget)
             (let (MkSized $ls (: (quote (lam $p2 $b)) (-> $t $t)))
                  (nu:type $kb $ctx (- $budget 1) (: @(lam $p $body) (-> $t $t)))
               (if (nu:uses-binder @(lam $p2 $b))
                   (MkSized (+ 1 $ls) (: @(fix (lam $p2 $b)) $t))
                   (empty))){$ls $b $t}
             (empty)))
       (empty))
     (empty)))

;; The application of a given hypothesis to a given individual: 1 for the
;; application, the individual, and the hypothesis.
(= (nu:type-app $kb $budget $h $c)
   (if (<= 3 $budget)
       (let* (((MkSized $cs (: (quote $c2) $a))
               (nu:type $kb nil (- $budget 2) (: $c $a)))
              ((MkSized $hs (: (quote $h2) (-> $a $r)))
               (nu:type $kb nil (- $budget (+ $cs 1)) (: $h (-> $a $r)))))
         (MkSized (+ 1 (+ $cs $hs)) (: @($h2 $c2) $r))){$cs $c2 $a $hs $h2 $r}
       (empty)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Opening and instances ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Opening a lambda at an argument: substitution into code, by `lift let`.
;; The match takes the body out with the lambda's binder ($x is that binder
;; itself), and the body's code receives the argument in the binder's slot.
;; The caller's hypothesis keeps its binder, and the argument is inserted as
;; written.
(= (nu:open $code $arg)
   (unify $code (quote (lam $x $body))
     (lift let $x $arg @$body)
     (empty)))

;; An alpha-equivalent copy of code with fresh binders: printing and reading
;; it back, since each read allocates its own variables.
(= (nu:fresh-copy $code) (let $c $code (let $text (repr $c) (parse $text))))

;; The instance of a hypothesis at its arguments.  A recursive hypothesis
;; unfolds once: its binder is opened at a fresh copy of the hypothesis
;; itself, so the inserted code never shares a binder with the code it
;; enters.  Then each argument opens one binder, in order.
(= (nu:unfold-head $h)
   (unify $h (quote (fix $f))
     (let $copy (nu:fresh-copy $h) (nu:open @$f $copy)){$copy}
     $h))

(= (nu:open-args $h $args)
   (if (== $args ())
       $h
       (let ($c $rest) (decons-atom $args)
         (let $opened (nu:open $h (lift $c))
           (nu:open-args $opened $rest)){$opened}){$c $rest}))

(= (nu:at $h $args)
   (let $unfolded (nu:unfold-head $h)
     (nu:open-args $unfolded $args)))

;;;;;;;;;;;;;;;;
;; Evaluation ;;
;;;;;;;;;;;;;;;;

;; Evidence in the knowledge base.
(= (nu:eval $kb $budget (: $prf (≞ $s $tv)))
   (unify $s (quote $a)
     (if (<= 1 $budget)
         (match $kb (: $prf (≞ (quote $a) $tv)) (MkSized 1 (: $prf (≞ @$a $tv)))){$prf $a $tv}
         (empty))
     (empty)))

;; Conjunction evaluates to ⊤ when both parts do (cet), to ⊥ when the left
;; (cefl) or else the right (cefr) part does.  The choice rules commit to
;; their first alternative, so every goal has at most one proof.
(= (nu:eval $kb $budget (: (cet $pp $qp) (≞ $s ⊤)))
   (unify $s (quote (∧ $p $q))
     (if (<= 3 $budget)
         (let* (((MkSized $ps (: $pp (≞ $sp ⊤)))
                 (nu:eval $kb (- $budget 2) (: $pp (≞ @$p ⊤))))
                ((MkSized $qs (: $qp (≞ $sq ⊤)))
                 (nu:eval $kb (- $budget (+ $ps 1)) (: $qp (≞ @$q ⊤)))))
           (MkSized (+ 1 (+ $ps $qs)) (: (cet $pp $qp) (≞ $s ⊤)))){$ps $pp $sp $qs $qp $sq}
         (empty))
     (empty)))

(= (nu:eval $kb $budget (: $prf (≞ $s ⊥)))
   (unify $s (quote (∧ $p $q))
     (if (<= 2 $budget)
         (let $left (collapse (nu:eval $kb (- $budget 1) (: $pp (≞ @$p ⊥))))
           (if (== $left ())
               (let (MkSized $qs (: $qp $jq))
                    (nu:first (nu:eval $kb (- $budget 1) (: $qp (≞ @$q ⊥))))
                 (MkSized (+ 1 $qs) (: (cefr $qp) (≞ $s ⊥)))){$qs $qp $jq}
               (let (MkSized $ps (: $pp2 $jp)) (car-atom $left)
                 (MkSized (+ 1 $ps) (: (cefl $pp2) (≞ $s ⊥)))){$ps $pp2 $jp})){$left}
         (empty))
     (empty)))

;; Disjunction: ⊤ from the left (cotl) or else the right (cotr) part, ⊥ from
;; both (cof).
(= (nu:eval $kb $budget (: $prf (≞ $s ⊤)))
   (unify $s (quote (∨ $p $q))
     (if (<= 2 $budget)
         (let $left (collapse (nu:eval $kb (- $budget 1) (: $pp (≞ @$p ⊤))))
           (if (== $left ())
               (let (MkSized $qs (: $qp $jq))
                    (nu:first (nu:eval $kb (- $budget 1) (: $qp (≞ @$q ⊤))))
                 (MkSized (+ 1 $qs) (: (cotr $qp) (≞ $s ⊤)))){$qs $qp $jq}
               (let (MkSized $ps (: $pp2 $jp)) (car-atom $left)
                 (MkSized (+ 1 $ps) (: (cotl $pp2) (≞ $s ⊤)))){$ps $pp2 $jp})){$left}
         (empty))
     (empty)))

(= (nu:eval $kb $budget (: (cof $pp $qp) (≞ $s ⊥)))
   (unify $s (quote (∨ $p $q))
     (if (<= 3 $budget)
         (let* (((MkSized $ps (: $pp (≞ $sp ⊥)))
                 (nu:eval $kb (- $budget 2) (: $pp (≞ @$p ⊥))))
                ((MkSized $qs (: $qp (≞ $sq ⊥)))
                 (nu:eval $kb (- $budget (+ $ps 1)) (: $qp (≞ @$q ⊥)))))
           (MkSized (+ 1 (+ $ps $qs)) (: (cof $pp $qp) (≞ $s ⊥)))){$ps $pp $sp $qs $qp $sq}
         (empty))
     (empty)))

;; Negation swaps the truth value.
(= (nu:eval $kb $budget (: (cnt $pp) (≞ $s ⊤)))
   (unify $s (quote (¬ $p))
     (if (<= 2 $budget)
         (let (MkSized $ps (: $pp (≞ $sp ⊥)))
              (nu:eval $kb (- $budget 1) (: $pp (≞ @$p ⊥)))
           (MkSized (+ 1 $ps) (: (cnt $pp) (≞ $s ⊤)))){$ps $pp $sp}
         (empty))
     (empty)))

(= (nu:eval $kb $budget (: (cnf $pp) (≞ $s ⊥)))
   (unify $s (quote (¬ $p))
     (if (<= 2 $budget)
         (let (MkSized $ps (: $pp (≞ $sp ⊤)))
              (nu:eval $kb (- $budget 1) (: $pp (≞ @$p ⊤)))
           (MkSized (+ 1 $ps) (: (cnf $pp) (≞ $s ⊥)))){$ps $pp $sp}
         (empty))
     (empty)))

;; Existential: ⊤ from the first witness of the domain (∃I), ⊥ from a
;; refutation at every individual of the domain (∃none).  The body is opened
;; at each individual.
(= (nu:eval $kb $budget (: $prf (≞ $s ⊤)))
   (unify $s (quote (exists $a $l))
     (if (<= 2 $budget)
         (let $domain (collapse (match $kb (: $w $a) $w){$w $a})
           (let (MkSized $n $proof) (nu:first-witness $kb (- $budget 1) @$l $domain)
             (let $size (+ 1 $n)
               (MkSized $size (: $proof (≞ $s ⊤)))){$size}){$n $proof}){$domain}
         (empty))
     (empty)))

;; The first individual of the domain, in order, at which the body is proven.
(= (nu:first-witness $kb $budget $l $domain)
   (if (== $domain ())
       (empty)
       (let ($w $rest) (decons-atom $domain)
         (let $instance (nu:open $l (lift $w))
           (let $proofs (collapse (nu:eval $kb $budget (: $p (≞ $instance ⊤))))
             (if (== $proofs ())
                 (nu:first-witness $kb $budget $l $rest)
                 (let (MkSized $n (: $wp $j)) (car-atom $proofs)
                   (MkSized $n (∃I $w $wp))){$n $wp $j})){$proofs}){$instance}){$w $rest}))

(= (nu:eval $kb $budget (: (∃none $wps) (≞ $s ⊥)))
   (unify $s (quote (exists $a $l))
     (if (<= 1 $budget)
         (let $domain (collapse (match $kb (: $w $a) $w){$w $a})
           (let (MkSized $n $wps) (nu:refute-all $kb (- $budget 1) @$l $domain)
             (MkSized (+ 1 $n) (: (∃none $wps) (≞ $s ⊥)))){$n $wps}){$domain}
         (empty))
     (empty)))

(= (nu:refute-all $kb $budget $l $domain)
   (if (== $domain ())
       (MkSized 0 ())
       (let ($w $rest) (decons-atom $domain)
         (let $instance (nu:open $l (lift $w))
           (let (MkSized $s (: $p (≞ $i ⊥)))
                (nu:first (nu:eval $kb $budget (: $p (≞ $instance ⊥))))
             (let $left (- $budget $s)
               (let (MkSized $n $ps) (nu:refute-all $kb $left $l $rest)
                 (let $total (+ $s $n)
                   (MkSized $total (cons-atom ($w $p) $ps))){$total}){$n $ps}){$left}){$s $p $i}){$instance}){$w $rest}))

;; A call of a recursive predicate unfolds it once.
(= (nu:eval $kb $budget (: (unfold $p) (≞ $s $tv)))
   (unify $s (quote ((fix $f) $u $v))
     (if (<= 2 $budget)
         (let $instance (nu:at @(fix $f) ($u $v))
           (let (MkSized $n (: $p (≞ $i $tv)))
                (nu:eval $kb (- $budget 1) (: $p (≞ $instance $tv)))
             (MkSized (+ 1 $n) (: (unfold $p) (≞ $s $tv)))){$n $p $i $tv}){$instance}
         (empty))
     (empty)))

;; Product, as in nuPLN: both parts must be proven.
(= (nu:eval $kb $budget (: (, $x $y) (, $a $b)))
   (if (<= 3 $budget)
       (let* (((MkSized $xs (: $x $a)) (nu:eval $kb (- $budget 2) (: $x $a)))
              ((MkSized $ys (: $y $b)) (nu:eval $kb (- $budget (+ $xs 1)) (: $y $b))))
         (MkSized (+ 1 (+ $xs $ys)) (: (, $x $y) (, $a $b)))){$xs $x $a $ys $y $b}
       (empty)))

;; The first answer of a search, computed as a list so that the search is
;; finished before the choice is made.
(= (nu:first $search)
   (let $all (collapse $search)
     (if (== $all ()) (empty) (car-atom $all))))

;;;;;;;;;;;;;;
;; Learning ;;
;;;;;;;;;;;;;;

;; Examples are (Example (c ...) tv).  A hypothesis h gives the statement
;; (≞ instance tv) for each, the instance being h opened at c ...; the
;; obligations are their product.
(= (nu:obligations $h $examples)
   (let ($example $rest) (decons-atom $examples)
     (let (Example $args $tv) $example
       (let $instance (nu:at $h $args)
         (if (== $rest ())
             (≞ $instance $tv)
             (, (≞ $instance $tv) (nu:obligations $h $rest)))))))

;; The shapes a hypothesis may take: a lambda, and a recursive predicate when
;; the knowledge base allows one.
(= (nu:hypothesis-shape $kb) @(lam $x $body))
(= (nu:hypothesis-shape $kb) (match $kb (nu:bias fix) @(fix (lam $p $body))))

;; All hypotheses of type T whose typing has exactly the given size, in
;; generation order.
(= (nu:candidates $kb $type $size)
   (collapse
     (let $shape (nu:hypothesis-shape $kb)
       (let (MkSized $s (: $h $t)) (nu:type $kb nil $size (: $shape $type))
         (if (== $s $size) $h (empty))))))

;; A proof that h explains every example: each obligation is proven on its
;; own within the budget, the first failure ends the attempt, and the proofs
;; form nuPLN's product.
(= (nu:explain $kb $h $examples $budget)
   (let ($example $rest) (decons-atom $examples)
     (let (Example $args $tv) $example
       (let $instance (nu:at $h $args)
         (let $proofs (collapse (nu:eval $kb $budget (: $prf (≞ $instance $tv))))
           (if (== $proofs ())
               (empty)
               (let (MkSized $s (: $p $j)) (car-atom $proofs)
                 (if (== $rest ())
                     (MkSized $s (: $p $j))
                     (let $more (collapse (nu:explain $kb $h $rest $budget))
                       (if (== $more ())
                           (empty)
                           (let (MkSized $s2 (: $p2 $j2)) (car-atom $more)
                             (let $n (+ 1 (+ $s $s2))
                               (MkSized $n (: (, $p $p2) (, $j $j2)))){$n}){$s2 $p2 $j2})){$more})){$s $p $j}))))))

;; The first candidate, in generation order, that explains every example.
;; The candidates of one size are tested in one pass: a list walk that
;; copies the rest of the list at each step would be quadratic.
(= (nu:first-consistent $kb $candidates $examples $proof-bound)
   (let $found
        (collapse
          (let $h (superpose $candidates)
            (let $proofs (collapse (nu:explain $kb $h $examples $proof-bound))
              (if (== $proofs ())
                  (empty)
                  (let (MkSized $cost (: $proof $g)) (car-atom $proofs)
                    (Explained $h $cost $proof)){$cost $proof $g}))))
     (if (== $found ()) none (car-atom $found))))

;; The same, trying candidates one at a time and stopping at the first that
;; explains every example.  For a short list whose later candidates may
;; recurse without progress, such as the instances of a recursive metarule.
(= (nu:first-in-order $kb $candidates $examples $proof-bound)
   (if (== $candidates ())
       none
       (let ($h $rest) (decons-atom $candidates)
         (let $proofs (collapse (nu:explain $kb $h $examples $proof-bound))
           (if (== $proofs ())
               (nu:first-in-order $kb $rest $examples $proof-bound)
               (let (MkSized $cost (: $proof $g)) (car-atom $proofs)
                 (Explained $h $cost $proof)){$cost $proof $g})){$proofs}){$h $rest}))

;; Iterative deepening on hypothesis size: the smallest hypothesis that
;; explains every example, with its typing size, proof size and proof.
(= (nu:learn $kb $type $examples $size-limit $proof-bound)
   (nu:learn-from $kb $type $examples 1 $size-limit $proof-bound))

(= (nu:learn-from $kb $type $examples $size $limit $proof-bound)
   (if (> $size $limit)
       (NotLearned $limit)
       (let $found
            (nu:first-consistent $kb (nu:candidates $kb $type $size)
                                 $examples $proof-bound)
         (if (== $found none)
             (let $next (+ $size 1)
               (nu:learn-from $kb $type $examples $next $limit $proof-bound)){$next}
             (let (Explained $h $cost $proof) $found
               (Learned $h $size $cost $proof)){$h $cost $proof})){$found}))

;; The same, with the learned program shown as source text:
;; (LearnedShown source typing-size proof-size proof).
(= (nu:learn-shown $kb $type $examples $size-limit $proof-bound)
   (nu:learn-shown-from $kb $type $examples 1 $size-limit $proof-bound))

(= (nu:learn-shown-from $kb $type $examples $size $limit $proof-bound)
   (if (> $size $limit)
       (NotLearned $limit)
       (let $found
            (nu:first-consistent $kb (nu:candidates $kb $type $size)
                                 $examples $proof-bound)
         (if (== $found none)
             (let $next (+ $size 1)
               (nu:learn-shown-from $kb $type $examples $next $limit $proof-bound)){$next}
             (let (Explained $h $cost $proof) $found
               (LearnedShown (nu:source $kb $h) $size $cost $proof)){$h $cost $proof})){$found}))

;; How many candidates each size level offers, for search accounting.
(= (nu:level-counts $kb $type $size $limit)
   (if (> $size $limit)
       ()
       (let $c (nu:candidates $kb $type $size)
         (let $n (size-atom $c)
           (let $next (+ $size 1)
             (let $rest (nu:level-counts $kb $type $next $limit)
               (cons-atom ($size $n) $rest)){$rest}){$next}){$n}){$c}))

;;;;;;;;;;;;;;;;;;
;; Presentation ;;
;;;;;;;;;;;;;;;;;;

;; A program with its binders named for reading: a binder at depth k is
;; opened at the k-th name the knowledge base does not declare, and the
;; lambda is rebuilt over that name; a recursive predicate is named self.  The result is code with symbol
;; binders, alpha-equivalent to the learned code, and it runs the same way.
(= (nu:named $kb $names $code)
   (unify $code (quote (lam $x $body))
     (let $n (nu:fresh-name $kb $names)
       (let $rest (nu:names-after $n $names)
         (let (quote $opened) (nu:open @(lam $x $body) (lift $n))
           (let (quote $inner) (nu:named $kb $rest @$opened)
             (lift app @lam (lift $n) @$inner)){$inner}){$opened}){$rest}){$n}
     (unify $code (quote (exists $a $l))
       (let (quote $l2) (nu:named $kb $names @$l) @(exists $a $l2)){$l2}
       (unify $code (quote (fix (lam $p $body)))
         (let (quote $opened) (nu:open @(lam $p $body) @self)
           (let (quote $inner) (nu:named $kb $names @$opened)
             @(fix (lam self $inner))){$inner}){$opened}
         (unify $code (quote (¬ $p))
           (let (quote $p2) (nu:named $kb $names @$p) @(¬ $p2)){$p2}
           (unify $code (quote ($c $p $q))
             (if (nu:connective $c)
                 (let* (((quote $p2) (nu:named $kb $names @$p))
                        ((quote $q2) (nu:named $kb $names @$q)))
                   @($c $p2 $q2)){$p2 $q2}
                 $code)
             $code))))))
(= (nu:connective $c)
   (if (noreduce-eq $c ∧) True (if (noreduce-eq $c ∨) True False)))

;; The first name the knowledge base does not declare, and the names after
;; it.
(= (nu:fresh-name $kb $names)
   (let ($n $rest) (decons-atom $names)
     (let $declared (collapse (match $kb (: $n $t) $t){$n})
       (if (== $declared ()) $n (nu:fresh-name $kb $rest)))))
(= (nu:names-after $n $names)
   (let ($m $rest) (decons-atom $names)
     (if (== $m $n) $rest (nu:names-after $n $rest))))

;; A program as source text.
(= (nu:source $kb $code)
   (let $named (nu:named $kb (x z y w v u a b c) $code)
     (unify $named (quote $t) (repr @$t) (empty))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Knowledge-base helpers ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Closed-world completion of a binary relation over a domain: each pair
;; without positive evidence receives explicit negative evidence, named
;; (cwa R a b) so that every proof shows where its negatives came from.
(= (nu:close-binary $kb $r $dom)
   (let $individuals (collapse (match $kb (: $a $dom) $a){$a $dom})
     (collapse
       (let* (($a (superpose $individuals))
              ($b (superpose $individuals))
              ($statement (lift app (lift $r) (lift $a) (lift $b)))
              ($positive (collapse (match $kb (: $p (≞ $statement ⊤)) $p){$p $statement})))
         (if (== $positive ())
             (add-atom $kb (: (cwa $r $a $b) (≞ $statement ⊥)))
             (empty))){$a})))

;; The value evidence gives a closed statement, for ordinary code.
(= (nu:bool ⊤) True)
(= (nu:bool ⊥) False)
(= (nu:holds $kb $statement)
   (match $kb (: $p (≞ $statement $tv)) (nu:bool $tv)){$statement})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; The object language as ordinary code ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; A learned program, once activated, runs as ordinary code wherever its
;; predicates have executable meanings.  The connectives are lazy: a
;; conjunction whose left part is False never runs its right part.
(= (∧ $p $q) (if $p $q False))
(= (∨ $p $q) (if $p True $q))
(= (¬ $p) (if $p False True))

;; An existential runs over the individuals a program lists for the domain,
;; (= (nu:individuals A) (a b ...)), and stops at the first witness.
(= (exists $dom $pred) (nu:any-at $pred (nu:individuals $dom)))
(= (nu:any-at $pred $individuals)
   (if (== $individuals ())
       False
       (let ($w $rest) (decons-atom $individuals)
         (if ($pred $w) True (nu:any-at $pred $rest))){$w $rest}))

;; A recursive program unfolds once per call.  Only an applied fixpoint
;; unfolds, since an argument is evaluated when a lambda is applied.
(= ((fix $f) $u) (($f (fix $f)) $u))
(= ((fix $f) $u $v) (($f (fix $f)) $u $v))
