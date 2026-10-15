; Transport of checked HOL Light theorems into a Prime theory space.
;
; A HOL Light state keeps, for every theorem, the request that produced it
; and the keys of its premises. This module replays that derivation in a
; target theory space: every primitive step becomes a theorem of the target
; whose proof the target's own checker verifies. No HOL Light theorem is
; admitted as an assumption.
;
; The interpretation:
;   types      bool -> prop, fun -> ->, ind -> the type named by the
;              interpretation, a type variable -> a declared base type;
;   terms      = -> eq at the instance type, a defined constant -> a defined
;              constant of the target with the translated body, at each type
;              instance used, @ -> a declared choice constant per type;
;   sequents   hyps |- c with free variables x... -> all x... hyps -> c.
; Polymorphism is replayed monomorphically: a premise used through INST_TYPE
; is replayed at the instantiated types.
;
; What the target must additionally commit to is published as named axioms
; and listed by hol-light:transport-commitments:
;   functional extensionality at each pair of types an ABS step uses;
;   propositional extensionality, for DEDUCT_ANTISYM_RULE;
;   each instance of a HOL Light axiom (ETA_AX, SELECT_AX, INFINITY_AX) used.
; TYPE_DEFINITION steps are not transported.
;
; Syntax values are returned with (function (return ...)): a returned
; expression is otherwise evaluated, and a translated lambda application
; would be reduced by the evaluator rather than kept as the sentence says.
;
; Import the translated HOL Light library before this module.

; ---------------------------------------------------------------------------
; Building syntax without evaluating it
; ---------------------------------------------------------------------------

(= (hol-light:tr-node1 $head $a) (function (return ($head $a))))
(= (hol-light:tr-node2 $head $a $b) (function (return ($head $a $b))))
(= (hol-light:tr-node3 $head $a $b $c) (function (return ($head $a $b $c))))
(= (hol-light:tr-node4 $head $a $b $c $d) (function (return ($head $a $b $c $d))))
(= (hol-light:tr-cons $first $rest) (function (return (HOL:Cons $first $rest))))

(= (hol-light:tr-lam $name $type $body)
  (let $binder (hol-light:tr-node2 $name : $type)
    (hol-light:tr-node2 lam $binder $body)))

(= (hol-light:tr-all $type $name $body)
  (let $family (hol-light:tr-lam $name $type $body)
    (hol-light:tr-node2 all $type $family)))

(= (hol-light:tr-imp $left $right) (hol-light:tr-node2 imp $left $right))

; A HOL list of target atoms as one expression.
(= (hol-light:tr-expr $items)
  (case $items
    ((HOL:Nil (function (return ())))
     ((HOL:Cons $first $rest)
       (let $tail (hol-light:tr-expr $rest)
         (let $whole (cons-atom $first $tail) (function (return $whole)))))
     ($other (function (return ()))))))

(= (hol-light:tr-symbol $text) (parse $text))

(= (hol-light:tr-index-symbol $prefix $index)
  (hol-light:tr-symbol (format-args "{}{}" ($prefix $index))))

; Membership in a tuple returned by collapse.  A pattern variable bound
; outside a match is a fresh variable inside it, so lookups collect the
; candidates and compare them here.
(= (hol-light:tr-member-tuple $item $items)
  (if (== $items ()) False
    (let $first (car-atom $items)
      (if (== $first $item) True
        (let $rest (cdr-atom $items) (hol-light:tr-member-tuple $item $rest))))))

; ---------------------------------------------------------------------------
; Interpretations and names
; ---------------------------------------------------------------------------

; (HOL:Interpretation ind-type) : the target type that HOL Light's ind denotes.
(= (hol-light:tr-ind (HOL:Interpretation $ind)) $ind)

(= (hol-light:tr-type-code $type)
  (case $type
    (((Tyapp "bool" HOL:Nil) "B")
     ((Tyapp "ind" HOL:Nil) "I")
     ((Tyapp "fun" (HOL:Cons $a (HOL:Cons $b HOL:Nil)))
       (let $ca (hol-light:tr-type-code $a)
         (let $cb (hol-light:tr-type-code $b)
           (format-args "F{}{}" ($ca $cb)))))
     ((Tyvar $name) (format-args "V{}." ($name)))
     ((Tyapp $name $arguments)
       (let $cs (hol-light:tr-type-codes $arguments)
         (format-args "T{}.{}" ($name $cs))))
     ($other "?"))))

(= (hol-light:tr-type-codes $types)
  (case $types
    ((HOL:Nil "")
     ((HOL:Cons $first $rest)
       (let $c (hol-light:tr-type-code $first)
         (let $cs (hol-light:tr-type-codes $rest)
           (format-args "{}{}" ($c $cs)))))
     ($other "?"))))

; The base name of a HOL Light constant in the target.
(= (hol-light:tr-constant-base $name)
  (case $name
    (("T" "hl:T") ("F" "hl:F") ("/\\" "hl:and") ("\\/" "hl:or")
     ("==>" "hl:imp") ("!" "hl:all") ("?" "hl:ex") ("?!" "hl:ex1")
     ("~" "hl:not") ("@" "hl:select") ("ONE_ONE" "hl:one-one")
     ("ONTO" "hl:onto")
     ($other (format-args "hl:c.{}" ($other))))))

