; The provenance forest: Poker's generalisation step over the provenance
; library.  Requires (import! &self provenance).
;
; Poker generalises an example by proving it with Vanilla's meta-interpreter
; and keeping, for each refutation, the set of metasubstitutions it used.  Here
; the problem is first grounded into a rule program, one rule per
; metasubstitution the constraints allow and per invention state, and the
; supports of the example's derivations are then computed by the provenance
; library, each sub-derivation once.
;
; A problem is given by definitions and atoms in &self:
;   (forest:signature)          the target, then the invented predicates in order
;   (metarule Id P Sub (m P X ...) ((m Q A ...) ...))
;                               a metarule over literals of any arity
;   (forest:allowed Sub)        True when the metarule constraints accept the
;                               ground metasubstitution Sub
;   (m R A ...)                 background knowledge: facts of one to four
;                               arguments, possibly with variables (as
;                               encapsulated DCG terminals are)
;
; (forest:generalise EXAMPLE BOUND), EXAMPLE an atom (m Target A ...) and BOUND
; (steps K) or (labels K), answers the sorted supports, each the sorted list of
; the metasubstitutions of a refutation; (forest:generalise-all (EXAMPLE ...)
; BOUND) answers ((EXAMPLE SUPPORTS) ...), grounding the metarules once:
;   (steps K)   refutations of at most K steps, hypothesis clauses not reused
;               (Vanilla fetching background and metarules only);
;   (labels K)  refutations reusing hypothesis clauses, with at most K distinct
;               metasubstitutions (Vanilla's default).
; (forest:program EXAMPLE BOUND) answers the ground program,
; (program FACTS RULES GOALS), for inspection.  The rules do not depend on the
; example: they cover every background predicate of the problem, and a
; predicate an example never reaches only has no facts.
;
; Invention order: the I-th invented predicate may head a clause only once the
; (I-1)-th is available, as in Vanilla.  The highest rank available before and
; after each literal is part of its predicate, (d P M M2), threaded through
; clause bodies left to right; only differences up to the highest rank minus
; one matter, so states are capped there.
;
; The background is explored from the example's first argument: with
; (steps K) every fact a refutation uses lies within K times the longest
; metarule body of it; with (labels K) the whole reachable background is
; explored, which must be finite.  Facts are found by their first argument, and
; also by a later one when a metarule calls a literal with its first argument
; open and a later one bound (as Inverse does).

; ---------- small list helpers ----------

(= (forest:append $xs $ys)
   (if (== $xs ()) $ys
       (let ($h $t) (decons-atom $xs)
         (let $r (forest:append $t $ys) (cons-atom $h $r)))))

(= (forest:member $x $xs)
   (if (== $xs ()) False
       (let ($h $t) (decons-atom $xs) (if (== $h $x) True (forest:member $x $t)))))

(= (forest:range $i $n) (if (> $i $n) () (cons-atom $i (forest:range (+ $i 1) $n))))

(= (forest:max-list $xs)
   (if (== $xs ()) 0
       (let ($h $t) (decons-atom $xs) (max $h (forest:max-list $t)))))

; a literal (m Q A ...) as its predicate and its arguments
(= (forest:pred $lit) (let ($m $rest) (decons-atom $lit) (car-atom $rest)))
(= (forest:args $lit) (let ($m $rest) (decons-atom $lit) (cdr-atom $rest)))

; ---------- ranks and invention states ----------

(= (forest:rank $P) (forest:index $P (forest:signature) 0))
(= (forest:index $x $xs $i)
   (let ($h $t) (decons-atom $xs) (if (== $h $x) $i (forest:index $x $t (+ $i 1)))))

(= (forest:state-cap)
   (let $sig (forest:signature) (max 0 (- (size-atom $sig) 2))))

; a clause headed by P may be added in state m; the state after it
(= (forest:head-state $P $m $cap)
   (let $r (forest:rank $P) (if (<= $r (+ $m 1)) (min (max $r $m) $cap) (empty))))

; ---------- the background ----------

; breadth first from the inputs, level by level up to the limit; the nodes
; seen and the facts found are kept in a space; answers the distinct facts.
; From several inputs at once this is the union of what each one reaches.
(= (forest:explore $x $limit $back) (forest:explore-from ($x) $limit $back))
(= (forest:explore-from $xs $limit $back)
   (let $sp (new-space)
     (let* (($_s (forest:add-all $sp (collapse (let $x (superpose $xs) (forest-seen $x)))))
            ($_l (forest:levels $sp $xs 0 $limit $back)))
       (unique-atom (collapse (match $sp (forest-fact $f) $f))))))

(= (forest:levels $sp $frontier $depth $limit $back)
   (if (forest:stop $frontier $depth $limit) ()
       (let $next (collapse (let $u (superpose $frontier) (forest:visit $sp $u $back)))
         (forest:levels $sp $next (+ $depth 1) $limit $back))))

(= (forest:stop $frontier $depth $limit)
   (if (== $frontier ()) True (if (== $limit inf) False (>= $depth $limit))))

; records the facts at u; answers each node they reach that was not seen yet
(= (forest:visit $sp $u $back)
   (let $f (forest:fact-at $u $back)
     (let* (($_ (add-atom $sp (forest-fact $f)))
            ($args (forest:fact-args $f))
            ($w (superpose $args)))
       (if (forest:seen $sp $w) (empty)
           (let $_2 (add-atom $sp (forest-seen $w)) $w)))))
(= (forest:fact-args $f) (cdr-atom (cdr-atom $f)))

(= (forest:fact-at $u $back) (match &self (m $R $u) (fact $R $u)))
(= (forest:fact-at $u $back) (match &self (m $R $u $v) (fact $R $u $v)))
(= (forest:fact-at $u $back) (match &self (m $R $u $v $w) (fact $R $u $v $w)))
(= (forest:fact-at $u $back) (match &self (m $R $u $v $w $z) (fact $R $u $v $w $z)))
(= (forest:fact-at $u True) (match &self (m $R $a $u) (fact $R $a $u)))
(= (forest:fact-at $u True) (match &self (m $R $a $u $w) (fact $R $a $u $w)))
(= (forest:fact-at $u True) (match &self (m $R $a $b $u) (fact $R $a $b $u)))
(= (forest:fact-at $u True) (match &self (m $R $a $u $w $z) (fact $R $a $u $w $z)))
(= (forest:fact-at $u True) (match &self (m $R $a $b $u $z) (fact $R $a $b $u $z)))
(= (forest:fact-at $u True) (match &self (m $R $a $b $c $u) (fact $R $a $b $c $u)))
(= (forest:seen $sp $w) (if (== (collapse (match $sp (forest-seen $w) True)) ()) False True))

(= (forest:background-predicates)
   (unique-atom (collapse (superpose ((match &self (m $R $u) $R)
                                      (match &self (m $R $u $v) $R)
                                      (match &self (m $R $u $v $w) $R)
                                      (match &self (m $R $u $v $w $z) $R))))))

; ---------- the metarules ----------

; whether some body literal is called with its first argument open and a later
; one bound: pass 1 calls a clause with its first argument bound only
(= (forest:needs-backward)
   (let $hits (collapse (match &self (metarule $id $P $s $head $body)
                          (let $x (car-atom (forest:args $head))
                            (if (forest:backward-body $body ($x)) True (empty)))))
     (if (== $hits ()) False True)))
(= (forest:backward-body $lits $bound)
   (if (== $lits ()) False
       (let* ((($lit $rest) (decons-atom $lits))
              ($args (forest:args $lit)))
         (if (forest:member (car-atom $args) $bound)
             (forest:backward-body $rest (forest:append $args $bound))
             (if (forest:any-member (cdr-atom $args) $bound) True
                 (forest:backward-body $rest (forest:append $args $bound)))))))
(= (forest:any-member $xs $ys)
   (if (== $xs ()) False
       (let ($h $t) (decons-atom $xs) (if (forest:member $h $ys) True (forest:any-member $t $ys)))))

(= (forest:longest-body)
   (forest:max-list (collapse (match &self (metarule $id $P $s $h $body) (size-atom $body)))))

; variable index: the head's arguments first, then the body's own variables
(= (forest:var-index $v $vars $i)
   (let ($h $t) (decons-atom $vars) (if (== $h $v) $i (forest:var-index $v $t (+ $i 1)))))
(= (forest:add-vars $xs $vars)
   (if (== $xs ()) $vars
       (let ($h $t) (decons-atom $xs)
         (forest:add-vars $t (if (forest:member $h $vars) $vars (forest:append $vars ($h)))))))
(= (forest:body-vars $lits $vars)
   (if (== $lits ()) $vars
       (let ($lit $rest) (decons-atom $lits)
         (forest:body-vars $rest (forest:add-vars (forest:args $lit) $vars)))))
(= (forest:indices $xs $vars)
   (if (== $xs ()) ()
       (let ($h $t) (decons-atom $xs)
         (cons-atom (forest:var-index $h $vars 0) (forest:indices $t $vars)))))

;; The program is written in Vanilla's own encapsulation (HiLog): a learned
;; literal is (m P A ... IN OUT), carrying the invention state it is entered in
;; and the one it leaves; a background literal is (b R A ...) and passes the
;; state through; each metasubstitution the constraints allow is a labelled
;; fact (labelled SUB (allowed ID) P Q ...), and the head states Vanilla allows
;; are facts (hs P M M1).  A metarule becomes one rule per choice of learned or
;; background for each body literal; each rule takes its label from the
;; allowed fact it matches.  So no rule is built per metasubstitution.
;;
;; The allowed metasubstitutions: per metarule and per choice of learned or
;; background for each body literal, one join over a space holding
;; (forest-sym P) for the signature, (forest-sig Q) for it again and
;; (forest-bk Q) for the background predicates the examples reach, then the
;; constraints.  Only predicates with facts can take part, and with (steps K) a
;; clause with d learned literals needs d + 1 steps, so at most K - 1 learned
;; literals are chosen.
(= (forest:symbol-space $sig $bk)
   (let $sp (new-space)
     (let* (($_s (forest:add-all $sp (collapse (let $P (superpose $sig) (forest-sym $P)))))
            ($_g (forest:add-all $sp (collapse (let $P (superpose $sig) (forest-sig $P)))))
            ($_b (forest:add-all $sp (collapse (let $Q (superpose $bk) (forest-bk $Q))))))
       $sp)))

;; the choices for a body: each a list of conjuncts, at most $d learned
(= (forest:kind-choices $lits $d)
   (if (== $lits ()) ()
       (let* ((($lit $rest) (decons-atom $lits)) ($Q (forest:pred $lit)))
         (superpose
           ((if (> $d 0) (cons-atom (forest-sig $Q) (forest:kind-choices $rest (- $d 1))) (empty))
            (cons-atom (forest-bk $Q) (forest:kind-choices $rest $d)))))))

(= (forest:learned-limit (steps $k)) (- $k 1))
(= (forest:learned-limit (labels $k)) 64)
(= (forest:add-all $sp $xs)
   (if (== $xs ()) ()
       (let ($h $t) (decons-atom $xs) (let $_ (add-atom $sp $h) (forest:add-all $sp $t)))))

(= (forest:pred-conjuncts $lits)
   (if (== $lits ()) ()
       (let ($lit $rest) (decons-atom $lits)
         (cons-atom (forest-pred (forest:pred $lit)) (forest:pred-conjuncts $rest)))))

; a metasubstitution (F ID A ...) and its arguments A ...
(= (forest:sub-args $s) (cdr-atom (cdr-atom $s)))

(= (forest:allowed-facts $sig $bk $bound)
   (let $space (forest:symbol-space $sig $bk)
     (collapse
       (match &self (metarule $id $P $s $head $body)
         (let* (($fact (cons-atom labelled (cons-atom $s (cons-atom (allowed $id) (forest:sub-args $s)))))
                ($kinds (forest:kind-choices $body (forest:learned-limit $bound)))
                ($pick (cons-atom , (cons-atom (forest-sym $P) $kinds))))
           (let $p $pick
             (match $space $p (if (forest:allowed $s) $fact (empty)))))))))

;; The rules of a metarule.  Variable indices: the head's arguments, the body's
;; other variables, then the metasubstitution's arguments not among them, then
;; the states: the head's IN, the body's first IN, one more per learned literal.
(= (forest:metarule-rules)
   (collapse
     (match &self (metarule $id $P $s $head $body)
       (let* (($hargs (forest:args $head))
              ($dvars (forest:body-vars $body $hargs))
              ($vars (forest:add-vars (forest:sub-args $s) $dvars))
              ($n (size-atom $vars))
              ($pi (forest:var-index $P $vars 0))
              ($his (forest:indices $hargs $vars))
              ($ais (forest:indices (forest:sub-args $s) $vars))
              ((body $lits $out) (forest:kind-lits $body $vars (+ $n 1) (+ $n 2)))
              ($k (size-atom $lits)))
         (rule-from $k
                    (cons-atom lit (cons-atom m (cons-atom $pi (forest:append $his ($n $out)))))
                    (forest:append $lits
                                   ((cons-atom lit (cons-atom (allowed $id) $ais))
                                    (lit hs $pi $n (+ $n 1)))))))))

;; each body literal learned or background, nondeterministically; states thread
;; from index $in, fresh ones from $next; yields (body LITS OUT)
(= (forest:kind-lits $lits $vars $in $next)
   (if (== $lits ()) (body () $in)
       (let* ((($lit $rest) (decons-atom $lits))
              ($qi (forest:var-index (forest:pred $lit) $vars 0))
              ($is (forest:indices (forest:args $lit) $vars)))
         (superpose
           ((let (body $ls $out) (forest:kind-lits $rest $vars $next (+ $next 1))
              (body (cons-atom (cons-atom lit (cons-atom m (cons-atom $qi (forest:append $is ($in $next))))) $ls) $out))
            (let (body $ls $out) (forest:kind-lits $rest $vars $in $next)
              (body (cons-atom (cons-atom lit (cons-atom b (cons-atom $qi $is))) $ls) $out)))))))

;; the head states as facts
(= (forest:state-facts $sig $states $cap)
   (collapse
     (let* (($P (superpose $sig)) ($m (superpose $states)) ($m1 (forest:head-state $P $m $cap)))
       (fact hs $P $m $m1))))

; ---------- the learner ----------

(= (forest:limit (steps $k)) (* $k (forest:longest-body)))
(= (forest:limit (labels $k)) inf)

; the problem's part of the program for the background the examples reach:
; (problem RULES FACTS), FACTS the allowed metasubstitutions and head states
(= (forest:problem-rules $bk $bound)
   (let* (($sig (forest:signature))
          ($cap (forest:state-cap))
          ($states (forest:range 0 $cap)))
     (problem (forest:metarule-rules)
              (forest:append (forest:allowed-facts $sig (subtraction-atom $bk $sig) $bound)
                             (forest:state-facts $sig $states $cap)))))

(= (forest:fact-preds $found) (unique-atom (collapse (let $f (superpose $found) (car-atom (cdr-atom $f))))))

(= (forest:program $example $bound)
   (let* (($found (forest:explore (car-atom (forest:args $example)) (forest:limit $bound) (forest:needs-backward)))
          ((problem $rules $pfacts) (forest:problem-rules (forest:fact-preds $found) $bound))
          ($bfacts (collapse (let $f (superpose $found) (cons-atom fact (cons-atom b (cdr-atom $f)))))))
     (program (forest:append $pfacts $bfacts) $rules ((forest:goal $example)))))

(= (forest:generalise $example $bound)
   (let (program $facts $rules $goals) (forest:program $example $bound)
     (provenance:supports $facts $rules $goals $bound)))

; all examples in one computation: the background each one reaches, the
; problem's facts once, one goal group per example
(= (forest:generalise-all $examples $bound)
   (let* (($inputs (collapse (let $e (superpose $examples) (car-atom (forest:args $e)))))
          ($found (forest:explore-from (unique-atom $inputs) (forest:limit $bound) (forest:needs-backward)))
          ((problem $rules $pfacts) (forest:problem-rules (forest:fact-preds $found) $bound))
          ($bfacts (collapse (let $f (superpose $found) (cons-atom fact (cons-atom b (cdr-atom $f))))))
          ($groups (collapse (let $e (superpose $examples) ((forest:goal $e)))))
          ($fams (provenance:supports-each (forest:append $pfacts $bfacts) $rules $groups $bound)))
     (forest:pairs $examples $fams)))

(= (forest:goal $example)
   (cons-atom goal (cons-atom m (cons-atom (forest:pred $example)
                                           (forest:append (forest:args $example) (0 $out))))))

(= (forest:pairs $xs $ys)
   (if (== $xs ()) ()
       (let* ((($x $xt) (decons-atom $xs)) (($y $yt) (decons-atom $ys)))
         (cons-atom ($x $y) (forest:pairs $xt $yt)))))
