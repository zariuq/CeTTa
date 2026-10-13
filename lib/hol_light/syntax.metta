; Named HOL Light terms enter through read-term. Bound occurrences are
; represented by positions, so alpha equality is structural equality.
; Source constructors Var, Const, Comb, Abs, Tyvar and Tyapp are data.

(= (hol-light:bool) (Tyapp "bool" HOL:Nil))
(= (hol-light:fun $domain $codomain)
  (Tyapp "fun" (HOL:Cons $domain (HOL:Cons $codomain HOL:Nil))))
(= (hol-light:eq-type $type)
  (hol-light:fun $type (hol-light:fun $type (hol-light:bool))))

(= (hol-light:natural $value)
  (if (== (get-type $value) Number)
    (if (< $value 0) False
      (if (== $value 0) True (hol-light:natural (- $value 1)))) False))

(= (hol-light:type-formed $operators $type)
  (case $type
    (((Tyvar $name) (== (get-type $name) String))
     ((Tyapp $name $arguments)
       (case (hol-light:lookup $name $operators)
         (((Some (HOL:TypeOperator $arity $origin))
           (if (hol-light:list-valid $arguments)
             (if (== (hol-light:length $arguments) $arity)
               (hol-light:types-formed $operators $arguments) False) False))
          ($unknown False))))
     ($malformed False))))

(= (hol-light:types-formed $operators $types)
  (case $types
    ((HOL:Nil True)
     ((HOL:Cons $first $rest)
       (if (hol-light:type-formed $operators $first)
         (hol-light:types-formed $operators $rest) False))
     ($malformed False))))

; Matching a polymorphic schema does not unify distinct type variables.
(= (hol-light:type-match $schema $instance $assignments)
  (case $schema
    (((Tyvar $name)
       (case (hol-light:lookup $name $assignments)
         ((None (Some (HOL:Cons (HOL:Entry $name $instance) $assignments)))
          ((Some $previous)
           (if (== $previous $instance) (Some $assignments) None))
          ($malformed None))))
     ((Tyapp $operator $arguments)
       (case $instance
         (((Tyapp $target $values)
           (if (== $operator $target)
             (hol-light:type-match-list $arguments $values $assignments) None))
          ($malformed None))))
     ($malformed None))))

(= (hol-light:type-match-list $schemas $instances $assignments)
  (case ($schemas $instances)
    (((HOL:Nil HOL:Nil) (Some $assignments))
     (((HOL:Cons $first $rest) (HOL:Cons $value $values))
       (case (hol-light:type-match $first $value $assignments)
         (((Some $next) (hol-light:type-match-list $rest $values $next))
          ($malformed None))))
     ($malformed None))))

(= (hol-light:bound-index $variable $context $index)
  (case $context
    ((HOL:Nil None)
     ((HOL:Cons $binder $rest)
       (if (== $variable $binder) (Some $index)
         (hol-light:bound-index $variable $rest (+ $index 1))))
     ($malformed None))))

(= (hol-light:read-term $operators $constants $context $source)
  (case $source
    (((Var $name $type)
       (if (== (get-type $name) String)
         (if (hol-light:type-formed $operators $type)
           (case (hol-light:bound-index $source $context 0)
             ((None (Some (HOL:Free $name $type)))
              ((Some $index) (Some (HOL:Bound $index $type)))
              ($malformed None))) None) None))
     ((Const $name $type)
       (if (hol-light:type-formed $operators $type)
         (case (hol-light:lookup $name $constants)
           (((Some (HOL:Constant $schema $identity))
             (case (hol-light:type-match $schema $type HOL:Nil)
               (((Some $assignments) (Some (HOL:C $identity $type)))
                ($malformed None))))
            ($unknown None))) None))
     ((Comb $function $argument)
       (case ((hol-light:read-term $operators $constants $context $function)
              (hol-light:read-term $operators $constants $context $argument))
         ((((Some $fn) (Some $value))
           (case (hol-light:term-type $fn)
             (((Some (Tyapp "fun" (HOL:Cons $domain (HOL:Cons $codomain HOL:Nil))))
               (if (== (hol-light:term-type $value) (Some $domain))
                 (Some (HOL:App $fn $value)) None))
              ($malformed None))))
          ($malformed None))))
     ((Abs (Var $name $type) $body)
       (if (== (get-type $name) String)
         (if (hol-light:type-formed $operators $type)
           (case (hol-light:read-term $operators $constants
                   (HOL:Cons (Var $name $type) $context) $body)
             (((Some $value) (Some (HOL:Lam $type $value)))
              ($malformed None))) None) None))
     ($malformed None))))

(= (hol-light:term-type $term)
  (case $term
    (((HOL:Free $name $type) (Some $type))
     ((HOL:Bound $index $type) (Some $type))
     ((HOL:C $identity $type) (Some $type))
     ((HOL:Lam $domain $body)
       (case (hol-light:term-type $body)
         (((Some $codomain) (Some (hol-light:fun $domain $codomain)))
          ($malformed None))))
     ((HOL:App $function $argument)
       (case (hol-light:term-type $function)
         (((Some (Tyapp "fun" (HOL:Cons $domain (HOL:Cons $codomain HOL:Nil))))
           (if (== (hol-light:term-type $argument) (Some $domain))
             (Some $codomain) None))
          ($malformed None))))
     ($malformed None))))