; A polymorphic constant has one target constant per type instance.
(= (hol-light:tr-constant-symbol $name $schema $type)
  (let $base (hol-light:tr-constant-base $name)
    (if (== (hol-light:type-vars $schema) HOL:Nil)
      (hol-light:tr-symbol $base)
      (let $code (hol-light:tr-type-code $type)
        (hol-light:tr-symbol (format-args "{}@{}" ($base $code)))))))

; ---------------------------------------------------------------------------
; Types
; ---------------------------------------------------------------------------

(= (hol-light:tr-tyvar $name)
  (hol-light:tr-symbol (format-args "hl-tyvar-{}" ($name))))

(= (hol-light:tr-type $interp $type)
  (case $type
    (((Tyapp "bool" HOL:Nil) prop)
     ((Tyapp "ind" HOL:Nil) (hol-light:tr-ind $interp))
     ((Tyapp "fun" (HOL:Cons $a (HOL:Cons $b HOL:Nil)))
       (let $x (hol-light:tr-type $interp $a)
         (let $y (hol-light:tr-type $interp $b)
           (hol-light:tr-node2 -> $x $y))))
     ((Tyvar $name) (hol-light:tr-tyvar $name))
     ($other (function (return (HOL:TransportUnsupported (type $type))))))))

(= (hol-light:tr-declare $target $symbol $type)
  (let $declared (collapse (match $target (: $name $known) $name))
    (if (hol-light:tr-member-tuple $symbol $declared) ()
      (add-atom $target (: $symbol $type)))))

; Type variables become declared base types of the target.
(= (hol-light:tr-declare-type $target $type)
  (case $type
    (((Tyvar $name)
       (let $symbol (hol-light:tr-tyvar $name)
         (hol-light:tr-declare $target $symbol (u 0))))
     ((Tyapp $name $arguments) (hol-light:tr-declare-types $target $arguments))
     ($other ()))))

(= (hol-light:tr-declare-types $target $types)
  (case $types
    ((HOL:Nil ())
     ((HOL:Cons $first $rest)
       (let $done (hol-light:tr-declare-type $target $first)
         (hol-light:tr-declare-types $target $rest)))
     ($other ()))))

; ---------------------------------------------------------------------------
; Terms
; ---------------------------------------------------------------------------
;
; A translation context:
;   (HOL:TrEnv target interp frees binders depth mode extra)
; frees: the free variables of the sentence, outermost first;
; binders: the names of the enclosing HOL Light abstractions, innermost first;
; mode: Statement (free variables are named x0, x1, ...) or Proof (free
; variables are the proof's term variables pf:var);
; extra: term variables a proof introduces inside the sentence's own,
; innermost first.

(= (hol-light:tr-position $item $items $index)
  (case $items
    ((HOL:Nil None)
     ((HOL:Cons $first $rest)
       (if (== $first $item) (Some $index)
         (hol-light:tr-position $item $rest (+ $index 1))))
     ($other None))))

(= (hol-light:tr-free $env $variable)
  (let (HOL:TrEnv $target $interp $frees $binders $depth $mode $extra) $env
    (case (hol-light:tr-position $variable $extra 0)
      (((Some $inner) (hol-light:tr-node1 pf:var $inner))
       (None
         (case (hol-light:tr-position $variable $frees 0)
           (((Some $k)
              (case $mode
                ((Statement (hol-light:tr-index-symbol "x" $k))
                 (Proof
                   (let $n (hol-light:length $frees)
                     (let $outer (hol-light:length $extra)
                       (let $index (+ (- (- $n 1) $k) $outer)
                         (hol-light:tr-node1 pf:var $index))))))))
            (None (function (return (HOL:TransportUnsupported (free-variable $variable))))))))))))

; The head of an application spine and its arguments, first argument first.
(= (hol-light:tr-spine $term $arguments)
  (case $term
    (((HOL:App $function $argument)
       (hol-light:tr-spine $function (HOL:Cons $argument $arguments)))
     ($head (HOL:Spine $head $arguments)))))

(= (hol-light:tr-terms $env $terms)
  (case $terms
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
       (let $x (hol-light:tr-term $env $first)
         (let $xs (hol-light:tr-terms $env $rest)
           (hol-light:tr-cons $x $xs))))
     ($other HOL:Nil))))

(= (hol-light:tr-term $env $term)
  (let (HOL:TrEnv $target $interp $frees $binders $depth $mode $extra) $env
    (case $term
      (((HOL:Bound $index $type)
         (case (hol-light:at $index $binders)
           (((Some $name) $name)
            ($none (function (return (HOL:TransportUnsupported (bound $index))))))))
       ((HOL:Free $name $type) (hol-light:tr-free $env $term))
       ((HOL:Lam $type $body)
         (let $name (hol-light:tr-index-symbol "b" $depth)
           (let $inner (HOL:TrEnv $target $interp $frees (HOL:Cons $name $binders)
                         (+ $depth 1) $mode $extra)
             (let $domain (hol-light:tr-type $interp $type)
               (let $value (hol-light:tr-term $inner $body)
                 (hol-light:tr-lam $name $domain $value))))))
       ((HOL:App $function $argument)
         (let (HOL:Spine $head $arguments) (hol-light:tr-spine $term HOL:Nil)
           (let $items (hol-light:tr-terms $env $arguments)
             (case $head
               (((HOL:C (HOL:Primitive "=") (Tyapp "fun" (HOL:Cons $a $rest)))
                  (let $carrier (hol-light:tr-type $interp $a)
                    (let $with-carrier (hol-light:tr-cons $carrier $items)
                      (let $all (hol-light:tr-cons eq $with-carrier)
                        (hol-light:tr-expr $all)))))
                ($other
                  (let $h (hol-light:tr-term $env $head)
                    (let $all (hol-light:tr-cons $h $items)
                      (hol-light:tr-expr $all)))))))))
       ((HOL:C $identity $type) (hol-light:tr-constant $env $identity $type))
       ($other (function (return (HOL:TransportUnsupported (term $term)))))))))

