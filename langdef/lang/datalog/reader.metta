; Positive Datalog (D0) source, read from the native parser's selected packet.
;
; A packet node is (node LABEL FIELDS): FIELDS is a pair-list of nodes, or nil,
; or (cp N) for one scalar of a lexical class. Terminal spellings are not in
; the packet; layout nodes carry no meaning.
;
; The program is returned as D0 IR:
;   (D0Program statements)
;   statement  (D0Declaration name attributes) | (D0Fact name values)
;              | (D0Rule head premises) | (D0Input name) | (D0Output name)
;   name       a list of strings, the parts of a qualified name
;   attribute  (D0Attribute "name" type), type D0Symbol | D0Number | D0Unsigned | D0Float
;   value      (D0SymbolValue "text") | (D0NumberValue n) | (D0UnsignedValue n)
;   atom       (D0Atom name arguments)
;   argument   (D0Var "x") | D0Anonymous | (D0Literal value)
;   premise    (D0Positive atom) | (D0Compare operator left right)
;   operator   D0Equal | D0Different | D0Less | D0LessEqual | D0Greater | D0GreaterEqual
; Reader errors stay visible as (D0Error reason) subterms; a program that
; holds one is refused.

; ---- Fields and text --------------------------------------------------------

(= (d0:layout? $label)
   (case $label
     ((dl-src:trivia-empty True) (dl-src:trivia-space True) (dl-src:trivia-line-comment True)
      (dl-src:trivia-block-comment True) (dl-src:directive-space True) ($other False))))

; The fields of a node that carry meaning, in order.
(= (d0:fields nil) ())
(= (d0:fields (pair $first $rest))
   (case $first
     (((node $label $f)
        (if (d0:layout? $label) (d0:fields $rest) (cons-atom $first (d0:fields $rest))))
      ($other (cons-atom $first (d0:fields $rest))))))

; The scalars of a subtree in source order, prepended to $tail.
(= (d0:codes (node $label $fields) $tail)
   (case $fields
     (((cp $n) (cons-atom $n $tail))
      (nil $tail)
      ((pair $first $rest) (d0:codes $first (d0:codes-of $rest $tail))))))
(= (d0:codes-of nil $tail) $tail)
(= (d0:codes-of (pair $first $rest) $tail) (d0:codes $first (d0:codes-of $rest $tail)))

(= (d0:text $node) (str:from-codepoints (d0:codes $node ())))

(= (d0:reverse $items $acc)
   (case $items ((() $acc) ((cons $first $rest) (d0:reverse $rest (cons-atom $first $acc))))))

; ---- Errors -----------------------------------------------------------------

(= (d0:find-error $term)
   (case $term
     (((D0Error $reason) (Some $reason))
      ($other (if (== (get-metatype $term) Expression) (d0:find-error-in $term) None)))))
(= (d0:find-error-in $items)
   (case $items
     ((() None)
      ((cons $first $rest)
        (case (d0:find-error $first) (((Some $reason) (Some $reason)) ($none (d0:find-error-in $rest))))))))

; ---- Names ------------------------------------------------------------------

(= (d0:qualified-name (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:qualified-name-one ($identifier)) ((d0:text $identifier)))
      ((dl-src:qualified-name-more ($identifier $rest))
        (cons-atom (d0:text $identifier) (d0:qualified-name $rest)))
      ($other (D0Error (UnknownName $label))))))

; ---- Literals ---------------------------------------------------------------

(= (d0:digit-value $code)
   (if (and (>= $code 48) (<= $code 57)) (- $code 48)
     (if (and (>= $code 97) (<= $code 102)) (- $code 87)
       (- $code 55))))
(= (d0:fold-digits $base $value $codes)
   (case $codes
     ((() $value)
      ((cons $code $rest) (d0:fold-digits $base (+ (* $base $value) (d0:digit-value $code)) $rest)))))
(= (d0:magnitude $base $node) (d0:fold-digits $base 0 (d0:codes $node ())))

; The 64-bit domain of the pinned profile: numbers in [-2^63, 2^63), unsigned below 2^64.
(= (d0:two-63) (* 4294967296 2147483648))
(= (d0:number $value)
   (if (and (>= $value (- 0 (d0:two-63))) (< $value (d0:two-63)))
     (D0NumberValue $value) (D0Error NumberOutsideDomain)))
(= (d0:unsigned $value)
   (if (< $value (* 2 (d0:two-63))) (D0UnsignedValue $value) (D0Error UnsignedOutsideDomain)))

