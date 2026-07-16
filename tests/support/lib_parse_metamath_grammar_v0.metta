!(import! &self gparse)
!(import! &self fs)
!(import! &self str)
!(import! &self text_stream)

; Shared bounded Metamath grammar-data fixture for lib_parse tests.
; Labels/symbols/proof words are lexical-class leaves; statement structure is grammar data.
; Raw-text support below adds only a thin declared L0 lexer plus db-level summary helpers.

(= (mmlabel $x) (Lex mm-label $x))
(= (mmsym $x) (Lex mm-sym $x))
(= (mmproof $x) (Lex mm-proof-label $x))
(= (mmcstep $x) (Lex mm-cstep $x))

(= (mm-g) (mkGram (
  (LexD mm-label label_tok)
  (LexD mm-sym sym_tok)
  (LexD mm-proof-label proof_tok)
  (LexD mm-cstep cstep_tok)

  (Prod sl1 sym_list ((Hl sym_tok)))
  (Prod sl2 sym_list ((Hl sym_tok)(Hl sym_list)))
  (Prod pl1 proof_list ((Hl proof_tok)))
  (Prod pl2 proof_list ((Hl proof_tok)(Hl proof_list)))
  (Prod ph0 proof_hdr_list ())
  (Prod ph2 proof_hdr_list ((Hl proof_tok)(Hl proof_hdr_list)))
  (Prod cl1 cstep_list ((Hl cstep_tok)))
  (Prod cl2 cstep_list ((Hl cstep_tok)(Hl cstep_list)))

  (Prod st_c stmt ((Tm MM_C)(Hl sym_list)(Tm MM_DOT)))
  (Prod st_v stmt ((Tm MM_V)(Hl sym_list)(Tm MM_DOT)))
  (Prod st_d stmt ((Tm MM_D)(Hl sym_list)(Tm MM_DOT)))
  (Prod st_f stmt ((Hl label_tok)(Tm MM_F)(Hl sym_tok)(Hl sym_tok)(Tm MM_DOT)))
  (Prod st_e stmt ((Hl label_tok)(Tm MM_E)(Hl sym_list)(Tm MM_DOT)))
  (Prod st_a stmt ((Hl label_tok)(Tm MM_A)(Hl sym_list)(Tm MM_DOT)))
  (Prod st_pn stmt ((Hl label_tok)(Tm MM_P)(Hl sym_list)(Tm MM_EQPROOF)(Hl proof_list)(Tm MM_DOT)))
  (Prod st_pc stmt ((Hl label_tok)(Tm MM_P)(Hl sym_list)(Tm MM_EQPROOF)(Tm MM_LP)(Hl proof_hdr_list)(Tm MM_RP)(Hl cstep_list)(Tm MM_DOT))) )))

(= (mm-db-g) (mkGram (
  (LexD mm-label label_tok)
  (LexD mm-sym sym_tok)
  (LexD mm-proof-label proof_tok)
  (LexD mm-cstep cstep_tok)

  (Prod sl1 sym_list ((Hl sym_tok)))
  (Prod sl2 sym_list ((Hl sym_tok)(Hl sym_list)))
  (Prod pl1 proof_list ((Hl proof_tok)))
  (Prod pl2 proof_list ((Hl proof_tok)(Hl proof_list)))
  (Prod ph0 proof_hdr_list ())
  (Prod ph2 proof_hdr_list ((Hl proof_tok)(Hl proof_hdr_list)))
  (Prod cl1 cstep_list ((Hl cstep_tok)))
  (Prod cl2 cstep_list ((Hl cstep_tok)(Hl cstep_list)))

  (Prod st_c stmt ((Tm MM_C)(Hl sym_list)))
  (Prod st_v stmt ((Tm MM_V)(Hl sym_list)))
  (Prod st_d stmt ((Tm MM_D)(Hl sym_list)))
  (Prod st_f stmt ((Hl label_tok)(Tm MM_F)(Hl sym_tok)(Hl sym_tok)))
  (Prod st_e stmt ((Hl label_tok)(Tm MM_E)(Hl sym_list)))
  (Prod st_a stmt ((Hl label_tok)(Tm MM_A)(Hl sym_list)))
  (Prod st_pn stmt ((Hl label_tok)(Tm MM_P)(Hl sym_list)(Tm MM_EQPROOF)(Hl proof_list)))
  (Prod st_pc stmt ((Hl label_tok)(Tm MM_P)(Hl sym_list)(Tm MM_EQPROOF)(Tm MM_LP)(Hl proof_hdr_list)(Tm MM_RP)(Hl cstep_list)))

  (Prod db1 db ((Hl stmt)(Tm MM_DOT)))
  (Prod db2 db ((Hl stmt)(Tm MM_DOT)(Hl db)))
  (Prod dbb1 db ((Tm LP)(Hl db)(Tm RP)))
  (Prod dbb2 db ((Tm LP)(Hl db)(Tm RP)(Hl db))) )))

