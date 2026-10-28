; Closed protocol values over the existing framed record codec. The admitted
; scalar kinds are explicit: symbols, UTF-8 text, machine naturals and fully
; supported packed U8 vectors. Expressions retain every ordered child.
; No host variable, opaque handle or arbitrary Grounded value is serialized.
!(import! &self (library lib_binary_schema))

(= (binary-value:schema)
   ((Binary:Form 1 BinaryValue:Symbol (Symbol))
    (Binary:Form 2 BinaryValue:Text (Text))
    (Binary:Form 3 BinaryValue:Natural (Natural))
    (Binary:Form 4 BinaryValue:Bytes (Bytes))
    (Binary:Form 5 BinaryValue:Expression
      ((Vector (Either (BinaryValue:Symbol BinaryValue:Text BinaryValue:Natural BinaryValue:Bytes BinaryValue:Expression)))))
    (Binary:Form 6 BinaryValue:Archive
      (Text (Either (BinaryValue:Symbol BinaryValue:Text BinaryValue:Natural BinaryValue:Bytes BinaryValue:Expression))))))

(= (binary-value:to-form $value)
   (case (binary:value-kind $value)
     ((Symbol (BinaryValue:Some (BinaryValue:Symbol $value)))
      (Expression
       (case (binary-value:to-forms $value)
         (((BinaryValue:Some $children) (BinaryValue:Some (BinaryValue:Expression $children))) ($other $other))))
      (Natural (BinaryValue:Some (BinaryValue:Natural $value)))
      (Text (BinaryValue:Some (BinaryValue:Text $value)))
      (Bytes (BinaryValue:Some (BinaryValue:Bytes $value)))
      ($other (BinaryValue:Fault UnsupportedValue)))))
(= (binary-value:to-forms $values)
   (if (== $values ()) (BinaryValue:Some ())
     (let ($head $tail) (decons-atom $values)
       (case ((binary-value:to-form $head) (binary-value:to-forms $tail))
         ((((BinaryValue:Some $first) (BinaryValue:Some $rest))
           (let $items (cons-atom $first $rest) (BinaryValue:Some $items)))
          (($failure (BinaryValue:Some $rest)) $failure)
          (($first $failure) $failure))))))

; Decoding constructs values with cons-atom; it never invokes a decoded head.
(= (binary-value:from-form $form)
   (case $form
     (((BinaryValue:Symbol $value) (BinaryValue:Value $value))
      ((BinaryValue:Text $value) (BinaryValue:Value $value))
      ((BinaryValue:Natural $value) (BinaryValue:Value $value))
      ((BinaryValue:Bytes $value) (BinaryValue:Value $value))
      ((BinaryValue:Expression $children) (binary-value:from-forms $children))
      ($other (BinaryValue:Fault InvalidValue)))))
(= (binary-value:from-forms $forms)
   (if (== $forms ()) (BinaryValue:Value ())
     (let ($head $tail) (decons-atom $forms)
       (case ((binary-value:from-form $head) (binary-value:from-forms $tail))
         ((((BinaryValue:Value $first) (BinaryValue:Value $rest))
           (let $items (cons-atom $first $rest) (BinaryValue:Value $items)))
          ($other (BinaryValue:Fault InvalidValue)))))))

(= (binary-value:pack $value)
   (case (binary-value:to-form $value)
     (((BinaryValue:Some $form)
       (binary-schema:encode (binary-value:schema) (Record BinaryValue:Archive)
         (BinaryValue:Archive "BV1" $form)))
      ($other $other))))
(= (binary-value:unpack $bytes)
   (case (binary-schema:decode (binary-value:schema) (Record BinaryValue:Archive) $bytes)
     (((Binary:Value (BinaryValue:Archive "BV1" $form)) (binary-value:from-form $form))
      ((Binary:Value $other) (BinaryValue:Fault Version))
      ($other $other))))
