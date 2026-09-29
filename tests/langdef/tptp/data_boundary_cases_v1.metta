; Data on both hosts.  The equations below match the heads of read records
; and formulas, the leaf tags of the tagged policy and a parsed JSON number.
; Reading, inspecting, printing, importing, expanding includes and the binding
; operations pass records and formulas as data, so none of them fires, and
; the values are the same in the HE and PeTTa lanes.  `let(X,p,q)` is a
; formula whose mixed spelling is a host builtin; it stays a formula.

(= (p $x) BOOM-p)
(= (word $x) BOOM-word)
(= (and $x $y) BOOM-and)
(= (fof $n $r $f) BOOM-fof)
(= (JsonNumberV1 $x) BOOM-json)

(= (boundary:text)
   "fof(a,axiom,p(X) & and(X,n0)).fof(b,axiom,let(X,p,q)).fof(c,axiom,word(a)).tff(d,axiom,$let(c: $i, c := a, p(c))).")

(= (boundary:root) "tests/langdef/tptp/data_boundary_v1/root.p")
(= (boundary:library) (TPTP:LibraryRoot "tests/langdef/tptp/data_boundary_v1"))

; 1. Read: the records as data.
(= (boundary:read $reader $policy)
   (DataBoundaryV1 read (tptp:read-text $reader $policy (boundary:text))))

; 2. Inspect: the name, role and formula of the first record, and the formula
;    of the second, whose mixed spelling is (let X p q).
(= (boundary:inspect $reader $policy)
   (let $records (tptp:read-text $reader $policy (boundary:text))
     (let $first (car-atom $records)
       (let $rest (cdr-atom $records)
         (let $second (car-atom $rest)
         (DataBoundaryV1 inspect
           (tptp:record-family $first)
           (tptp:record-name $first)
           (tptp:record-role $first)
           (tptp:record-formula $first)
           (tptp:record-formula $second)))))))

; 3. Print: the text of the records, and that it reads back to the same records.
(= (boundary:print $reader $policy)
   (let $records (tptp:read-text $reader $policy (boundary:text))
     (let $text (tptp:print $reader $records)
       (let $again (tptp:read-text $reader $policy $text)
         (DataBoundaryV1 print $text (if (== $records $again) same different))))))

; 4. Import: the records enter a space as data and match back as data, each
;    handed out inside a constructor whose fields are data.
(: BoundaryStored (-> Atom Atom Atom Atom))
(= (boundary:import $reader $policy $space)
   (let $records (tptp:import-text $space $reader $policy (boundary:text))
     (let $stored (collapse (match $space (fof $n $r $f) (BoundaryStored $n $r $f)))
       (DataBoundaryV1 import $stored))))

; 5. Includes: the records of a root that includes a file, in expansion order.
(= (boundary:includes $reader $policy)
   (let $expanded
        (tptp:read-file-expanded-with-reader $reader $policy (boundary:root) (boundary:library))
     (DataBoundaryV1 includes (tptp:expanded-records $expanded))))

; 6. Binding: free variables, substitution, renaming apart and alpha equality
;    over formulas spelled with p and and.  The bindings are program data
;    written in the program: they enter through a parameter typed Atom, the
;    one position where both hosts take a written expression as it is.
(: boundary:binding (-> Atom Atom Atom %Undefined%))
(= (boundary:binding $reader $policy $bindings)
   (let $records (tptp:read-text $reader $policy "fof(l,axiom,![X]: (p(X) & and(X,Y))).fof(r,axiom,![Z]: (p(Z) & and(Z,Y))).")
     (let $first (car-atom $records)
       (let $rest (cdr-atom $records)
         (let $second (car-atom $rest)
           (let (TPTP:RecordFormula $left) (tptp:record-formula $first)
             (let (TPTP:RecordFormula $right) (tptp:record-formula $second)
               (DataBoundaryV1 binding
                 (tptp:free-variables $policy $left)
                 (tptp:substitute $policy $bindings $left)
                 (tptp:rename-apart $policy () $left)
                 (if (tptp:alpha-equal $policy $left $right) alpha-equal alpha-different)))))))))

; 7. JSON: a parsed value is data, and stringifies; each value is handed
;    out inside a constructor whose field is data, and the operations
;    compose as calls.  (A value a program passes through a variable into a
;    JSON operation is that program's own value: HE evaluates it at the
;    call, PeTTa does not; the JSON library's own fixtures state both.)
(: BoundaryData (-> Atom Atom))
(= (boundary:json)
   (let $number (json:parse "1")
     (let $array (json:parse "[1, 2]")
       (DataBoundaryV1 json
         (BoundaryData $number)
         (BoundaryData $array)
         (json:stringify (json:parse "[1, 2]"))))))

(: boundary:all (-> Atom Atom Atom %Undefined%))
(= (boundary:all $policy $space $bindings)
   (let $reader (tptp:open-reader strict)
     (DataBoundaryV1Results $policy
       (boundary:read $reader $policy)
       (boundary:inspect $reader $policy)
       (boundary:print $reader $policy)
       (boundary:import $reader $policy $space)
       (boundary:includes $reader $policy)
       (boundary:binding $reader $policy $bindings)
       (boundary:json))))