(= (mm-cdecl-toks)
   (Cons MM_C
     (Cons (mmsym wff)
       (Cons (mmsym turn)
         (Cons (mmsym impl)
           (Cons MM_DOT Nil))))))

(= (mm-cdecl-demo0-toks)
   (Cons MM_C
     (Cons (mmsym "0")
       (Cons (mmsym "+")
         (Cons (mmsym "=")
             (Cons (mmsym "->")
               (Cons (mmsym "(")
                 (Cons (mmsym ")")
                 (Cons (mmsym "term")
                   (Cons (mmsym "wff")
                     (Cons (mmsym "|-")
                       (Cons MM_DOT Nil))))))))))))

(= (mm-vdecl-toks)
   (Cons MM_V
     (Cons (mmsym ph)
       (Cons (mmsym ps)
         (Cons MM_DOT Nil)))))

(= (mm-vdecl-demo0-toks)
   (Cons MM_V
     (Cons (mmsym "t")
       (Cons (mmsym "r")
         (Cons (mmsym "s")
           (Cons (mmsym "P")
             (Cons (mmsym "Q")
               (Cons MM_DOT Nil))))))))

(= (mm-vdecl-raw-toks)
   (Cons MM_V
     (Cons (mmsym "ph")
       (Cons (mmsym "ps")
         (Cons MM_DOT Nil)))))

(= (mm-float-toks)
   (Cons (mmlabel wph)
     (Cons MM_F
       (Cons (mmsym wff)
         (Cons (mmsym ph)
           (Cons MM_DOT Nil))))))

(= (mm-assert-toks)
   (Cons (mmlabel ax-1)
     (Cons MM_A
       (Cons (mmsym turn)
         (Cons (mmsym ph)
           (Cons (mmsym impl)
             (Cons (mmsym ph)
               (Cons MM_DOT Nil))))))))

(= (mm-normal-thm-toks)
   (Cons (mmlabel id)
     (Cons MM_P
       (Cons (mmsym turn)
         (Cons (mmsym ph)
           (Cons MM_EQPROOF
             (Cons (mmproof wph)
               (Cons (mmproof ax-1)
                 (Cons MM_DOT Nil)))))))))

(= (mm-compressed-thm-toks)
   (Cons (mmlabel id2)
     (Cons MM_P
       (Cons (mmsym turn)
         (Cons (mmsym ph)
           (Cons MM_EQPROOF
             (Cons MM_LP
               (Cons (mmproof wph)
                 (Cons (mmproof ax-1)
                   (Cons MM_RP
                     (Cons (mmcstep ABZ)
                       (Cons MM_DOT Nil))))))))))))

(= (mm-empty-compressed-thm-toks)
   (Cons (mmlabel id0)
     (Cons MM_P
       (Cons (mmsym turn)
         (Cons (mmsym ph)
           (Cons MM_EQPROOF
             (Cons MM_LP
               (Cons MM_RP
                 (Cons (mmcstep ABZ)
                   (Cons MM_DOT Nil))))))))))

(= (mm-dv-toks)
   (Cons MM_D
     (Cons (mmsym ph)
       (Cons (mmsym ps)
         (Cons MM_DOT Nil)))))

(= (mm-cdecl-raw)
   "$c 0 + = -> ( ) term wff |- $.")

(= (mm-vdecl-raw)
   "$v ph ps $.")

(= (mm-float-raw)
   "wph $f wff ph $.")

(= (mm-dv-raw)
   "$d ph ps $.")

(= (mm-normal-thm-raw)
   "id $p |- ph $= wph ax-1 $.")

(= (mm-compressed-thm-raw)
   "id2 $p |- ph $= ( wph ax-1 ) ABZ $.")

