; The official reader on each TPTP family: the kind and the input names of a
; corpus problem of each family, the families fixture's tcf, tpi, include and
; annotated inputs, the main connectives of one formula of four families, and
; four texts read to their records.  A record is (FAMILY NAME ROLE FORMULA
; ANNOTATION...) or (incl FILE SELECTION...).
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (family-test:kind $record) (car-atom $record))
(= (family-test:name $record) (car-atom (cdr-atom $record)))
(= (family-test:names $records)
   (if (== (size-atom $records) 0)
       ()
       (cons-atom (family-test:name (car-atom $records))
                  (family-test:names (cdr-atom $records)))))
(= (family-test:has-kind $kind $records)
   (if (== (size-atom $records) 0)
       False
       (if (== (family-test:kind (car-atom $records)) $kind)
           True
           (family-test:has-kind $kind (cdr-atom $records)))))
(= (family-test:find-kind $kind $records)
   (if (== (size-atom $records) 0)
       (missing $kind)
       (if (== (family-test:kind (car-atom $records)) $kind)
           (car-atom $records)
           (family-test:find-kind $kind (cdr-atom $records)))))
(= (family-test:find-name $name $records)
   (if (== (size-atom $records) 0)
       (missing $name)
       (if (== (family-test:name (car-atom $records)) $name)
           (car-atom $records)
           (family-test:find-name $name (cdr-atom $records)))))

(= (family-test:text $tag $reader $text $expected)
   (let $actual (repr (tptp:read-text $reader $text))
     (if (== $actual $expected)
         (TptpFamilyText $tag)
         (TptpFamilyTextMismatch $tag $expected $actual))))

(= (family-test:row $tag $reader $path $expect-kind)
   (let $records (tptp:read-file $reader $path)
     (let $kind (family-test:kind (car-atom $records))
       (if (== $kind $expect-kind)
           (TptpFamilyRow $tag $kind (family-test:names $records))
           (TptpFamilyKindMismatch $tag $expect-kind $kind)))))
(= (family-test:row-has $tag $reader $path $expect-kind)
   (let $records (tptp:read-file $reader $path)
     (if (family-test:has-kind $expect-kind $records)
         (TptpFamilyRow $tag $expect-kind (family-test:names $records))
         (TptpFamilyKindMismatch $tag $expect-kind missing))))

; An include of the fixture names fof_formula in its selection.
(= (family-test:include-row $reader $path)
   (let $include (family-test:find-kind incl (tptp:read-file $reader $path))
     (let $selection (car-atom (cdr-atom (cdr-atom $include)))
       (if (== (car-atom $selection) fof_formula)
           (TptpFamilyInclude (car-atom (cdr-atom $include)) $selection)
           (TptpFamilyIncludeMismatch $include)))))

; The fixture's annotated input is inferred by magic from fof_formula.
(= (family-test:inference-row $reader $path)
   (let $record (family-test:find-name annotated (tptp:read-file $reader $path))
     (let $source (car-atom (cdr-atom (cdr-atom (cdr-atom (cdr-atom $record)))))
       (if (== (car-atom $source) inference)
           (TptpFamilyInference (car-atom (cdr-atom $source))
             (car-atom (cdr-atom (cdr-atom (cdr-atom $source)))))
           (TptpFamilyInferenceMissing $record)))))

; The connectives of a formula, through quantifiers, implications and
; disjunctions.
(= (family-test:head $formula)
   (if (== (get-metatype $formula) Expression)
       (family-test:head-of (car-atom $formula) $formula)
       atom))
(= (family-test:head-of ∀ $formula)
   (∀ (family-test:head (car-atom (cdr-atom (cdr-atom $formula))))))
(= (family-test:head-of ∨ $formula)
   (∨ (family-test:head (car-atom (cdr-atom $formula)))
      (family-test:head (car-atom (cdr-atom (cdr-atom $formula))))))
(= (family-test:head-of → $formula)
   (→ (family-test:head (car-atom (cdr-atom $formula)))
      (family-test:head (car-atom (cdr-atom (cdr-atom $formula))))))
(= (family-test:head-of ∃ $_formula) ∃)
(= (family-test:head-of ¬ $_formula) ¬)
(= (family-test:head-of $other $_formula)
   (if (or (== $other ∀) (or (== $other ∨) (or (== $other →) (or (== $other ∃) (== $other ¬)))))
       (empty)
       atom))
(= (family-test:formula $name $records)
   (car-atom (cdr-atom (cdr-atom (cdr-atom (family-test:find-name $name $records))))))
(= (family-test:connectives $reader $path)
   (let $records (tptp:read-file $reader $path)
     (TptpFamilyConnectives
       (family-test:head (family-test:formula thf_formula $records))
       (family-test:head (family-test:formula fof_formula $records))
       (family-test:head (family-test:formula cnf_formula $records))
       (family-test:head (family-test:formula tcf_formula $records)))))

(= (family-test:run $reader)
   (let $empty (family-test:text empty $reader "" "[]")
     (let $fof (family-test:text fof-atom $reader "fof(a,axiom,p)." "[(fof a axiom p)]")
       (let $quoted
            (family-test:text quoted $reader "fof('X',axiom,p(X,'X'))."
              "[(fof (word \"X\") axiom (p X (word \"X\")))]")
         (let $cnf-text
              (family-test:text cnf-or $reader "cnf(c,axiom,(~p(X) | q(X)))."
                "[(cnf c axiom (∨ (¬ (p X)) (q X)))]")
           (let $cnf (family-test:row cnf $reader (system:arg 1) cnf)
             (let $fof-file (family-test:row fof $reader (system:arg 2) fof)
               (let $tff (family-test:row tff $reader (system:arg 3) tff)
                 (let $thf (family-test:row thf $reader (system:arg 4) thf)
                   (let $txf (family-test:row txf $reader (system:arg 5) tff)
                     (let $tcf (family-test:row-has tcf $reader (system:arg 7) tcf)
                       (let $tpi (family-test:row tpi $reader (system:arg 7) tpi)
                         (let $include (family-test:include-row $reader (system:arg 7))
                           (let $inference (family-test:inference-row $reader (system:arg 7))
                             (let $connectives (family-test:connectives $reader (system:arg 7))
                               (TptpOfficialSyntaxRecordsFamilyV1Summary
                                 $empty $fof $quoted $cnf-text
                                 $cnf $fof-file $tff $thf $txf $tcf $tpi $include
                                 $inference $connectives))))))))))))))))

(= (family-test:use $reader)
   (case $reader
     (((NativeHandle "cetta.tptp-reader.v1" $_id)
        (println! (family-test:run $reader)))
      ($fault (println! (TptpFamilyReaderFailure $fault))))))
!(family-test:use (tptp:official-reader (system:arg 0)))
