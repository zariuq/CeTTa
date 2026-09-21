; Native CST compact records for the item-2 files (default path: no EBNF forest).
; The shell gate compares these printed atoms byte-for-byte against the
; independently executed authored dump.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self ../../../langdef/tptp/official_prepared_reader_v1.metta)
!(import! &self system)

(= (diff:emit $tag $records)
   (println! (TptpDump $tag $records)))
(= (diff:run $reader)
   (let $_ (diff:emit empty (tptp:read-text $reader ""))
     (let $_ (diff:emit fof-atom (tptp:read-text $reader "fof(a,axiom,p)."))
       (let $_ (diff:emit quoted (tptp:read-text $reader "fof('X',axiom,p(X,'X'))."))
         (let $_ (diff:emit lexical-kinds
                   (tptp:read-text $reader
                     "fof(42,axiom,p(42,1/3,-2.5,\"distinct value\"))."))
           (let $_ (diff:emit cnf-or (tptp:read-text $reader "cnf(c,axiom,(~p(X) | q(X)))."))
           (let $_ (diff:emit qualified
                     (tptp:read-text $reader
                       "include('Axioms/A.ax',[a],'space')."))
             (let $_ (diff:emit puz001-1 (tptp:read-file $reader (system:arg 1)))
               (let $_ (diff:emit puz001+1 (tptp:read-file $reader (system:arg 2)))
                 (let $_ (diff:emit syn000_1 (tptp:read-file $reader (system:arg 3)))
                   (let $_ (diff:emit puz140 (tptp:read-file $reader (system:arg 4)))
                     (let $_ (diff:emit syn000_4 (tptp:read-file $reader (system:arg 5)))
                       (let $_ (diff:emit families (tptp:read-file $reader (system:arg 6)))
                         (println! TptpNativeDumpDone))))))))))))))
(= (gate:use $parser)
   (case $parser
     (((NativeHandle "cetta.tptp-reader.v1" $_id)
        (diff:run $parser))
      ((EBNF:Parser $handle $receipt)
        (let $_ (diff:run (EBNF:Parser $handle $receipt))
          (bnf:close-ebnf-parser (EBNF:Parser $handle $receipt))))
      ($fault (println! (TptpDiffParserFailure $fault))))))
!(gate:use (tptp:official-reader (system:arg 0)))
