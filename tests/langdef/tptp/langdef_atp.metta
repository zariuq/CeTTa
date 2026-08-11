!(import! &self langdef)
!(import! &self ../../../langdef/tptp/runtime.metta)

(= (test:tptp-info-valid $info)
   (case $info
     (((LangDef.Info TptpFofCnfV1
         $source $compiler $environment $pack $manifest) True)
      ($_ False))))

(= (test:tptp-rejected $parsed)
   (case $parsed
     (((LangDef.ParseRejected TptpFofCnfV1
         $pack 26 $expected) True)
      ($_ False))))

(= (test:tptp-imported $result)
   (case $result
     (((LangDef.Imported TptpFofCnfV1 $pack 1) True)
      ($_ False))))

(= (test:tptp-import-rejected $result)
   (case $result
     (((LangDef.ImportRejected TptpFofCnfV1 $pack
         (TPTP.Unsupported ground-cnf-subset)) True)
      ($_ False))))

(= (test:tptp-prove
      (TPTP.ATPReady (TPTP.ATPInput $usable $support)))
   (atp:resolution-search:prove
     $usable $support
     (ATP.GivenClauseBounds 20 100)
     (ATP.AgeWeight 5 1)))

(= (test:tptp-refutation-replays $result)
   (case $result
     (((ATP.ResolutionSearchResult
         (ATP.Refutation $proof) $stats)
       (atp:resolution-search:replay $proof))
      ($_ False))))

(= (test:tptp-incomplete $result)
   (case $result
     (((ATP.ResolutionSearchResult
         (ATP.Incomplete $reason) $stats) True)
      ($_ False))))

!(bind! &tptp-langdef (langdef:load "langdef/tptp/langdef.metta"))
!(assertEqual
   (test:tptp-info-valid (langdef:info &tptp-langdef))
   True)

!(bind! &tptp-import
   (langdef:import-file &tptp-langdef
     "tests/langdef/tptp/ground_refutation.p"))
!(assertEqual (test:tptp-imported &tptp-import) True)
!(bind! &tptp-atp-input
   (match &self
     (TPTP.ATPInput $usable $support)
     (TPTP.ATPReady (TPTP.ATPInput $usable $support))))

!(assertEqual
   &tptp-atp-input
   (TPTP.ATPReady
     (TPTP.ATPInput
       ((ATP.NamedClause disjunction
          ((ATP.Pos (p a)) (ATP.Pos (q a))))
        (ATP.NamedClause not_p ((ATP.Neg (p a)))))
       ((ATP.NamedClause not_q ((ATP.Neg (q a))))))))

!(bind! &tptp-proof (test:tptp-prove &tptp-atp-input))
!(assertEqual (test:tptp-refutation-replays &tptp-proof) True)

!(bind! &tptp-satisfiable-input
   (tptp:parse->atp
     (langdef:parse-file &tptp-langdef
       "tests/langdef/tptp/ground_satisfiable.p")))
!(bind! &tptp-satisfiable-search
   (test:tptp-prove &tptp-satisfiable-input))
!(assertEqual (test:tptp-incomplete &tptp-satisfiable-search) True)

!(bind! &tptp-before-rejected-import (count-atoms &self))
!(bind! &tptp-rejected-import
   (langdef:import-file &tptp-langdef
     "tests/langdef/tptp/unsupported_variable.p"))
!(assertEqual
   (test:tptp-import-rejected
     &tptp-rejected-import)
   True)
!(assertEqual (count-atoms &self) &tptp-before-rejected-import)

!(assertEqual
   (test:tptp-rejected
     (langdef:parse-file &tptp-langdef
       "tests/langdef/tptp/invalid_missing_period.p"))
   True)

!(assertEqual (langdef:close &tptp-langdef) True)
