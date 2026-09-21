; Public source-document projection into tptp-fo / fo-cnf, agreed with the
; older reader on its common domain and checked for real source metadata.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self ../../../langdef/tptp/official_prepared_reader_v1.metta)
!(import! &self langdef)
!(import! &self system)

(= (rec-fo:scope) (tptp-fo:occurrence (tptp-fo:source-digest "") 0))
(= (rec-cnf:scope) (fo-cnf:occurrence (fo-cnf:source-digest "") 0))

(= (old:unpack (LangDef:ImportPrepared TptpFofCnfV1 $_pack $digest ($ast)))
   ($digest $ast))
(= (old:syntax-tree $raw)
   (let $forward
        (langdef:load-structural-tree-relabel
          "langdef/tptp/cnf_clause_data_bridge_v1.metta"
          tptp:raw->syntax-tree tptp:syntax-tree-label)
     (let $syntax (langdef:structural-tree-relabel $forward $raw)
       (let $_ (langdef:close-structural-tree-relabel $forward)
         $syntax))))
(= (old:clause-data $path)
   (let $lang (langdef:load "langdef/tptp/langdef.metta")
     (let $prepared (langdef:prepare-import-file $lang $path)
       (let ($digest $raw) (old:unpack $prepared)
         (let $syntax (old:syntax-tree $raw)
           (let $plan
                (langdef:load-deterministic-equations
                  (LangDef:EquationSources
                    "langdef/tptp/cnf_clause_data_bridge_v1.metta"))
             (let $problem
                  (langdef:run-deterministic-equations $plan
                    (LangDef:DeterministicCall tptp:syntax-tree->problem $syntax $digest))
               (let $_ (langdef:close-deterministic-equations $plan)
                 $problem))))))))
(= (old:document $path)
   (let $lang (langdef:load "langdef/tptp/langdef.metta")
     (let $prepared (langdef:prepare-import-file $lang $path)
       (let ($digest $raw) (old:unpack $prepared)
         (let $syntax (old:syntax-tree $raw)
           (let $plan
                (langdef:load-deterministic-equations
                  (LangDef:EquationSources
                    "langdef/tptp/cnf_clause_data_bridge_v1.metta"
                    "langdef/tptp/first_order_document_bridge_v1.metta"))
             (let $document
                  (langdef:run-deterministic-equations $plan
                    (LangDef:DeterministicCall tptp-doc:syntax-tree->document $syntax $digest))
               (let $_ (langdef:close-deterministic-equations $plan)
                 $document))))))))

(= (norm-cnf:term (fo-cnf:term-variable (fo-cnf:variable-id $_name $var)))
   (fo-cnf:term-variable (fo-cnf:variable-id (rec-cnf:scope) $var)))
(= (norm-cnf:term (fo-cnf:term-function $symbol $args))
   (fo-cnf:term-function $symbol (norm-cnf:terms $args)))
(= (norm-cnf:terms (fo-cnf:terms-nil)) (fo-cnf:terms-nil))
(= (norm-cnf:terms (fo-cnf:terms-cons $head $tail))
   (fo-cnf:terms-cons (norm-cnf:term $head) (norm-cnf:terms $tail)))
(= (norm-cnf:atom (fo-cnf:atom-predicate $symbol $args))
   (fo-cnf:atom-predicate $symbol (norm-cnf:terms $args)))
(= (norm-cnf:atom (fo-cnf:atom-equality $left $right))
   (fo-cnf:atom-equality (norm-cnf:term $left) (norm-cnf:term $right)))
(= (norm-cnf:lit (fo-cnf:literal-positive $atom))
   (fo-cnf:literal-positive (norm-cnf:atom $atom)))
(= (norm-cnf:lit (fo-cnf:literal-negative $atom))
   (fo-cnf:literal-negative (norm-cnf:atom $atom)))
(= (norm-cnf:lits (fo-cnf:literals-nil)) (fo-cnf:literals-nil))
(= (norm-cnf:lits (fo-cnf:literals-cons $lit $tail))
   (fo-cnf:literals-cons (norm-cnf:lit $lit) (norm-cnf:lits $tail)))
(= (norm-cnf:clause (fo-cnf:clause $_occ $name $role $lits $_ann $_span))
   (fo-cnf:clause (rec-cnf:scope) $name $role (norm-cnf:lits $lits)
     (fo-cnf:annotation-none) (fo-cnf:source-span-unknown)))
(= (norm-cnf:clauses (fo-cnf:clauses-nil)) (fo-cnf:clauses-nil))
(= (norm-cnf:clauses (fo-cnf:clauses-cons $clause $tail))
   (fo-cnf:clauses-cons (norm-cnf:clause $clause) (norm-cnf:clauses $tail)))
(= (norm-cnf:problem (fo-cnf:problem $_src $clauses))
   (fo-cnf:problem (fo-cnf:source-digest "") (norm-cnf:clauses $clauses)))

(= (join LNil $ys) $ys)
(= (join (LCons $x $xs) $ys) (LCons $x (join $xs $ys)))
(= (fo-collect-or $formula)
   (case $formula
     (((tptp-fo:formula-or $left $right)
        (join (fo-collect-or $left) (fo-collect-or $right)))
      ($other (LCons (norm-fo:formula $other) LNil)))))