; Constants: = alone, the choice constant, and defined constants.
(= (hol-light:tr-constant $env $identity $type)
  (let (HOL:TrEnv $target $interp $frees $binders $depth $mode $extra) $env
    (case $identity
      (((HOL:Primitive "=")
         (case $type
           (((Tyapp "fun" (HOL:Cons $a $rest))
              (let $carrier (hol-light:tr-type $interp $a)
                (hol-light:tr-node1 eq $carrier)))
            ($other (function (return (HOL:TransportUnsupported (constant $identity))))))))
       ((HOL:Primitive "@") (hol-light:tr-select $target $interp $type))
       ((HOL:Defined $name $schema $body)
         (hol-light:tr-defined $target $interp $name $schema $body $type))
       ($other (function (return (HOL:TransportUnsupported (constant $identity)))))))))

; The choice constant at one type, declared on first use.
(= (hol-light:tr-select $target $interp $type)
  (let $schema (hol-light:fun (hol-light:fun (Tyvar "A") (hol-light:bool)) (Tyvar "A"))
    (let $symbol (hol-light:tr-constant-symbol "@" $schema $type)
      (let $declared (hol-light:tr-declare-type $target $type)
        (let $target-type (hol-light:tr-type $interp $type)
          (let $done (hol-light:tr-declare $target $symbol $target-type)
            $symbol))))))

; A defined constant at one type instance: on first use, its body at that
; instance is translated and defined in the target.
(= (hol-light:tr-defined $target $interp $name $schema $body $type)
  (let $symbol (hol-light:tr-constant-symbol $name $schema $type)
    (case (hol-light:tr-member-tuple $symbol
            (collapse (match $target (hol-light:transport-defined $defined) $defined)))
      ((False (case (hol-light:type-match $schema $type HOL:Nil)
             (((Some $assignments)
                (let $instance (hol-light:tr-maplets $assignments)
                  (let $value (hol-light:term-inst-type $instance $body)
                    (let $declared (hol-light:tr-declare-type $target $type)
                      (let $env (HOL:TrEnv $target $interp HOL:Nil HOL:Nil 0 Statement HOL:Nil)
                        (let $translated (hol-light:tr-term $env $value)
                          (let $target-type (hol-light:tr-type $interp $type)
                            (let $equation (hol-light:tr-node2 = $symbol $translated)
                              (let $quoted (hol-light:tr-node1 quote $equation)
                                (let $defined (hol-light:tr-define $target $symbol $target-type $quoted)
                                  (if (== $defined $symbol)
                                    (let $marked (add-atom $target (hol-light:transport-defined $symbol))
                                      $symbol)
                                    (function (return (HOL:TransportUnsupported
                                      (definition $name $defined)))))))))))))))
              ($mismatch (function (return (HOL:TransportUnsupported (instance $name $type))))))))
       ($known $symbol)))))

; type-match yields (HOL:Entry name type); substitution wants maplets.
(= (hol-light:tr-maplets $assignments)
  (case $assignments
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons (HOL:Entry $name $value) $rest)
       (let $tail (hol-light:tr-maplets $rest)
         (HOL:Cons (HOL:Maplet $value (Tyvar $name)) $tail)))
     ($other HOL:Nil))))

; ---------------------------------------------------------------------------
; Sequents
; ---------------------------------------------------------------------------

(= (hol-light:tr-free-list $terms)
  (case $terms
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
       (hol-light:union (hol-light:free-vars $first) (hol-light:tr-free-list $rest)))
     ($other HOL:Nil))))

; The free variables of a sequent, in one fixed order.
(= (hol-light:tr-frees $hyps $conclusion)
  (hol-light:tr-free-list (HOL:Cons $conclusion $hyps)))

(= (hol-light:tr-implications $env $hyps $conclusion)
  (case $hyps
    ((HOL:Nil (hol-light:tr-term $env $conclusion))
     ((HOL:Cons $first $rest)
       (let $h (hol-light:tr-term $env $first)
         (let $c (hol-light:tr-implications $env $rest $conclusion)
           (hol-light:tr-imp $h $c))))
     ($other (function (return (HOL:TransportUnsupported (hypotheses $hyps))))))))

(= (hol-light:tr-generalize $interp $frees $index $body)
  (case $frees
    ((HOL:Nil (function (return $body)))
     ((HOL:Cons (HOL:Free $name $type) $rest)
       (let $inner (hol-light:tr-generalize $interp $rest (+ $index 1) $body)
         (let $domain (hol-light:tr-type $interp $type)
           (let $variable (hol-light:tr-index-symbol "x" $index)
             (hol-light:tr-all $domain $variable $inner)))))
     ($other (function (return (HOL:TransportUnsupported (frees $frees))))))))

