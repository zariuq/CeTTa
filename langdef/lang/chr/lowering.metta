; Ground CHR (C0): Prolog source terms (reader.metta) become C0's independent
; IR, written as the machine data that machine.metta runs:
;   value  integer | "atom" | (ChrTuple values)
;   term   (ChrValue value) | (ChrVar index) | (ChrTupleTerm terms)
;   head   (ChrHead "name" arity terms)
;   rule   (ChrRule kept removed guards body kind)
; Lists of machine data are ChrNil / (ChrCons first rest). Every function
; returns a value or (C0Error message); errors propagate unchanged.
; In the PeTTa lane `and`/`or` evaluate both arguments and a call matching no
; equation fails, so guards nest as `if` and every function is total.

; ---- Lists ------------------------------------------------------------------

(= (c0:chr-list $items)
   (case $items ((() ChrNil) ((cons $first $rest) (ChrCons $first (c0:chr-list $rest))))))

(= (c0:error? $value) (case $value (((C0Error $message) True) ($other False))))

; Map a function over a list, stopping at the first error.
(= (c0:map $function $items)
   (case $items
     ((() ())
      ((cons $first $rest)
        (let $head ($function $first)
          (if (c0:error? $head) $head
            (let $tail (c0:map $function $rest)
              (if (c0:error? $tail) $tail (cons-atom $head $tail)))))))))

(= (c0:contains? $item $items)
   (case $items ((() False) ((cons $first $rest) (if (== $first $item) True (c0:contains? $item $rest))))))

(= (c0:distinct $items $seen)
   (case $items
     ((() ())
      ((cons $first $rest)
        (if (c0:contains? $first $seen) (c0:distinct $rest $seen)
          (cons-atom $first (c0:distinct $rest (cons-atom $first $seen))))))))

(= (c0:append $xs $ys)
   (case $xs ((() $ys) ((cons $first $rest) (cons-atom $first (c0:append $rest $ys))))))

(= (c0:length $items) (case $items ((() 0) ((cons $first $rest) (+ 1 (c0:length $rest))))))

(= (c0:range $from $count)
   (if (== $count 0) () (cons-atom $from (c0:range (+ $from 1) (- $count 1)))))

; ---- Variables and their allocation ---------------------------------------

; Number anonymous variables so each occurrence is distinct.
(= (c0:number-fresh $term $next)
   (case $term
     (((PlFresh) (C0Numbered (PlVar (str:concat "_#" (str:number->string $next))) (+ $next 1)))
      ((PlCompound $name $arguments)
        (case (c0:number-fresh-list $arguments $next)
          (((C0Numbered $numbered $after) (C0Numbered (PlCompound $name $numbered) $after)))))
      ($other (C0Numbered $term $next)))))
(= (c0:number-fresh-list $terms $next)
   (case $terms
     ((() (C0Numbered () $next))
      ((cons $first $rest)
        (case (c0:number-fresh $first $next)
          (((C0Numbered $head $middle)
             (case (c0:number-fresh-list $rest $middle)
               (((C0Numbered $tail $after) (C0Numbered (cons-atom $head $tail) $after)))))))))))

; Variable spellings in order of occurrence, prepended to $tail.
(= (c0:variables $term $tail)
   (case $term
     (((PlVar $spelling) (cons-atom $spelling $tail))
      ((PlCompound $name $arguments) (c0:variables-of $arguments $tail))
      ($other $tail))))
(= (c0:variables-of $terms $tail)
   (case $terms ((() $tail) ((cons $first $rest) (c0:variables $first (c0:variables-of $rest $tail))))))

; A canonical printer name V<n> keeps index n when the whole scope's names are
; such names with distinct indices; otherwise variables are numbered in order.
(= (c0:preferred $spelling)
   (if (str:starts-with $spelling "V")
     (let $digits (str:byte-slice $spelling 1 (str:byte-length $spelling))
       (if (== $digits "") None
         (if (str:is-all-in $digits "0123456789")
           (let $index (str:string->number $digits)
             (if (== (str:concat "V" (str:number->string $index)) $spelling) (Some $index) None))
           None)))
     None))

