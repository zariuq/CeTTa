; Example: a small first-order Knuth--Bendix ordering for replay experiments.
;
; Object variables are explicit (ATP.Var name).  All symbol weights are
; strictly positive, which avoids the special admissibility conditions for
; zero-weight unary symbols.  A configuration is inspectable data:
;
;   (ATP.KBO variable-weight default-symbol-weight
;            ((ATP.SymbolWeight symbol weight) ...)
;            (highest-precedence ... lowest-precedence))
;
; Missing precedence comparisons are incomparable, never guessed.

(= (atp:kbo:positive-number $value)
   (if (== (get-type $value) Number)
       (> $value 0)
       False))

(= (atp:kbo:term-list-valid ()) True)

(= (atp:kbo:term-list-valid $terms)
   (let ($head $tail) (decons-atom $terms)
     (if (atp:kbo:term-valid $head)
         (atp:kbo:term-list-valid $tail)
         False)))

(= (atp:kbo:term-valid $term)
   (case $term
     (((ATP.Var $name)
        (if (== (get-metatype $name) Variable)
            False
            (if (== (get-metatype $name) Expression) False True)))
      (() False)
      ($_
        (if (== (get-metatype $term) Variable)
            False
            (if (== (get-metatype $term) Expression)
                (let ($head $arguments) (decons-atom $term)
                  (if (== (get-metatype $head) Variable)
                      False
                      (if (== (get-metatype $head) Expression)
                          False
                          (atp:kbo:term-list-valid $arguments))))
                True))))))

(= (atp:kbo:weights-valid () $seen) True)

(= (atp:kbo:weights-valid $weights $seen)
   (let ($entry $rest) (decons-atom $weights)
     (case $entry
       (((ATP.SymbolWeight $symbol $weight)
          (if (== (get-metatype $symbol) Variable)
              False
              (if (== (get-metatype $symbol) Expression)
                  False
                  (if (atp:kbo:positive-number $weight)
                      (if (atp:resolution:member $symbol $seen)
                          False
                          (atp:kbo:weights-valid
                            $rest (cons-atom $symbol $seen)))
                      False))))
        ($_ False)))))

(= (atp:kbo:precedence-valid () $seen) True)

(= (atp:kbo:precedence-valid $precedence $seen)
   (let ($symbol $rest) (decons-atom $precedence)
     (if (== (get-metatype $symbol) Variable)
         False
         (if (== (get-metatype $symbol) Expression)
             False
             (if (atp:resolution:member $symbol $seen)
                 False
                 (atp:kbo:precedence-valid
                   $rest (cons-atom $symbol $seen)))))))

(= (atp:kbo:config-valid $configuration)
   (case $configuration
     (((ATP.KBO $variable-weight $default-weight $weights $precedence)
        (if (atp:kbo:positive-number $variable-weight)
            (if (atp:kbo:positive-number $default-weight)
                (if (atp:kbo:weights-valid $weights ())
                    (atp:kbo:precedence-valid $precedence ())
                    False)
                False)
            False))
      ($_ False))))

(= (atp:kbo:symbol-weight () $symbol $default) $default)

(= (atp:kbo:symbol-weight $weights $symbol $default)
   (let ($entry $rest) (decons-atom $weights)
     (case $entry
       (((ATP.SymbolWeight $candidate $weight)
          (if (== $candidate $symbol)
              $weight
              (atp:kbo:symbol-weight $rest $symbol $default)))
        ($_ $default)))))

(= (atp:kbo:arguments-weight $configuration ()) 0)

(= (atp:kbo:arguments-weight $configuration $arguments)
   (let ($head $tail) (decons-atom $arguments)
     (+ (atp:kbo:term-weight $configuration $head)
        (atp:kbo:arguments-weight $configuration $tail))))

(= (atp:kbo:term-weight
     (ATP.KBO $variable-weight $default-weight $weights $precedence)
     $term)
   (case $term
     (((ATP.Var $name) $variable-weight)
      ($_
        (if (== (get-metatype $term) Expression)
            (let ($head $arguments) (decons-atom $term)
              (+ (atp:kbo:symbol-weight $weights $head $default-weight)
                 (atp:kbo:arguments-weight
                   (ATP.KBO $variable-weight $default-weight
                            $weights $precedence)
                   $arguments)))
            (atp:kbo:symbol-weight $weights $term $default-weight))))))

(= (atp:kbo:variable-count-list $name ()) 0)