; The target sentence of a sequent: all x... h1 -> ... -> hm -> c.
(= (hol-light:tr-sequent $target $interp $hyps $conclusion)
  (let $frees (hol-light:tr-frees $hyps $conclusion)
    (let $declared (hol-light:tr-declare-free-types $target $frees)
      (let $env (HOL:TrEnv $target $interp $frees HOL:Nil 0 Statement HOL:Nil)
        (let $body (hol-light:tr-implications $env $hyps $conclusion)
          (hol-light:tr-generalize $interp $frees 0 $body))))))

(= (hol-light:tr-declare-free-types $target $frees)
  (case $frees
    ((HOL:Nil ())
     ((HOL:Cons (HOL:Free $name $type) $rest)
       (let $done (hol-light:tr-declare-type $target $type)
         (hol-light:tr-declare-free-types $target $rest)))
     ($other ()))))

; ---------------------------------------------------------------------------
; Proof contexts
; ---------------------------------------------------------------------------
;
; A proof is built inside the sentence's own skeleton: one all-intro per
; free variable, then one imp-intro per hypothesis.
;   (HOL:ProofCtx target interp frees hyps extra local)
; extra: further term variables introduced inside (innermost first);
; local: further hypotheses introduced inside (innermost first).

(= (hol-light:tr-skeleton $frees $hyps $body)
  (let $with-hyps (hol-light:tr-intro-hyps $hyps $body)
    (hol-light:tr-intro-vars $frees $with-hyps)))

(= (hol-light:tr-intro-hyps $hyps $body)
  (case $hyps
    ((HOL:Nil (function (return $body)))
     ((HOL:Cons $first $rest)
       (let $inner (hol-light:tr-intro-hyps $rest $body)
         (hol-light:tr-node1 pf:imp-intro $inner)))
     ($other (function (return $body))))))

(= (hol-light:tr-intro-vars $frees $body)
  (case $frees
    ((HOL:Nil (function (return $body)))
     ((HOL:Cons $first $rest)
       (let $inner (hol-light:tr-intro-vars $rest $body)
         (hol-light:tr-node1 pf:all-intro $inner)))
     ($other (function (return $body))))))

; A term in the proof's context.
(= (hol-light:tr-proof-term $ctx $term)
  (let (HOL:ProofCtx $target $interp $frees $hyps $extra $local) $ctx
    (let $env (HOL:TrEnv $target $interp $frees HOL:Nil 0 Proof $extra)
      (hol-light:tr-term $env $term))))

; The proof of a hypothesis available in the context.
(= (hol-light:tr-hypothesis $ctx $hyp)
  (let (HOL:ProofCtx $target $interp $frees $hyps $extra $local) $ctx
    (case (hol-light:tr-position $hyp $local 0)
      (((Some $index) (hol-light:tr-node1 pf:hyp $index))
       (None
         (case (hol-light:tr-position $hyp $hyps 0)
           (((Some $j)
              (let $m (hol-light:length $hyps)
                (let $l (hol-light:length $local)
                  (let $index (+ (- (- $m 1) $j) $l)
                    (hol-light:tr-node1 pf:hyp $index)))))
            (None (function (return (HOL:TransportUnsupported (hypothesis $hyp))))))))))))

; A witness of a type: a closed term of it, for a premise variable the
; conclusion no longer mentions.
(= (hol-light:tr-witness $target $interp $type)
  (case $type
    (((Tyapp "bool" HOL:Nil) Falsum)
     ((Tyapp "fun" (HOL:Cons $a (HOL:Cons $b HOL:Nil)))
       (let $domain (hol-light:tr-type $interp $a)
         (let $value (hol-light:tr-witness $target $interp $b)
           (hol-light:tr-lam w $domain $value))))
     ($base
       (let $select (hol-light:tr-select $target $interp $type)
         (let $domain (hol-light:tr-type $interp $type)
           (let $predicate (hol-light:tr-lam w $domain Falsum)
             (hol-light:tr-node1 $select $predicate))))))))

; Use a premise theorem: instantiate its variables (by `mapping`, else by
; themselves, else by a witness) and discharge its hypotheses from the
; context.
;   premise: (HOL:Premise name hyps conclusion), already at its type instance.
(= (hol-light:tr-use $ctx $premise $mapping)
  (let (HOL:Premise $name $phyps $pconclusion) $premise
    (let $pfrees (hol-light:tr-frees $phyps $pconclusion)
      (let $known (hol-light:tr-node1 pf:known $name)
        (let $instantiated (hol-light:tr-instantiate $ctx $known $pfrees $mapping)
          (hol-light:tr-discharge $ctx $instantiated $phyps $mapping))))))

(= (hol-light:tr-instantiate $ctx $proof $pfrees $mapping)
  (case $pfrees
    ((HOL:Nil (function (return $proof)))
     ((HOL:Cons $variable $rest)
       (let $witness (hol-light:tr-instance-of $ctx $variable $mapping)
         (let $applied (hol-light:tr-node2 pf:all-elim $proof $witness)
           (hol-light:tr-instantiate $ctx $applied $rest $mapping))))
     ($other (function (return $proof))))))