(= (c0:preferred-all $names)
   (case $names
     ((() ())
      ((cons $first $rest)
        (case (c0:preferred $first)
          (((Some $index)
             (let $tail (c0:preferred-all $rest) (if (== $tail None) None (cons-atom $index $tail))))
           ($other None)))))))

(= (c0:zip $names $indices)
   (case ($names $indices)
     (((() ()) ())
      (((cons $name $rest) (cons $index $more)) (cons-atom (C0Slot $name $index) (c0:zip $rest $more))))))

(= (c0:allocate $term)
   (let $names (c0:distinct (c0:variables $term ()) ())
     (let $preferred (c0:preferred-all $names)
       (if (== $preferred None)
         (c0:zip $names (c0:range 0 (c0:length $names)))
         (if (== (c0:distinct $preferred ()) $preferred)
           (c0:zip $names $preferred)
           (c0:zip $names (c0:range 0 (c0:length $names))))))))

(= (c0:index-of $allocation $spelling)
   (case $allocation
     ((() None)
      ((cons (C0Slot $name $index) $rest) (if (== $name $spelling) (Some $index) (c0:index-of $rest $spelling))))))

; ---- Terms, heads, expressions, guards, goals -----------------------------

(= (c0:lower-term $allocation $term)
   (case $term
     (((PlVar $spelling)
        (case (c0:index-of $allocation $spelling)
          (((Some $index) (ChrVar $index))
           ($other (C0Error "source variable is outside its rule scope")))))
      ((PlInt $n) (ChrValue $n))
      ((PlAtom $name) (ChrValue $name))
      ((PlCompound $name $arguments)
        (if (== $name "tuple")
          (let $fields (c0:map (c0:lower-term $allocation) $arguments)
            (if (c0:error? $fields) $fields (ChrTupleTerm (c0:chr-list $fields))))
          (C0Error "C0 compound values use tuple(...)")))
      ($other (C0Error "C0 values are integers, atoms and tuples")))))

; A name and its arguments: atoms are zero-argument applications.
(= (c0:application? $term)
   (case $term
     (((PlAtom $name) (C0App $name ()))
      ((PlCompound $name $arguments) (C0App $name $arguments))
      ($other None))))

(= (c0:head $allocation $term)
   (case (c0:application? $term)
     (((C0App $name $arguments)
        (let $terms (c0:map (c0:lower-term $allocation) $arguments)
          (if (c0:error? $terms) $terms (ChrHead $name (c0:length $terms) (c0:chr-list $terms)))))
      ($other (C0Error "expected a CHR constraint")))))

(= (c0:int-operator "+") ChrAdd) (= (c0:int-operator "-") ChrSubtract) (= (c0:int-operator "*") ChrMultiply)
(= (c0:int-operator "//") ChrQuotient) (= (c0:int-operator "rem") ChrRemainder) (= (c0:int-operator "mod") ChrModulo)
(= (c0:int-operator? $name) (c0:contains? $name ("+" "-" "*" "//" "rem" "mod")))

(= (c0:expression $allocation $term)
   (case $term
     (((PlInt $n) (ChrLiteral $n))
      ((PlVar $spelling)
        (case (c0:index-of $allocation $spelling)
          (((Some $index) (ChrIndex $index))
           ($other (C0Error "arithmetic variable is outside its rule scope")))))
      ((PlCompound $name ($operand))
        (if (== $name "-")
          (let $inner (c0:expression $allocation $operand)
            (if (c0:error? $inner) $inner (ChrOperation ChrSubtract (ChrLiteral 0) $inner)))
          (if (== $name "+") (c0:expression $allocation $operand)
            (C0Error "unsupported C0 unary integer operation"))))
      ((PlCompound $name ($left $right))
        (if (c0:int-operator? $name)
          (let $l (c0:expression $allocation $left)
            (if (c0:error? $l) $l
              (let $r (c0:expression $allocation $right)
                (if (c0:error? $r) $r (ChrOperation (c0:int-operator $name) $l $r)))))
          (C0Error "unsupported C0 integer operation")))
      ((PlCompound $name $arguments) (C0Error "invalid C0 arithmetic arity"))
      ($other (C0Error "C0 arithmetic requires an integer expression")))))

