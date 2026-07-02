!(import! &self lib_parse)
!(import! &self gparse)
!(import! &self ./support/lib_parse_metamath_grammar_v0.metta)
!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)

(= (gparse-test:bad-root-label (NodeC $label $lhs $left $right $kids))
   (NodeC BAD_ROOT_LABEL $lhs $left $right $kids))

(= (gparse-test:mm-cdecl-wrong-head-toks)
   (mkL (MM_V (mmsym wff) (mmsym turn) (mmsym impl) MM_DOT)))

(= (gparse-test:rho-send-wrong-op-toks)
   (mkL (AT LBR ZERO RBR BAR LP ZERO RP)))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) (lp-shared-parse-cert (mm-g) stmt (mm-cdecl-toks) $cert))
     ($o False)))
  True)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (case (lp-shared-cert-tree (mm-g) stmt (mm-cdecl-toks) $cert)
        (((Ok $tree) (lp-tree-shared-cert (mm-g) stmt (mm-cdecl-toks) $tree))
         ((Err $e) (Err $e)))))
     ($o (Err no-unique-cert))))
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) (Ok $cert))
     ($o (Err no-unique-cert)))))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) (gparse:cert-shape $cert))
     ($o $o)))
  (GParseCertShape 4 3 2 0 0 5))

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) (lp-shared-parse-cert (mm-g) stmt (mm-cdecl-toks) $cert))
     ($o False)))
  True)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (case (lp-shared-cert-tree (mm-g) stmt (mm-cdecl-toks) $cert)
        (((Ok $tree) (lp-tree-shared-cert (mm-g) stmt (mm-cdecl-toks) $tree))
         ((Err $e) (Err $e)))))
     ($o (Err no-unique-cert))))
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) (Ok $cert))
     ($o (Err no-unique-cert)))))

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) (gparse:cert-shape $cert))
     ($o $o)))
  (GParseCertShape 4 3 2 0 0 5))

!(assertEqual
  (gparse:dual-parse-compare
    (mm-g)
    (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
    mm-g
    stmt
    (mm-cdecl-toks))
  (GParseDualUnique
    (GParseCertShape 4 3 2 0 0 5)
    (GParseCertShape 4 3 2 0 0 5)
    True
    True
    True
    True))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) $cert)
     ($o (Err no-unique-cert))))
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert) $cert)
     ($o (Err no-unique-cert)))))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (mm-cdecl-toks)
        (lp-shared-cert-corrupt-first-token $cert)))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (mm-cdecl-toks)
        (lp-shared-cert-corrupt-first-token $cert)))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (gparse-test:mm-cdecl-wrong-head-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (gparse-test:mm-cdecl-wrong-head-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (mm-cdecl-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (mm-cdecl-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        sym_list
        (mm-cdecl-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        sym_list
        (mm-cdecl-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (mm-cdecl-toks)
        (gparse-test:bad-root-label $cert)))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/lib_parse_metamath_grammar_v0.metta")
      mm-g
      stmt
      (mm-cdecl-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (mm-cdecl-toks)
        (gparse-test:bad-root-label $cert)))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (gparse-test:rho-send-wrong-op-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (gparse-test:rho-send-wrong-op-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) (lp-shared-parse-cert (rho-g) proc (rho-send-nil-toks) $cert))
     ($o False)))
  True)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (case (lp-shared-cert-tree (rho-g) proc (rho-send-nil-toks) $cert)
        (((Ok $tree) (lp-tree-shared-cert (rho-g) proc (rho-send-nil-toks) $tree))
         ((Err $e) (Err $e)))))
     ($o (Err no-unique-cert))))
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) (Ok $cert))
     ($o (Err no-unique-cert)))))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) (gparse:cert-shape $cert))
     ($o $o)))
  (GParseCertShape 4 0 8 0 0 8))

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) (lp-shared-parse-cert (rho-g) proc (rho-send-nil-toks) $cert))
     ($o False)))
  True)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (case (lp-shared-cert-tree (rho-g) proc (rho-send-nil-toks) $cert)
        (((Ok $tree) (lp-tree-shared-cert (rho-g) proc (rho-send-nil-toks) $tree))
         ((Err $e) (Err $e)))))
     ($o (Err no-unique-cert))))
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) (Ok $cert))
     ($o (Err no-unique-cert)))))

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) (gparse:cert-shape $cert))
     ($o $o)))
  (GParseCertShape 4 0 8 0 0 8))

!(assertEqual
  (gparse:dual-parse-compare
    (rho-g)
    (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
    rho-g
    proc
    (rho-send-nil-toks))
  (GParseDualUnique
    (GParseCertShape 4 0 8 0 0 8)
    (GParseCertShape 4 0 8 0 0 8)
    True
    True
    True
    True))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) $cert)
     ($o (Err no-unique-cert))))
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert) $cert)
     ($o (Err no-unique-cert)))))

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (rho-send-nil-toks)
        (lp-shared-cert-corrupt-first-token $cert)))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (rho-send-nil-toks)
        (lp-shared-cert-corrupt-first-token $cert)))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (rho-send-nil-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (mm-g)
        stmt
        (rho-send-nil-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        name
        (rho-send-nil-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        name
        (rho-send-nil-toks)
        $cert))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:glr-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (rho-send-nil-toks)
        (gparse-test:bad-root-label $cert)))
     ($o False)))
  False)

!(assertEqual
  (case (gparse:gll-parse-shared
      (gparse:repo-path "tests/support/rhocalc_lib_parse_translator_v3.metta")
      rho-g
      proc
      (rho-send-nil-toks))
    (((Unique $cert)
      (lp-shared-parse-cert
        (rho-g)
        proc
        (rho-send-nil-toks)
        (gparse-test:bad-root-label $cert)))
     ($o False)))
  False)

; Negative class coverage for rho-g / rho-g-ambig lives in the focused
; native generalized parser lanes. Keep this integration lane centered on
; the shared-cert and dual-backend witness checks.
