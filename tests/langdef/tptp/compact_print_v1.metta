!(import! &self ../../../lib/lib_tptp.metta)

; Printed compact records read back to the same records under every policy and
; profile, and printing leaves data alone: source names that are builtins or
; heads of the receiving space's own equations stay names.
(= (and $x $y) BuiltinNameMustNotEvaluate)
(= (collision $x) RewriteMustNotApply)

(: print-test:same (-> Atom Atom %Undefined%))
(= (print-test:same $a $b) (== (quote $a) (quote $b)))

; Records travel quoted, which keeps HE from reducing names inside them.
(= (print-test:file $reader $policy $path)
   (let $records (tptp:read-file $reader $policy $path)
     (let $printed (tptp:print $reader (quote $records))
       (let $again (tptp:read-text $reader $policy $printed)
         (if (print-test:same $records $again) ok (PrintMismatch $policy $path $printed))))))

(= (print-test:files $reader $policy $paths)
   (if (== $paths ())
       ()
       (let $head (print-test:file $reader $policy (car-atom $paths))
         (let $tail (print-test:files $reader $policy (cdr-atom $paths))
           (cons-atom $head $tail)))))

(= (print-test:profile $profile $paths)
   (let $reader (tptp:open-reader $profile)
     (TptpCompactPrintV1 $profile
       (print-test:files $reader tagged $paths)
       (print-test:files $reader mixed $paths)
       (print-test:files $reader native $paths))))

(= (print-test:strict-files)
   ("tests/langdef/tptp/cnf_clause_data_features.p"
    "tests/langdef/tptp/e_ground_refutation.tstp"
    "tests/langdef/tptp/fof_cnf_document_features.p"
    "tests/langdef/tptp/fof_shadowing.p"
    "tests/langdef/tptp/ground_refutation.p"
    "tests/langdef/tptp/ground_satisfiable.p"
    "tests/langdef/tptp/nonground_refutation.p"
    "tests/langdef/tptp/official_ground_resolution_refutation_v9200.p"
    "tests/langdef/tptp/official_source_keyword_priority_v9200.p"
    "tests/langdef/tptp/official_syntax_delimited_boundaries_v9200.p"
    "tests/langdef/tptp/official_syntax_every_form_v9200.p"
    "tests/langdef/tptp/official_syntax_families_v9200.p"
    "tests/langdef/tptp/official_syntax_layout_only_v9200.p"
    "tests/langdef/tptp/official_syntax_unicode_include_span_v9200.p"
    "tests/langdef/tptp/official_syntax_unicode_span_v9200.p"
    "tests/langdef/tptp/tstp/PUZ001+1.e.tstp"
    "tests/langdef/tptp/tstp/PUZ001+1.vampire.tstp"
    "tests/langdef/tptp/tstp/PUZ001-1.e.tstp"
    "tests/langdef/tptp/tstp/PUZ001-1.vampire.tstp"
    "tests/langdef/tptp/unsupported_variable.p"
    "tests/langdef/tptp/vampire_ground_refutation.tstp"))
(= (print-test:compatible-files)
   ("tests/langdef/tptp/official_internal_source_compatibility_v9200.p"
    "tests/langdef/tptp/official_nhf_bare_parameter_compatibility_v9200.p"
    "tests/langdef/tptp/official_syntax_parenthesized_cnf_v9200.p"))

!(print-test:profile strict (print-test:strict-files))
!(print-test:profile corpus-compatible (print-test:compatible-files))
!(print-test:profile corpus-compatible (print-test:strict-files))

; The boundary: builtin and rewritten names, read, printed and read again.
!(let $reader (tptp:open-reader strict)
   (let $records (tptp:read-text $reader mixed "fof(a,axiom,and(X,n0)).fof(b,axiom,collision(X)).fof(c,axiom,let(X,p,q)).fof(d,axiom,or(not(X),quote(Y))).fof(e,axiom,'+'(1,2)).")
     (let $printed (tptp:print $reader (quote $records))
       (let $again (tptp:read-text $reader mixed $printed)
         (TptpCompactPrintBoundaryV1 $printed (print-test:same $records $again))))))
