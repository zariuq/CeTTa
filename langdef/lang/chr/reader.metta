; Ground CHR (C0) source terms, read from the native parser's selected packet.
;
; A packet node is (node LABEL FIELDS): FIELDS is a pair-list of nodes, or nil,
; or (cp N) for one scalar of a lexical class. Terminal spellings are not in the
; packet; layout nodes carry no meaning. The package's generated definitions,
; from the same source profile as its grammar, give each pl:apply:N its operator
; as (c0:operator N) = (name specifier priority) and name the layout labels with
; (c0:layout? LABEL).
;
; Prolog terms are returned as
;   (PlAtom NAME) (PlInt N) (PlFloat SPELLING) (PlVar SPELLING) (PlFresh)
;   (PlCompound NAME ARGUMENTS) (PlNil)
; with NAME and SPELLING strings and ARGUMENTS an expression of terms.
; Double-quoted text is an atom, as under double_quotes=atom. SWI's [] is a
; constant of its own, (PlNil), not the atom '[]'.

; ---- Fields and text --------------------------------------------------------

; The fields of a node that carry meaning, in order.
(= (c0:fields nil) ())
(= (c0:fields (pair $first $rest))
   (case $first
     (((node $label $f)
        (if (c0:layout? $label) (c0:fields $rest) (cons-atom $first (c0:fields $rest))))
      ($other (cons-atom $first (c0:fields $rest))))))

; The scalars of a subtree in source order, prepended to $tail.
(= (c0:codes (node $label $fields) $tail)
   (case $fields
     (((cp $n) (cons-atom $n $tail))
      (nil $tail)
      ((pair $first $rest) (c0:codes $first (c0:codes-of $rest $tail))))))
(= (c0:codes-of nil $tail) $tail)
(= (c0:codes-of (pair $first $rest) $tail) (c0:codes $first (c0:codes-of $rest $tail)))

(= (c0:text $node) (str:from-codepoints (c0:codes $node ())))

(= (c0:label-index $label $prefix)
   (str:string->number (str:byte-slice (str:symbol->string $label) (str:byte-length $prefix)
     (str:byte-length (str:symbol->string $label)))))

; ---- Numbers ----------------------------------------------------------------

(= (c0:digit-value $code)
   (if (and (>= $code 48) (<= $code 57)) (- $code 48)
     (if (and (>= $code 97) (<= $code 102)) (- $code 87)
       (- $code 55))))

(= (c0:fold-digits $radix $value $codes)
   (case $codes
     ((() $value)
      ((cons $code $rest) (c0:fold-digits $radix (+ (* $radix $value) (c0:digit-value $code)) $rest)))))
(= (c0:digits-value $radix $node) (c0:fold-digits $radix 0 (c0:codes $node ())))

; The value of a PlNumber node.
(= (c0:number (node $label $fields))
   (case ($label (c0:fields $fields))
     (((pl:integer ($digits)) (PlInt (c0:digits-value 10 $digits)))
      ((pl:hex ($digits)) (PlInt (c0:digits-value 16 $digits)))
      ((pl:oct ($digits)) (PlInt (c0:digits-value 8 $digits)))
      ((pl:bin ($digits)) (PlInt (c0:digits-value 2 $digits)))
      ((pl:character-code ($scalar)) (PlInt (car-atom (c0:codes $scalar ()))))
      ((pl:character-escape ($escape)) (c0:int-or-error (c0:escape-code $escape)))
      ((pl:float $parts) (PlFloat (c0:text (node $label $fields))))
      ((pl:float-exponent $parts) (PlFloat (c0:text (node $label $fields))))
      ($other (PlError (UnknownNumber $label))))))

; ---- Quoted text ------------------------------------------------------------

; SWI's named escapes.
(= (c0:named-escape $code)
   (case $code
     ((97 7) (98 8) (101 27) (102 12) (110 10) (114 13) (115 32) (116 9) (118 11)
      (92 92) (39 39) (34 34) (96 96)
      ($other (PlError (UnknownEscape $other))))))

; A Unicode scalar value: at most U+10FFFF and not a surrogate.
(= (c0:scalar? $n) (or (< $n 55296) (and (>= $n 57344) (<= $n 1114111))))

; The character code that escape digits denote. Like the source reader, it
; refuses a value once it exceeds U+10FFFF, and a surrogate.
(= (c0:fold-code $radix $value $codes)
   (case $codes
     ((() (if (c0:scalar? $value) $value (PlError InvalidCharacterCode)))
      ((cons $code $rest)
        (let $next (+ (* $radix $value) (c0:digit-value $code))
          (if (> $next 1114111) (PlError InvalidCharacterCode) (c0:fold-code $radix $next $rest)))))))
