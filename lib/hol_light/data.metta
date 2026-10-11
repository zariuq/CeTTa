; Portable data operations for the HOL Light guest.
; HOL:Nil and HOL:Cons are data, independent of an engine's list syntax.
; All object-language names are strings; program variables begin with $.

(= (hol-light:member $item $items)
  (case $items
    ((HOL:Nil False)
     ((HOL:Cons $first $rest)
       (if (== $item $first) True (hol-light:member $item $rest)))
     ($malformed False))))

(= (hol-light:insert $item $items)
  (if (hol-light:member $item $items) $items (HOL:Cons $item $items)))

(= (hol-light:union $left $right)
  (case $left
    ((HOL:Nil $right)
     ((HOL:Cons $first $rest)
       (hol-light:union $rest (hol-light:insert $first $right)))
     ($malformed None))))

(= (hol-light:remove $item $items)
  (case $items
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
       (let $tail (hol-light:remove $item $rest)
         (if (== $item $first) $tail (HOL:Cons $first $tail))))
     ($malformed None))))

(= (hol-light:subset $left $right)
  (case $left
    ((HOL:Nil True)
     ((HOL:Cons $first $rest)
       (if (hol-light:member $first $right)
         (hol-light:subset $rest $right) False))
     ($malformed False))))

(= (hol-light:append $left $right)
  (case $left
    ((HOL:Nil $right)
     ((HOL:Cons $first $rest)
       (HOL:Cons $first (hol-light:append $rest $right)))
     ($malformed None))))

(= (hol-light:length $items)
  (case $items
    ((HOL:Nil 0)
     ((HOL:Cons $first $rest) (+ 1 (hol-light:length $rest)))
     ($malformed -1))))

(= (hol-light:lookup $key $entries)
  (case $entries
    ((HOL:Nil None)
     ((HOL:Cons (HOL:Entry $name $value) $rest)
       (if (== $name $key) (Some $value) (hol-light:lookup $key $rest)))
     ($malformed None))))

(= (hol-light:at $index $items)
  (case $items
    ((HOL:Nil None)
     ((HOL:Cons $first $rest)
       (if (== $index 0) (Some $first)
         (if (> $index 0) (hol-light:at (- $index 1) $rest) None)))
     ($malformed None))))

(= (hol-light:all-list $predicate $items)
  (case $items
    ((HOL:Nil True)
     ((HOL:Cons $first $rest)
       (if ($predicate $first) (hol-light:all-list $predicate $rest) False))
     ($malformed False))))

(= (hol-light:list-valid $items)
  (case $items
    ((HOL:Nil True)
     ((HOL:Cons $first $rest) (hol-light:list-valid $rest))
     ($malformed False))))

(= (hol-light:map $function $items)
  (case $items
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
       (HOL:Cons ($function $first) (hol-light:map $function $rest)))
     ($malformed None))))

(= (hol-light:type-vars $type)
  (case $type
    (((Tyvar $name) (HOL:Cons $name HOL:Nil))
     ((Tyapp $operator $arguments) (hol-light:type-vars-list $arguments))
     ($malformed None))))

(= (hol-light:type-vars-list $types)
  (case $types
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
       (hol-light:union (hol-light:type-vars $first)
                       (hol-light:type-vars-list $rest)))
     ($malformed None))))

; HOL Light substitution pairs are (replacement, key), first match wins.
; Non-variable keys in a type substitution have no effect.
(= (hol-light:type-replacement $name $entries)
  (case $entries
    ((HOL:Nil (Tyvar $name))
     ((HOL:Cons (HOL:Maplet $replacement $key) $rest)
       (case $key
         (((Tyvar $candidate)
           (if (== $candidate $name) $replacement
             (hol-light:type-replacement $name $rest)))
          ($ignored (hol-light:type-replacement $name $rest)))))
     ($malformed None))))

(= (hol-light:type-subst $entries $type)
  (case $type
    (((Tyvar $name) (hol-light:type-replacement $name $entries))
     ((Tyapp $operator $arguments)
       (Tyapp $operator (hol-light:type-subst-list $entries $arguments)))
     ($malformed None))))

(= (hol-light:type-subst-list $entries $types)
  (case $types
    ((HOL:Nil HOL:Nil)
     ((HOL:Cons $first $rest)
       (HOL:Cons (hol-light:type-subst $entries $first)
                 (hol-light:type-subst-list $entries $rest)))
     ($malformed None))))

(= (hol-light:native-list $items)
  (case $items
    ((HOL:Nil ())
     ((HOL:Cons $first $rest)
       (cons-atom $first (hol-light:native-list $rest)))
     ($malformed ()))))

(= (hol-light:tagged-list $items)
  (case $items
    ((() HOL:Nil)
     ((cons $first $rest) (HOL:Cons $first (hol-light:tagged-list $rest)))
     ($malformed None))))

(= (hol-light:sorted-names $items)
  (hol-light:tagged-list (sort-atom (hol-light:native-list $items))))