(= (mm-empty-compressed-thm-raw)
   "id0 $p |- ph $= ( ) ABZ $.")

(= (mm-a1-raw)
   "a1 $a |- ( t = r -> ( t = s -> r = s ) ) $.")

(= (mm-demo0-theorem-stmt-raw)
   "th1 $p |- t = t $= tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1 mp mp $.")

(= (mm-demo0-theorem-noblock-stmt-raw)
   "th1 $p |- t = t $= tt tze tpl tt weq tt tt weq tt a2 tt tze tpl tt weq tt tze tpl tt weq tt tt weq wim tt a2 tt tze tpl tt tt a1 $.")

(= (mm-bad-compressed-raw)
   "id2 $p |- ph $= ( wph ax-1 ) $.")

(= (mm-bad-compressed-lex-raw)
   "id2 $p |- ph $= ( wph ax-1 ABZ $.")

(= (mm-mini-db-path)
   "tests/support/metamath_mini_v0.mm")

(= (mm-mini-db-summary)
   (MMDbSummary 10 "const:zero,const:term,const:wff,const:turn,var:t,var:P,hyp:tt,hyp:wp,assert:tze,assert:axp"))

(= (mm-mini-thm-path)
   "tests/support/metamath_mini_thm_v0.mm")

(= (mm-mini-thm-summary)
   (MMDbSummary 11 "const:zero,const:term,const:wff,const:turn,var:t,var:P,hyp:tt,hyp:wp,assert:tze,assert:axp,assert:id"))

(= (mm-mini-thm-compressed-path)
   "tests/support/metamath_mini_thm_compressed_v0.mm")

(= (mm-mini-thm-compressed-summary)
   (MMDbSummary 10 "const:wff,const:lp,const:rp,const:impl,var:ph,var:ps,hyp:wph,hyp:wps,assert:wi,assert:th1"))

(= (mm-mmtest-toplevel-e-path)
   "../metamath/metamath-test/tests/unit/test67_toplevel_essential.mm")

(= (mm-mmtest-toplevel-e-summary)
   (MMDbSummary 8 "const:wff,const:turn,var:ph,var:ps,hyp:wp,hyp:wq,hyp:hyp1,assert:ax1"))

(= (mm-mmtest-d-before-float-path)
   "../metamath/metamath-test/tests/unit/test70_djvars_before_float.mm")

(= (mm-mmtest-d-before-float-summary)
   (MMDbSummary 7 "const:wff,const:turn,var:x,var:y,hyp:hx,hyp:hy,assert:a1"))

(= (mm-mmtest-compressed-z-path)
   "../metamath/metamath-test/tests/unit/test57_compressed_z_minimal.mm")

(= (mm-mmtest-compressed-z-summary)
   (MMDbSummary 10 "const:wff,const:lp,const:rp,const:impl,var:ph,var:ps,hyp:wph,hyp:wps,assert:wi,assert:th1"))

(= (mm-mmtest-min-found-path)
   "../metamath/metamath-test/tests/core/small/min_found.mm")

(= (mm-mmtest-min-found-summary)
   (MMDbSummary 17 "const:A,const:B,const:C,const:D,assert:ax-1,assert:ax-2,assert:ax-3,hyp:in1.1,assert:in1,hyp:in2.1,hyp:in2.2,hyp:in2.3,hyp:in2.4,hyp:in2.5,assert:in2,assert:th1,assert:th2"))

(= (mm-repeat-vars-path)
   "tests/support/metamath_repeat_vars_v0.mm")

(= (mm-repeat-vars-summary)
   (MMDbSummary 7 "const:wff,const:turn,var:A,var:B,hyp:wa,hyp:wb,assert:ax"))

(= (mm-core-anatomy-path)
   "../metamath/metamath-test/tests/core/small/anatomy.mm")

(= (mm-core-anatomy-summary)
   (MMDbSummary 14 "const:lp,const:rp,const:impl,const:wff,var:p,var:q,var:r,var:s,hyp:wp,hyp:wq,hyp:wr,hyp:ws,assert:w2,assert:wnew"))

(= (mm-core-local-var-good-path)
   "../metamath/metamath-test/tests/core/small/local-var-good.mm")

(= (mm-core-local-var-good-summary)
   (MMDbSummary 8 "const:formula,const:A.,const:setvar,var:ph,hyp:wph,var:x,hyp:vx.wal,assert:wal"))