(= (c0:comparison "=:=") ChrEqual) (= (c0:comparison "=\\=") ChrDifferent) (= (c0:comparison "<") ChrLess)
(= (c0:comparison "=<") ChrLessEqual) (= (c0:comparison ">") ChrGreater) (= (c0:comparison ">=") ChrGreaterEqual)
(= (c0:comparison? $name) (c0:contains? $name ("=:=" "=\\=" "<" "=<" ">" ">=")))

(= (c0:guard $allocation $term)
   (case $term
     (((PlCompound $name ($left $right))
        (if (or (== $name "==") (== $name "\\=="))
          (let $l (c0:lower-term $allocation $left)
            (if (c0:error? $l) $l
              (let $r (c0:lower-term $allocation $right)
                (if (c0:error? $r) $r (if (== $name "==") (ChrIdentical $l $r) (ChrDistinct $l $r))))))
          (if (c0:comparison? $name)
            (let $l (c0:expression $allocation $left)
              (if (c0:error? $l) $l
                (let $r (c0:expression $allocation $right)
                  (if (c0:error? $r) $r (ChrCompare (c0:comparison $name) $l $r)))))
            (C0Error "unsupported C0 guard test"))))
      ($other (C0Error "expected a C0 guard test")))))

; The conjuncts of a term, left to right.
(= (c0:conjunction $term)
   (case $term
     (((PlCompound "," ($left $right)) (c0:append (c0:conjunction $left) (c0:conjunction $right)))
      ($other ($term)))))

(= (c0:without-true $terms)
   (case $terms
     ((() ())
      ((cons $first $rest)
        (if (== $first (PlAtom "true")) (c0:without-true $rest) (cons-atom $first (c0:without-true $rest)))))))

(= (c0:goal $allocation $term)
   (if (== $term (PlAtom "fail")) ChrFailGoal
     (case $term
       (((PlCompound "is" ((PlVar $spelling) $right))
          (case (c0:index-of $allocation $spelling)
            (((Some $index)
               (let $e (c0:expression $allocation $right)
                 (if (c0:error? $e) $e (ChrAssign $index $e))))
             ($other (C0Error "assignment variable is outside its rule scope")))))
        ((PlCompound "is" ($left $right)) (C0Error "C0 assignments require a variable on the left of is"))
        ($other
          (let $head (c0:head $allocation $term) (if (c0:error? $head) $head (ChrTell $head))))))))

(= (c0:guards $allocation $term) (c0:map (c0:guard $allocation) (c0:without-true (c0:conjunction $term))))
(= (c0:goals $allocation $term) (c0:map (c0:goal $allocation) (c0:without-true (c0:conjunction $term))))
(= (c0:heads $allocation $term) (c0:map (c0:head $allocation) (c0:conjunction $term)))

; ---- Rules and forms --------------------------------------------------------

; A rule with its name: "name @ body", or "rule:<ordinal>" when unnamed.
(= (c0:named-rule $ordinal $term)
   (case $term
     (((PlCompound "@" ((PlAtom $name) $body)) (C0Named $name $body))
      ((PlCompound "@" ($name $body)) (C0Error "CHR rule names must be atoms"))
      ($other (C0Named (str:concat "rule:" (str:number->string $ordinal)) $term)))))

; Each anonymous variable is its own variable; then the rule's variables are
; allocated in order of first occurrence.
(= (c0:rule $ordinal $written)
   (case (c0:number-fresh $written 0) (((C0Numbered $source $count) (c0:rule-numbered $ordinal $source)))))
(= (c0:rule-numbered $ordinal $source)
   (let $allocation (c0:allocate $source)
     (case (c0:named-rule $ordinal $source)
       (((C0Error $m) (C0Error $m))
        ((C0Named $name $body)
          (case $body
            (((PlCompound $kind ($left $right))
               (let $sides (c0:rule-heads $kind $allocation $left)
                 (if (c0:error? $sides) $sides
                   (case $sides
                     (((C0Sides $kept $removed)
                        (let $guarded (c0:rule-guarded $allocation $right)
                          (if (c0:error? $guarded) $guarded
                            (case $guarded
                              (((C0Guarded $guards $goals)
                                 (C0Rule $name $kept $removed $guards $goals))))))))))))
             ($other (C0Error "expected a CHR rule")))))))))