(= (hol-light:tr-instance-of $ctx $variable $mapping)
  (let (HOL:ProofCtx $target $interp $frees $hyps $extra $local) $ctx
    (case (hol-light:lookup $variable $mapping)
      (((Some $value) (hol-light:tr-proof-term $ctx $value))
       (None
         (if (hol-light:member $variable $frees)
           (hol-light:tr-proof-term $ctx $variable)
           (if (hol-light:member $variable $extra)
             (hol-light:tr-proof-term $ctx $variable)
             (let (HOL:Free $vname $vtype) $variable
               (hol-light:tr-witness $target $interp $vtype)))))))))

(= (hol-light:tr-discharge $ctx $proof $phyps $mapping)
  (case $phyps
    ((HOL:Nil (function (return $proof)))
     ((HOL:Cons $hyp $rest)
       (let $instance (hol-light:term-subst $mapping $hyp)
         (let $evidence (hol-light:tr-hypothesis $ctx $instance)
           (let $applied (hol-light:tr-node2 pf:imp-elim $proof $evidence)
             (hol-light:tr-discharge $ctx $applied $rest $mapping)))))
     ($other (function (return $proof))))))

; ---------------------------------------------------------------------------
; Commitments
; ---------------------------------------------------------------------------

(= (hol-light:tr-commit $target $name $statement)
  (case (hol-light:tr-member-tuple $name
          (collapse (match $target (hol-light:transport-commitment $known $s) $known)))
    ((False (let $quoted (hol-light:tr-node1 quote $statement)
           (let $published (hol-light:tr-axiom $target $name $quoted)
             (if (== $published $name)
               (let $marked (add-atom $target (hol-light:transport-commitment $name $statement))
                 $name)
               (function (return (HOL:TransportUnsupported (commitment $name $published))))))))
     ($known $name))))

; Propositional extensionality: all p q. (p -> q) -> (q -> p) -> p = q.
(= (hol-light:tr-propext $target)
  (let $pq (hol-light:tr-imp p q)
    (let $qp (hol-light:tr-imp q p)
      (let $eq (hol-light:tr-node3 eq prop p q)
        (let $tail (hol-light:tr-imp $qp $eq)
          (let $body (hol-light:tr-imp $pq $tail)
            (let $inner (hol-light:tr-all prop q $body)
              (let $statement (hol-light:tr-all prop p $inner)
                (hol-light:tr-commit $target hl:propext $statement)))))))))

; Functional extensionality at one pair of types.
(= (hol-light:tr-funext $target $interp $domain $codomain)
  (let $a (hol-light:tr-type $interp $domain)
    (let $b (hol-light:tr-type $interp $codomain)
      (let $ab (hol-light:tr-node2 -> $a $b)
        (let $fx (hol-light:tr-node1 f x)
          (let $gx (hol-light:tr-node1 g x)
            (let $pointwise-eq (hol-light:tr-node3 eq $b $fx $gx)
              (let $pointwise (hol-light:tr-all $a x $pointwise-eq)
                (let $fg (hol-light:tr-node3 eq $ab f g)
                  (let $law (hol-light:tr-imp $pointwise $fg)
                    (let $inner (hol-light:tr-all $ab g $law)
                      (let $statement (hol-light:tr-all $ab f $inner)
                        (let $code-a (hol-light:tr-type-code $domain)
                          (let $code-b (hol-light:tr-type-code $codomain)
                            (let $name (hol-light:tr-symbol (format-args "hl:funext@{}@{}" ($code-a $code-b)))
                              (hol-light:tr-commit $target $name $statement))))))))))))))))

; A HOL Light axiom at one type instance, stated as HOL Light states it.
; `code` names the instance.
(= (hol-light:tr-axiom $target $interp $name $term $code)
  (let $statement (hol-light:tr-sequent $target $interp HOL:Nil $term)
    (let $symbol (hol-light:tr-symbol (format-args "hl:axiom.{}@{}" ($name $code)))
      (hol-light:tr-commit $target $symbol $statement))))

(= (hol-light:transport-commitments $target)
  (collapse (match $target (hol-light:transport-commitment $name $statement) $name)))

; ---------------------------------------------------------------------------
; Replaying a derivation
; ---------------------------------------------------------------------------
;
; thetas: the type instantiations to apply to a stored theorem, first one
; first. A premise used through INST_TYPE is replayed under one more.

(= (hol-light:tr-inst-one $thetas $term)
  (case $thetas
    ((HOL:Nil $term)
     ((HOL:Cons $entries $rest)
       (hol-light:tr-inst-one $rest (hol-light:term-inst-type $entries $term)))
     ($other $term))))

(= (hol-light:tr-inst-all $thetas $terms)
  (case $terms
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
       (HOL:Cons (hol-light:tr-inst-one $thetas $first) (hol-light:tr-inst-all $thetas $rest)))
     ($other HOL:Nil))))

(= (hol-light:tr-inst-type $thetas $type)
  (case $thetas
    ((HOL:Nil $type)
     ((HOL:Cons $entries $rest)
       (hol-light:tr-inst-type $rest (hol-light:type-subst $entries $type)))
     ($other $type))))

(= (hol-light:tr-inst-entries $thetas $entries)
  (case $entries
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons (HOL:Entry $variable $value) $rest)
       (HOL:Cons (HOL:Entry (hol-light:tr-inst-one $thetas $variable)
                            (hol-light:tr-inst-one $thetas $value))
                 (hol-light:tr-inst-entries $thetas $rest)))
     ($other HOL:Nil))))