(= (mm-mmverify-nested-scope-path)
   "../metamath/metamath-test/tests/mmverify/test_nested_scope.mm")

(= (mm-mmverify-nested-scope-summary)
   (MMDbSummary 13 "const:term,const:turn,const:|=,var:R,var:S,var:T,hyp:tr,hyp:ts,hyp:tt,hyp:outer.1,hyp:inner.1,assert:inner-thm,assert:outer-thm"))

(= (mm-mmverify-dv-scope-path)
   "../metamath/metamath-test/tests/mmverify/test_dv_scope.mm")

(= (mm-mmverify-dv-scope-summary)
   (MMDbSummary 12 "const:wff,const:two,var:ph,var:ps,var:a,var:b,hyp:vph,hyp:vps,hyp:va,hyp:vb,assert:ax-two,assert:test_scope_good"))

(= (mm-mmverify-dv-multivar-path)
   "../metamath/metamath-test/tests/mmverify/test_dv_multivar.mm")

(= (mm-mmverify-dv-multivar-summary)
   (MMDbSummary 16 "const:wff,const:three,var:ph,var:ps,var:ch,var:p,var:y,var:z,hyp:vph,hyp:vps,hyp:vch,hyp:vp,hyp:vy,hyp:vz,assert:ax-3way,assert:test_multivar_good"))

(= (mm-demo0-defs-path)
   "tests/support/metamath_demo0_defs_v0.mm")