(= (atp:kbo:variable-count-list $name $terms)
   (let ($head $tail) (decons-atom $terms)
     (+ (atp:kbo:variable-count $name $head)
        (atp:kbo:variable-count-list $name $tail))))

(= (atp:kbo:variable-count $name $term)
   (case $term
     (((ATP.Var $candidate) (if (== $name $candidate) 1 0))
      ($_
        (if (== (get-metatype $term) Expression)
            (atp:kbo:variable-count-list $name $term)
            0)))))

(= (atp:kbo:variables-dominated-list $left ()) True)

(= (atp:kbo:variables-dominated-list $left $terms)
   (let ($head $tail) (decons-atom $terms)
     (if (atp:kbo:variables-dominated $left $head)
         (atp:kbo:variables-dominated-list $left $tail)
         False)))

; Every variable occurs at least as often in the proposed greater term.
(= (atp:kbo:variables-dominated $left $right)
   (case $right
     (((ATP.Var $name)
        (>= (atp:kbo:variable-count $name $left)
            (atp:kbo:variable-count $name $right)))
      ($_
        (if (== (get-metatype $right) Expression)
            (atp:kbo:variables-dominated-list $left $right)
            True)))))

(= (atp:kbo:precedence-greater $left $right ()) False)

(= (atp:kbo:precedence-greater $left $right $precedence)
   (let ($head $tail) (decons-atom $precedence)
     (if (== $head $left)
         (atp:resolution:member $right $tail)
         (if (== $head $right)
             False
             (atp:kbo:precedence-greater $left $right $tail)))))

(= (atp:kbo:lex-greater $configuration () ()) False)

(= (atp:kbo:lex-greater $configuration () $right) False)

(= (atp:kbo:lex-greater $configuration $left ()) False)

(= (atp:kbo:lex-greater $configuration $left $right)
   (let ($left-head $left-tail) (decons-atom $left)
     (let ($right-head $right-tail) (decons-atom $right)
       (if (== $left-head $right-head)
           (atp:kbo:lex-greater
             $configuration $left-tail $right-tail)
           (atp:kbo:greater-valid
             $configuration $left-head $right-head)))))

(= (atp:kbo:root-and-arguments $term)
   (case $term
     (((ATP.Var $name) ATP.KBOVariable)
      ($_
        (if (== (get-metatype $term) Expression)
            (let ($head $arguments) (decons-atom $term)
              (ATP.KBORoot $head $arguments))
            (ATP.KBORoot $term ()))))))

(= (atp:kbo:equal-weight-greater
     (ATP.KBO $variable-weight $default-weight $weights $precedence)
     $left $right)
   (case (ATP.KBORootPair
           (atp:kbo:root-and-arguments $left)
           (atp:kbo:root-and-arguments $right))
     (((ATP.KBORootPair
         (ATP.KBORoot $left-root $left-arguments)
         (ATP.KBORoot $right-root $right-arguments))
       (if (== $left-root $right-root)
           (atp:kbo:lex-greater
             (ATP.KBO $variable-weight $default-weight
                      $weights $precedence)
             $left-arguments $right-arguments)
           (atp:kbo:precedence-greater
             $left-root $right-root $precedence)))
      ($_ False))))

(= (atp:kbo:greater-valid
     (ATP.KBO $variable-weight $default-weight $weights $precedence)
     $left $right)
   (if (== $left $right)
       False
       (if (atp:kbo:variables-dominated $left $right)
           (let $left-weight
                (atp:kbo:term-weight
                  (ATP.KBO $variable-weight $default-weight
                           $weights $precedence)
                  $left)
             (let $right-weight
                  (atp:kbo:term-weight
                    (ATP.KBO $variable-weight $default-weight
                             $weights $precedence)
                    $right)
               (if (> $left-weight $right-weight)
                   True
                   (if (< $left-weight $right-weight)
                       False
                       (atp:kbo:equal-weight-greater
                         (ATP.KBO $variable-weight $default-weight
                                  $weights $precedence)
                         $left $right)))))
           False)))

; Public comparison predicate.  Invalid terms/configurations fail closed.
(= (atp:kbo:greater $configuration $left $right)
   (if (atp:kbo:config-valid $configuration)
       (if (atp:kbo:term-valid $left)
           (if (atp:kbo:term-valid $right)
               (atp:kbo:greater-valid $configuration $left $right)
               False)
           False)
       False))
