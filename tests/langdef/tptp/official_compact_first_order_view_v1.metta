!(import! &self ../../../lib/lib_tptp.metta)

(= (fixture:first-order-text)
   "include('A.ax',[a],q).\nfof('n',axiom,(! [X] : p(X,42,1/3,-2.5,\"object\")),inference(rule,[],[q])).\n")
(= (fixture:clause-text)
   "cnf(c,axiom,(p(42)|~q(\"d\")),inference(rule,[],[])).\n")

(= (gate:fo-annotation $value)
   (case $value
     (((tptp-fo:annotation-tree
          (tptp-fo:syntax-list
            (tptp-fo:syntax-nodes-cons
              (tptp-fo:syntax-atom "annotation")
              (tptp-fo:syntax-nodes-cons
                (tptp-fo:syntax-list
                  (tptp-fo:syntax-nodes-cons
                    (tptp-fo:syntax-atom "inference") $_inference))
                $_optional))))
        True)
      ($_ False))))

(= (gate:cnf-annotation $value)
   (case $value
     (((fo-cnf:annotation-tree
          (fo-cnf:syntax-list
            (fo-cnf:syntax-nodes-cons
              (fo-cnf:syntax-atom "annotation")
              (fo-cnf:syntax-nodes-cons
                (fo-cnf:syntax-list
                  (fo-cnf:syntax-nodes-cons
                    (fo-cnf:syntax-atom "inference") $_inference))
                $_optional))))
        True)
      ($_ False))))

(= (gate:first-order $value)
   (case $value
     (((TPTP:StageValue first-order-document
          (tptp-fo:document (tptp-fo:source-digest $digest)
            (tptp-fo:inputs-cons
              (tptp-fo:include-input
                (tptp-fo:occurrence (tptp-fo:source-digest $digest) 0)
                (tptp-fo:symbol-name (tptp-fo:symbol-quoted) "A.ax")
                (tptp-fo:include-names
                  (tptp-fo:formula-names-cons
                    (tptp-fo:formula-name-atomic
                      (tptp-fo:symbol-name (tptp-fo:symbol-lower) "a"))
                    (tptp-fo:formula-names-nil)))
                (tptp-fo:formula-name-some
                  (tptp-fo:formula-name-atomic
                    (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")))
                (tptp-fo:source-span 0 23))
              (tptp-fo:inputs-cons
                (tptp-fo:formula-input
                  (tptp-fo:occurrence (tptp-fo:source-digest $digest) 1)
                  (tptp-fo:dialect-fof)
                  (tptp-fo:formula-name-atomic
                    (tptp-fo:symbol-name (tptp-fo:symbol-quoted) "n"))
                  (tptp-fo:role "axiom" (tptp-fo:annotation-none))
                  (tptp-fo:formula-forall
                    (tptp-fo:variables-cons
                      (tptp-fo:variable-id
                        (tptp-fo:occurrence
                          (tptp-fo:source-digest $digest) 1)
                        (tptp-fo:variable-name "X"))
                      (tptp-fo:variables-nil))
                    (tptp-fo:formula-atom
                      (tptp-fo:atom-predicate
                        (tptp-fo:symbol-name
                          (tptp-fo:symbol-lower) "p")
                        (tptp-fo:terms-cons
                          (tptp-fo:term-variable
                            (tptp-fo:variable-id
                              (tptp-fo:occurrence
                                (tptp-fo:source-digest $digest) 1)
                              (tptp-fo:variable-name "X")))
                          (tptp-fo:terms-cons
                            (tptp-fo:term-number
                              (tptp-fo:number-integer) "42")
                            (tptp-fo:terms-cons
                              (tptp-fo:term-number
                                (tptp-fo:number-rational) "1/3")
                              (tptp-fo:terms-cons
                                (tptp-fo:term-number
                                  (tptp-fo:number-real) "-2.5")
                                (tptp-fo:terms-cons
                                  (tptp-fo:term-distinct "\"object\"")
                                  (tptp-fo:terms-nil)))))))))
                  $annotation
                  (tptp-fo:source-span 23 98))
                (tptp-fo:inputs-nil)))))
        (TptpCompactFirstOrderValueV1
          (case (== $digest
                    "46616c06dffa8dbd3ae3ae8df9d7dcfd24f1a1f28b8d30e099421fc773dcfe60")
            ((True True) (False False)))
          (gate:fo-annotation $annotation)))
      ($_ (TptpCompactFirstOrderValueV1 False False)))))

(= (gate:clause-data $value)
   (case $value
     (((TPTP:StageValue clause-data
          (fo-cnf:problem (fo-cnf:source-digest $digest)
            (fo-cnf:clauses-cons
              (fo-cnf:clause
                (fo-cnf:occurrence (fo-cnf:source-digest $digest) 0)
                (fo-cnf:formula-name-atomic
                  (fo-cnf:symbol-name (fo-cnf:symbol-lower) "c"))
                (fo-cnf:role "axiom" (fo-cnf:annotation-none))
                (fo-cnf:literals-cons
                  (fo-cnf:literal-positive
                    (fo-cnf:atom-predicate
                      (fo-cnf:symbol-name (fo-cnf:symbol-lower) "p")
                      (fo-cnf:terms-cons
                        (fo-cnf:term-number (fo-cnf:number-integer) "42")
                        (fo-cnf:terms-nil))))
                  (fo-cnf:literals-cons
                    (fo-cnf:literal-negative
                      (fo-cnf:atom-predicate
                        (fo-cnf:symbol-name (fo-cnf:symbol-lower) "q")
                        (fo-cnf:terms-cons
                          (fo-cnf:term-distinct "\"d\"")
                          (fo-cnf:terms-nil))))
                    (fo-cnf:literals-nil)))
                $annotation
                (fo-cnf:source-span 0 52))
              (fo-cnf:clauses-nil))))
        (TptpCompactClauseDataValueV1
          (case (== $digest
                    "200cc40066c5690289974d12b5a14cdbf249d9a695cd721334f25a6b4354c897")
            ((True True) (False False)))
          (gate:cnf-annotation $annotation)))
      ($_ (TptpCompactClauseDataValueV1 False False)))))

(= (gate:malformed $value)
   (case $value
     (((TPTP:NoParse 12 TPTP_file) True)
      ($_ False))))

(= (gate:unsupported $value)
   (case $value
     (((TPTP:UnsupportedSourceDocument (not-a-source-document)) True)
      ($_ False))))

!(println!
   (TptpCompactFirstOrderViewV1Summary
     (gate:first-order
       (tptp:read-text-first-order-document (fixture:first-order-text)))
     (gate:clause-data
       (tptp:read-text-clause-data (fixture:clause-text)))
     (gate:malformed
       (tptp:read-text-first-order-document "fof(a,axiom,)."))
     (gate:unsupported
       (tptp:source-document->first-order-document
         (not-a-source-document)))))