(= (c0:code-value $radix $node) (c0:fold-code $radix 0 (c0:codes $node ())))
(= (c0:codes-of-nodes $nodes)
   (case $nodes ((() ()) ((cons $first $rest) (c0:codes $first (c0:codes-of-nodes $rest))))))

; The code a PlEscape node denotes: a named escape or a numeric character
; code, or a reader error.
(= (c0:escape-code (node $label $fields))
   (case ($label (c0:fields $fields))
     (((pl:escape-named ($scalar)) (c0:named-escape (car-atom (c0:codes $scalar ()))))
      ((pl:escape-hex-code ($digits)) (c0:code-value 16 $digits))
      ((pl:escape-octal-code ($digits)) (c0:code-value 8 $digits))
      ((pl:escape-hex-open ((node $open $inner))) (c0:code-value 16 (car-atom (c0:fields $inner))))
      ((pl:escape-octal-open ((node $open $inner))) (c0:code-value 8 (car-atom (c0:fields $inner))))
      ((pl:escape-unicode-four $digits) (c0:fold-code 16 0 (c0:codes-of-nodes $digits)))
      ((pl:escape-unicode-eight $digits) (c0:fold-code 16 0 (c0:codes-of-nodes $digits)))
      ($other (PlError (UnknownEscape $label))))))

(= (c0:int-or-error $code) (case $code (((PlError $reason) (PlError $reason)) ($n (PlInt $n)))))

; Reader errors stay visible as (PlError reason) subterms; a sentence that
; holds one is refused before lowering.
(= (c0:find-error $term)
   (case $term
     (((PlError $reason) (Some $reason))
      ($other (if (== (get-metatype $term) Expression) (c0:find-error-in $term) None)))))
(= (c0:find-error-in $items)
   (case $items
     ((() None)
      ((cons $first $rest)
        (case (c0:find-error $first) (((Some $reason) (Some $reason)) ($none (c0:find-error-in $rest))))))))

; Text from decoded codes, or the first reader error among them.
(= (c0:codes-text $codes)
   (case (c0:find-error-in $codes) (((Some $reason) (PlError $reason)) ($none (str:from-codepoints $codes)))))

; The codes of a quoted body (single, double or back quotes), prepended to $tail.
(= (c0:body-codes (node $label $fields))
   (case ($label (c0:fields $fields))
     (((pl:single-empty ()) ())
      ((pl:double-empty ()) ())
      ((pl:back-empty ()) ())
      ((pl:single-more ($scalar $rest)) (c0:codes $scalar (c0:body-codes $rest)))
      ((pl:double-more ($scalar $rest)) (c0:codes $scalar (c0:body-codes $rest)))
      ((pl:back-more ($scalar $rest)) (c0:codes $scalar (c0:body-codes $rest)))
      ((pl:single-escape ($escape $rest)) (cons-atom (c0:escape-code $escape) (c0:body-codes $rest)))
      ((pl:double-escape ($escape $rest)) (cons-atom (c0:escape-code $escape) (c0:body-codes $rest)))
      ((pl:back-escape ($escape $rest)) (cons-atom (c0:escape-code $escape) (c0:body-codes $rest)))
      ((pl:single-double ($rest)) (cons-atom 39 (c0:body-codes $rest)))
      ((pl:double-double ($rest)) (cons-atom 34 (c0:body-codes $rest)))
      ((pl:back-double ($rest)) (cons-atom 96 (c0:body-codes $rest))))))

; The name of a PlAtom node.
(= (c0:atom-name (node $label $fields))
   (case ($label (c0:fields $fields))
     (((pl:atom-word ($word)) (c0:text $word))
      ((pl:atom-graphic ($word)) (c0:text $word))
      ((pl:atom-quoted ($body)) (c0:codes-text (c0:body-codes $body)))
      ((pl:atom-cut ()) "!")
      ((pl:atom-semicolon ()) ";"))))

; ---- Terms ------------------------------------------------------------------

(= (c0:operator-of $index)
   (case (c0:operator $index) ((($name $specifier $priority) ($name $specifier)))))

; Total predicates: in the PeTTa lane a call that matches no equation fails.
(= (c0:prefix? $specifier) (or (== $specifier fx) (== $specifier fy)))
(= (c0:postfix? $specifier) (or (== $specifier xf) (== $specifier yf)))

(= (c0:digit-code? $code) (and (>= $code 48) (<= $code 57)))

