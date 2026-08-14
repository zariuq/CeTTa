!(import! &self ./support/rhocalc_lib_parse_translator_v3.metta)
!(import! &self gparse)

!(assertEqual (lp-admit (rho-g)) Admitted)

!(assertEqual
  (rho->canon (rho-send-nil-toks))
  (Ok (rho:send (rho:quote rho:nil) rho:nil)))

!(assertEqual
  (rho->canon (rho-recv-drop-x-toks))
  (Ok (rho:recv-db (rho:quote rho:nil) (rho:drop (rho:bound 0)))))

!(assertEqual
  (rho->canon (rho-recv-drop-x-toks))
  (rho->canon (rho-recv-drop-y-toks)))

!(assertEqual
  (rho->canon (rho-free-drop-z-toks))
  (Ok (rho:drop (rho:free z))))

!(assertEqual
  (rho->canon (rho-pure-syntax-toks))
  (Ok
    (rho:par
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:recv-db (rho:quote rho:nil) (rho:drop (rho:bound 0))))))

!(assertEqual
  (rho->canon (rho-shadowing-syntax-toks))
  (Ok
    (rho:par
      (rho:recv-db
        (rho:quote rho:nil)
        (rho:par
          (rho:recv-db (rho:bound 0) (rho:drop (rho:bound 0)))
          (rho:send (rho:bound 0) rho:nil)))
      (rho:send
        (rho:quote rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertEqual
  (rho-canon->syntax-toks (rho:recv-db (rho:quote rho:nil) (rho:drop (rho:bound 0))))
  (Ok (mkL (FOR LP DOLLAR x FROM AT LBR ZERO RBR RP LBR STAR DOLLAR x RBR))))

!(assertEqual
  (rho-canon->syntax-toks (rho:send (rho:quote rho:nil) rho:nil))
  (Ok (mkL (AT LBR ZERO RBR BANG LP ZERO RP))))

!(assertEqual
  (rho-lex-string "@{0}!(0)")
  (Ok
    (Cons (STok AT 0 1)
      (Cons (STok LBR 1 2)
        (Cons (STok ZERO 2 3)
          (Cons (STok RBR 3 4)
            (Cons (STok BANG 4 5)
              (Cons (STok LP 5 6)
                (Cons (STok ZERO 6 7)
                  (Cons (STok RP 7 8) Nil))))))))))

!(assertEqual
  (rho-lex-string->toks "@{0}!(0) | for ($x <- @{0}) {*$x}")
  (rho-pure-syntax-toks))

!(assertEqual
  (rho-text->canon "@{0}!(0) | for ($x <- @{0}) {*$x}")
  (rho->canon (rho-pure-syntax-toks)))

!(assertEqual
  (rho-file->canon "tests/rhocalc/pure_rho.rho")
  (rho->canon (rho-pure-syntax-toks)))

!(assertEqual
  (rho-canon->mrho-text (rho:send (rho:quote rho:nil) rho:nil))
  "(rho:send (rho:quote rho:nil) rho:nil)")

!(assertEqual
  (rho-file->mrho-text "tests/rhocalc/pure_rho.rho")
  "(rho:par (rho:send (rho:quote rho:nil) rho:nil) (rho:recv (rho:quote rho:nil) $x (rho:drop $x)))")

!(assertEqual (fresh-name-text 9) "x_rho3")
!(assertEqual (fresh-name-text 10) "y_rho3")
!(assertEqual (fresh-name-text 11) "z_rho3")

(= (rho-deep-binder-10)
   (rho:recv-db
     (rho:quote rho:nil)
     (rho:recv-db
       (rho:bound 0)
       (rho:recv-db
         (rho:bound 1)
         (rho:recv-db
           (rho:bound 2)
           (rho:recv-db
             (rho:bound 3)
             (rho:recv-db
               (rho:bound 4)
               (rho:recv-db
                 (rho:bound 5)
                 (rho:recv-db
                   (rho:bound 6)
                   (rho:recv-db
                     (rho:bound 7)
                     (rho:recv-db
                       (rho:bound 8)
                       (rho:drop (rho:bound 9)))))))))))))

!(assertEqual
  (rho-canon->mrho-text (rho-deep-binder-10))
  (case (gparse:rho-canon->mrho-text (rho-deep-binder-10))
    (((Ok $s) $s)
     ($o $o))))

!(assertEqual
  (rho-file->canon "tests/rhocalc/rho_shadowing.rho")
  (rho->canon (rho-shadowing-syntax-toks)))

!(assertEqual
  (rho-file->canon "tests/rhocalc/rho_quote_inner_binder_restores_scope.rho")
  (rho-text->canon "for ($x <- @{0}) {@{for ($y <- @{0}) {*$y}}!(0) | $x!(0)} | @{0}!(@{0}!(0))"))

!(assertEqual
  (rho-file->canon "tests/rhocalc/rho_open_name_variable.rho")
  (Ok (rho:send (rho:free x) rho:nil)))

!(assertEqual
  (rho-file->canon "tests/rhocalc/rho_name_output.rho")
  (Ok
    (rho:par
      (rho:send
        (rho:quote rho:nil)
        (rho:drop (rho:quote rho:nil)))
      (rho:recv-db
        (rho:quote rho:nil)
        (rho:drop (rho:bound 0))))))

!(assertEqual
  (rho-file->canon "tests/rhocalc/rho_quote_seals_outer_binder.rho")
  (Ok
    (rho:par
      (rho:recv-db
        (rho:quote rho:nil)
        (rho:send (rho:quote (rho:drop (rho:bound 0))) rho:nil))
      (rho:send
        (rho:quote rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertEqual
  (rho-file->canon "tests/rhocalc/paper_reflection_name_generation_h4.rho")
  (Ok
    (rho:par
      (rho:recv-db
        (rho:quote rho:nil)
        (rho:par
          (rho:send (rho:bound 0) rho:nil)
          (rho:drop (rho:bound 0))))
      (rho:send
        (rho:quote rho:nil)
        (rho:send (rho:quote rho:nil) rho:nil)))))

!(assertEqual
  (rho-file-syntax-roundtrip "tests/rhocalc/pure_rho.rho")
  (rho-syntax-roundtrip (rho-pure-syntax-toks)))

!(assertEqual
  (rho-file-syntax-roundtrip "tests/rhocalc/rho_shadowing.rho")
  (rho-syntax-roundtrip (rho-shadowing-syntax-toks)))

!(assertEqual
  (rho-file-syntax-roundtrip "tests/rhocalc/rho_name_output.rho")
  (rho-text-syntax-roundtrip "@{0}!(*@{0}) | for ($x <- @{0}) {*$x}"))

!(assertEqual
  (rho-lex-string "@{0}?")
  (Rejected (lex unexpected-char 4 "?")))

!(assertEqual
  (rho-file-corpus-unique (rho-corpus-files))
  True)

!(assertEqual
  (rho-certified-unambiguous (rho-g))
  True)

!(assertEqual
  (rho-unambiguity-status (rho-g) proc (rho-lite-corpus))
  CertifiedUnambiguous)

!(assertEqual
  (rho-file-unambiguity-status (rho-corpus-files))
  CertifiedUnambiguous)

!(assertEqual
  (rho-certified-unambiguous (rho-g-lite))
  False)

!(assertEqual
  (rho-unambiguity-status (rho-g-lite) proc (rho-lite-corpus))
  CorpusChecked)

!(assertEqual
  (rho-unambiguity-status (rho-g-ambig) proc (rho-ambig-corpus))
  Unchecked)

!(assertEqual
  (rho-file->canon "tests/rhocalc/reject_quoted_payload.rho")
  (Rejected noparse))

!(assertEqual
  (rho-text->canon "@{0!(0)")
  (Rejected noparse))

!(assertEqual
  (lp-parse (rho-g-lite) proc (mkL (ZERO)))
  (Unique (Nd pnil Nil)))

!(assertEqual
  (rho-canon-roundtrip (rho-send-nil-toks))
  (rho->canon (rho-send-nil-toks)))

!(assertEqual
  (rho-canon-roundtrip (rho-recv-drop-x-toks))
  (rho->canon (rho-recv-drop-x-toks)))

!(assertEqual
  (rho-canon-roundtrip (rho-shadowing-syntax-toks))
  (rho->canon (rho-shadowing-syntax-toks)))

!(assertEqual
  (lp-parse-cert
    (rho-g)
    proc
    (rho-recv-drop-x-toks)
    (rho-raw-tree (rho-recv-drop-x-toks))
    (rho-canon-tree (rho-recv-drop-x-toks)))
  True)

!(assertEqual
  (lp-parse-cert
    (rho-g)
    proc
    (rho-recv-drop-x-toks)
    (rho-raw-tree (rho-recv-drop-x-toks))
    (rho-canon-tree (rho-send-nil-toks)))
  False)

!(assertEqual
  (lp-parse-cert
    (rho-g)
    proc
    (rho-recv-drop-x-toks)
    (rho-raw-tree (rho-send-nil-toks)))
  False)

!(assertEqual
  (rho-syntax-roundtrip (rho-bad-toks))
  (Rejected noparse))

!(assertEqual
  (lp-parse (rho-g-ambig) proc (mkL (x BAR x BAR x)))
  Ambiguous)