(= (c0:rule-heads $kind $allocation $left)
   (if (== $kind "==>")
     (let $kept (c0:heads $allocation $left) (if (c0:error? $kept) $kept (C0Sides $kept ())))
     (if (== $kind "<=>")
       (case $left
         (((PlCompound "\\" ($kept $removed))
            (let $k (c0:heads $allocation $kept)
              (if (c0:error? $k) $k
                (let $r (c0:heads $allocation $removed) (if (c0:error? $r) $r (C0Sides $k $r))))))
          ($other (let $r (c0:heads $allocation $left) (if (c0:error? $r) $r (C0Sides () $r))))))
       (C0Error "expected <=> or ==>"))))

(= (c0:rule-guarded $allocation $right)
   (case $right
     (((PlCompound "|" ($guards $body))
        (let $g (c0:guards $allocation $guards)
          (if (c0:error? $g) $g
            (let $b (c0:goals $allocation $body) (if (c0:error? $b) $b (C0Guarded $g $b))))))
      ($other (let $b (c0:goals $allocation $right) (if (c0:error? $b) $b (C0Guarded () $b)))))))

(= (c0:declaration $term)
   (case $term
     (((PlCompound "/" ((PlAtom $name) (PlInt $arity)))
        (if (< $arity 0) (C0Error "constraint arity must be nonnegative") (C0Declaration $name $arity)))
      ($other (C0Error "expected a constraint name/arity declaration")))))

(= (c0:ground-constraint $term)
   (let $head (c0:head () $term)
     (if (c0:error? $head) (C0Error "initial C0 queries must be ground") $head)))

; One sentence's form; $ordinal numbers the rules.
(= (c0:form $ordinal $term)
   (case $term
     (((PlCompound ":-" ($directive)) (c0:directive $directive))
      ((PlCompound "?-" ($query))
        (let $constraints (c0:map c0:ground-constraint (c0:without-true (c0:conjunction $query)))
          (if (c0:error? $constraints) $constraints (C0Query $constraints))))
      ($other (let $rule (c0:rule $ordinal $term) (if (c0:error? $rule) $rule (C0RuleForm $rule)))))))

(= (c0:directive $directive)
   (case $directive
     (((PlCompound "chr_constraint" ($declarations))
        (let $rows (c0:map c0:declaration (c0:conjunction $declarations))
          (if (c0:error? $rows) $rows (C0Declarations $rows))))
      ((PlCompound "use_module" ($library))
        (if (== $library (PlCompound "library" ((PlAtom "chr")))) C0Metadata
          (C0Error "C0 does not execute host module imports")))
      ((PlCompound "chr_option" ($mode $value))
        (if (and (== $mode (PlAtom "optimize")) (== $value (PlAtom "off"))) C0Metadata
          (C0Error "C0 uses its declared refined execution profile")))
      ((PlCompound "set_prolog_flag" ($flag $mode))
        (if (== $flag (PlAtom "double_quotes"))
          (if (== $mode (PlAtom "atom")) C0Metadata
            (C0Error "C0 reads double-quoted text as atoms; other double_quotes modes need value kinds C0 does not have"))
          (C0Error "unsupported C0 source flag")))
      ($other (C0Error "unsupported C0 source directive")))))

; ---- Programs ---------------------------------------------------------------

; Read the forms of all sentences; rules are numbered from 0.
(= (c0:forms $terms $ordinal)
   (case $terms
     ((() ())
      ((cons $first $rest)
        (let $form (c0:form $ordinal $first)
          (if (c0:error? $form) $form
            (let $next (case $form (((C0RuleForm $r) (+ $ordinal 1)) ($other $ordinal)))
              (let $tail (c0:forms $rest $next)
                (if (c0:error? $tail) $tail (cons-atom $form $tail))))))))))

(= (c0:collect-declarations $forms)
   (case $forms
     ((() ())
      ((cons (C0Declarations $rows) $rest) (c0:append $rows (c0:collect-declarations $rest)))
      ((cons $other $rest) (c0:collect-declarations $rest)))))
