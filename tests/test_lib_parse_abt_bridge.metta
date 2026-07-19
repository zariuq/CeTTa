!(import! &self lib_parse_abt)
!(import! &self gparse)
!(import! &self ./support/lib_parse_gslt_core_grammars.metta)

(= (abt-test:g-lam) (mkGram (
   (VarD x term)(VarD y term)
   (Prod lam term ((Tm LAM)(HlB term term)(Tm DOT)(Hl term))) )))

(= (abt-test:g-rho) (mkGram (
   (VarD x rho_id)
   (Prod pnil proc ((Tm ZERO)))
   (Prod pdrop proc ((Tm STAR)(Hl name)))
   (Prod precv proc
     ((Tm FOR)(HlB rho_id proc)(Tm FROM)(Hl name)(Tm DOT)(Hl proc)))
   (Prod nquot name ((Tm AT)(Hl proc)))
   (Prod nvar name ((Tm DOLLAR)(Hl rho_id))) )))

(= (abt-test:g-typed) (mkGram (
   (VarD x name)(VarD A ty)
   (Prod lamT term
     ((Tm LAM)(HlB name term)(Tm COLON)(Hl ty)(Tm DOT)(Hl term)))
   (Prod tvar term ((Hl name))) )))

(= (abt-test:g-two) (mkGram (
   (VarD x name)(VarD y name)
   (Prod lam2 term
     ((Tm LAM)(HlB name term)(Tm COMMA)(HlB name term)(Tm DOT)(Hl term)))
   (Prod tvar term ((Hl name))) )))

(= (abt-test:g-dangling) (mkGram (
   (VarD x name)
   (Prod bad term ((Tm LAM)(HlB name term))) )))

(= (abt-test:gslt-path)
   (gparse:repo-path "tests/support/lib_parse_gslt_core_grammars.metta"))
(= (abt-test:hol-x)
   (mkL (HLAM (Lex hol-id x) HDOT (Lex hol-id x))))
(= (abt-test:hol-y)
   (mkL (HLAM (Lex hol-id y) HDOT (Lex hol-id y))))
(= (abt-test:hol-nested)
   (mkL
     (HLAM (Lex hol-id x) HDOT
       HLAM (Lex hol-id y) HDOT (Lex hol-id x))))
(= (abt-test:hol-free-bound)
   (mkL
     (HLAM (Lex hol-id x) HDOT
       (Lex hol-id y) (Lex hol-id x))))
(= (abt-test:hol-one-bound)
   (h-tbd
     (h-lam
       (h-tif
         (h-imf
           (h-djf
             (h-cjf
               (h-eqf
                 (h-ngf
                   (h-apf
                     (h-atf (h-id (Var 0)))))))))))))
(= (abt-test:hol-two-bound)
   (h-tbd
     (h-lam
       (h-tbd
         (h-lam
           (h-tif
             (h-imf
               (h-djf
                 (h-cjf
                   (h-eqf
                     (h-ngf
                       (h-apf
                         (h-atf (h-id (Var 1)))))))))))))))
(= (abt-test:hol-free-bound-term)
   (h-tbd
     (h-lam
       (h-tif
         (h-imf
           (h-djf
             (h-cjf
               (h-eqf
                 (h-ngf
                   (h-apf
                     (h-app
                       (h-atf (h-id (Con (Lex hol-id y))))
                       (h-id (Var 0)))))))))))))
(= (abt-test:corrupt-result $result)
   (case $result
     (((Unique $cert)
       (Unique (lp-shared-cert-corrupt-first-token $cert)))
      ($other $other))))
(= (abt-test:certificate-rejection $result)
   (case $result
     (((Rejected
         (LPABTError certificate-replay $culprit bad-cert)) True)
      ($other False))))

!(assertEqual
  (lp-abt:signature-set (abt-test:g-lam))
  (Ok (AbtSignatures
    (AbtSignature lam (Fields (Hole 0))
      (BindFields (BCons (BField (Hole 0) 1) BNil))))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-lam)
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil))))
  (Ok (lam (Var 0))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-lam)
    (Nd lam (Cons (Lf y) (Cons (Lf y) Nil))))
  (Ok (lam (Var 0))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-lam)
    (Nd lam (Cons (Lf x) (Cons (Lf y) Nil))))
  (Ok (lam (Con y))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-lam)
    (Nd lam (Cons (Lf x)
      (Cons (Nd lam (Cons (Lf y) (Cons (Lf x) Nil))) Nil))))
  (Ok (lam (lam (Var 1)))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-lam)
    (Nd lam (Cons (Lf x)
      (Cons (Nd lam (Cons (Lf x) (Cons (Lf x) Nil))) Nil))))
  (Ok (lam (lam (Var 0)))))

!(assertEqual
  (lp-abt:signature-set (abt-test:g-rho))
  (Ok (AbtSignatures
    (AbtSignature pnil (Fields) (BindFields BNil))
    (AbtSignature pdrop (Fields (Hole 0))
      (BindFields (BCons (BField (Hole 0) 0) BNil)))
    (AbtSignature precv (Fields (Hole 0) (Hole 1))
      (BindFields
        (BCons (BField (Hole 0) 0)
          (BCons (BField (Hole 1) 1) BNil))))
    (AbtSignature nquot (Fields (Hole 0))
      (BindFields (BCons (BField (Hole 0) 0) BNil)))
    (AbtSignature nvar (Fields (Hole 0))
      (BindFields (BCons (BField (Hole 0) 0) BNil))))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-rho)
    (Nd precv
      (Cons (Lf x)
        (Cons (Nd nquot (Cons (Nd pnil Nil) Nil))
          (Cons (Nd pdrop (Cons (Nd nvar (Cons (Lf x) Nil)) Nil)) Nil)))))
  (Ok (precv (nquot (pnil)) (pdrop (nvar (Var 0))))))