(= (mm-demo0-defs-summary)
   (MMDbSummary 23 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:tze,assert:tpl,assert:weq,assert:wim"))

(= (mm-demo0-axioms-path)
   "tests/support/metamath_demo0_axioms_v0.mm")

(= (mm-demo0-axioms-summary)
   (MMDbSummary 25 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:tze,assert:tpl,assert:weq,assert:wim,assert:a1,assert:a2"))

(= (mm-demo0-defs-block-pair-path)
   "tests/support/metamath_demo0_defs_block_pair_v0.mm")

(= (mm-demo0-defs-block-pair-summary)
   (MMDbSummary 25 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:tze,assert:tpl,assert:weq,assert:wim,hyp:min,hyp:maj"))

(= (mm-demo0-defs-a1-block-pair-path)
   "tests/support/metamath_demo0_defs_a1_block_pair_v0.mm")

(= (mm-demo0-defs-a1-block-pair-summary)
   (MMDbSummary 26 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:tze,assert:tpl,assert:weq,assert:wim,assert:a1,hyp:min,hyp:maj"))

(= (mm-demo0-defs-a1-block-triplet-path)
   "tests/support/metamath_demo0_defs_a1_block_triplet_v0.mm")

(= (mm-demo0-defs-a1-block-triplet-summary)
   (MMDbSummary 27 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:tze,assert:tpl,assert:weq,assert:wim,assert:a1,hyp:min,hyp:maj,assert:mp"))

(= (mm-demo0-block-path)
   "tests/support/metamath_demo0_block_v0.mm")

(= (mm-demo0-block-summary)
   (MMDbSummary 28 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:tze,assert:tpl,assert:weq,assert:wim,assert:a1,assert:a2,hyp:min,hyp:maj,assert:mp"))

(= (mm-a1-block-pair-path)
   "tests/support/metamath_a1_block_pair_v0.mm")

(= (mm-a1-block-pair-summary)
   (MMDbSummary 22 "const:zero,const:plus,const:eq,const:impl,const:lp,const:rp,const:term,const:wff,const:turn,var:t,var:r,var:s,var:P,var:Q,hyp:tt,hyp:tr,hyp:ts,hyp:wp,hyp:wq,assert:a1,hyp:min,hyp:maj"))

(= (mm-demo0-a1-path)
   "tests/support/metamath_demo0_a1_v0.mm")

(= (mm-bad-compressed-toks)
   (Cons (mmlabel id2)
     (Cons MM_P
       (Cons (mmsym turn)
         (Cons (mmsym ph)
           (Cons MM_EQPROOF
             (Cons MM_LP
               (Cons (mmproof wph)
                 (Cons (mmproof ax-1)
                   (Cons (mmcstep ABZ)
                     (Cons MM_DOT Nil)))))))))))

(= (mm-float-raw-toks)
   (Cons (mmlabel "wph")
     (Cons MM_F
       (Cons (mmsym "wff")
         (Cons (mmsym "ph")
           (Cons MM_DOT Nil))))))

(= (mm-dv-raw-toks)
   (Cons MM_D
     (Cons (mmsym "ph")
       (Cons (mmsym "ps")
         (Cons MM_DOT Nil)))))

(= (mm-normal-thm-raw-toks)
   (Cons (mmlabel "id")
     (Cons MM_P
       (Cons (mmsym "|-")
         (Cons (mmsym "ph")
           (Cons MM_EQPROOF
             (Cons (mmproof "wph")
               (Cons (mmproof "ax-1")
                 (Cons MM_DOT Nil)))))))))

(= (mm-compressed-thm-raw-toks)
   (Cons (mmlabel "id2")
     (Cons MM_P
       (Cons (mmsym "|-")
         (Cons (mmsym "ph")
           (Cons MM_EQPROOF
             (Cons MM_LP
               (Cons (mmproof "wph")
                 (Cons (mmproof "ax-1")
                   (Cons MM_RP
                     (Cons (mmcstep "ABZ")
                       (Cons MM_DOT Nil))))))))))))

(= (mm-empty-compressed-thm-raw-toks)
   (Cons (mmlabel "id0")
     (Cons MM_P
       (Cons (mmsym "|-")
         (Cons (mmsym "ph")
           (Cons MM_EQPROOF
             (Cons MM_LP
               (Cons MM_RP
                 (Cons (mmcstep "ABZ")
                   (Cons MM_DOT Nil))))))))))

(= (mm-a1-raw-toks)
   (Cons (mmlabel "a1")
     (Cons MM_A
       (Cons (mmsym "|-")
         (Cons (mmsym "(")
           (Cons (mmsym "t")
             (Cons (mmsym "=")
               (Cons (mmsym "r")
                 (Cons (mmsym "->")
                   (Cons (mmsym "(")
                     (Cons (mmsym "t")
                       (Cons (mmsym "=")
                         (Cons (mmsym "s")
                           (Cons (mmsym "->")
                             (Cons (mmsym "r")
                               (Cons (mmsym "=")
                                 (Cons (mmsym "s")
                                   (Cons (mmsym ")")
                                     (Cons (mmsym ")")
                                       (Cons MM_DOT Nil))))))))))))))))))))

; --- thin declared L0 lexer for raw .mm text ---

(= (mm-control-like $tok)
   (if (== $tok "$c") True
   (if (== $tok "$v") True
   (if (== $tok "$d") True
   (if (== $tok "$f") True
   (if (== $tok "$e") True
   (if (== $tok "$a") True
   (if (== $tok "$p") True
   (if (== $tok "$=") True
   (if (== $tok "$.") True
   (if (== $tok "${") True
(if (== $tok "$}") True
(if (== $tok "$[") True
(if (== $tok "$]") True
(if (== $tok "$(") True
(if (== $tok "$)") True
       False))))))))))))))))

(= (mm-raw-token $stream)
   (let $res (text-stream:read-token $stream)
     (case $res
       ((Empty Empty)
        ((TokenResult $tok $next) (mm-raw-token2 $tok $next))
        ($o Empty)))))

(= (mm-raw-token2 $tok $next)
   (if (== $tok "$(")
       (mm-skip-comment $next)
       (TokenResult $tok $next)))

(= (mm-skip-comment $stream)
   (case (text-stream:read-until $stream "$)")
     ((Empty Empty)
      ((ReadUntilResult $body $next) (mm-raw-token (text-stream:advance $next 2)))
      ($o Empty))))

(= (mm-word-label $raw) (Lex mm-label $raw))
(= (mm-word-sym $raw) (Lex mm-sym $raw))
(= (mm-word-proof $raw) (Lex mm-proof-label $raw))
(= (mm-word-cstep $raw) (Lex mm-cstep $raw))

(= (mm-emit $tok $next $state)
   (MMEmit $tok $next $state))

(= (mm-start-token $tok $next)
   (if (== $tok "$c") (mm-emit MM_C $next Syms)
   (if (== $tok "$v") (mm-emit MM_V $next Syms)
   (if (== $tok "$d") (mm-emit MM_D $next Syms)
   (if (== $tok "${") (mm-emit LP $next Start)
   (if (== $tok "$}") (mm-emit RP $next Start)
(if (== $tok "$[") (Rejected (mm-lex unsupported-include-open))
(if (== $tok "$]") (Rejected (mm-lex unsupported-include-close))
   (if (mm-control-like $tok)
       (Rejected (mm-lex bad-start-token $tok))
       (mm-emit (mm-word-label $tok) $next AfterLabel))))))))))

(= (mm-after-label-token $tok $next)
   (if (== $tok "$f") (mm-emit MM_F $next Syms)
   (if (== $tok "$e") (mm-emit MM_E $next Syms)
(if (== $tok "$a") (mm-emit MM_A $next Syms)
   (if (== $tok "$p") (mm-emit MM_P $next Theorem)
       (Rejected (mm-lex bad-label-continuation $tok)))))))

(= (mm-sym-token $tok $next)
   (if (== $tok "$.")
       (mm-emit MM_DOT $next Start)
       (if (mm-control-like $tok)
           (Rejected (mm-lex bad-sym-token $tok))
           (mm-emit (mm-word-sym $tok) $next Syms))))

(= (mm-theorem-token $tok $next)
   (if (== $tok "$=")
       (mm-emit MM_EQPROOF $next ProofStart)
       (if (mm-control-like $tok)
           (Rejected (mm-lex bad-theorem-token $tok))
           (mm-emit (mm-word-sym $tok) $next Theorem))))

(= (mm-proof-start-token $tok $next)
   (if (== $tok "(")
       (mm-emit MM_LP $next ProofHdr)
       (if (mm-control-like $tok)
           (Rejected (mm-lex bad-proof-start-token $tok))
           (mm-emit (mm-word-proof $tok) $next ProofNorm))))

(= (mm-proof-norm-token $tok $next)
   (if (== $tok "$.")
       (mm-emit MM_DOT $next Start)
       (if (== $tok "(")
           (Rejected (mm-lex bad-proof-token $tok))
       (if (== $tok ")")
           (Rejected (mm-lex bad-proof-token $tok))
       (if (mm-control-like $tok)
           (Rejected (mm-lex bad-proof-token $tok))
           (mm-emit (mm-word-proof $tok) $next ProofNorm))))))

(= (mm-proof-hdr-token $tok $next)
   (if (== $tok ")")
       (mm-emit MM_RP $next ProofBody)
       (if (== $tok "(")
           (Rejected (mm-lex bad-proof-header-token $tok))
       (if (mm-control-like $tok)
           (Rejected (mm-lex bad-proof-header-token $tok))
           (mm-emit (mm-word-proof $tok) $next ProofHdr)))))

(= (mm-proof-body-token $tok $next)
   (if (== $tok "$.")
       (mm-emit MM_DOT $next Start)
       (if (== $tok "(")
           (Rejected (mm-lex bad-compressed-token $tok))
       (if (== $tok ")")
           (Rejected (mm-lex bad-compressed-token $tok))
       (if (mm-control-like $tok)
           (Rejected (mm-lex bad-compressed-token $tok))
           (mm-emit (mm-word-cstep $tok) $next ProofBody))))))

(= (mm-lex-finish Start $rev)
   (Ok (rev $rev)))
(= (mm-lex-finish AfterLabel $rev)
   (Rejected (mm-lex eof-after-label)))
(= (mm-lex-finish Syms $rev)
   (Rejected (mm-lex eof-in-syms)))
(= (mm-lex-finish Theorem $rev)
   (Rejected (mm-lex eof-in-theorem)))
(= (mm-lex-finish ProofStart $rev)
   (Rejected (mm-lex eof-in-proof-start)))
(= (mm-lex-finish ProofNorm $rev)
   (Rejected (mm-lex eof-in-proof)))
(= (mm-lex-finish ProofHdr $rev)
   (Rejected (mm-lex eof-in-proof-header)))
(= (mm-lex-finish ProofBody $rev)
   (Rejected (mm-lex eof-in-compressed-proof)))

(= (mm-lex-step Start $tok $next)
   (mm-start-token $tok $next))
(= (mm-lex-step AfterLabel $tok $next)
   (mm-after-label-token $tok $next))
(= (mm-lex-step Syms $tok $next)
   (mm-sym-token $tok $next))
(= (mm-lex-step Theorem $tok $next)
   (mm-theorem-token $tok $next))
(= (mm-lex-step ProofStart $tok $next)
   (mm-proof-start-token $tok $next))
(= (mm-lex-step ProofNorm $tok $next)
   (mm-proof-norm-token $tok $next))
(= (mm-lex-step ProofHdr $tok $next)
   (mm-proof-hdr-token $tok $next))
(= (mm-lex-step ProofBody $tok $next)
   (mm-proof-body-token $tok $next))

(= (mm-lex-go $stream $state $rev)
   (let $res (mm-raw-token $stream)
     (case $res
       ((Empty (mm-lex-finish $state $rev))
        ((TokenResult $tok $next)
         (case (mm-lex-step $state $tok $next)
           (((MMEmit $out $next2 $state2)
             (mm-lex-go $next2 $state2 (Cons $out $rev)))
            ($o $o))))
        ($o (Rejected (mm-lex bad-state $state)))))))

(= (mm-lex-string $text)
   (mm-lex-go (text-stream:new $text) Start Nil))

(= (mm-lex-file $path)
   (case (fs:read-text $path)
      (((Error $call $why) (Rejected (mm-lex fs-read $path $why)))
       ($text (mm-lex-string $text)))))

(= (mm-grammar-source-path)
   (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta"))

(= (mm-generalized-class $def $start $toks)
   (case (gparse:parse-class
           (mm-grammar-source-path) $def $start $toks)
     ((Unique Unique)
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o BadParse))))

(= (mm-parse-stmt-class $text)
   (case (mm-lex-string $text)
     (((Ok $toks) (mm-generalized-class mm-g stmt $toks))
      ($o LexError))))

(= (mm-parse-stmt $text)
   (case (mm-lex-string $text)
     (((Ok $toks) (lp-parse (mm-g) stmt $toks))
      ($o (Rejected bad-lex)))))

(= (mm-stmt-lex-count $text)
   (case (mm-lex-string $text)
     (((Ok $toks) (mm-count $toks))
      ($o $o))))

(= (mm-stmt-prefix-class $text $count)
   (case (mm-lex-string $text)
     (((Ok $toks)
       (mm-generalized-class mm-g stmt (mm-take $count $toks)))
      ($o LexError))))

; --- db parsing + bounded summary for raw demo files ---

(= (mm-db-parse-class $path)
   (case (mm-lex-file $path)
     (((Ok $toks) (mm-generalized-class mm-db-g db $toks))
      ($o LexError))))

(= (mm-db-parse-class-text $text)
   (case (mm-lex-string $text)
     (((Ok $toks) (mm-generalized-class mm-db-g db $toks))
      ($o LexError))))

(= (mm-db-lex-count-text $text)
   (case (mm-lex-string $text)
     (((Ok $toks) (mm-count $toks))
      ($o $o))))

(= (mm-db-parse-raw $path)
   (case (mm-lex-file $path)
     (((Ok $toks) (lp-parse-raw (mm-db-g) db $toks))
      ($o (Rejected bad-lex)))))

(= (mm-db-parse $path)
   (case (mm-lex-file $path)
     (((Ok $toks) (lp-parse (mm-db-g) db $toks))
      ($o (Rejected bad-lex)))))

(= (mm-db-lex-count $path)
   (case (mm-lex-file $path)
     (((Ok $toks) (mm-count $toks))
      ($o $o))))

(= (mm-db-prefix-class $path $count)
   (case (mm-lex-file $path)
     (((Ok $toks)
       (mm-generalized-class mm-db-g db (mm-take $count $toks)))
      ($o LexError))))

(= (mm-leaf-raw (Lf (Lex $cls $raw))) $raw)
(= (mm-sym-raws $tree)
   (case $tree
     (((Nd sl1 (Cons $x Nil))
       (Cons (mm-leaf-raw $x) Nil))
      ((Nd sl2 (Cons $x (Cons $xs Nil)))
       (Cons (mm-leaf-raw $x) (mm-sym-raws $xs)))
      ($o Nil))))

(= (mm-count $xs) (mm-count-acc $xs 0))
(= (mm-count-acc Nil $n) $n)
(= (mm-count-acc (Cons $x $xs) $n)
   (mm-count-acc $xs (+ $n 1)))

(= (mm-take $n $xs)
   (if (<= $n 0)
       Nil
       (mm-take-rev $n $xs Nil)))

(= (mm-take-rev $n Nil $rev) (rev $rev))
(= (mm-take-rev $n (Cons $x $rest) $rev)
   (if (<= $n 0)
       (rev $rev)
       (mm-take-rev (- $n 1) $rest (Cons $x $rev))))

(= (mm-canon-const $raw)
   (if (== $raw "(") "lp"
   (if (== $raw ")") "rp"
   (if (== $raw "|-") "turn"
   (if (== $raw "->") "impl"
   (if (== $raw "0") "zero"
   (if (== $raw "+") "plus"
   (if (== $raw "=") "eq"
       $raw))))))))

(= (mm-prefix-all $prefix Nil) Nil)
(= (mm-prefix-all $prefix (Cons $x $xs))
   (Cons (str:concat $prefix $x) (mm-prefix-all $prefix $xs)))

(= (mm-join-comma $xs)
   (case $xs
     ((Nil "")
      ((Cons $x Nil) $x)
      ((Cons $x $rest)
       (str:concat $x (str:concat "," (mm-join-comma $rest))))
      ($o ""))))

(= (mm-map-canon-const Nil) Nil)
(= (mm-map-canon-const (Cons $x $xs))
   (Cons (mm-canon-const $x) (mm-map-canon-const $xs)))

(= (mm-stmt-labels $stmt)
   (case $stmt
     (((Nd st_c (Cons $xs Nil))
       (mm-prefix-all "const:" (mm-map-canon-const (mm-sym-raws $xs))))
      ((Nd st_v (Cons $xs Nil))
       (mm-prefix-all "var:" (mm-sym-raws $xs)))
      ((Nd st_d (Cons $xs Nil)) Nil)
      ((Nd st_f (Cons $label $rest))
       (Cons (str:concat "hyp:" (mm-leaf-raw $label)) Nil))
      ((Nd st_e (Cons $label $rest))
       (Cons (str:concat "hyp:" (mm-leaf-raw $label)) Nil))
      ((Nd st_a (Cons $label $rest))
       (Cons (str:concat "assert:" (mm-leaf-raw $label)) Nil))
      ((Nd st_pn (Cons $label $rest))
       (Cons (str:concat "assert:" (mm-leaf-raw $label)) Nil))
      ((Nd st_pc (Cons $label $rest))
       (Cons (str:concat "assert:" (mm-leaf-raw $label)) Nil))
      ($o Nil))))

(= (mm-db-labels $tree)
   (case $tree
     (((Nd db1 (Cons $stmt Nil)) (mm-stmt-labels $stmt))
      ((Nd db2 (Cons $stmt (Cons $rest Nil)))
       (app2 (mm-stmt-labels $stmt) (mm-db-labels $rest)))
      ((Nd dbb1 (Cons $inner Nil))
       (mm-db-labels $inner))
      ((Nd dbb2 (Cons $inner (Cons $rest Nil)))
       (app2 (mm-db-labels $inner) (mm-db-labels $rest)))
      ($o Nil))))

(= (mm-dedup-labels $xs)
   (mm-dedup-labels-seen Nil $xs))

(= (mm-dedup-labels-seen $seen Nil)
   (rev $seen))

(= (mm-dedup-labels-seen $seen (Cons $x $xs))
   (if (memb $x $seen)
       (mm-dedup-labels-seen $seen $xs)
       (mm-dedup-labels-seen (Cons $x $seen) $xs)))

(= (mm-db-summary-labels $tree)
   (mm-dedup-labels (mm-db-labels $tree)))

(= (mm-db-summary $path)
   (case (mm-db-parse $path)
     (((Unique $tree)
       (let $labels (mm-db-summary-labels $tree)
         (MMDbSummary (mm-count $labels) (mm-join-comma $labels))))
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o $o))))

(= (mm-db-summary-raw $path)
   (case (mm-db-parse-raw $path)
     (((Unique $tree)
       (let $labels (mm-db-summary-labels $tree)
         (MMDbSummary (mm-count $labels) (mm-join-comma $labels))))
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o $o))))

(= (mm-db-label-count-raw $path)
   (case (mm-db-parse-raw $path)
     (((Unique $tree) (mm-count (mm-db-summary-labels $tree)))
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o $o))))

(= (mm-db-label-join-raw $path)
   (case (mm-db-parse-raw $path)
     (((Unique $tree) (mm-join-comma (mm-db-summary-labels $tree)))
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o $o))))