(= (d0:integer (node $label $fields))
   (let $digits (car-atom (d0:fields $fields))
     (case $label
       ((dl-src:integer-decimal (d0:number (d0:magnitude 10 $digits)))
        (dl-src:integer-binary (d0:number (d0:magnitude 2 $digits)))
        (dl-src:integer-hex (d0:number (d0:magnitude 16 $digits)))
        (dl-src:integer-negative (d0:number (- 0 (d0:magnitude 10 $digits))))
        (dl-src:integer-negative-binary (d0:number (- 0 (d0:magnitude 2 $digits))))
        (dl-src:integer-negative-hex (d0:number (- 0 (d0:magnitude 16 $digits))))
        ($other (D0Error (UnknownNumber $label)))))))

(= (d0:unsigned-literal (node $label $fields))
   (let $digits (car-atom (d0:fields $fields))
     (case $label
       ((dl-src:unsigned-decimal (d0:unsigned (d0:magnitude 10 $digits)))
        (dl-src:unsigned-binary (d0:unsigned (d0:magnitude 2 $digits)))
        (dl-src:unsigned-hex (d0:unsigned (d0:magnitude 16 $digits)))
        ($other (D0Error (UnknownNumber $label)))))))

(= (d0:escape $label)
   (case $label
     ((dl-src:escape-quote 34) (dl-src:escape-apostrophe 39) (dl-src:escape-slash 92)
      (dl-src:escape-bell 7) (dl-src:escape-backspace 8) (dl-src:escape-formfeed 12)
      (dl-src:escape-newline 10) (dl-src:escape-return 13) (dl-src:escape-tab 9)
      (dl-src:escape-vertical-tab 11)
      ($other (D0Error (UnknownEscape $other))))))

; A string literal body, gathered in reverse into a ground accumulator.
(= (d0:push-codes $codes $acc)
   (case $codes ((() $acc) ((cons $first $rest) (d0:push-codes $rest (cons-atom $first $acc))))))
(= (d0:symbol-codes (node $label $fields) $acc)
   (case ($label (d0:fields $fields))
     (((dl-src:symbol-empty ()) (d0:reverse $acc ()))
      ((dl-src:symbol-body ($scalar $rest)) (d0:symbol-codes $rest (d0:push-codes (d0:codes $scalar ()) $acc)))
      ((dl-src:symbol-escape ((node $escape $e) $rest)) (d0:symbol-codes $rest (cons-atom (d0:escape $escape) $acc)))
      ($other ((D0Error (UnknownSymbolBody $label)))))))
(= (d0:symbol (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:symbol ($body))
        (let $codes (d0:symbol-codes $body ())
          (case (d0:find-error-in $codes)
            (((Some $reason) (D0Error $reason))
             ($none (D0SymbolValue (str:from-codepoints $codes)))))))
      ($other (D0Error (UnknownSymbol $label))))))

(= (d0:constant (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:constant-symbol ($symbol)) (d0:symbol $symbol))
      ((dl-src:constant-integer ($number)) (d0:integer $number))
      ((dl-src:constant-unsigned ($number)) (d0:unsigned-literal $number))
      ((dl-src:constant-float ($number)) (D0Error FloatingLiteral))
      ($other (D0Error (UnknownConstant $label))))))

; ---- Terms, atoms and premises ---------------------------------------------

(= (d0:term (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:term-variable ($name)) (D0Var (d0:text $name)))
      ((dl-src:term-anonymous ()) D0Anonymous)
      ((dl-src:term-constant ($constant)) (D0Literal (d0:constant $constant)))
      ($other (D0Error (UnknownTerm $label))))))

; Separated lists: label-empty, label-one (item) and label-more (item rest).
(= (d0:list $empty $one $more $read (node $label $fields))
   (let $parts (d0:fields $fields)
     (if (== $label $empty) ()
       (if (== $label $one) (($read (car-atom $parts)))
         (if (== $label $more)
           (cons-atom ($read (car-atom $parts)) (d0:list $empty $one $more $read (car-atom (cdr-atom $parts))))
           ((D0Error (UnknownList $label))))))))

(= (d0:terms $node) (d0:list dl-src:terms-empty dl-src:terms-one dl-src:terms-more d0:term $node))
(= (d0:constants $node) (d0:list dl-src:constants-empty dl-src:constants-one dl-src:constants-more d0:constant $node))

(= (d0:atom (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:atom ($name $arguments)) (D0Atom (d0:qualified-name $name) (d0:terms $arguments)))
      ($other (D0Error (UnknownAtom $label))))))