; The instance code of a type instantiation of `vars`.
(= (hol-light:tr-instance-code $thetas $vars)
  (case $vars
    ((HOL:Nil "")
     ((HOL:Cons $name $rest)
       (let $type (hol-light:tr-inst-type $thetas (Tyvar $name))
         (let $c (hol-light:tr-type-code $type)
           (let $cs (hol-light:tr-instance-code $thetas $rest)
             (format-args "{}{}" ($c $cs))))))
     ($other "?"))))

(= (hol-light:tr-fresh-name $target)
  (case (collapse (match $target (hol-light:transport-counter $count) $count))
    ((() (let $added (add-atom $target (hol-light:transport-counter 1))
           (hol-light:tr-index-symbol "hl:step-" 0)))
     (($current)
       (let $next (+ $current 1)
         (let $removed (remove-atom $target (hol-light:transport-counter $current))
           (let $added (add-atom $target (hol-light:transport-counter $next))
             (hol-light:tr-index-symbol "hl:step-" $current))))))))

; The target theorem already transported for a sequent, compared as HOL Light
; data, which evaluation leaves alone.
(= (hol-light:tr-transported $target $hyps $conclusion)
  (hol-light:tr-find-transported $hyps $conclusion
    (collapse (match $target (hol-light:transported $h $c $n) (HOL:Transported $h $c $n)))))

(= (hol-light:tr-find-transported $hyps $conclusion $records)
  (if (== $records ()) None
    (let $first (car-atom $records)
      (let (HOL:Transported $h $c $n) $first
        (if (== ($h $c) ($hyps $conclusion)) (Some $n)
          (let $rest (cdr-atom $records)
            (hol-light:tr-find-transported $hyps $conclusion $rest)))))))

; Replay the theorem of `key` under `thetas`; its target name, or a refusal.
; The state may be given by a bound token; it is read once here.
(= (hol-light:transport-replay $target $interp $given-state $key $thetas)
  (let $state $given-state (hol-light:tr-replay $target $interp $state $key $thetas)))

(= (hol-light:tr-replay $target $interp $state $key $thetas)
  (case (hol-light:known $state $key)
    (((Some (HOL:Thm $stored-hyps $stored-conclusion $dependencies $origin))
       (let $hyps (hol-light:tr-inst-all $thetas $stored-hyps)
         (let $conclusion (hol-light:tr-inst-one $thetas $stored-conclusion)
           (case (hol-light:tr-transported $target $hyps $conclusion)
             ((None
                (let $statement (hol-light:tr-sequent $target $interp $hyps $conclusion)
                  (hol-light:tr-prove $target $interp $state $thetas $hyps $conclusion
                    $statement $origin)))
              ((Some $known) $known))))))
     ($absent (function (return (HOL:TransportRefused (unknown-key $key))))))))

; A premise: replayed, with its sequent at the instance used.
(= (hol-light:tr-premise $target $interp $state $key $thetas)
  (case (hol-light:known $state $key)
    (((Some (HOL:Thm $stored-hyps $stored-conclusion $dependencies $origin))
       (let $name (hol-light:tr-replay $target $interp $state $key $thetas)
         (let $hyps (hol-light:tr-inst-all $thetas $stored-hyps)
           (let $conclusion (hol-light:tr-inst-one $thetas $stored-conclusion)
             (HOL:Premise $name $hyps $conclusion)))))
     ($absent (HOL:Premise (HOL:TransportRefused (unknown-key $key)) HOL:Nil HOL:Nil)))))

(= (hol-light:tr-prove $target $interp $state $thetas $hyps $conclusion $statement $origin)
  (let (HOL:Origin $key $request) $origin
    (let $frees (hol-light:tr-frees $hyps $conclusion)
      (let $ctx (HOL:ProofCtx $target $interp $frees $hyps HOL:Nil HOL:Nil)
        (let $body (hol-light:tr-rule $ctx $state $thetas $conclusion $request)
          (case (hol-light:tr-transported $target $hyps $conclusion)
            (((Some $known) $known)
             (None (hol-light:tr-publish $target $interp $hyps $conclusion $statement $key
                     $request $frees $body)))))))))

; The target's namespace: hol: in a space of the logic alone, set: in a set
; theory over it (HOTG among them).  Each publication is the judgment of that
; namespace.
(= (hol-light:tr-namespace $target)
  (if (== (collapse (match $target (hol:profile $p) $p)) ()) set hol))

(= (hol-light:tr-define $target $symbol $type $quoted)
  (if (== (hol-light:tr-namespace $target) hol)
    (hol:define $target $symbol $type $quoted)
    (set:define $target $symbol $type $quoted)))

(= (hol-light:tr-axiom $target $name $quoted)
  (if (== (hol-light:tr-namespace $target) hol)
    (hol:axiom $target $name $quoted)
    (set:axiom $target $name $quoted)))

(= (hol-light:tr-theorem $target $name $qs $qp)
  (if (== (hol-light:tr-namespace $target) hol)
    (hol:theorem $target $name $qs $qp)
    (set:theorem $target $name $qs $qp)))

(= (hol-light:tr-try-theorem $target $name $qs $qp)
  (if (== (hol-light:tr-namespace $target) hol)
    (try (hol:theorem $target $name $qs $qp))
    (try (set:theorem $target $name $qs $qp))))

