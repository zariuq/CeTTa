!(import! &self langdef)

(= (test:first-clause
      (fo-cnf:clause
        (fo-cnf:occurrence $source 0)
        (fo-cnf:formula-name-atomic
          (fo-cnf:symbol-name (fo-cnf:symbol-quoted) "quoted_clause"))
        (fo-cnf:role "axiom" (fo-cnf:annotation-tree $role-refinement))
        (fo-cnf:literals-cons
          (fo-cnf:literal-positive
            (fo-cnf:atom-predicate
              (fo-cnf:symbol-name (fo-cnf:symbol-lower) "p")
              (fo-cnf:terms-cons
                (fo-cnf:term-variable
                  (fo-cnf:variable-id
                    (fo-cnf:occurrence $source 0)
                    (fo-cnf:variable-name "X")))
                (fo-cnf:terms-nil))))
          (fo-cnf:literals-cons
            (fo-cnf:literal-positive
              (fo-cnf:atom-equality
                (fo-cnf:term-variable
                  (fo-cnf:variable-id
                    (fo-cnf:occurrence $source 0)
                    (fo-cnf:variable-name "X")))
                (fo-cnf:term-function
                  (fo-cnf:symbol-name (fo-cnf:symbol-lower) "f")
                  (fo-cnf:terms-cons
                    (fo-cnf:term-function
                      (fo-cnf:symbol-name (fo-cnf:symbol-lower) "a")
                      (fo-cnf:terms-nil))
                    (fo-cnf:terms-nil)))))
            (fo-cnf:literals-nil)))
        (fo-cnf:annotation-tree $source-annotation)
        (fo-cnf:source-span-unknown)))
   True)

(= (test:second-clause
      (fo-cnf:clause
        (fo-cnf:occurrence $source 1)
        (fo-cnf:formula-name-integer "2")
        (fo-cnf:role "negated_conjecture" (fo-cnf:annotation-none))
        (fo-cnf:literals-cons
          (fo-cnf:literal-negative
            (fo-cnf:atom-predicate
              (fo-cnf:symbol-name (fo-cnf:symbol-lower) "q")
              (fo-cnf:terms-cons
                (fo-cnf:term-variable
                  (fo-cnf:variable-id
                    (fo-cnf:occurrence $source 1)
                    (fo-cnf:variable-name "X")))
                (fo-cnf:terms-nil))))
          (fo-cnf:literals-cons
            (fo-cnf:literal-negative
              (fo-cnf:atom-equality
                (fo-cnf:term-variable
                  (fo-cnf:variable-id
                    (fo-cnf:occurrence $source 1)
                    (fo-cnf:variable-name "X")))
                (fo-cnf:term-distinct "object")))
            (fo-cnf:literals-nil)))
        (fo-cnf:annotation-tree $source-annotation)
        (fo-cnf:source-span-unknown)))
   True)

!(bind! &tptp-langdef (langdef:load "langdef/tptp/langdef.metta"))
!(bind! &clause-language
   (langdef:load-language
     "langdef/logic/first_order_clause_data_v1.metta"))
!(assertEqual
   (tptp:raw->syntax-tree
     (node lower-word (pair (cp 112) nil)))
   (tptp-cst:node
     (tptp-cst:label-lower-word)
     (tptp-cst:pair (tptp-cst:codepoint 112) (tptp-cst:nil))))
!(assertEqual
   (tptp:syntax-tree->raw
     (tptp-cst:node
       (tptp-cst:label-lower-word)
       (tptp-cst:pair (tptp-cst:codepoint 112) (tptp-cst:nil))))
   (node lower-word (pair (cp 112) nil)))
!(bind! &prepared
   (langdef:prepare-import-file &tptp-langdef
     "tests/langdef/tptp/cnf_clause_data_features.p"))
!(bind! &problem (tptp:parse->clause-data &prepared))
!(assertEqual
   (langdef:admit-term &clause-language "Problem" &problem)
   (LangDef:TermAdmitted))

!(assertEqual
   (case &problem
     (((fo-cnf:problem
         (fo-cnf:source-digest
           "117608d13103cfd3cd83b686195cec2bce2f08e54f39bb180543726173ceea81")
         (fo-cnf:clauses-cons $first
           (fo-cnf:clauses-cons $second (fo-cnf:clauses-nil))))
       (let $first-ok (test:first-clause $first)
         (let $second-ok (test:second-clause $second)
           True)))
      ($_ False)))
   True)

!(assertEqual (langdef:close-language &clause-language) True)
!(assertEqual (langdef:close &tptp-langdef) True)
