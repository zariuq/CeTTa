!(import! &self lib/lib_tptp.metta)
!(let (TptpPreparedPackV2 $m (EBNF:LoweredGrammar $o $policy $plain $orig))
      (tptp:read-atom (tptp:official-reader-cache-path))
   (tptp:write-atom "tests/langdef/tptp/kimi_tmp/lowered_source_v1.metta" (LoweredSource $plain $policy $orig)))
