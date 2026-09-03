; Historical ground-only TPTP-to-Prime-search example.  The maintained TPTP
; library uses the semantic ClauseData path; this file is intentionally local
; to the ATP examples and exposes its limited ground subset explicitly.
!(import! &self langdef)
!(import! &self ./search_modules.metta)
!(import! &self ./ground_tptp_bridge.generated.metta)

(= (test:tptp-rejected $parsed)
   (case $parsed
     (((LangDef.ParseRejected TptpFofCnfV1
         $pack 26 $expected) True)
      ($_ False))))

(= (test:tptp-unsupported $result)
   (case $result
     (((TPTP.Unsupported ground-cnf-subset) True)
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

; Pass the parse result through a variable-headed equation before the bridge
; destructures it.  This keeps bind tokens out of structural left-hand sides.
(= (test:tptp-convert $parsed) (tptp:parse->atp $parsed))

!(bind! &tptp-langdef (langdef:load "langdef/tptp/langdef.metta"))

!(bind! &tptp-parsed
   (langdef:parse-file &tptp-langdef
     "tests/langdef/tptp/ground_refutation.p"))
!(bind! &tptp-atp-input (test:tptp-convert &tptp-parsed))

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
   (test:tptp-convert
     (langdef:parse-file &tptp-langdef
       "tests/langdef/tptp/ground_satisfiable.p")))
!(bind! &tptp-satisfiable-search
   (test:tptp-prove &tptp-satisfiable-input))
!(assertEqual (test:tptp-incomplete &tptp-satisfiable-search) True)

!(bind! &tptp-unsupported
   (test:tptp-convert
     (langdef:parse-file &tptp-langdef
       "tests/langdef/tptp/unsupported_variable.p")))
!(assertEqual
   (test:tptp-unsupported &tptp-unsupported)
   True)

!(assertEqual
   (test:tptp-rejected
     (langdef:parse-file &tptp-langdef
       "tests/langdef/tptp/invalid_missing_period.p"))
   True)

!(assertEqual (langdef:close &tptp-langdef) True)