(= (fo-right-or $items)
   (case $items
     (((LCons $only LNil) $only)
      ((LCons $head $tail)
        (tptp-fo:formula-or $head (fo-right-or $tail))))))
(= (norm-fo:term (tptp-fo:term-variable (tptp-fo:variable-id $_ $var)))
   (tptp-fo:term-variable (tptp-fo:variable-id (rec-fo:scope) $var)))
(= (norm-fo:term (tptp-fo:term-function $symbol $args))
   (tptp-fo:term-function $symbol (norm-fo:terms $args)))
(= (norm-fo:terms (tptp-fo:terms-nil)) (tptp-fo:terms-nil))
(= (norm-fo:terms (tptp-fo:terms-cons $head $tail))
   (tptp-fo:terms-cons (norm-fo:term $head) (norm-fo:terms $tail)))
(= (norm-fo:atom (tptp-fo:atom-predicate $symbol $args))
   (tptp-fo:atom-predicate $symbol (norm-fo:terms $args)))
(= (norm-fo:atom (tptp-fo:atom-equality $left $right))
   (tptp-fo:atom-equality (norm-fo:term $left) (norm-fo:term $right)))
(= (norm-fo:vars (tptp-fo:variables-nil)) (tptp-fo:variables-nil))
(= (norm-fo:vars (tptp-fo:variables-cons (tptp-fo:variable-id $_ $n) $tail))
   (tptp-fo:variables-cons (tptp-fo:variable-id (rec-fo:scope) $n)
     (norm-fo:vars $tail)))
(= (norm-fo:formula (tptp-fo:formula-atom $atom))
   (tptp-fo:formula-atom (norm-fo:atom $atom)))
(= (norm-fo:formula (tptp-fo:formula-not $body))
   (tptp-fo:formula-not (norm-fo:formula $body)))
(= (norm-fo:formula (tptp-fo:formula-and $left $right))
   (tptp-fo:formula-and (norm-fo:formula $left) (norm-fo:formula $right)))
(= (norm-fo:formula (tptp-fo:formula-or $left $right))
   (fo-right-or (fo-collect-or (tptp-fo:formula-or $left $right))))
(= (norm-fo:formula (tptp-fo:formula-implies $left $right))
   (tptp-fo:formula-implies (norm-fo:formula $left) (norm-fo:formula $right)))
(= (norm-fo:formula (tptp-fo:formula-iff $left $right))
   (tptp-fo:formula-iff (norm-fo:formula $left) (norm-fo:formula $right)))
(= (norm-fo:formula (tptp-fo:formula-forall $vars $body))
   (tptp-fo:formula-forall (norm-fo:vars $vars) (norm-fo:formula $body)))
(= (norm-fo:formula (tptp-fo:formula-exists $vars $body))
   (tptp-fo:formula-exists (norm-fo:vars $vars) (norm-fo:formula $body)))
(= (norm-fo:input (tptp-fo:formula-input $_occ $dialect $name $role $formula $_ann $_span))
   (tptp-fo:formula-input (rec-fo:scope) $dialect $name $role
     (norm-fo:formula $formula) (tptp-fo:annotation-none)
     (tptp-fo:source-span-unknown)))
(= (norm-fo:inputs (tptp-fo:inputs-nil)) (tptp-fo:inputs-nil))
(= (norm-fo:inputs (tptp-fo:inputs-cons $head $tail))
   (tptp-fo:inputs-cons (norm-fo:input $head) (norm-fo:inputs $tail)))
(= (norm-fo:document (tptp-fo:document $_src $inputs))
   (tptp-fo:document (tptp-fo:source-digest "") (norm-fo:inputs $inputs)))

(= (agree:bool $left $right)
   (case (== $left $right)
     ((True True)
      (False False))))
(= (gate:compare $cnf-document $fof-document $cnf-path $fof-path)
   (let $from-cnf (tptp:source-document->clause-data $cnf-document)
     (let $old-cnf (old:clause-data $cnf-path)
       (let $from-fof
            (tptp:source-document->first-order-document $fof-document)
         (let $old-fof (old:document $fof-path)
           (case $from-cnf
             (((TPTP:StageValue clause-data
                  (fo-cnf:problem $_a $_b))
                (case $old-cnf
                  (((fo-cnf:problem $_c $_d)
                     (case $from-fof
                       (((TPTP:StageValue first-order-document
                            (tptp-fo:document $_e $_f))
                          (case $old-fof
                            (((tptp-fo:document $_g $_h)
                               (TptpFofCnfViewAgreementV1
                                 (agree:bool
                                   (norm-cnf:problem
                                     (fo-cnf:problem $_a $_b))
                                   (norm-cnf:problem $old-cnf))
                                 (agree:bool
                                   (norm-fo:document
                                     (tptp-fo:document $_e $_f))
                                   (norm-fo:document $old-fof))))
                             ($o (TptpFofCnfViewFailure old-fof)))))
                        ($o (TptpFofCnfViewFailure rec-fof)))))
                   ($o (TptpFofCnfViewFailure old-cnf)))))
              ($o (TptpFofCnfViewFailure rec-cnf)))))))))
(= (gate:run $cnf-path $fof-path)
   (let $cnf-document (tptp:read-file-document $cnf-path)
     (let $fof-document (tptp:read-file-document $fof-path)
       (gate:compare $cnf-document $fof-document $cnf-path $fof-path))))
!(println! (gate:run (system:arg 0) (system:arg 1)))