(= (c0:collect-rules $forms)
   (case $forms
     ((() ())
      ((cons (C0RuleForm $rule) $rest) (cons-atom $rule (c0:collect-rules $rest)))
      ((cons $other $rest) (c0:collect-rules $rest)))))
(= (c0:collect-inputs $forms)
   (case $forms
     ((() ())
      ((cons (C0Query $rows) $rest) (c0:append $rows (c0:collect-inputs $rest)))
      ((cons $other $rest) (c0:collect-inputs $rest)))))

(= (c0:declared? $declarations (ChrHead $name $arity $terms))
   (c0:contains? (C0Declaration $name $arity) $declarations))

(= (c0:all-declared? $declarations $heads)
   (case $heads
     ((() True)
      ((cons $first $rest) (if (c0:declared? $declarations $first) (c0:all-declared? $declarations $rest) False)))))

; Assemble the program and check admission, as the source compiler does.
(= (c0:assemble $forms)
   (let* (($declarations (c0:collect-declarations $forms))
          ($rules (c0:collect-rules $forms))
          ($inputs (c0:collect-inputs $forms)))
     (if (not (c0:all-declared? $declarations $inputs))
       (C0Error "initial C0 constraint has no matching name/arity declaration")
       (let $diagnostic (c0:diagnose $declarations $rules)
         (if (== $diagnostic None) (C0Program $declarations $rules $inputs)
           (C0Error (str:concat "C0 admission refused: " (repr $diagnostic))))))))

; ---- Admission --------------------------------------------------------------

(= (c0:term-vars $term $tail)
   (case $term
     (((ChrVar $index) (cons-atom $index $tail))
      ((ChrTupleTerm $fields) (c0:chr-terms-vars $fields $tail))
      ($other $tail))))
(= (c0:chr-terms-vars $terms $tail)
   (case $terms (((ChrCons $first $rest) (c0:term-vars $first (c0:chr-terms-vars $rest $tail))) (ChrNil $tail))))
(= (c0:head-vars (ChrHead $name $arity $terms)) (c0:chr-terms-vars $terms ()))
(= (c0:heads-vars $heads)
   (case $heads ((() ()) ((cons $first $rest) (c0:append (c0:head-vars $first) (c0:heads-vars $rest))))))
(= (c0:expr-vars $expression)
   (case $expression
     (((ChrIndex $index) ($index))
      ((ChrOperation $operator $left $right) (c0:append (c0:expr-vars $left) (c0:expr-vars $right)))
      ($other ()))))
(= (c0:guard-vars $guard)
   (case $guard
     (((ChrIdentical $l $r) (c0:term-vars $l (c0:term-vars $r ())))
      ((ChrDistinct $l $r) (c0:term-vars $l (c0:term-vars $r ())))
      ((ChrCompare $c $l $r) (c0:append (c0:expr-vars $l) (c0:expr-vars $r))))))

(= (c0:available? $bound $read)
   (case $read ((() True) ((cons $first $rest) (if (c0:contains? $first $bound) (c0:available? $bound $rest) False)))))

(= (c0:first-duplicate $declarations $seen)
   (case $declarations
     ((() None)
      ((cons $first $rest)
        (if (c0:contains? $first $seen) (DuplicateDeclaration $first)
          (c0:first-duplicate $rest (cons-atom $first $seen)))))))

(= (c0:goals-diagnostic $declarations $index $bound $goals)
   (case $goals
     ((() None)
      ((cons $goal $rest)
        (case $goal
          (((ChrTell $head)
             (if (not (c0:declared? $declarations $head)) (UndeclaredGoal $index $head)
               (if (not (c0:available? $bound (c0:head-vars $head))) (UnboundGoal $index $goal)
                 (c0:goals-diagnostic $declarations $index $bound $rest))))
           ((ChrAssign $target $expression)
             (if (not (c0:available? $bound (c0:expr-vars $expression))) (UnboundGoal $index $goal)
               (c0:goals-diagnostic $declarations $index (cons-atom $target $bound) $rest)))
           (ChrFailGoal (c0:goals-diagnostic $declarations $index $bound $rest))))))))

