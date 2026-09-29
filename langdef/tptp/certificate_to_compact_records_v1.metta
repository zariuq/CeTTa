; A checked resolution certificate as compact TPTP records: the derivation
; in TSTP form.
;
; Each input clause of the problem becomes (cnf i_N ROLE CLAUSE (file PATH
; NAME)): N is its position, so two copies of a twice-included clause have
; distinct names, the role is the source record's, the path is that of the
; file its occurrence names and NAME the source record's own name, a
; clausified formula's for a clause at a clausal occurrence.  Each step of
; the certificate becomes (cnf c_N plain RESOLVENT
; (inference resolution [(status thm)] [LEFT RIGHT])), N the derived
; clause's index and the parents named as their records are; the last step
; concludes $false.  Clauses are spelled as the mixed policy reads them: a
; disjunction of literals, (¬ A) and (≉ L R) for negative literals, the
; empty clause as §false, variables by their names, renamed apart within a
; clause when two variables of distinct scopes share a name.  An introduced
; symbol is named sk or df followed by its introduction index, or, when a
; symbol of the problem or the certificate already has that spelling, by the
; first of that name followed by _1, _2, ... that none has: indices are
; digits, so no two introduced symbols share a name, and no introduced symbol
; takes a problem symbol's name.  The digest of
; every source is paired with its path by the files carrier; a digest the
; carrier lacks is written as it is.  The certificate is not checked here:
; the trace checker and the provenance check accept it first.
(gslt-presentation-v1 TptpCertificateToCompactRecordsV1
  (signature
    (operator metta-equation 2) (operator + 2)
    (operator LNil 0) (operator LCons 2)
    (operator langdef:value-view 1) (operator langdef:ground-equal 2)
    (operator langdef:decimal-integer->number 1)
    (operator langdef:decimal-rational->value 1)
    (operator langdef:decimal-real->float 1)
    (operator str:concat 2) (operator str:set 1) (operator str:set-member? 2)
    (operator LangDef:IntegerValue 1)
    (operator LangDef:SameValue 0) (operator LangDef:DifferentValue 0)
    (operator tptp-compact:expression 1) (operator tptp-compact:list-value 1)
    (operator tptp-compact:symbol 1) (operator tptp-compact:word 2)
    (operator tptp-compact:quoted-word 2) (operator tptp-compact:variable 2)
    (operator tptp-compact:defined 2) (operator tptp-compact:system 2)
    (operator tptp-compact:rational 1) (operator tptp-compact:float-or-exact 2)
    (operator fo-resolution:problem 2)
    (operator fo-resolution:clauses-nil 0) (operator fo-resolution:clauses-cons 2)
    (operator fo-resolution:clause 4)
    (operator fo-resolution:source-digest 1)
    (operator fo-resolution:occurrence 2) (operator fo-resolution:included-occurrence 3)
    (operator fo-resolution:clausal-occurrence 2)
    (operator fo-resolution:formula-name-atomic 1) (operator fo-resolution:formula-name-integer 1)
    (operator fo-resolution:role 1)
    (operator fo-resolution:symbol-name 2)
    (operator fo-resolution:symbol-lower 0) (operator fo-resolution:symbol-quoted 0)
    (operator fo-resolution:symbol-defined 0) (operator fo-resolution:symbol-system 0)
    (operator fo-resolution:symbol-skolem 0) (operator fo-resolution:symbol-definition 0)
    (operator fo-resolution:variable-name 1) (operator fo-resolution:variable-id 2)
    (operator fo-resolution:number-integer 0) (operator fo-resolution:number-rational 0)
    (operator fo-resolution:number-real 0)
    (operator fo-resolution:term-variable 1) (operator fo-resolution:term-function 2)
    (operator fo-resolution:term-number 2) (operator fo-resolution:term-distinct 1)
    (operator fo-resolution:terms-nil 0) (operator fo-resolution:terms-cons 2)
    (operator fo-resolution:atom-predicate 2) (operator fo-resolution:atom-equality 2)
    (operator fo-resolution:literal-positive 1) (operator fo-resolution:literal-negative 1)
    (operator fo-resolution:literals-nil 0) (operator fo-resolution:literals-cons 2)
    (operator fo-proof:certificate 3)
    (operator fo-proof:steps-nil 0) (operator fo-proof:steps-cons 2)
    (operator fo-proof:step 4) (operator fo-proof:parent 3)
    (operator fo-proof:input-clause 1) (operator fo-proof:derived-clause 1)
    (operator tptp-cert-v1:files-nil 0) (operator tptp-cert-v1:files-cons 2)
    (operator tptp-cert-v1:file 2)
    (operator tptp-cert-v1:records 3) (operator tptp-cert-v1:append 2)
    (operator tptp-cert-v1:input-records 4) (operator tptp-cert-v1:input-record 7)
    (operator tptp-cert-v1:input-label 1)
    (operator tptp-cert-v1:step-records 3) (operator tptp-cert-v1:step-record 6)
    (operator tptp-cert-v1:record 4)
    (operator tptp-cert-v1:name 1) (operator tptp-cert-v1:derived-name 1)
    (operator tptp-cert-v1:parent-name 2) (operator tptp-cert-v1:input-name 3)
    (operator tptp-cert-v1:input-name-at 4)
    (operator tptp-cert-v1:role 1)
    (operator tptp-cert-v1:source 2) (operator tptp-cert-v1:file-of 1)
    (operator tptp-cert-v1:path-of 2) (operator tptp-cert-v1:path-at 4)
    (operator tptp-cert-v1:inference 2) (operator tptp-cert-v1:status 0)
    (operator tptp-cert-v1:clause-value 2) (operator tptp-cert-v1:clause-of 2)
    (operator tptp-cert-v1:disjuncts 2)
    (operator tptp-cert-v1:literals 2) (operator tptp-cert-v1:literal 2)
    (operator tptp-cert-v1:negative 2) (operator tptp-cert-v1:atom 2)
    (operator tptp-cert-v1:applied 3) (operator tptp-cert-v1:terms 2)
    (operator tptp-cert-v1:term 2) (operator tptp-cert-v1:symbol 2)
    (operator tptp-cert-v1:number 2)
    (operator tptp-cert-v1:integer-text 1) (operator tptp-cert-v1:integer-text-of 1)
    (operator tptp-cert-v1:names 1) (operator tptp-cert-v1:ids 2)
    (operator tptp-cert-v1:ids-of-atom 2) (operator tptp-cert-v1:ids-of-terms 2)
    (operator tptp-cert-v1:ids-of-term 2) (operator tptp-cert-v1:ids-add 2)
    (operator tptp-cert-v1:ids-add-at 3) (operator tptp-cert-v1:ids-member 2)
    (operator tptp-cert-v1:ids-member-at 3) (operator tptp-cert-v1:reverse 2)
    (operator tptp-cert-v1:assign 3) (operator tptp-cert-v1:assign-with 5)
    (operator tptp-cert-v1:named 2)
    (operator tptp-cert-v1:fresh 2) (operator tptp-cert-v1:fresh-at 3)
    (operator tptp-cert-v1:fresh-numbered 4) (operator tptp-cert-v1:fresh-numbered-at 5)
    (operator tptp-cert-v1:spelling 2) (operator tptp-cert-v1:spelling-at 4)
    (operator tptp-cert-v1:records-with 4) (operator tptp-cert-v1:scope 2)
    (operator tptp-cert-v1:vocabulary 2)
    (operator tptp-cert-v1:clause-texts 2) (operator tptp-cert-v1:step-texts 2)
    (operator tptp-cert-v1:literal-texts 2) (operator tptp-cert-v1:atom-texts 2)
    (operator tptp-cert-v1:terms-texts 2) (operator tptp-cert-v1:term-texts 2)
    (operator tptp-cert-v1:symbol-texts 2)
    (operator tptp-cert-v1:fresh-symbol 2) (operator tptp-cert-v1:fresh-symbol-at 4)
    (operator tptp-cert-v1:fresh-symbol-numbered 4)
    (operator tptp-cert-v1:fresh-symbol-numbered-at 5)
    (operator tptp-cert-v1:vocabulary-of 1) (operator tptp-cert-v1:known 2)
    (operator tptp-cert-v1:names-exhausted 1))
  (equations)
  (rewrites
    ; ------------------------------------------------------------- records
    (rule cert-records (head (metta-equation
      (tptp-cert-v1:records (fo-resolution:problem ?source ?clauses)
        (fo-proof:certificate ?certificate-source ?steps ?empty) ?files)
      (tptp-cert-v1:records-with ?clauses ?steps ?files
        (tptp-cert-v1:vocabulary ?clauses ?steps)))) (body))
    (rule cert-records-with (head (metta-equation
      (tptp-cert-v1:records-with ?clauses ?steps ?files ?vocabulary)
      (tptp-compact:list-value
        (tptp-cert-v1:append (tptp-cert-v1:input-records ?clauses ?files 0 ?vocabulary)
          (tptp-cert-v1:step-records ?steps ?clauses ?vocabulary))))) (body))
    (rule cert-append-nil (head (metta-equation (tptp-cert-v1:append LNil ?rest) ?rest)) (body))
    (rule cert-append-cons (head (metta-equation
      (tptp-cert-v1:append (LCons ?first ?more) ?rest)
      (LCons ?first (tptp-cert-v1:append ?more ?rest)))) (body))
    (rule cert-record (head (metta-equation
      (tptp-cert-v1:record ?name ?role ?formula ?source)
      (tptp-compact:expression (LCons (tptp-compact:symbol "cnf")
        (LCons ?name (LCons ?role (LCons ?formula (LCons ?source LNil)))))))) (body))

    ; --------------------------------------------------------------- inputs
    ; An input record is named i_N by its position, so a clause included
    ; twice gives two records with distinct names; the source names the
    ; record it came from by its own name.
    (rule cert-input-records-nil (head (metta-equation
      (tptp-cert-v1:input-records (fo-resolution:clauses-nil) ?files ?index ?vocabulary) LNil)) (body))
    (rule cert-input-records-cons (head (metta-equation
      (tptp-cert-v1:input-records
        (fo-resolution:clauses-cons (fo-resolution:clause ?occurrence ?name ?role ?literals) ?rest)
        ?files ?index ?vocabulary)
      (LCons (tptp-cert-v1:input-record ?occurrence ?name ?role ?literals ?files ?index ?vocabulary)
        (tptp-cert-v1:input-records ?rest ?files (+ ?index 1) ?vocabulary)))) (body))
    (rule cert-input-record (head (metta-equation
      (tptp-cert-v1:input-record ?occurrence ?name ?role ?literals ?files ?index ?vocabulary)
      (tptp-cert-v1:record (tptp-cert-v1:input-label ?index)
        (tptp-cert-v1:role ?role)
        (tptp-cert-v1:clause-value ?literals ?vocabulary)
        (tptp-cert-v1:source
          (tptp-cert-v1:path-of (tptp-cert-v1:file-of ?occurrence) ?files)
          (tptp-cert-v1:name ?name))))) (body))
    (rule cert-input-label (head (metta-equation
      (tptp-cert-v1:input-label ?index)
      (tptp-compact:symbol (str:concat "i_" (tptp-cert-v1:integer-text ?index))))) (body))
    (rule cert-source (head (metta-equation
      (tptp-cert-v1:source ?path ?name)
      (tptp-compact:expression (LCons (tptp-compact:symbol "file")
        (LCons ?path (LCons ?name LNil)))))) (body))
    (rule cert-role (head (metta-equation
      (tptp-cert-v1:role (fo-resolution:role ?text)) (tptp-compact:word mixed ?text))) (body))

    (rule cert-name-atomic (head (metta-equation
      (tptp-cert-v1:name (fo-resolution:formula-name-atomic (fo-resolution:symbol-name ?kind ?text)))
      (tptp-compact:quoted-word mixed ?text))) (body))
    (rule cert-name-integer (head (metta-equation
      (tptp-cert-v1:name (fo-resolution:formula-name-integer ?text))
      (langdef:decimal-integer->number ?text))) (body))
    ; The file of an occurrence: the digest it names, paired with its path.
    (rule cert-file-of-plain (head (metta-equation
      (tptp-cert-v1:file-of (fo-resolution:occurrence ?digest ?index)) ?digest)) (body))
    (rule cert-file-of-included (head (metta-equation
      (tptp-cert-v1:file-of (fo-resolution:included-occurrence ?digest ?index ?via)) ?digest)) (body))
    (rule cert-file-of-clausal (head (metta-equation
      (tptp-cert-v1:file-of (fo-resolution:clausal-occurrence ?formula ?position))
      (tptp-cert-v1:file-of ?formula))) (body))
    (rule cert-path-of-nil (head (metta-equation
      (tptp-cert-v1:path-of (fo-resolution:source-digest ?digest) (tptp-cert-v1:files-nil))
      ?digest)) (body))
    (rule cert-path-of-cons (head (metta-equation
      (tptp-cert-v1:path-of (fo-resolution:source-digest ?digest)
        (tptp-cert-v1:files-cons (tptp-cert-v1:file ?candidate ?path) ?rest))
      (tptp-cert-v1:path-at (langdef:ground-equal ?digest ?candidate) ?path
        (fo-resolution:source-digest ?digest) ?rest))) (body))
    (rule cert-path-at-same (head (metta-equation
      (tptp-cert-v1:path-at LangDef:SameValue ?path ?source ?rest) ?path)) (body))
    (rule cert-path-at-different (head (metta-equation
      (tptp-cert-v1:path-at LangDef:DifferentValue ?path ?source ?rest)
      (tptp-cert-v1:path-of ?source ?rest))) (body))

    ; ---------------------------------------------------------------- steps
    (rule cert-step-records-nil (head (metta-equation
      (tptp-cert-v1:step-records (fo-proof:steps-nil) ?clauses ?vocabulary) LNil)) (body))
    (rule cert-step-records-cons (head (metta-equation
      (tptp-cert-v1:step-records
        (fo-proof:steps-cons
          (fo-proof:step ?conclusion
            (fo-proof:parent ?left ?left-index ?left-substitution)
            (fo-proof:parent ?right ?right-index ?right-substitution)
            ?literals)
          ?rest)
        ?clauses ?vocabulary)
      (LCons (tptp-cert-v1:step-record ?conclusion ?left ?right ?literals ?clauses ?vocabulary)
        (tptp-cert-v1:step-records ?rest ?clauses ?vocabulary)))) (body))
    (rule cert-step-record (head (metta-equation
      (tptp-cert-v1:step-record ?conclusion ?left ?right ?literals ?clauses ?vocabulary)
      (tptp-cert-v1:record (tptp-cert-v1:parent-name ?conclusion ?clauses)
        (tptp-compact:symbol "plain")
        (tptp-cert-v1:clause-value ?literals ?vocabulary)
        (tptp-cert-v1:inference (tptp-cert-v1:parent-name ?left ?clauses)
          (tptp-cert-v1:parent-name ?right ?clauses))))) (body))
    (rule cert-inference (head (metta-equation
      (tptp-cert-v1:inference ?left ?right)
      (tptp-compact:expression (LCons (tptp-compact:symbol "inference")
        (LCons (tptp-compact:symbol "resolution")
          (LCons (tptp-cert-v1:status)
            (LCons (tptp-compact:list-value (LCons ?left (LCons ?right LNil))) LNil))))))) (body))
    (rule cert-status (head (metta-equation
      (tptp-cert-v1:status)
      (tptp-compact:list-value
        (LCons (tptp-compact:expression
                 (LCons (tptp-compact:symbol "status") (LCons (tptp-compact:symbol "thm") LNil)))
          LNil)))) (body))
    (rule cert-parent-derived (head (metta-equation
      (tptp-cert-v1:parent-name (fo-proof:derived-clause ?index) ?clauses)
      (tptp-cert-v1:derived-name ?index))) (body))
    (rule cert-parent-input (head (metta-equation
      (tptp-cert-v1:parent-name (fo-proof:input-clause ?occurrence) ?clauses)
      (tptp-cert-v1:input-name ?occurrence ?clauses 0))) (body))
    (rule cert-derived-name (head (metta-equation
      (tptp-cert-v1:derived-name ?index)
      (tptp-compact:symbol (str:concat "c_" (tptp-cert-v1:integer-text ?index))))) (body))
    ; An input parent is named as its record, by position; a certificate
    ; citing an input the problem lacks has no name here, which the checks
    ; reject before.
    (rule cert-input-name-cons (head (metta-equation
      (tptp-cert-v1:input-name ?occurrence
        (fo-resolution:clauses-cons (fo-resolution:clause ?candidate ?name ?role ?literals) ?rest)
        ?index)
      (tptp-cert-v1:input-name-at (langdef:ground-equal ?occurrence ?candidate)
        ?occurrence ?index ?rest))) (body))
    (rule cert-input-name-same (head (metta-equation
      (tptp-cert-v1:input-name-at LangDef:SameValue ?occurrence ?index ?rest)
      (tptp-cert-v1:input-label ?index))) (body))
    (rule cert-input-name-different (head (metta-equation
      (tptp-cert-v1:input-name-at LangDef:DifferentValue ?occurrence ?index ?rest)
      (tptp-cert-v1:input-name ?occurrence ?rest (+ ?index 1)))) (body))

    ; -------------------------------------------------------------- clauses
    (rule cert-clause-value (head (metta-equation
      (tptp-cert-v1:clause-value ?literals ?vocabulary)
      (tptp-cert-v1:clause-of ?literals
        (tptp-cert-v1:scope ?vocabulary (tptp-cert-v1:names ?literals))))) (body))
    ; A clause is the disjunction of its literals, as the reader keeps every
    ; cnf clause; the empty clause is the disjunction of §false.
    (rule cert-clause-of (head (metta-equation
      (tptp-cert-v1:clause-of ?literals ?table)
      (tptp-compact:expression (LCons (tptp-compact:symbol "∨")
        (tptp-cert-v1:disjuncts ?literals ?table))))) (body))
    (rule cert-disjuncts-empty (head (metta-equation
      (tptp-cert-v1:disjuncts (fo-resolution:literals-nil) ?table)
      (LCons (tptp-compact:defined mixed "false") LNil))) (body))
    (rule cert-disjuncts-cons (head (metta-equation
      (tptp-cert-v1:disjuncts (fo-resolution:literals-cons ?literal ?rest) ?table)
      (LCons (tptp-cert-v1:literal ?literal ?table) (tptp-cert-v1:literals ?rest ?table)))) (body))
    (rule cert-literals-nil (head (metta-equation
      (tptp-cert-v1:literals (fo-resolution:literals-nil) ?table) LNil)) (body))
    (rule cert-literals-cons (head (metta-equation
      (tptp-cert-v1:literals (fo-resolution:literals-cons ?literal ?rest) ?table)
      (LCons (tptp-cert-v1:literal ?literal ?table) (tptp-cert-v1:literals ?rest ?table)))) (body))
    (rule cert-literal-positive (head (metta-equation
      (tptp-cert-v1:literal (fo-resolution:literal-positive ?atom) ?table)
      (tptp-cert-v1:atom ?atom ?table))) (body))
    (rule cert-literal-negative (head (metta-equation
      (tptp-cert-v1:literal (fo-resolution:literal-negative ?atom) ?table)
      (tptp-cert-v1:negative ?atom ?table))) (body))
    (rule cert-negative-predicate (head (metta-equation
      (tptp-cert-v1:negative (fo-resolution:atom-predicate ?symbol ?terms) ?table)
      (tptp-compact:expression (LCons (tptp-compact:symbol "¬")
        (LCons (tptp-cert-v1:atom (fo-resolution:atom-predicate ?symbol ?terms) ?table) LNil))))) (body))
    (rule cert-negative-equality (head (metta-equation
      (tptp-cert-v1:negative (fo-resolution:atom-equality ?left ?right) ?table)
      (tptp-compact:expression (LCons (tptp-compact:symbol "≉")
        (LCons (tptp-cert-v1:term ?left ?table)
          (LCons (tptp-cert-v1:term ?right ?table) LNil)))))) (body))
    (rule cert-atom-predicate (head (metta-equation
      (tptp-cert-v1:atom (fo-resolution:atom-predicate ?symbol ?terms) ?table)
      (tptp-cert-v1:applied (tptp-cert-v1:symbol ?symbol ?table) ?terms ?table))) (body))
    (rule cert-atom-equality (head (metta-equation
      (tptp-cert-v1:atom (fo-resolution:atom-equality ?left ?right) ?table)
      (tptp-compact:expression (LCons (tptp-compact:symbol "≈")
        (LCons (tptp-cert-v1:term ?left ?table)
          (LCons (tptp-cert-v1:term ?right ?table) LNil)))))) (body))
    (rule cert-applied-constant (head (metta-equation
      (tptp-cert-v1:applied ?head (fo-resolution:terms-nil) ?table) ?head)) (body))
    (rule cert-applied-terms (head (metta-equation
      (tptp-cert-v1:applied ?head (fo-resolution:terms-cons ?first ?rest) ?table)
      (tptp-compact:expression
        (LCons ?head (tptp-cert-v1:terms (fo-resolution:terms-cons ?first ?rest) ?table))))) (body))
    (rule cert-terms-nil (head (metta-equation
      (tptp-cert-v1:terms (fo-resolution:terms-nil) ?table) LNil)) (body))
    (rule cert-terms-cons (head (metta-equation
      (tptp-cert-v1:terms (fo-resolution:terms-cons ?first ?rest) ?table)
      (LCons (tptp-cert-v1:term ?first ?table) (tptp-cert-v1:terms ?rest ?table)))) (body))
    (rule cert-term-variable (head (metta-equation
      (tptp-cert-v1:term (fo-resolution:term-variable ?id) (tptp-cert-v1:scope ?vocabulary ?names))
      (tptp-compact:variable mixed (tptp-cert-v1:spelling ?id ?names)))) (body))
    (rule cert-term-function (head (metta-equation
      (tptp-cert-v1:term (fo-resolution:term-function ?symbol ?terms) ?table)
      (tptp-cert-v1:applied (tptp-cert-v1:symbol ?symbol ?table) ?terms ?table))) (body))
    (rule cert-term-number (head (metta-equation
      (tptp-cert-v1:term (fo-resolution:term-number ?kind ?text) ?table)
      (tptp-cert-v1:number ?kind ?text))) (body))
    (rule cert-term-distinct (head (metta-equation
      (tptp-cert-v1:term (fo-resolution:term-distinct ?text) ?table) ?text)) (body))
    (rule cert-number-integer (head (metta-equation
      (tptp-cert-v1:number (fo-resolution:number-integer) ?text)
      (langdef:decimal-integer->number ?text))) (body))
    (rule cert-number-rational (head (metta-equation
      (tptp-cert-v1:number (fo-resolution:number-rational) ?text)
      (tptp-compact:rational (langdef:decimal-rational->value ?text)))) (body))
    (rule cert-number-real (head (metta-equation
      (tptp-cert-v1:number (fo-resolution:number-real) ?text)
      (tptp-compact:float-or-exact (langdef:decimal-real->float ?text) ?text))) (body))

    ; Symbols as the mixed policy spells them; an introduced symbol carries
    ; its introduction index.
    (rule cert-symbol-lower (head (metta-equation
      (tptp-cert-v1:symbol (fo-resolution:symbol-name (fo-resolution:symbol-lower) ?text) ?table)
      (tptp-compact:word mixed ?text))) (body))
    (rule cert-symbol-quoted (head (metta-equation
      (tptp-cert-v1:symbol (fo-resolution:symbol-name (fo-resolution:symbol-quoted) ?text) ?table)
      (tptp-compact:quoted-word mixed ?text))) (body))
    (rule cert-symbol-defined (head (metta-equation
      (tptp-cert-v1:symbol (fo-resolution:symbol-name (fo-resolution:symbol-defined) ?text) ?table)
      (tptp-compact:defined mixed ?text))) (body))
    (rule cert-symbol-system (head (metta-equation
      (tptp-cert-v1:symbol (fo-resolution:symbol-name (fo-resolution:symbol-system) ?text) ?table)
      (tptp-compact:system mixed ?text))) (body))
    (rule cert-symbol-skolem (head (metta-equation
      (tptp-cert-v1:symbol (fo-resolution:symbol-name (fo-resolution:symbol-skolem) ?text) ?table)
      (tptp-compact:symbol (tptp-cert-v1:fresh-symbol (str:concat "sk" ?text) ?table)))) (body))
    (rule cert-symbol-definition (head (metta-equation
      (tptp-cert-v1:symbol (fo-resolution:symbol-name (fo-resolution:symbol-definition) ?text) ?table)
      (tptp-compact:symbol (tptp-cert-v1:fresh-symbol (str:concat "df" ?text) ?table)))) (body))
    (rule cert-integer-text (head (metta-equation
      (tptp-cert-v1:integer-text ?n)
      (tptp-cert-v1:integer-text-of (langdef:value-view ?n)))) (body))
    (rule cert-integer-text-of (head (metta-equation
      (tptp-cert-v1:integer-text-of (LangDef:IntegerValue ?text)) ?text)) (body))

    ; -------------------------------------------------------- variable names
    ; The variables of a clause in order of first occurrence, each spelled
    ; by its name, or by its name and a number when an earlier variable of
    ; another scope took that spelling.
    (rule cert-names (head (metta-equation
      (tptp-cert-v1:names ?literals)
      (tptp-cert-v1:assign (tptp-cert-v1:reverse (tptp-cert-v1:ids ?literals LNil) LNil)
        LNil LNil))) (body))
    (rule cert-ids-nil (head (metta-equation
      (tptp-cert-v1:ids (fo-resolution:literals-nil) ?acc) ?acc)) (body))
    (rule cert-ids-positive (head (metta-equation
      (tptp-cert-v1:ids (fo-resolution:literals-cons (fo-resolution:literal-positive ?atom) ?rest) ?acc)
      (tptp-cert-v1:ids ?rest (tptp-cert-v1:ids-of-atom ?atom ?acc)))) (body))
    (rule cert-ids-negative (head (metta-equation
      (tptp-cert-v1:ids (fo-resolution:literals-cons (fo-resolution:literal-negative ?atom) ?rest) ?acc)
      (tptp-cert-v1:ids ?rest (tptp-cert-v1:ids-of-atom ?atom ?acc)))) (body))
    (rule cert-ids-of-predicate (head (metta-equation
      (tptp-cert-v1:ids-of-atom (fo-resolution:atom-predicate ?symbol ?terms) ?acc)
      (tptp-cert-v1:ids-of-terms ?terms ?acc))) (body))
    (rule cert-ids-of-equality (head (metta-equation
      (tptp-cert-v1:ids-of-atom (fo-resolution:atom-equality ?left ?right) ?acc)
      (tptp-cert-v1:ids-of-term ?right (tptp-cert-v1:ids-of-term ?left ?acc)))) (body))
    (rule cert-ids-of-terms-nil (head (metta-equation
      (tptp-cert-v1:ids-of-terms (fo-resolution:terms-nil) ?acc) ?acc)) (body))
    (rule cert-ids-of-terms-cons (head (metta-equation
      (tptp-cert-v1:ids-of-terms (fo-resolution:terms-cons ?first ?rest) ?acc)
      (tptp-cert-v1:ids-of-terms ?rest (tptp-cert-v1:ids-of-term ?first ?acc)))) (body))
    (rule cert-ids-of-variable (head (metta-equation
      (tptp-cert-v1:ids-of-term (fo-resolution:term-variable ?id) ?acc)
      (tptp-cert-v1:ids-add ?id ?acc))) (body))
    (rule cert-ids-of-function (head (metta-equation
      (tptp-cert-v1:ids-of-term (fo-resolution:term-function ?symbol ?terms) ?acc)
      (tptp-cert-v1:ids-of-terms ?terms ?acc))) (body))
    (rule cert-ids-of-number (head (metta-equation
      (tptp-cert-v1:ids-of-term (fo-resolution:term-number ?kind ?text) ?acc) ?acc)) (body))
    (rule cert-ids-of-distinct (head (metta-equation
      (tptp-cert-v1:ids-of-term (fo-resolution:term-distinct ?text) ?acc) ?acc)) (body))
    (rule cert-ids-add (head (metta-equation
      (tptp-cert-v1:ids-add ?id ?acc)
      (tptp-cert-v1:ids-add-at (tptp-cert-v1:ids-member ?id ?acc) ?id ?acc))) (body))
    (rule cert-ids-add-present (head (metta-equation
      (tptp-cert-v1:ids-add-at True ?id ?acc) ?acc)) (body))
    (rule cert-ids-add-absent (head (metta-equation
      (tptp-cert-v1:ids-add-at False ?id ?acc) (LCons ?id ?acc))) (body))
    (rule cert-ids-member-nil (head (metta-equation
      (tptp-cert-v1:ids-member ?id LNil) False)) (body))
    (rule cert-ids-member-cons (head (metta-equation
      (tptp-cert-v1:ids-member ?id (LCons ?candidate ?rest))
      (tptp-cert-v1:ids-member-at (langdef:ground-equal ?id ?candidate) ?id ?rest))) (body))
    (rule cert-ids-member-same (head (metta-equation
      (tptp-cert-v1:ids-member-at LangDef:SameValue ?id ?rest) True)) (body))
    (rule cert-ids-member-different (head (metta-equation
      (tptp-cert-v1:ids-member-at LangDef:DifferentValue ?id ?rest)
      (tptp-cert-v1:ids-member ?id ?rest))) (body))
    (rule cert-reverse-nil (head (metta-equation (tptp-cert-v1:reverse LNil ?acc) ?acc)) (body))
    (rule cert-reverse-cons (head (metta-equation
      (tptp-cert-v1:reverse (LCons ?first ?rest) ?acc)
      (tptp-cert-v1:reverse ?rest (LCons ?first ?acc)))) (body))
    (rule cert-assign-nil (head (metta-equation
      (tptp-cert-v1:assign LNil ?used ?table) ?table)) (body))
    (rule cert-assign-cons (head (metta-equation
      (tptp-cert-v1:assign
        (LCons (fo-resolution:variable-id ?scope (fo-resolution:variable-name ?base)) ?rest)
        ?used ?table)
      (tptp-cert-v1:assign-with (tptp-cert-v1:fresh ?base ?used)
        (fo-resolution:variable-id ?scope (fo-resolution:variable-name ?base))
        ?rest ?used ?table))) (body))
    (rule cert-assign-with (head (metta-equation
      (tptp-cert-v1:assign-with ?spelling ?id ?rest ?used ?table)
      (tptp-cert-v1:assign ?rest (LCons ?spelling ?used)
        (LCons (tptp-cert-v1:named ?id ?spelling) ?table)))) (body))
    (rule cert-fresh (head (metta-equation
      (tptp-cert-v1:fresh ?base ?used)
      (tptp-cert-v1:fresh-at (str:set-member? (str:set ?used) ?base) ?base ?used))) (body))
    (rule cert-fresh-free (head (metta-equation
      (tptp-cert-v1:fresh-at False ?base ?used) ?base)) (body))
    ; The numbered search tries base_1, base_2, ...  Every taken candidate is
    ; a distinct used spelling, so the used list, one element spent for each
    ; taken candidate, always outlasts the search.
    (rule cert-fresh-taken (head (metta-equation
      (tptp-cert-v1:fresh-at True ?base ?used)
      (tptp-cert-v1:fresh-numbered ?base ?used ?used 1))) (body))
    (rule cert-fresh-numbered (head (metta-equation
      (tptp-cert-v1:fresh-numbered ?base ?used ?fuel ?k)
      (tptp-cert-v1:fresh-numbered-at
        (str:set-member? (str:set ?used)
          (str:concat ?base (str:concat "_" (tptp-cert-v1:integer-text ?k))))
        ?base ?used ?fuel ?k))) (body))
    (rule cert-fresh-numbered-free (head (metta-equation
      (tptp-cert-v1:fresh-numbered-at False ?base ?used ?fuel ?k)
      (str:concat ?base (str:concat "_" (tptp-cert-v1:integer-text ?k))))) (body))
    (rule cert-fresh-numbered-taken (head (metta-equation
      (tptp-cert-v1:fresh-numbered-at True ?base ?used (LCons ?spent ?fuel) ?k)
      (tptp-cert-v1:fresh-numbered ?base ?used ?fuel (+ ?k 1)))) (body))
    (rule cert-fresh-numbered-exhausted (head (metta-equation
      (tptp-cert-v1:fresh-numbered-at True ?base ?used LNil ?k)
      (tptp-cert-v1:names-exhausted ?base))) (body))
    (rule cert-spelling-cons (head (metta-equation
      (tptp-cert-v1:spelling ?id (LCons (tptp-cert-v1:named ?candidate ?text) ?rest))
      (tptp-cert-v1:spelling-at (langdef:ground-equal ?id ?candidate) ?text ?id ?rest))) (body))
    (rule cert-spelling-same (head (metta-equation
      (tptp-cert-v1:spelling-at LangDef:SameValue ?text ?id ?rest) ?text)) (body))
    (rule cert-spelling-different (head (metta-equation
      (tptp-cert-v1:spelling-at LangDef:DifferentValue ?text ?id ?rest)
      (tptp-cert-v1:spelling ?id ?rest))) (body))

    ; ------------------------------------------------ introduced symbol names
    ; The vocabulary is every symbol text of the problem's clauses and the
    ; certificate's steps, as a quoted and a plain word name one symbol.
    ; The vocabulary is the set of the source texts, beside their list.
    (rule cert-vocabulary (head (metta-equation
      (tptp-cert-v1:vocabulary ?clauses ?steps)
      (tptp-cert-v1:vocabulary-of
        (tptp-cert-v1:step-texts ?steps (tptp-cert-v1:clause-texts ?clauses LNil))))) (body))
    (rule cert-vocabulary-of (head (metta-equation
      (tptp-cert-v1:vocabulary-of ?texts)
      (tptp-cert-v1:known (str:set ?texts) ?texts))) (body))
    (rule cert-clause-texts-nil (head (metta-equation
      (tptp-cert-v1:clause-texts (fo-resolution:clauses-nil) ?acc) ?acc)) (body))
    (rule cert-clause-texts-cons (head (metta-equation
      (tptp-cert-v1:clause-texts
        (fo-resolution:clauses-cons (fo-resolution:clause ?occurrence ?name ?role ?literals) ?rest) ?acc)
      (tptp-cert-v1:clause-texts ?rest (tptp-cert-v1:literal-texts ?literals ?acc)))) (body))
    (rule cert-step-texts-nil (head (metta-equation
      (tptp-cert-v1:step-texts (fo-proof:steps-nil) ?acc) ?acc)) (body))
    (rule cert-step-texts-cons (head (metta-equation
      (tptp-cert-v1:step-texts
        (fo-proof:steps-cons (fo-proof:step ?conclusion ?left ?right ?literals) ?rest) ?acc)
      (tptp-cert-v1:step-texts ?rest (tptp-cert-v1:literal-texts ?literals ?acc)))) (body))
    (rule cert-literal-texts-nil (head (metta-equation
      (tptp-cert-v1:literal-texts (fo-resolution:literals-nil) ?acc) ?acc)) (body))
    (rule cert-literal-texts-positive (head (metta-equation
      (tptp-cert-v1:literal-texts
        (fo-resolution:literals-cons (fo-resolution:literal-positive ?atom) ?rest) ?acc)
      (tptp-cert-v1:literal-texts ?rest (tptp-cert-v1:atom-texts ?atom ?acc)))) (body))
    (rule cert-literal-texts-negative (head (metta-equation
      (tptp-cert-v1:literal-texts
        (fo-resolution:literals-cons (fo-resolution:literal-negative ?atom) ?rest) ?acc)
      (tptp-cert-v1:literal-texts ?rest (tptp-cert-v1:atom-texts ?atom ?acc)))) (body))
    (rule cert-atom-texts-predicate (head (metta-equation
      (tptp-cert-v1:atom-texts (fo-resolution:atom-predicate ?symbol ?terms) ?acc)
      (tptp-cert-v1:terms-texts ?terms (tptp-cert-v1:symbol-texts ?symbol ?acc)))) (body))
    (rule cert-atom-texts-equality (head (metta-equation
      (tptp-cert-v1:atom-texts (fo-resolution:atom-equality ?left ?right) ?acc)
      (tptp-cert-v1:term-texts ?right (tptp-cert-v1:term-texts ?left ?acc)))) (body))
    (rule cert-terms-texts-nil (head (metta-equation
      (tptp-cert-v1:terms-texts (fo-resolution:terms-nil) ?acc) ?acc)) (body))
    (rule cert-terms-texts-cons (head (metta-equation
      (tptp-cert-v1:terms-texts (fo-resolution:terms-cons ?first ?rest) ?acc)
      (tptp-cert-v1:terms-texts ?rest (tptp-cert-v1:term-texts ?first ?acc)))) (body))
    (rule cert-term-texts-variable (head (metta-equation
      (tptp-cert-v1:term-texts (fo-resolution:term-variable ?id) ?acc) ?acc)) (body))
    (rule cert-term-texts-function (head (metta-equation
      (tptp-cert-v1:term-texts (fo-resolution:term-function ?symbol ?terms) ?acc)
      (tptp-cert-v1:terms-texts ?terms (tptp-cert-v1:symbol-texts ?symbol ?acc)))) (body))
    (rule cert-term-texts-number (head (metta-equation
      (tptp-cert-v1:term-texts (fo-resolution:term-number ?kind ?text) ?acc) ?acc)) (body))
    (rule cert-term-texts-distinct (head (metta-equation
      (tptp-cert-v1:term-texts (fo-resolution:term-distinct ?text) ?acc) ?acc)) (body))
    (rule cert-symbol-texts-lower (head (metta-equation
      (tptp-cert-v1:symbol-texts (fo-resolution:symbol-name (fo-resolution:symbol-lower) ?text) ?acc)
      (LCons ?text ?acc))) (body))
    (rule cert-symbol-texts-quoted (head (metta-equation
      (tptp-cert-v1:symbol-texts (fo-resolution:symbol-name (fo-resolution:symbol-quoted) ?text) ?acc)
      (LCons ?text ?acc))) (body))
    (rule cert-symbol-texts-defined (head (metta-equation
      (tptp-cert-v1:symbol-texts (fo-resolution:symbol-name (fo-resolution:symbol-defined) ?text) ?acc)
      ?acc)) (body))
    (rule cert-symbol-texts-system (head (metta-equation
      (tptp-cert-v1:symbol-texts (fo-resolution:symbol-name (fo-resolution:symbol-system) ?text) ?acc)
      ?acc)) (body))
    (rule cert-symbol-texts-skolem (head (metta-equation
      (tptp-cert-v1:symbol-texts (fo-resolution:symbol-name (fo-resolution:symbol-skolem) ?text) ?acc)
      ?acc)) (body))
    (rule cert-symbol-texts-definition (head (metta-equation
      (tptp-cert-v1:symbol-texts (fo-resolution:symbol-name (fo-resolution:symbol-definition) ?text) ?acc)
      ?acc)) (body))
    ; An introduced symbol's name: its base, or the first numbered variant of
    ; it, that the vocabulary lacks.
    (rule cert-fresh-symbol (head (metta-equation
      (tptp-cert-v1:fresh-symbol ?base (tptp-cert-v1:scope (tptp-cert-v1:known ?set ?texts) ?names))
      (tptp-cert-v1:fresh-symbol-at (str:set-member? ?set ?base) ?base ?set ?texts))) (body))
    (rule cert-fresh-symbol-free (head (metta-equation
      (tptp-cert-v1:fresh-symbol-at False ?base ?set ?texts) ?base)) (body))
    ; As for variable names, the vocabulary's texts outlast the search.
    (rule cert-fresh-symbol-taken (head (metta-equation
      (tptp-cert-v1:fresh-symbol-at True ?base ?set ?texts)
      (tptp-cert-v1:fresh-symbol-numbered ?base ?set ?texts 1))) (body))
    (rule cert-fresh-symbol-numbered (head (metta-equation
      (tptp-cert-v1:fresh-symbol-numbered ?base ?set ?fuel ?k)
      (tptp-cert-v1:fresh-symbol-numbered-at
        (str:set-member? ?set
          (str:concat ?base (str:concat "_" (tptp-cert-v1:integer-text ?k))))
        ?base ?set ?fuel ?k))) (body))
    (rule cert-fresh-symbol-numbered-free (head (metta-equation
      (tptp-cert-v1:fresh-symbol-numbered-at False ?base ?set ?fuel ?k)
      (str:concat ?base (str:concat "_" (tptp-cert-v1:integer-text ?k))))) (body))
    (rule cert-fresh-symbol-numbered-taken (head (metta-equation
      (tptp-cert-v1:fresh-symbol-numbered-at True ?base ?set (LCons ?spent ?fuel) ?k)
      (tptp-cert-v1:fresh-symbol-numbered ?base ?set ?fuel (+ ?k 1)))) (body))
    (rule cert-fresh-symbol-numbered-exhausted (head (metta-equation
      (tptp-cert-v1:fresh-symbol-numbered-at True ?base ?set LNil ?k)
      (tptp-cert-v1:names-exhausted ?base))) (body))))