; Publish a replayed step: the proof is checked by the target's own checker.
(= (hol-light:tr-publish $target $interp $hyps $conclusion $statement $key $request $frees $body)
          (let $proof (hol-light:tr-skeleton $frees $hyps $body)
            (let $name (hol-light:tr-fresh-name $target)
              (let $qs (hol-light:tr-node1 quote $statement)
                (let $qp (hol-light:tr-node1 quote $proof)
                  (let $published (hol-light:tr-theorem $target $name $qs $qp)
                    (if (== $published $name)
                      (let $marked (add-atom $target (hol-light:transported $hyps $conclusion $name))
                        $name)
                      (let $why (hol-light:tr-try-theorem $target $name $qs $qp)
                        (function (return (HOL:TransportRefused $key $request $why)))))))))))

; The proof body of one primitive step, inside the conclusion's skeleton.
(= (hol-light:tr-rule $ctx $state $thetas $conclusion $request)
  (let (HOL:ProofCtx $target $interp $frees $hyps $extra $local) $ctx
    (case $request
      (((REFL $source)
         (case (hol-light:dest-eq $conclusion)
           (((Some (HOL:Equation $left $right))
              (case (hol-light:term-type $left)
                (((Some $type)
                   (let $carrier (hol-light:tr-type $interp $type)
                     (let $term (hol-light:tr-proof-term $ctx $left)
                       (hol-light:tr-node2 pf:eq-refl $carrier $term))))
                 ($none (function (return (HOL:TransportUnsupported (REFL $conclusion))))))))
            ($none (function (return (HOL:TransportUnsupported (REFL $conclusion))))))))
       ((BETA $source)
         (case (hol-light:dest-eq $conclusion)
           (((Some (HOL:Equation $left $right))
              (case (hol-light:term-type $right)
                (((Some $type)
                   (let $carrier (hol-light:tr-type $interp $type)
                     (let $term (hol-light:tr-proof-term $ctx $right)
                       (hol-light:tr-node2 pf:eq-refl $carrier $term))))
                 ($none (function (return (HOL:TransportUnsupported (BETA $conclusion))))))))
            ($none (function (return (HOL:TransportUnsupported (BETA $conclusion))))))))
       ((ASSUME $source) (hol-light:tr-hypothesis $ctx $conclusion))
       ((TRANS $first $second)
         (let $p1 (hol-light:tr-premise $target $interp $state $first $thetas)
           (let $p2 (hol-light:tr-premise $target $interp $state $second $thetas)
             (let $u1 (hol-light:tr-use $ctx $p1 HOL:Nil)
               (let $u2 (hol-light:tr-use $ctx $p2 HOL:Nil)
                 (hol-light:tr-node2 pf:eq-trans $u1 $u2))))))
       ((EQ_MP $first $second)
         (let $p1 (hol-light:tr-premise $target $interp $state $first $thetas)
           (let $p2 (hol-light:tr-premise $target $interp $state $second $thetas)
             (let $u1 (hol-light:tr-use $ctx $p1 HOL:Nil)
               (let $u2 (hol-light:tr-use $ctx $p2 HOL:Nil)
                 (let $equation (hol-light:tr-node1 pf:eq-prop-elim $u1)
                   (hol-light:tr-node2 pf:imp-elim $equation $u2)))))))
       ((MK_COMB $first $second)
         (hol-light:tr-mk-comb $ctx $state $thetas $conclusion $first $second))
       ((ABS $variable $premise)
         (hol-light:tr-abs $ctx $state $thetas $conclusion $variable $premise))
       ((DEDUCT_ANTISYM_RULE $first $second)
         (hol-light:tr-deduct-antisym $ctx $state $thetas $conclusion $first $second))
       ((INST $entries $premise)
         (case (hol-light:read-maplets $state $entries)
           (((Some $substitution)
              (let $mapping (hol-light:tr-inst-entries $thetas $substitution)
                (let $p (hol-light:tr-premise $target $interp $state $premise $thetas)
                  (hol-light:tr-use $ctx $p $mapping))))
            ($none (function (return (HOL:TransportUnsupported (INST $entries))))))))
       ((INST_TYPE $entries $premise)
         (let $p (hol-light:tr-premise $target $interp $state $premise (HOL:Cons $entries $thetas))
           (hol-light:tr-use $ctx $p HOL:Nil)))
       ((AXIOM $name $source)
         (case (hol-light:read $state $source)
           (((Some $stored)
              (let $vars (hol-light:term-type-vars $stored)
                (let $code (hol-light:tr-instance-code $thetas $vars)
                  (let $commitment (hol-light:tr-axiom $target $interp $name $conclusion $code)
                    (hol-light:tr-node1 pf:known $commitment)))))
            ($none (function (return (HOL:TransportUnsupported (AXIOM $name))))))))
       ((DEFINITION $name $schema $source)
         (case (hol-light:dest-eq $conclusion)
           (((Some (HOL:Equation $left $right))
              (case (hol-light:term-type $left)
                (((Some $type)
                   (let $carrier (hol-light:tr-type $interp $type)
                     (let $term (hol-light:tr-proof-term $ctx $left)
                       (hol-light:tr-node2 pf:eq-refl $carrier $term))))
                 ($none (function (return (HOL:TransportUnsupported (DEFINITION $name))))))))
            ($none (function (return (HOL:TransportUnsupported (DEFINITION $name))))))))
       ($other (function (return (HOL:TransportUnsupported (rule $request)))))))))

