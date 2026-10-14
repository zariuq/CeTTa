; Declaration and definition operations extend an immutable theory state.
; Definitions retain their bodies as identities; no theorem name is authority.
; AXIOM may use only the separately supplied commitment with that formula.

(= (hol-light:declare-type
     (HOL:State $operators $constants $axioms $cache $receipts) $key $name $arity)
  (if (== (get-type $name) String)
    (if (hol-light:natural $arity)
      (case (hol-light:lookup $name $operators)
        ((None
          (Some (HOL:State
            (HOL:Cons (HOL:Entry $name (HOL:TypeOperator $arity (HOL:Primitive $name))) $operators)
            $constants $axioms $cache
            (HOL:Cons (HOL:Receipt $key (DECLARE_TYPE $name $arity) HOL:Nil) $receipts))))
         ($duplicate None))) None) None))

(= (hol-light:declare-constant
     (HOL:State $operators $constants $axioms $cache $receipts) $key $name $schema)
  (if (== (get-type $name) String)
    (if (hol-light:type-formed $operators $schema)
      (case (hol-light:lookup $name $constants)
        ((None
          (Some (HOL:State $operators
            (HOL:Cons (HOL:Entry $name (HOL:Constant $schema (HOL:Primitive $name))) $constants)
            $axioms $cache
            (HOL:Cons (HOL:Receipt $key (DECLARE_CONSTANT $name $schema) HOL:Nil) $receipts))))
         ($duplicate None))) None) None))

(= (hol-light:axiom $state $name $source)
  (case $state
    (((HOL:State $operators $constants $axioms $cache $receipts)
      (case (hol-light:lookup $name $axioms)
        (((Some $commitment)
          (case ((hol-light:read $state $source) (hol-light:read $state $commitment))
            ((((Some $term) (Some $expected))
              (if (== $term $expected)
                (if (== (hol-light:term-type $term) (Some (hol-light:bool)))
                  (Some (HOL:Thm HOL:Nil $term
                    (HOL:Cons (HOL:AxiomDependency $name $term) HOL:Nil) AXIOM)) None) None))
             ($malformed None))))
         ($undeclared None))))
     ($malformed None))))

(= (hol-light:definition
     (HOL:State $operators $constants $axioms $cache $receipts) $key $name $schema $source)
  (let $state (HOL:State $operators $constants $axioms $cache $receipts)
    (if (== (get-type $name) String)
      (if (hol-light:type-formed $operators $schema)
        (case ((hol-light:lookup $name $constants) (hol-light:read $state $source))
          (((None (Some $body))
            (if (== (hol-light:term-type $body) (Some $schema))
              (if (== (hol-light:free-vars $body) HOL:Nil)
                (if (hol-light:subset (hol-light:term-type-vars $body)
                                      (hol-light:type-vars $schema))
                  (let $identity (HOL:Defined $name $schema $body)
                    (let $extended (HOL:State $operators
                      (HOL:Cons (HOL:Entry $name (HOL:Constant $schema $identity)) $constants)
                      $axioms $cache $receipts)
                      (hol-light:publish $extended $key (DEFINITION $name $schema $source)
                        (hol-light:theorem-eq HOL:Nil (HOL:C $identity $schema) $body HOL:Nil DEFINITION))))
                  None) None) None))
           ($malformed None))) None) None)))

(= (hol-light:type-parameters $names)
  (case $names
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $name $rest)
      (HOL:Cons (Tyvar $name) (hol-light:type-parameters $rest)))
     ($malformed None))))

(= (hol-light:type-definition
     (HOL:State $operators $constants $axioms $cache $receipts)
     $key $name $absname $repname $premise)
  (let $state (HOL:State $operators $constants $axioms $cache $receipts)
    (if (== (get-type $name) String)
      (if (== (get-type $absname) String)
        (if (== (get-type $repname) String)
          (if (== $absname $repname) None
            (case ((hol-light:lookup $name $operators)
                   (hol-light:lookup $absname $constants)
                   (hol-light:lookup $repname $constants)
                   (hol-light:known $state $premise))
              (((None None None
                  (Some (HOL:Thm HOL:Nil (HOL:App $predicate $witness) $dependencies $origin)))
                (if (== (hol-light:free-vars $predicate) HOL:Nil)
                  (case (hol-light:term-type $witness)
                    (((Some $representation)
                      (hol-light:extend-type $state $key $name $absname $repname $premise
                        $predicate $representation $dependencies))
                     ($malformed None))) None))
               ($malformed None)))) None) None) None)))

(= (hol-light:extend-type
     (HOL:State $operators $constants $axioms $cache $receipts)
     $key $name $absname $repname $premise $predicate $representation $dependencies)
  (let* (($parameters (hol-light:sorted-names (hol-light:term-type-vars $predicate)))
         ($newtype (Tyapp $name (hol-light:type-parameters $parameters)))
         ($abstype (hol-light:fun $representation $newtype))
         ($reptype (hol-light:fun $newtype $representation))
         ($abs (HOL:C (HOL:Abstraction $absname $name $predicate) $abstype))
         ($rep (HOL:C (HOL:Representation $repname $name $predicate) $reptype))
         ($newOperators (HOL:Cons
            (HOL:Entry $name (HOL:TypeOperator (hol-light:length $parameters)
              (HOL:DefinedType $name $predicate))) $operators))
         ($newConstants (HOL:Cons
            (HOL:Entry $absname (HOL:Constant $abstype (HOL:Abstraction $absname $name $predicate)))
            (HOL:Cons
              (HOL:Entry $repname (HOL:Constant $reptype (HOL:Representation $repname $name $predicate)))
              $constants)))
         ($extended (HOL:State $newOperators $newConstants $axioms $cache $receipts))
         ($a (HOL:Free "a" $newtype))
         ($r (HOL:Free "r" $representation))
         ($request (TYPE_DEFINITION $name $absname $repname $premise)))
    (case ((hol-light:equation (HOL:App $abs (HOL:App $rep $a)) $a)
           (hol-light:equation (HOL:App $rep (HOL:App $abs $r)) $r))
      ((((Some $first) (Some $retraction))
        (case (hol-light:equation (HOL:App $predicate $r) $retraction)
          (((Some $second)
            (case (hol-light:publish $extended $key $request
                    (Some (HOL:Thm HOL:Nil $first $dependencies TYPE_DEFINITION)))
              (((Some $withFirst)
                (hol-light:publish $withFirst (HOL:RepKey $key) $request
                  (Some (HOL:Thm HOL:Nil $second $dependencies TYPE_DEFINITION))))
               ($duplicate None))))
           ($malformed None))))
       ($malformed None)))))
