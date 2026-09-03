!(import! &self ../../../langdef/logic/clause_data_to_resolution_input_v1.generated.metta)
!(import! &self langdef)

(= (test:unknown-constructor-rejection $result)
   (case $result
     (((LangDef:TermRejected unknown_constructor $message) True)
      ($_ False))))

(= (test:source-problem)
   (fo-cnf:problem
     (fo-cnf:source-digest "source-digest")
     (fo-cnf:clauses-cons
       (fo-cnf:clause
         (fo-cnf:occurrence (fo-cnf:source-digest "source-digest") 7)
         (fo-cnf:formula-name-atomic
           (fo-cnf:symbol-name (fo-cnf:symbol-quoted) "quoted clause"))
         (fo-cnf:role "negated_conjecture"
           (fo-cnf:annotation-tree (fo-cnf:syntax-atom "role-refinement")))
         (fo-cnf:literals-cons
           (fo-cnf:literal-positive
             (fo-cnf:atom-predicate
               (fo-cnf:symbol-name (fo-cnf:symbol-lower) "p")
               (fo-cnf:terms-cons
                 (fo-cnf:term-variable
                   (fo-cnf:variable-id
                     (fo-cnf:occurrence
                       (fo-cnf:source-digest "source-digest") 7)
                     (fo-cnf:variable-name "X")))
                 (fo-cnf:terms-nil))))
           (fo-cnf:literals-cons
             (fo-cnf:literal-negative
               (fo-cnf:atom-equality
                 (fo-cnf:term-variable
                   (fo-cnf:variable-id
                     (fo-cnf:occurrence
                       (fo-cnf:source-digest "source-digest") 7)
                     (fo-cnf:variable-name "X")))
                 (fo-cnf:term-number (fo-cnf:number-rational) "3/2")))
             (fo-cnf:literals-nil)))
         (fo-cnf:annotation-tree (fo-cnf:syntax-atom "source-annotation"))
         (fo-cnf:source-span 11 97))
       (fo-cnf:clauses-nil))))

(= (test:expected-resolution-problem)
   (fo-resolution:problem
     (fo-resolution:source-digest "source-digest")
     (fo-resolution:clauses-cons
       (fo-resolution:clause
         (fo-resolution:occurrence
           (fo-resolution:source-digest "source-digest") 7)
         (fo-resolution:formula-name-atomic
           (fo-resolution:symbol-name
             (fo-resolution:symbol-quoted) "quoted clause"))
         (fo-resolution:role "negated_conjecture")
         (fo-resolution:literals-cons
           (fo-resolution:literal-positive
             (fo-resolution:atom-predicate
               (fo-resolution:symbol-name
                 (fo-resolution:symbol-lower) "p")
               (fo-resolution:terms-cons
                 (fo-resolution:term-variable
                   (fo-resolution:variable-id
                     (fo-resolution:occurrence
                       (fo-resolution:source-digest "source-digest") 7)
                     (fo-resolution:variable-name "X")))
                 (fo-resolution:terms-nil))))
           (fo-resolution:literals-cons
             (fo-resolution:literal-negative
               (fo-resolution:atom-equality
                 (fo-resolution:term-variable
                   (fo-resolution:variable-id
                     (fo-resolution:occurrence
                       (fo-resolution:source-digest "source-digest") 7)
                     (fo-resolution:variable-name "X")))
                 (fo-resolution:term-number
                   (fo-resolution:number-rational) "3/2")))
             (fo-resolution:literals-nil))))
       (fo-resolution:clauses-nil))))

!(assertEqual
   (fo-resolution:project-problem (test:source-problem))
   (test:expected-resolution-problem))

; Equal display spellings in different source occurrences remain distinct.
!(assertEqual
   (==
     (fo-resolution:project-variable-id
       (fo-cnf:variable-id
         (fo-cnf:occurrence (fo-cnf:source-digest "source-digest") 7)
         (fo-cnf:variable-name "X")))
     (fo-resolution:project-variable-id
       (fo-cnf:variable-id
         (fo-cnf:occurrence (fo-cnf:source-digest "source-digest") 8)
         (fo-cnf:variable-name "X"))))
   False)

; A source constructor outside the authored projection is not silently
; interpreted as a resolution term.
!(assertEqual
   (collapse
     (fo-resolution:project-term
       (fo-cnf:invented-term "payload")))
   ((fo-resolution:project-term (fo-cnf:invented-term "payload"))))

!(bind! &clause-data-language
   (langdef:load-language
     "langdef/logic/first_order_clause_data_v1.metta"))
!(bind! &resolution-input-language
   (langdef:load-language
     "langdef/logic/first_order_resolution_input_v1.metta"))

!(assertEqual
   (langdef:admit-term
     &clause-data-language "Problem" (test:source-problem))
   (LangDef:TermAdmitted))
!(assertEqual
   (langdef:admit-term
     &resolution-input-language "Problem"
     (fo-resolution:project-problem (test:source-problem)))
   (LangDef:TermAdmitted))
!(assertEqual
   (test:unknown-constructor-rejection
     (langdef:admit-term
       &resolution-input-language "Problem" (test:source-problem)))
   True)
!(assertEqual (langdef:close-language &clause-data-language) True)
!(assertEqual (langdef:close-language &resolution-input-language) True)

!(println! (ClauseDataToResolutionInputV1Summary 8 8 0))
