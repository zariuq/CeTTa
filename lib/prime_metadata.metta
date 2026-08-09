; Prime metadata records are ordinary data admitted against declarations stored
; in the destination metadata space.  Producers may construct records, but only
; the admission relation writes them.
;
; Effect algebra used below:
;   prime-meta:after-unit () value = value
;   prime-meta:after-unit error value = error
; Its first argument is demanded exactly once by Prime's call-time Need heap.

(= (prime-meta:after-unit $effect $value)
   (case $effect
     ((() $value)
      ($effect-error $effect-error))))

(= (prime-meta:closed-items $items)
   (if (== $items ())
       True
       (let ($head $tail) (decons-atom $items)
         (if (prime-meta:closed $head)
             (prime-meta:closed-items $tail)
             False))))

(= (prime-meta:closed $atom)
   (let $meta (get-metatype $atom)
     (if (== $meta Variable)
         False
         (if (== $meta Expression)
             (prime-meta:closed-items $atom)
             True))))

(= (prime-meta:name-valid $name)
   (case $name
     (((quote $payload) (prime-meta:closed $payload))
      ($other False))))

(= (prime-meta:span-valid $span)
   (case $span
     (((SpanV1 $start $end)
       (if (== (get-type $start) Number)
           (if (== (get-type $end) Number)
               (if (>= $start 0) (<= $start $end) False)
               False)
           False))
      ($other False))))

(= (prime-meta:field-valid $kind $value)
   (case $kind
     ((Name (prime-meta:name-valid $value))
      (String (== (get-type $value) String))
      (Span (prime-meta:span-valid $value))
      (Atom True)
      ($other False))))

(= (prime-meta:field-pair-valid $decl $value)
   (case (Pair $decl $value)
     (((Pair (FieldV1 $label $kind) (ValueV1 $label $payload))
       (prime-meta:field-valid $kind $payload))
      ($other False))))

(= (prime-meta:fields-valid $decls $values)
   (if (== $decls ())
       (== $values ())
       (if (== $values ())
           False
           (let ($decl $decl-tail) (decons-atom $decls)
             (let ($value $value-tail) (decons-atom $values)
               (if (prime-meta:field-pair-valid $decl $value)
                   (prime-meta:fields-valid $decl-tail $value-tail)
                   False))))))

(= (prime-meta:schema-values-valid $schema $values)
   (if (== (car-atom $schema) FieldsV1)
       (if (== (car-atom $values) ValuesV1)
           (prime-meta:fields-valid
             (cdr-atom $schema) (cdr-atom $values))
           False)
       False))

(= (prime-meta:schema-candidates $space $key)
   ; A metadata key is used as syntax inside the match pattern.  `case`
   ; explicitly demands the quoted name before constructing that pattern;
   ; otherwise call-by-need would correctly leave a once-used key suspended.
   (case $key
     (((quote $payload)
       (reify
         (match $space
           (MetaKeyV1 (quote $payload) $fields)
           $fields)))
      ($other ()))))

(= (prime-meta:check-record $space $record)
   (case $record
     (((MetaRecordV1 $key $values)
       (case (prime-meta:schema-candidates $space $key)
         ((() (Rejected UndeclaredMetaKey $key))
          (($schema)
           (if (prime-meta:schema-values-valid $schema $values)
               (Admissible $key)
               (Rejected SchemaMismatch $key)))
          ($many (Rejected AmbiguousMetaKey $key)))))
      ($other (Rejected MalformedMetaRecordV1 $other)))))

(= (prime-meta:add-record $space $record)
   (case (prime-meta:check-record $space $record)
     (((Admissible $key)
       ; Reconstruct the checked outer record so neither storage nor the
       ; admission result exposes the suspended constructor call supplied by
       ; the caller.
       (case $record
         (((MetaRecordV1 $stored-key $stored-values)
           ; Prime `let` is call-by-need: an unused receipt cannot sequence an
           ; effect.  The unit eliminator demands the effect before the
           ; admitted value crosses this authority boundary.
           (prime-meta:after-unit
             (add-atom-nodup
               $space (MetaRecordV1 $stored-key $stored-values))
             (Admitted
               (MetaRecordV1 $stored-key $stored-values))))
          ($other (Rejected MalformedMetaRecordV1 $other)))))
      ((Rejected $reason $detail) (Rejected $reason $detail)))))

(= (prime-meta:source-recognized $space $source)
   (case $source
     (((quote $payload)
       (if (== (reify
                 (match $space
                   (RecognizedSourceV1 (quote $payload)) recognized))
               ())
           False
           True))
      ($other False))))

; A source is recognized by explicit policy data in the destination space.
(= (recognize-doc-source! $space $source)
   (case $source
     (((quote $payload)
       (if (prime-meta:closed $payload)
           ; Store reconstructed policy data, not the evaluator-private
           ; argument suspension.  Exact matching must not need authority to
           ; inspect an internal promise at this boundary.
           (prime-meta:after-unit
             (add-atom-nodup
               $space (RecognizedSourceV1 (quote $payload)))
             (Recognized (quote $payload)))
           (Rejected MalformedName (quote $payload))))
      ($other (Rejected MalformedName $other)))))

(= (install-doc-schema! $space)
   (prime-meta:after-unit
     (add-atom-nodup
       $space
       (MetaKeyV1 (quote doc)
         (FieldsV1
           (FieldV1 subject Name)
           (FieldV1 text String)
           (FieldV1 source Name)
           (FieldV1 span Span))))
     (SchemaInstalled (quote doc))))

; `doc` is an untrusted constructor.  It has no authority to write a space.
(= (doc $subject $text $source $start $end)
   (MetaRecordV1 (quote doc)
     (ValuesV1
       (ValueV1 subject $subject)
       (ValueV1 text $text)
       (ValueV1 source $source)
       (ValueV1 span (SpanV1 $start $end)))))

; `add-doc` is total and fail-closed.  A valid schema is necessary but not
; sufficient: the record's named source must also satisfy local recognition.
(= (add-doc $space $record)
   (case $record
     (((MetaRecordV1 (quote doc)
         (ValuesV1
           (ValueV1 subject $subject)
           (ValueV1 text $text)
           (ValueV1 source $source)
           (ValueV1 span $span)))
       (case (prime-meta:check-record $space $record)
         (((Admissible $key)
           (if (prime-meta:source-recognized $space $source)
               (prime-meta:add-record $space $record)
               (Rejected UnrecognizedSource $source)))
          ((Rejected $reason $detail) (Rejected $reason $detail)))))
      ($other (Rejected MalformedDocV1 $other)))))
