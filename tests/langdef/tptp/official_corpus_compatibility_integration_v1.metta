!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self ../../../langdef/tptp/official_prepared_reader_v1.metta)
!(import! &self system)

(= (compat-integration:classification $outcome)
   (case $outcome
     (((EBNF:Parse (EBNF:Accepted $_binding $_source $_trees) $_receipt)
        accepted)
      ((EBNF:Parse
         (EBNF:Rejected $_binding $_source $_offset $_expected) $_receipt)
        rejected)
      ($_ unexpected))))
(= (compat-integration:parser
     (bnf-v1:grammar-input $document $authority))
   (bnf:parser-from-ebnf-document $document $authority))
(= (compat-integration:parser
     (TPTP:CorpusCompatibleGrammarV1 $_source $input))
   (compat-integration:parser $input))
(= (compat-integration:use $strict $compat $_strict-input $_compat-input)
   (let $strict-ordinary
        (compat-integration:classification
          (bnf:parse-ebnf-text $strict "tff(ok,axiom,p)."))
     (let $compat-ordinary
          (compat-integration:classification
            (bnf:parse-ebnf-text $compat "tff(ok,axiom,p)."))
       (let $strict-extension
            (compat-integration:classification
              (bnf:parse-ebnf-text
                $strict "tff(compat,axiom,(~ p != ~ q))."))
         (let $compat-extension
              (compat-integration:classification
                (bnf:parse-ebnf-text
                  $compat "tff(compat,axiom,(~ p != ~ q))."))
           (let $strict-introduced
                (compat-integration:classification
                  (bnf:parse-ebnf-text $strict
                    "fof(legacy,axiom,p,introduced(assumption,[old]))."))
             (let $compat-introduced
                  (compat-integration:classification
                    (bnf:parse-ebnf-text $compat
                      "fof(legacy,axiom,p,introduced(assumption,[old]))."))
               (let $strict-nhf
                    (compat-integration:classification
                      (bnf:parse-ebnf-text $strict
                        "thf(legacy,axiom,{$necessary(agent)} @ p)."))
                 (let $compat-nhf
                      (compat-integration:classification
                        (bnf:parse-ebnf-text $compat
                          "thf(legacy,axiom,{$necessary(agent)} @ p)."))
                   (let $strict-tcf
                        (compat-integration:classification
                          (bnf:parse-ebnf-text $strict
                            "tcf(legacy,axiom,((a = b) | (a != b)))."))
                     (let $compat-tcf
                          (compat-integration:classification
                            (bnf:parse-ebnf-text $compat
                              "tcf(legacy,axiom,((a = b) | (a != b)))."))
                       (let $strict-malformed
                            (compat-integration:classification
                              (bnf:parse-ebnf-text
                                $strict "tff(bad,axiom,(~ p != ))."))
                         (let $compat-malformed
                              (compat-integration:classification
                                (bnf:parse-ebnf-text
                                  $compat "tff(bad,axiom,(~ p != ))."))
                           (let $_ (bnf:close-ebnf-parser $strict)
                             (let $_ (bnf:close-ebnf-parser $compat)
                               (TptpOfficialCorpusCompatibilityIntegrationV1Summary
                                 $strict-ordinary $compat-ordinary
                                 $strict-extension $compat-extension
                                 $strict-introduced $compat-introduced
                                 $strict-nhf $compat-nhf
                                 $strict-tcf $compat-tcf
                                 $strict-malformed $compat-malformed))))))))))))))))
!(let ($source $document)
      (tptp-prep:document
        (tptp:extended-bnf-file->ast (system:arg 0)))
   (let $strict-input (tptp-prep:compose $source $document)
     (let $compat-input
          (tptp-prep:compose-corpus-compatible $source $document)
       (let $strict (compat-integration:parser $strict-input)
         (let $compat (compat-integration:parser $compat-input)
           (compat-integration:use
             $strict $compat $strict-input $compat-input))))))