(= (c0:first-undeclared $declarations $heads)
   (case $heads
     ((() None)
      ((cons $first $rest) (if (c0:declared? $declarations $first) (c0:first-undeclared $declarations $rest) $first)))))
(= (c0:first-unbound-guard $bound $guards)
   (case $guards
     ((() None)
      ((cons $first $rest) (if (c0:available? $bound (c0:guard-vars $first)) (c0:first-unbound-guard $bound $rest) $first)))))

(= (c0:rule-diagnostic $declarations $index (C0Rule $name $kept $removed $guards $goals))
   (let $heads (c0:append $kept $removed)
     (if (== $heads ()) (EmptyHead $index)
       (let $undeclared (c0:first-undeclared $declarations $heads)
         (if (not (== $undeclared None)) (UndeclaredHead $index $undeclared)
           (let $bound (c0:heads-vars $heads)
             (let $unbound (c0:first-unbound-guard $bound $guards)
               (if (not (== $unbound None)) (UnboundGuard $index $unbound)
                 (c0:goals-diagnostic $declarations $index $bound $goals)))))))))

(= (c0:rules-diagnostic $declarations $rules $index)
   (case $rules
     ((() None)
      ((cons $first $rest)
        (let $found (c0:rule-diagnostic $declarations $index $first)
          (if (== $found None) (c0:rules-diagnostic $declarations $rest (+ $index 1)) $found))))))

(= (c0:diagnose $declarations $rules)
   (let $duplicate (c0:first-duplicate $declarations ())
     (if (== $duplicate None) (c0:rules-diagnostic $declarations $rules 0) $duplicate)))

; ---- Machine data -----------------------------------------------------------

(= (c0:machine-rule (C0Rule $name $kept $removed $guards $goals))
   (ChrRule (c0:chr-list $kept) (c0:chr-list $removed) (c0:chr-list $guards) (c0:chr-list $goals)
     (if (== $removed ()) ChrPropagating ChrSimplifying)))

(= (c0:head-key (ChrHead $name $arity $terms)) (ChrKey $name $arity))

; Head indices of a rule in the refined visit order: removed heads, then kept.
(= (c0:visit-order (C0Rule $name $kept $removed $guards $goals))
   (c0:append (c0:range (c0:length $kept) (c0:length $removed)) (c0:range 0 (c0:length $kept))))

(= (c0:nth $items $index)
   (case $items (((cons $first $rest) (if (== $index 0) $first (c0:nth $rest (- $index 1)))))))

; All occurrences (rule index, head index) in refined order.
(= (c0:occurrences $rules $index)
   (case $rules
     ((() ())
      ((cons $rule $rest)
        (c0:append (c0:map (c0:occurrence-of $index) (c0:visit-order $rule)) (c0:occurrences $rest (+ $index 1)))))))
(= (c0:occurrence-of $rule-index $head-index) (C0Occ $rule-index $head-index))

(= (c0:rule-heads-of (C0Rule $name $kept $removed $guards $goals)) (c0:append $kept $removed))

(= (c0:keys $rules)
   (c0:distinct (c0:map c0:head-key (c0:concat-heads $rules)) ()))
(= (c0:concat-heads $rules)
   (case $rules ((() ()) ((cons $rule $rest) (c0:append (c0:rule-heads-of $rule) (c0:concat-heads $rest))))))

(= (c0:occurrences-for $rules $key $occurrences)
   (case $occurrences
     ((() ())
      ((cons (C0Occ $r $h) $rest)
        (let $rule (c0:nth $rules $r)
          (if (== (c0:head-key (c0:nth (c0:rule-heads-of $rule) $h)) $key)
            (cons-atom (ChrAt $r $h (c0:machine-rule $rule)) (c0:occurrences-for $rules $key $rest))
            (c0:occurrences-for $rules $key $rest)))))))

(= (c0:table-entry $rules $occurrences $key)
   (ChrOccurrencesOf $key (c0:chr-list (c0:occurrences-for $rules $key $occurrences))))

(= (c0:table $rules)
   (let $occurrences (c0:occurrences $rules 0)
     (ChrProgram (c0:chr-list (c0:map (c0:table-entry $rules $occurrences) (c0:keys $rules))))))