(= (d0:operator $label)
   (case $label
     ((dl-src:comparison-equal D0Equal) (dl-src:comparison-different D0Different)
      (dl-src:comparison-less D0Less) (dl-src:comparison-less-equal D0LessEqual)
      (dl-src:comparison-greater D0Greater) (dl-src:comparison-greater-equal D0GreaterEqual)
      ($other (D0Error (UnknownOperator $other))))))

(= (d0:premise (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:positive-atom ($atom)) (D0Positive (d0:atom $atom)))
      ((dl-src:positive-comparison ((node dl-src:comparison $parts)))
        (case (d0:fields $parts)
          ((($left (node $operator $o) $right)
             (D0Compare (d0:operator $operator) (d0:term $left) (d0:term $right)))
           ($other (D0Error MalformedComparison)))))
      ($other (D0Error (UnknownPremise $label))))))

(= (d0:body (node $label $fields))
   (let $parts (d0:fields $fields)
     (case $label
       ((dl-src:body-one ((d0:premise (car-atom $parts))))
        (dl-src:body-more (cons-atom (d0:premise (car-atom $parts)) (d0:body (car-atom (cdr-atom $parts)))))
        ($other ((D0Error (UnknownBody $label))))))))

; ---- Statements -------------------------------------------------------------

(= (d0:attribute-type $label)
   (case $label
     ((dl-src:type-symbol D0Symbol) (dl-src:type-number D0Number)
      (dl-src:type-unsigned D0Unsigned) (dl-src:type-float D0Float)
      ($other (D0Error (UnknownType $other))))))
(= (d0:attribute (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:attr ($identifier (node $type $t))) (D0Attribute (d0:text $identifier) (d0:attribute-type $type)))
      ($other (D0Error (UnknownAttribute $label))))))
(= (d0:attributes $node) (d0:list dl-src:attributes-empty dl-src:attributes-one dl-src:attributes-more d0:attribute $node))

; One declaration names one or more relations with the same attributes.
(= (d0:declared-relations (node $label $fields) $attributes)
   (let $parts (d0:fields $fields)
     (case $label
       ((dl-src:relations-one ((D0Declaration ((d0:text (car-atom $parts))) $attributes)))
        (dl-src:relations-more
          (cons-atom (D0Declaration ((d0:text (car-atom $parts))) $attributes)
            (d0:declared-relations (car-atom (cdr-atom $parts)) $attributes)))
        ($other ((D0Error (UnknownRelations $label))))))))

(= (d0:fact (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:fact ((node dl-src:ground-atom $atom)))
        (case (d0:fields $atom)
          ((($name $constants) (D0Fact (d0:qualified-name $name) (d0:constants $constants)))
           ($other (D0Error MalformedFact)))))
      ($other (D0Error (UnknownFact $label))))))

(= (d0:rule (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:rule ($head $body)) (D0Rule (d0:atom $head) (d0:body $body)))
      ($other (D0Error (UnknownRule $label))))))

; The statements of one item, as a list.
(= (d0:item (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:item-declaration ((node dl-src:declaration $declaration)))
        (case (d0:fields $declaration)
          ((($relations $attributes) (d0:declared-relations $relations (d0:attributes $attributes)))
           ($other ((D0Error MalformedDeclaration))))))
      ((dl-src:item-fact ($fact)) ((d0:fact $fact)))
      ((dl-src:item-rule ($rule)) ((d0:rule $rule)))
      ((dl-src:item-input ((node dl-src:input $input)))
        ((D0Input (d0:qualified-name (car-atom (d0:fields $input))))))
      ((dl-src:item-output ((node dl-src:output $output)))
        ((D0Output (d0:qualified-name (car-atom (d0:fields $output))))))
      ($other ((D0Error (UnknownItem $label)))))))

(= (d0:append $front $back)
   (case $front ((() $back) ((cons $first $rest) (cons-atom $first (d0:append $rest $back))))))

(= (d0:items (node $label $fields))
   (let $parts (d0:fields $fields)
     (case $label
       ((dl-src:items-empty ())
        (dl-src:items-more (d0:append (d0:item (car-atom $parts)) (d0:items (car-atom (cdr-atom $parts)))))
        ($other ((D0Error (UnknownItems $label))))))))

; The program of one selected packet, or (D0Error reason).
(= (d0:program (node $label $fields))
   (case ($label (d0:fields $fields))
     (((dl-src:program ($items))
        (let $program (D0Program (d0:items $items))
          (case (d0:find-error $program) (((Some $reason) (D0Error $reason)) ($none $program)))))
      ($other (D0Error (UnknownProgram $label))))))