; MK_COMB: f = g and x = y give f x = g y, through congruence in the function
; and then in the argument.
(= (hol-light:tr-mk-comb $ctx $state $thetas $conclusion $first $second)
  (let (HOL:ProofCtx $target $interp $frees $hyps $extra $local) $ctx
    (case (hol-light:dest-eq $conclusion)
      (((Some (HOL:Equation (HOL:App $f $x) (HOL:App $g $y)))
         (case ((hol-light:term-type $x) (hol-light:term-type (HOL:App $f $x)))
           ((((Some $a) (Some $b))
              (let $domain (hol-light:tr-type $interp $a)
                (let $codomain (hol-light:tr-type $interp $b)
                  (let $p1 (hol-light:tr-premise $target $interp $state $first $thetas)
                    (let $p2 (hol-light:tr-premise $target $interp $state $second $thetas)
                      (let $u1 (hol-light:tr-use $ctx $p1 HOL:Nil)
                        (let $u2 (hol-light:tr-use $ctx $p2 HOL:Nil)
                          (let $xt (hol-light:tr-proof-term $ctx $x)
                            (let $gt (hol-light:tr-proof-term $ctx $g)
                              (let $left (hol-light:tr-node4 pf:eq-app $domain $codomain $u1 $xt)
                                (let $right (hol-light:tr-node4 pf:eq-app-arg $domain $codomain $gt $u2)
                                  (hol-light:tr-node2 pf:eq-trans $left $right))))))))))))
            ($other (function (return (HOL:TransportUnsupported (MK_COMB $conclusion))))))))
       ($other (function (return (HOL:TransportUnsupported (MK_COMB $conclusion)))))))))

; ABS: s = t with v free gives (\v. s) = (\v. t), by functional
; extensionality at the abstraction's types.
(= (hol-light:tr-abs $ctx $state $thetas $conclusion $variable-source $premise)
  (let (HOL:ProofCtx $target $interp $frees $hyps $extra $local) $ctx
    (case ((hol-light:read $state $variable-source) (hol-light:dest-eq $conclusion))
      ((((Some $stored-variable) (Some (HOL:Equation $left $right)))
         (let $variable (hol-light:tr-inst-one $thetas $stored-variable)
           (let (HOL:Free $vname $a) $variable
             (case (hol-light:term-type $left)
               (((Some (Tyapp "fun" (HOL:Cons $a2 (HOL:Cons $b HOL:Nil))))
                  (let $law (hol-light:tr-funext $target $interp $a $b)
                    (let $lt (hol-light:tr-proof-term $ctx $left)
                      (let $rt (hol-light:tr-proof-term $ctx $right)
                        (let $inner-ctx (HOL:ProofCtx $target $interp $frees $hyps
                                          (HOL:Cons $variable $extra) $local)
                          (let $p (hol-light:tr-premise $target $interp $state $premise $thetas)
                            (let $pointwise (hol-light:tr-use $inner-ctx $p HOL:Nil)
                              (let $all (hol-light:tr-node1 pf:all-intro $pointwise)
                                (let $known (hol-light:tr-node1 pf:known $law)
                                  (let $at-left (hol-light:tr-node2 pf:all-elim $known $lt)
                                    (let $at-right (hol-light:tr-node2 pf:all-elim $at-left $rt)
                                      (hol-light:tr-node2 pf:imp-elim $at-right $all))))))))))))
                ($other (function (return (HOL:TransportUnsupported (ABS $conclusion))))))))))
       ($other (function (return (HOL:TransportUnsupported (ABS $conclusion)))))))))

; DEDUCT_ANTISYM_RULE: p from Gamma and q from Delta give p = q, without q in
; Gamma and p in Delta, by propositional extensionality.
(= (hol-light:tr-deduct-antisym $ctx $state $thetas $conclusion $first $second)
  (let (HOL:ProofCtx $target $interp $frees $hyps $extra $local) $ctx
    (let $p1 (hol-light:tr-premise $target $interp $state $first $thetas)
      (let $p2 (hol-light:tr-premise $target $interp $state $second $thetas)
        (let (HOL:Premise $n1 $h1 $c1) $p1
          (let (HOL:Premise $n2 $h2 $c2) $p2
            (let $law (hol-light:tr-propext $target)
              (let $pt (hol-light:tr-proof-term $ctx $c1)
                (let $qt (hol-light:tr-proof-term $ctx $c2)
                  (let $with-p (HOL:ProofCtx $target $interp $frees $hyps $extra (HOL:Cons $c1 $local))
                    (let $with-q (HOL:ProofCtx $target $interp $frees $hyps $extra (HOL:Cons $c2 $local))
                      (let $q-from-p (hol-light:tr-use $with-p $p2 HOL:Nil)
                        (let $p-from-q (hol-light:tr-use $with-q $p1 HOL:Nil)
                          (let $pq (hol-light:tr-node1 pf:imp-intro $q-from-p)
                            (let $qp (hol-light:tr-node1 pf:imp-intro $p-from-q)
                              (let $known (hol-light:tr-node1 pf:known $law)
                                (let $at-p (hol-light:tr-node2 pf:all-elim $known $pt)
                                  (let $at-q (hol-light:tr-node2 pf:all-elim $at-p $qt)
                                    (let $first-arg (hol-light:tr-node2 pf:imp-elim $at-q $pq)
                                      (hol-light:tr-node2 pf:imp-elim $first-arg $qp))))))))))))))))))))
