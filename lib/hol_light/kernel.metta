; HOL Light revision 924faf0c3471a04174ffdeb8576ce7cabcb92d64.
; This is supplied-proof checking. A request names previously checked
; premises; a caller-supplied HOL:Thm is never accepted as a proof.
; HOL:Thm retains hypotheses, axiom dependencies and a proof origin.

(= (hol-light:initial $commitments)
  (HOL:State
    (HOL:Cons (HOL:Entry "bool" (HOL:TypeOperator 0 HOL:Primitive))
    (HOL:Cons (HOL:Entry "fun" (HOL:TypeOperator 2 HOL:Primitive))
    (HOL:Cons (HOL:Entry "ind" (HOL:TypeOperator 0 HOL:Primitive)) HOL:Nil)))
    (HOL:Cons (HOL:Entry "=" (HOL:Constant
      (hol-light:eq-type (Tyvar "A")) (HOL:Primitive "=")))
    (HOL:Cons (HOL:Entry "@" (HOL:Constant
      (hol-light:fun (hol-light:fun (Tyvar "A") (hol-light:bool)) (Tyvar "A"))
      (HOL:Primitive "@"))) HOL:Nil))
    $commitments HOL:Nil HOL:Nil))

(= (hol-light:read (HOL:State $operators $constants $axioms $cache $receipts) $term)
  (hol-light:read-term $operators $constants HOL:Nil $term))

(= (hol-light:known (HOL:State $operators $constants $axioms $cache $receipts) $key)
  (hol-light:lookup $key $cache))

(= (hol-light:theorem-eq $hyps $left $right $dependencies $origin)
  (case (hol-light:equation $left $right)
    (((Some $equation) (Some (HOL:Thm $hyps $equation $dependencies $origin)))
     ($malformed None))))

(= (hol-light:infer $state $request)
  (case $request
    (((REFL $term)
      (case (hol-light:read $state $term)
        (((Some $value) (hol-light:theorem-eq HOL:Nil $value $value HOL:Nil REFL))
         ($malformed None))))
     ((TRANS $first $second)
      (hol-light:trans (hol-light:known $state $first) (hol-light:known $state $second)))
     ((MK_COMB $first $second)
      (hol-light:mk-comb (hol-light:known $state $first) (hol-light:known $state $second)))
     ((ABS $variable $premise)
      (hol-light:abs (hol-light:read $state $variable) (hol-light:known $state $premise)))
     ((BETA $term) (hol-light:beta $state $term))
     ((ASSUME $term)
      (case (hol-light:read $state $term)
        (((Some $value)
          (if (== (hol-light:term-type $value) (Some (hol-light:bool)))
            (Some (HOL:Thm (HOL:Cons $value HOL:Nil) $value HOL:Nil ASSUME)) None))
         ($malformed None))))
     ((EQ_MP $first $second)
      (hol-light:eq-mp (hol-light:known $state $first) (hol-light:known $state $second)))
     ((DEDUCT_ANTISYM_RULE $first $second)
      (hol-light:deduct-antisym (hol-light:known $state $first) (hol-light:known $state $second)))
     ((INST $entries $premise)
      (case (hol-light:read-maplets $state $entries)
        (((Some $substitution)
          (hol-light:inst $substitution (hol-light:known $state $premise)))
         ($malformed None))))
     ((INST_TYPE $entries $premise)
      (if (hol-light:type-maplets-valid $state $entries)
        (hol-light:inst-type $entries (hol-light:known $state $premise)) None))
     ($unsupported None))))

(= (hol-light:trans $first $second)
  (case ($first $second)
    ((((Some (HOL:Thm $hyps1 $conclusion1 $deps1 $origin1))
       (Some (HOL:Thm $hyps2 $conclusion2 $deps2 $origin2)))
      (case ((hol-light:dest-eq $conclusion1) (hol-light:dest-eq $conclusion2))
        ((((Some (HOL:Equation $left $middle1)) (Some (HOL:Equation $middle2 $right)))
          (if (== $middle1 $middle2)
            (hol-light:theorem-eq (hol-light:union $hyps1 $hyps2) $left $right
              (hol-light:union $deps1 $deps2) TRANS) None))
         ($malformed None))))
     ($malformed None))))

(= (hol-light:mk-comb $first $second)
  (case ($first $second)
    ((((Some (HOL:Thm $hyps1 $conclusion1 $deps1 $origin1))
       (Some (HOL:Thm $hyps2 $conclusion2 $deps2 $origin2)))
      (case ((hol-light:dest-eq $conclusion1) (hol-light:dest-eq $conclusion2))
        ((((Some (HOL:Equation $fn1 $fn2)) (Some (HOL:Equation $arg1 $arg2)))
          (hol-light:theorem-eq (hol-light:union $hyps1 $hyps2)
            (HOL:App $fn1 $arg1) (HOL:App $fn2 $arg2)
            (hol-light:union $deps1 $deps2) MK_COMB))
         ($malformed None))))
     ($malformed None))))