; Read the PlArguments list of a compound.
(= (c0:arguments (node $label $fields))
   (case ($label (c0:fields $fields))
     (((pl:arguments-one ($term)) ((c0:term $term)))
      ((pl:arguments-more ($term $rest)) (cons-atom (c0:term $term) (c0:arguments $rest))))))

(= (c0:list-term $items $tail)
   (case $items
     ((() $tail)
      ((cons $head $rest) (PlCompound "[|]" ($head (c0:list-term $rest $tail)))))))

; A term node of any precedence level.
(= (c0:term (node $label $fields))
   (let $name (str:symbol->string $label)
     (if (str:starts-with $name "pl:level:")
       (c0:term (car-atom (c0:fields $fields)))
       (if (str:starts-with $name "pl:apply:")
         (c0:application (node $label $fields) (c0:operator-of (c0:label-index $label "pl:apply:")))
         (c0:primary $label (c0:fields $fields) (node $label $fields))))))

; An operator application. A prefix minus written directly before a number is a
; negative literal, as the source reader reads it: the layout after the
; operator is empty and the operand is a number.
(= (c0:application (node $label $fields) ($operator $specifier))
   (let $parts (c0:fields $fields)
     (if (c0:prefix? $specifier)
       (let $operand (car-atom (cdr-atom $parts))
         (if (and (== $operator "-") (c0:adjacent-number? $fields $operand))
           (c0:negate (c0:term $operand))
           (PlCompound $operator ((c0:term $operand)))))
       (if (c0:postfix? $specifier)
         (PlCompound $operator ((c0:term (car-atom $parts))))
         (PlCompound $operator ((c0:term (car-atom $parts)) (c0:term (car-atom (cdr-atom (cdr-atom $parts))))))))))

; The operator is followed by empty layout and then a number term.
(= (c0:adjacent-number? $fields $operand)
   (case $fields
     (((pair $op (pair (node pl:layout-empty nil) $rest)) (c0:number-term? $operand))
      ($other False))))
(= (c0:number-term? (node $label $fields))
   (if (str:starts-with (str:symbol->string $label) "pl:level:")
     (c0:number-term? (car-atom (c0:fields $fields)))
     (== $label pl:term-number)))
(= (c0:negate (PlInt $n)) (PlInt (- 0 $n)))
(= (c0:negate (PlFloat $spelling)) (PlFloat (str:concat "-" $spelling)))
(= (c0:negate (PlError $reason)) (PlError $reason))

; Primary terms (PlExpr0).
(= (c0:primary pl:term-atom ($atom) $node) (PlAtom (c0:atom-name $atom)))
(= (c0:primary pl:term-variable ($name) $node)
   (let $spelling (c0:text $name) (if (== $spelling "_") (PlFresh) (PlVar $spelling))))
(= (c0:primary pl:term-number ($number) $node) (c0:number $number))
(= (c0:primary pl:term-negative ($number) $node) (c0:negate (c0:number $number)))
(= (c0:primary pl:term-text ($body) $node) (PlAtom (c0:codes-text (c0:body-codes $body))))
(= (c0:primary pl:term-backtext ($body) $node)
   (c0:list-term (c0:int-terms (c0:body-codes $body)) (PlNil)))
(= (c0:primary pl:term-compound ($atom $arguments) $node)
   (PlCompound (c0:atom-name $atom) (c0:arguments $arguments)))
(= (c0:primary pl:term-zero-arity ($atom) $node) (PlCompound (c0:atom-name $atom) ()))
(= (c0:primary pl:term-parens ($inner) $node) (c0:term $inner))
(= (c0:primary pl:term-nil () $node) (PlNil))
(= (c0:primary pl:term-list ($arguments) $node) (c0:list-term (c0:arguments $arguments) (PlNil)))
(= (c0:primary pl:term-list-tail ($arguments $tail) $node)
   (c0:list-term (c0:arguments $arguments) (c0:term $tail)))
(= (c0:primary pl:term-braces-empty () $node) (PlAtom "{}"))
(= (c0:primary pl:term-braces ($inner) $node) (PlCompound "{}" ((c0:term $inner))))

(= (c0:int-terms $codes)
   (case $codes
     ((() ())
      ((cons $code $rest) (cons-atom (PlInt $code) (c0:int-terms $rest))))))

; A sentence packet: (node pl:sentence FIELDS) holding one PlExpr1200 term.
(= (c0:sentence-term (node pl:sentence $fields)) (c0:term (car-atom (c0:fields $fields))))