!(assertEqual
  (lp-abt:signature-set (abt-test:g-typed))
  (Ok (AbtSignatures
    (AbtSignature lamT (Fields (Hole 0) (Hole 1))
      (BindFields
        (BCons (BField (Hole 0) 0)
          (BCons (BField (Hole 1) 1) BNil))))
    (AbtSignature tvar (Fields (Hole 0))
      (BindFields (BCons (BField (Hole 0) 0) BNil))))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-typed)
    (Nd lamT
      (Cons (Lf x)
        (Cons (Lf A) (Cons (Nd tvar (Cons (Lf x) Nil)) Nil)))))
  (Ok (lamT (Con A) (tvar (Var 0)))))

!(assertEqual
  (lp-abt:signature-set (abt-test:g-two))
  (Ok (AbtSignatures
    (AbtSignature lam2 (Fields (Hole 0))
      (BindFields (BCons (BField (Hole 0) 2) BNil)))
    (AbtSignature tvar (Fields (Hole 0))
      (BindFields (BCons (BField (Hole 0) 0) BNil))))))
!(assertEqual
  (lp-abt:admit-raw (abt-test:g-two)
    (Nd lam2
      (Cons (Lf x)
        (Cons (Lf y) (Cons (Nd tvar (Cons (Lf x) Nil)) Nil)))))
  (Ok (lam2 (tvar (Var 1)))))

; Fail closed on malformed binding metadata and loose indices.
!(assertEqual
  (lp-abt:signature-set (abt-test:g-dangling))
  (Rejected dangling-binder-scope))
!(assertEqual
  (lp-abt:admit-canonical (abt-test:g-lam)
    (Nd lam (Cons (Bd term wrong-scope) (Cons (Bv 0) Nil))))
  (Rejected binder-marker-mismatch))
!(assertEqual
  (lp-abt:admit-canonical (abt-test:g-lam)
    (Nd lam (Cons (Bd term term) (Cons (Bv 1) Nil))))
  (Rejected invalid-abt-scope))
!(assertEqual
  (lp-abt:admit-canonical (abt-test:g-lam)
    (Nd lam (Cons (Bd term term) Nil)))
  (Rejected missing-canonical-child))

; Native GLL/GLR certificates now cross the same replay -> lowering -> native
; scope-admission seam.  The parser never becomes binding authority.
!(assertEqual
  (lp-abt:admit-shared-result
    (hol-core-g) Term (abt-test:hol-x)
    (gparse:glr-parse-shared
      (abt-test:gslt-path) hol-core-g Term (abt-test:hol-x)))
  (Unique (abt-test:hol-one-bound)))
!(assertEqual
  (lp-abt:admit-shared-result
    (hol-core-g) Term (abt-test:hol-y)
    (gparse:gll-parse-shared
      (abt-test:gslt-path) hol-core-g Term (abt-test:hol-y)))
  (Unique (abt-test:hol-one-bound)))
!(assertEqual
  (==
    (lp-abt:admit-shared-result
      (hol-core-g) Term (abt-test:hol-x)
      (gparse:glr-parse-shared
        (abt-test:gslt-path) hol-core-g Term (abt-test:hol-x)))
    (lp-abt:admit-shared-result
      (hol-core-g) Term (abt-test:hol-y)
      (gparse:gll-parse-shared
        (abt-test:gslt-path) hol-core-g Term (abt-test:hol-y))))
  True)
!(assertEqual
  (lp-abt:admit-shared-result
    (hol-core-g) Term (abt-test:hol-nested)
    (gparse:glr-parse-shared
      (abt-test:gslt-path) hol-core-g Term (abt-test:hol-nested)))
  (Unique (abt-test:hol-two-bound)))
!(assertEqual
  (lp-abt:admit-shared-result
    (hol-core-g) Term (abt-test:hol-nested)
    (gparse:gll-parse-shared
      (abt-test:gslt-path) hol-core-g Term (abt-test:hol-nested)))
  (Unique (abt-test:hol-two-bound)))
!(assertEqual
  (lp-abt:admit-shared-result
    (hol-core-g) Term (abt-test:hol-free-bound)
    (gparse:glr-parse-shared
      (abt-test:gslt-path) hol-core-g Term (abt-test:hol-free-bound)))
  (Unique (abt-test:hol-free-bound-term)))
!(assertEqual
  (abt-test:certificate-rejection
    (lp-abt:admit-shared-result
      (hol-core-g) Term (abt-test:hol-x)
      (abt-test:corrupt-result
        (gparse:glr-parse-shared
          (abt-test:gslt-path) hol-core-g Term (abt-test:hol-x)))))
  True)
!(assertEqual
  (lp-abt:admit-shared-result
    (hol-core-g) Term (mkL (HLAM (Lex hol-id x) HDOT))
    (gparse:gll-parse-shared
      (abt-test:gslt-path) hol-core-g Term
      (mkL (HLAM (Lex hol-id x) HDOT))))
  NoParse)
!(assertEqual
  (lp-abt:admit-shared-result
    (hol-core-g) Term (abt-test:hol-x) Ambiguous)
  Ambiguous)

!(println!
  (LibParseABTBridgeSummary 25 25 0 admitted 18 rejected 5 preserved 2))