(= (hol-light:abs $variable $premise)
  (case ($variable $premise)
    ((((Some (HOL:Free $name $type)) (Some (HOL:Thm $hyps $conclusion $deps $origin)))
      (let $binder (HOL:Free $name $type)
        (if (hol-light:free-in $binder $hyps) None
          (case (hol-light:dest-eq $conclusion)
            (((Some (HOL:Equation $left $right))
              (hol-light:theorem-eq $hyps
                (HOL:Lam $type (hol-light:abstract $binder 0 $left))
                (HOL:Lam $type (hol-light:abstract $binder 0 $right)) $deps ABS))
             ($malformed None))))))
     ($malformed None))))

; The source BETA primitive accepts only (lambda v. body) v.
; Arbitrary beta instances are derived by INST, not admitted by this gate.
(= (hol-light:beta $state $term)
  (case $term
    (((Comb (Abs (Var $name $type) $body) $argument)
      (if (== $argument (Var $name $type))
        (case (hol-light:read $state $term)
          (((Some (HOL:App (HOL:Lam $domain $value) $arg))
            (hol-light:theorem-eq HOL:Nil
              (HOL:App (HOL:Lam $domain $value) $arg)
              (hol-light:open-binder $arg 0 $value) HOL:Nil BETA))
           ($malformed None))) None))
     ($malformed None))))

(= (hol-light:eq-mp $first $second)
  (case ($first $second)
    ((((Some (HOL:Thm $hyps1 $equation $deps1 $origin1))
       (Some (HOL:Thm $hyps2 $conclusion $deps2 $origin2)))
      (case (hol-light:dest-eq $equation)
        (((Some (HOL:Equation $left $right))
          (if (== $left $conclusion)
            (if (== (hol-light:term-type $right) (Some (hol-light:bool)))
              (Some (HOL:Thm (hol-light:union $hyps1 $hyps2) $right
                (hol-light:union $deps1 $deps2) EQ_MP)) None) None))
         ($malformed None))))
     ($malformed None))))

(= (hol-light:deduct-antisym $first $second)
  (case ($first $second)
    ((((Some (HOL:Thm $hyps1 $conclusion1 $deps1 $origin1))
       (Some (HOL:Thm $hyps2 $conclusion2 $deps2 $origin2)))
      (hol-light:theorem-eq
        (hol-light:union (hol-light:remove $conclusion2 $hyps1)
                         (hol-light:remove $conclusion1 $hyps2))
        $conclusion1 $conclusion2 (hol-light:union $deps1 $deps2) DEDUCT_ANTISYM_RULE))
     ($malformed None))))

(= (hol-light:read-maplets $state $entries)
  (case $entries
    ((HOL:Nil (Some HOL:Nil))
     ((HOL:Cons (HOL:Maplet $replacement (Var $name $type)) $rest)
      (case ((hol-light:read $state $replacement)
             (hol-light:read $state (Var $name $type))
             (hol-light:read-maplets $state $rest))
        ((((Some $value) (Some $variable) (Some $tail))
          (if (== (hol-light:term-type $value) (Some $type))
            (Some (HOL:Cons (HOL:Entry $variable $value) $tail)) None))
         ($malformed None))))
     ($malformed None))))

(= (hol-light:type-maplets-valid (HOL:State $operators $constants $axioms $cache $receipts) $entries)
  (case $entries
    ((HOL:Nil True)
     ((HOL:Cons (HOL:Maplet $replacement $key) $rest)
      (if (hol-light:type-formed $operators $replacement)
        (if (hol-light:type-formed $operators $key)
          (hol-light:type-maplets-valid
            (HOL:State $operators $constants $axioms $cache $receipts) $rest) False) False))
     ($malformed False))))

(= (hol-light:inst $entries $premise)
  (case $premise
    (((Some (HOL:Thm $hyps $conclusion $dependencies $origin))
      (Some (HOL:Thm (hol-light:inst-terms $entries $hyps)
        (hol-light:term-subst $entries $conclusion) $dependencies INST)))
     ($malformed None))))

(= (hol-light:inst-type $entries $premise)
  (case $premise
    (((Some (HOL:Thm $hyps $conclusion $dependencies $origin))
      (Some (HOL:Thm (hol-light:inst-types $entries $hyps)
        (hol-light:term-inst-type $entries $conclusion) $dependencies INST_TYPE)))
     ($malformed None))))

