; Static checks of a D0 program (reader.metta's IR): relation declarations,
; finite primitive values, clause typing with range restriction, and declared
; interfaces. The issues keep their statement positions:
;   DuplicateRelations | (InvalidDeclaration i) | (IllTypedFact i)
;   | (IllTypedClause i) | (UndeclaredInterface i)

(= (d0:map $f $items)
   (case $items ((() ()) ((cons $first $rest) (cons-atom ($f $first) (d0:map $f $rest))))))
(= (d0:all $p $items)
   (case $items ((() True) ((cons $first $rest) (if ($p $first) (d0:all $p $rest) False)))))
(= (d0:contains? $item $items)
   (case $items ((() False) ((cons $first $rest) (if (== $first $item) True (d0:contains? $item $rest))))))
(= (d0:distinct? $items)
   (case $items ((() True) ((cons $first $rest) (if (d0:contains? $first $rest) False (d0:distinct? $rest))))))
(= (d0:length $items)
   (case $items ((() 0) ((cons $first $rest) (+ 1 (d0:length $rest))))))

; ---- Declarations and the signature ----------------------------------------

(= (d0:declaration? $statement) (case $statement (((D0Declaration $name $attributes) True) ($other False))))
(= (d0:declarations $statements)
   (case $statements
     ((() ())
      ((cons $first $rest)
        (if (d0:declaration? $first) (cons-atom $first (d0:declarations $rest)) (d0:declarations $rest))))))
(= (d0:declaration-name (D0Declaration $name $attributes)) $name)
(= (d0:attribute-name (D0Attribute $name $type)) $name)
(= (d0:attribute-type-of (D0Attribute $name $type)) $type)

; The attribute types of a declared relation: (Some types) or None.
(= (d0:signature $declarations $relation)
   (case $declarations
     ((() None)
      ((cons (D0Declaration $name $attributes) $rest)
        (if (== $name $relation) (Some (d0:map d0:attribute-type-of $attributes)) (d0:signature $rest $relation))))))

(= (d0:nonempty-text? $text) (not (== $text "")))
(= (d0:declaration-valid? (D0Declaration $name $attributes))
   (if (== $name ()) False
     (if (not (d0:all d0:nonempty-text? $name)) False
       (let $names (d0:map d0:attribute-name $attributes)
         (if (d0:distinct? $names) (d0:all d0:nonempty-text? $names) False)))))

; ---- Values and arguments ---------------------------------------------------

(= (d0:value-type $value)
   (case $value
     (((D0SymbolValue $text) D0Symbol) ((D0NumberValue $n) D0Number) ((D0UnsignedValue $n) D0Unsigned))))

; Literals were read within the 64-bit domain, so a typed value is one of the right kind.
(= (d0:values-typed? $values $types)
   (case ($values $types)
     (((() ()) True)
      (((cons $value $more) (cons $type $rest))
        (if (== (d0:value-type $value) $type) (d0:values-typed? $more $rest) False))
      ($other False))))

(= (d0:fact-typed? $declarations (D0Fact $relation $values))
   (case (d0:signature $declarations $relation)
     (((Some $types) (d0:values-typed? $values $types)) (None False))))

; A variable context: a list of (name type), first occurrence first.
(= (d0:lookup $context $name)
   (case $context
     ((() None)
      ((cons ($key $type) $rest) (if (== $key $name) (Some $type) (d0:lookup $rest $name))))))

(= (d0:argument-typed? $context $argument $type)
   (case $argument
     (((D0Literal $value) (== (d0:value-type $value) $type))
      ((D0Var $name) (== (d0:lookup $context $name) (Some $type)))
      (D0Anonymous True))))

(= (d0:arguments-typed? $context $arguments $types)
   (case ($arguments $types)
     (((() ()) True)
      (((cons $argument $more) (cons $type $rest))
        (if (d0:argument-typed? $context $argument $type) (d0:arguments-typed? $context $more $rest) False))
      ($other False))))

(= (d0:atom-typed? $declarations $context (D0Atom $relation $arguments))
   (case (d0:signature $declarations $relation)
     (((Some $types) (d0:arguments-typed? $context $arguments $types)) (None False))))

(= (d0:operand-type $context $argument)
   (case $argument
     (((D0Literal $value) (Some (d0:value-type $value)))
      ((D0Var $name) (d0:lookup $context $name))
      (D0Anonymous None))))

(= (d0:comparison-typed? $context $left $right)
   (case ((d0:operand-type $context $left) (d0:operand-type $context $right))
     ((((Some $type) (Some $same)) (== $type $same))
      ($other False))))

(= (d0:premise-typed? $declarations $context $premise)
   (case $premise
     (((D0Positive $atom) (d0:atom-typed? $declarations $context $atom))
      ((D0Compare $operator $left $right) (d0:comparison-typed? $context $left $right)))))

; ---- Clauses ------------------------------------------------------------------

(= (d0:positive-atoms $premises)
   (case $premises
     ((() ())
      ((cons $premise $rest)
        (case $premise
          (((D0Positive $atom) (cons-atom $atom (d0:positive-atoms $rest)))
           ($other (d0:positive-atoms $rest))))))))

(= (d0:argument-variables $arguments)
   (case $arguments
     ((() ())
      ((cons $argument $rest)
        (case $argument
          (((D0Var $name) (cons-atom $name (d0:argument-variables $rest)))
           ($other (d0:argument-variables $rest))))))))

; Head arguments and comparison operands must be bound by positive atoms.
(= (d0:required-arguments (D0Rule (D0Atom $relation $arguments) $premises))
   (d0:append $arguments (d0:comparison-arguments $premises)))
(= (d0:comparison-arguments $premises)
   (case $premises
     ((() ())
      ((cons $premise $rest)
        (case $premise
          (((D0Compare $operator $left $right) (cons-atom $left (cons-atom $right (d0:comparison-arguments $rest))))
           ($other (d0:comparison-arguments $rest))))))))
(= (d0:atom-arguments (D0Atom $relation $arguments)) $arguments)
(= (d0:provided-variables $premises)
   (d0:concat-variables (d0:positive-atoms $premises)))
(= (d0:concat-variables $atoms)
   (case $atoms
     ((() ())
      ((cons $atom $rest) (d0:append (d0:argument-variables (d0:atom-arguments $atom)) (d0:concat-variables $rest))))))

(= (d0:not-anonymous? $argument) (not (== $argument D0Anonymous)))
(= (d0:range-restricted? $rule)
   (let* (($required (d0:required-arguments $rule))
          ($provided (case $rule (((D0Rule $head $premises) (d0:provided-variables $premises))))))
     (if (d0:all d0:not-anonymous? $required)
       (d0:all-provided? (d0:argument-variables $required) $provided)
       False)))
(= (d0:all-provided? $names $provided)
   (case $names
     ((() True)
      ((cons $name $rest) (if (d0:contains? $name $provided) (d0:all-provided? $rest $provided) False)))))

; Each variable takes the declared type of its first positive occurrence.
(= (d0:requirements $arguments $types)
   (case ($arguments $types)
     ((((cons (D0Var $name) $more) (cons $type $rest)) (cons-atom ($name $type) (d0:requirements $more $rest)))
      (((cons $argument $more) (cons $type $rest)) (d0:requirements $more $rest))
      ($other ()))))
(= (d0:atom-requirements $declarations (D0Atom $relation $arguments))
   (case (d0:signature $declarations $relation)
     (((Some $types) (d0:requirements $arguments $types)) (None ()))))
(= (d0:inferred-context $declarations $premises)
   (d0:concat-requirements $declarations (d0:positive-atoms $premises)))
(= (d0:concat-requirements $declarations $atoms)
   (case $atoms
     ((() ())
      ((cons $atom $rest)
        (d0:append (d0:atom-requirements $declarations $atom) (d0:concat-requirements $declarations $rest))))))

(= (d0:clause-typed? $declarations (D0Rule $head $premises))
   (let $context (d0:inferred-context $declarations $premises)
     (if (not (d0:range-restricted? (D0Rule $head $premises))) False
       (if (not (d0:atom-typed? $declarations $context $head)) False
         (d0:premises-typed? $declarations $context $premises)))))
(= (d0:premises-typed? $declarations $context $premises)
   (case $premises
     ((() True)
      ((cons $premise $rest)
        (if (d0:premise-typed? $declarations $context $premise) (d0:premises-typed? $declarations $context $rest) False)))))

; ---- The program --------------------------------------------------------------

(= (d0:declared? $declarations $relation)
   (case (d0:signature $declarations $relation) (((Some $types) True) (None False))))

(= (d0:statement-issue $declarations $index $statement)
   (case $statement
     (((D0Declaration $name $attributes)
        (if (d0:declaration-valid? $statement) None (Some (InvalidDeclaration $index))))
      ((D0Fact $relation $values)
        (if (d0:fact-typed? $declarations $statement) None (Some (IllTypedFact $index))))
      ((D0Rule $head $premises)
        (if (d0:clause-typed? $declarations $statement) None (Some (IllTypedClause $index))))
      ((D0Input $relation)
        (if (d0:declared? $declarations $relation) None (Some (UndeclaredInterface $index))))
      ((D0Output $relation)
        (if (d0:declared? $declarations $relation) None (Some (UndeclaredInterface $index)))))))

(= (d0:statement-issues $declarations $index $statements)
   (case $statements
     ((() ())
      ((cons $statement $rest)
        (case (d0:statement-issue $declarations $index $statement)
          (((Some $issue) (cons-atom $issue (d0:statement-issues $declarations (+ $index 1) $rest)))
           (None (d0:statement-issues $declarations (+ $index 1) $rest))))))))

; All issues of a program, in order; () when it is typed.
(= (d0:diagnose (D0Program $statements))
   (let $declarations (d0:declarations $statements)
     (let $issues (d0:statement-issues $declarations 0 $statements)
       (if (d0:distinct? (d0:map d0:declaration-name $declarations)) $issues
         (cons-atom DuplicateRelations $issues)))))

; Floating attributes need a floating comparison profile, which D0 does not declare.
(= (d0:floating-attribute? (D0Attribute $name $type)) (== $type D0Float))
(= (d0:floating-declaration? (D0Declaration $name $attributes))
   (not (d0:all d0:not-floating? $attributes)))
(= (d0:not-floating? $attribute) (not (d0:floating-attribute? $attribute)))
(= (d0:floating? (D0Program $statements))
   (not (d0:all d0:not-floating-declaration? (d0:declarations $statements))))
(= (d0:not-floating-declaration? $declaration) (not (d0:floating-declaration? $declaration)))

; Distinct declared relations keep distinct host spellings.
(= (d0:host-name $separator $name)
   (case $name
     (((cons $first $rest)
        (if (== $rest ()) $first (str:concat $first (str:concat $separator (d0:host-name $separator $rest)))))
      ($other ""))))
(= (d0:host-names-valid? $separator (D0Program $statements))
   (let $names (d0:map d0:declaration-name (d0:declarations $statements))
     (d0:distinct? (d0:host-names $separator (d0:distinct-names $names)))))
(= (d0:host-names $separator $names)
   (case $names ((() ()) ((cons $first $rest) (cons-atom (d0:host-name $separator $first) (d0:host-names $separator $rest))))))
(= (d0:distinct-names $names)
   (case $names
     ((() ())
      ((cons $first $rest) (if (d0:contains? $first $rest) (d0:distinct-names $rest) (cons-atom $first (d0:distinct-names $rest)))))))