(= (hol-light:equation $left $right)
  (case (hol-light:term-type $left)
    (((Some $type)
      (if (== (hol-light:term-type $right) (Some $type))
        (Some (HOL:App (HOL:App
          (HOL:C (HOL:Primitive "=") (hol-light:eq-type $type)) $left) $right)) None))
     ($malformed None))))

(= (hol-light:dest-eq $term)
  (case $term
    (((HOL:App (HOL:App (HOL:C (HOL:Primitive "=") $type) $left) $right)
      (Some (HOL:Equation $left $right)))
     ($malformed None))))

(= (hol-light:free-vars $term)
  (case $term
    (((HOL:Free $name $type) (HOL:Cons $term HOL:Nil))
     ((HOL:Bound $index $type) HOL:Nil)
     ((HOL:C $identity $type) HOL:Nil)
     ((HOL:Lam $type $body) (hol-light:free-vars $body))
     ((HOL:App $function $argument)
      (hol-light:union (hol-light:free-vars $function) (hol-light:free-vars $argument)))
     ($malformed None))))

(= (hol-light:free-in $variable $terms)
  (case $terms
    ((HOL:Nil False)
     ((HOL:Cons $first $rest)
      (if (hol-light:member $variable (hol-light:free-vars $first))
        True (hol-light:free-in $variable $rest)))
     ($malformed True))))

(= (hol-light:term-type-vars $term)
  (case $term
    (((HOL:Free $name $type) (hol-light:type-vars $type))
     ((HOL:Bound $index $type) (hol-light:type-vars $type))
     ((HOL:C $identity $type) (hol-light:type-vars $type))
     ((HOL:Lam $type $body)
      (hol-light:union (hol-light:type-vars $type) (hol-light:term-type-vars $body)))
     ((HOL:App $function $argument)
      (hol-light:union (hol-light:term-type-vars $function)
                      (hol-light:term-type-vars $argument)))
     ($malformed None))))

; Entering a binder shifts existing outer references and binds occurrences
; of the specified free variable. Nested binders keep their own positions.
(= (hol-light:abstract $variable $depth $term)
  (case $term
    (((HOL:Free $name $type)
      (if (== $variable $term) (HOL:Bound $depth $type) $term))
     ((HOL:Bound $index $type)
      (if (>= $index $depth) (HOL:Bound (+ $index 1) $type) $term))
     ((HOL:C $identity $type) $term)
     ((HOL:Lam $type $body)
      (HOL:Lam $type (hol-light:abstract $variable (+ $depth 1) $body)))
     ((HOL:App $function $argument)
      (HOL:App (hol-light:abstract $variable $depth $function)
               (hol-light:abstract $variable $depth $argument)))
     ($malformed None))))

(= (hol-light:open-binder $replacement $depth $term)
  (case $term
    (((HOL:Free $name $type) $term)
     ((HOL:Bound $index $type)
      (if (== $index $depth) $replacement
        (if (> $index $depth) (HOL:Bound (- $index 1) $type) $term)))
     ((HOL:C $identity $type) $term)
     ((HOL:Lam $type $body)
      (HOL:Lam $type (hol-light:open-binder $replacement (+ $depth 1) $body)))
     ((HOL:App $function $argument)
      (HOL:App (hol-light:open-binder $replacement $depth $function)
               (hol-light:open-binder $replacement $depth $argument)))
     ($malformed None))))

; Term replacements are closed with respect to positional binders. Their
; free variables remain free when inserted beneath an abstraction.
(= (hol-light:term-subst $entries $term)
  (case $term
    (((HOL:Free $name $type)
      (case (hol-light:lookup $term $entries)
        (((Some $replacement) $replacement) ($absent $term))))
     ((HOL:Bound $index $type) $term)
     ((HOL:C $identity $type) $term)
     ((HOL:Lam $type $body) (HOL:Lam $type (hol-light:term-subst $entries $body)))
     ((HOL:App $function $argument)
      (HOL:App (hol-light:term-subst $entries $function)
               (hol-light:term-subst $entries $argument)))
     ($malformed None))))

(= (hol-light:term-inst-type $entries $term)
  (case $term
    (((HOL:Free $name $type) (HOL:Free $name (hol-light:type-subst $entries $type)))
     ((HOL:Bound $index $type) (HOL:Bound $index (hol-light:type-subst $entries $type)))
     ((HOL:C $identity $type) (HOL:C $identity (hol-light:type-subst $entries $type)))
     ((HOL:Lam $type $body)
      (HOL:Lam (hol-light:type-subst $entries $type)
               (hol-light:term-inst-type $entries $body)))
     ((HOL:App $function $argument)
      (HOL:App (hol-light:term-inst-type $entries $function)
               (hol-light:term-inst-type $entries $argument)))
     ($malformed None))))

(= (hol-light:inst-terms $entries $terms)
  (case $terms
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
      (hol-light:insert (hol-light:term-subst $entries $first)
                        (hol-light:inst-terms $entries $rest)))
     ($malformed None))))

(= (hol-light:inst-types $entries $terms)
  (case $terms
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
      (hol-light:insert (hol-light:term-inst-type $entries $first)
                        (hol-light:inst-types $entries $rest)))
     ($malformed None))))