; A receipt keeps every premise position, including two uses of one proof.
(= (hol-light:premises $request)
  (case $request
    (((TRANS $first $second) (HOL:Cons $first (HOL:Cons $second HOL:Nil)))
     ((MK_COMB $first $second) (HOL:Cons $first (HOL:Cons $second HOL:Nil)))
     ((EQ_MP $first $second) (HOL:Cons $first (HOL:Cons $second HOL:Nil)))
     ((DEDUCT_ANTISYM_RULE $first $second) (HOL:Cons $first (HOL:Cons $second HOL:Nil)))
     ((ABS $variable $premise) (HOL:Cons $premise HOL:Nil))
     ((INST $entries $premise) (HOL:Cons $premise HOL:Nil))
     ((INST_TYPE $entries $premise) (HOL:Cons $premise HOL:Nil))
     ((TYPE_DEFINITION $name $abs $rep $premise) (HOL:Cons $premise HOL:Nil))
     ($noPremises HOL:Nil))))

(= (hol-light:publish
     (HOL:State $operators $constants $axioms $cache $receipts) $key $request $result)
  (if (hol-light:receipt-key-present $key $receipts) None
   (case $result
    (((Some (HOL:Thm $hyps $conclusion $dependencies $origin))
      (case (hol-light:lookup $key $cache)
        ((None
          (Some (HOL:State $operators $constants $axioms
            (HOL:Cons (HOL:Entry $key (HOL:Thm $hyps $conclusion $dependencies
              (HOL:Origin $key $request))) $cache)
            (HOL:Cons (HOL:Receipt $key $request (hol-light:premises $request)) $receipts))))
         ($duplicate None))))
     ($refused None)))))

(= (hol-light:ground $value)
  (case (get-metatype $value)
    ((Variable False)
     (Expression (hol-light:ground-list $value))
     ($atomic True))))

(= (hol-light:ground-list $values)
  (case $values
    ((() True)
     ((cons $first $rest)
      (if (hol-light:ground $first) (hol-light:ground-list $rest) False))
     ($malformed False))))

; The run starts with no cached theorems. Refusal cannot be reopened by a
; later command. Selected commitments are separate from the proof commands.
(: hol-light:run (-> Atom Atom %Undefined%))
(= (hol-light:run $commitments $commands)
  (if (hol-light:ground $commitments)
    (if (hol-light:ground $commands)
      (if (hol-light:list-valid $commitments)
        (hol-light:run-from (hol-light:initial $commitments) $commands) None) None) None))

(= (hol-light:run-from $state $commands)
  (case $commands
    ((HOL:Nil (Some $state))
     ((HOL:Cons (HOL:Step $key $request) $rest)
      (case (hol-light:step $state $key $request)
        (((Some $next) (hol-light:run-from $next $rest))
         ($refused None))))
     ($malformed None))))

(= (hol-light:step $state $key $request)
  (case $state
    (((HOL:State $operators $constants $axioms $cache $receipts)
      (if (hol-light:receipt-key-present $key $receipts) None
        (hol-light:dispatch $state $key $request)))
     ($malformed None))))

(= (hol-light:receipt-key-present $key $receipts)
  (case $receipts
    ((HOL:Nil False)
     ((HOL:Cons (HOL:Receipt $previous $request $premises) $rest)
      (if (== $key $previous) True (hol-light:receipt-key-present $key $rest)))
     ($malformed True))))

(= (hol-light:dispatch $state $key $request)
  (case $request
    (((DECLARE_TYPE $name $arity)
      (let $extension hol-light:declare-type ($extension $state $key $name $arity)))
     ((DECLARE_CONSTANT $name $schema)
      (let $extension hol-light:declare-constant ($extension $state $key $name $schema)))
     ((DEFINITION $name $schema $body)
      (let $extension hol-light:definition ($extension $state $key $name $schema $body)))
     ((TYPE_DEFINITION $name $abs $rep $premise)
      (let $extension hol-light:type-definition ($extension $state $key $name $abs $rep $premise)))
     ((AXIOM $name $term)
      (let $extension hol-light:axiom
        (hol-light:publish $state $key $request ($extension $state $name $term))))
     ($inference (hol-light:publish $state $key $request (hol-light:infer $state $request))))))

(= (hol-light:check-result $result $key $sourceHyps $sourceConclusion)
  (case $result
    (((Some $state)
      (case ((hol-light:known $state $key) (hol-light:read $state $sourceConclusion)
             (hol-light:read-hypotheses $state $sourceHyps))
        ((((Some (HOL:Thm $hyps $conclusion $dependencies $origin))
           (Some $expected) (Some $expectedHyps))
          (if (== $conclusion $expected)
            (if (hol-light:subset $hyps $expectedHyps)
              (hol-light:subset $expectedHyps $hyps) False) False))
         ($refused False))))
     ($refused False))))

(= (hol-light:read-hypotheses $state $terms)
  (case $terms
    ((HOL:Nil (Some HOL:Nil))
     ((HOL:Cons $first $rest)
      (case ((hol-light:read $state $first) (hol-light:read-hypotheses $state $rest))
        ((((Some $value) (Some $tail))
          (if (== (hol-light:term-type $value) (Some (hol-light:bool)))
            (Some (hol-light:insert $value $tail)) None))
         ($malformed None))))
     ($malformed None))))
