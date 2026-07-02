!(import! &self lib_parse)
!(import! &self gparse)
!(import! &self ./support/lib_parse_gslt_core_grammars.metta)

(= (gslt-grammar-path)
   (gparse:repo-path "tests/support/lib_parse_gslt_core_grammars.metta"))

(= (shared-result-class $res)
   (case $res
     (((Unique $cert) Unique)
      (Ambiguous Ambiguous)
      (NoParse NoParse)
      ($o (Err bad-shared-result)))))

(= (unique-cert-valid $g $start $toks $res)
   (case $res
     (((Unique $cert) (lp-shared-parse-cert $g $start $toks $cert))
      ($o False))))

; ---- (1) precedence / mixfix ----
; 2 + 3 * 4   -> Unique (precedence: 2 + (3*4))
(= (prec-toks-1) (mkL ((Lex gslt-num 2) PLUS (Lex gslt-num 3) STAR (Lex gslt-num 4))))
; 2 ^ 3 ^ 2   -> Unique (right assoc: 2 ^ (3 ^ 2))
(= (prec-toks-2) (mkL ((Lex gslt-num 2) CARET (Lex gslt-num 3) CARET (Lex gslt-num 2))))
; if x then 1 else 2  -> Unique (mixfix)
(= (prec-toks-3) (mkL (IF (Lex gslt-id x) THEN (Lex gslt-num 1) ELSE (Lex gslt-num 2))))

!(assertEqual
  (gparse:glr-class (gslt-grammar-path) prec-arith-g Expr (prec-toks-1)) Unique)
!(assertEqual
  (shared-result-class (gparse:gll-parse-shared (gslt-grammar-path) prec-arith-g Expr (prec-toks-1))) Unique)
!(assertEqual
  (unique-cert-valid (prec-arith-g) Expr (prec-toks-1)
    (gparse:gll-parse-shared (gslt-grammar-path) prec-arith-g Expr (prec-toks-1))) True)
!(assertEqual
  (gparse:glr-class (gslt-grammar-path) prec-arith-g Expr (prec-toks-2)) Unique)
!(assertEqual
  (gparse:glr-class (gslt-grammar-path) prec-arith-g Expr (prec-toks-3)) Unique)

; ---- (2) MeTTa core ----
; ( foo $x ( bar ) )  -> Unique  (variable name carried as a plain symbol, not a live MeTTa var)
(= (metta-toks-1)
   (mkL (LP (Lex metta-sym foo) (Lex metta-var x) LP (Lex metta-sym bar) RP RP)))

!(assertEqual
  (gparse:glr-class (gslt-grammar-path) metta-core-g Expr (metta-toks-1)) Unique)
!(assertEqual
  (shared-result-class (gparse:gll-parse-shared (gslt-grammar-path) metta-core-g Expr (metta-toks-1))) Unique)
!(assertEqual
  (unique-cert-valid (metta-core-g) Expr (metta-toks-1)
    (gparse:gll-parse-shared (gslt-grammar-path) metta-core-g Expr (metta-toks-1))) True)

; ---- (3) HOL Light core ----
; ! x . p ==> q   -> Unique  (greedy binder body: !x.(p==>q))
(= (hol-toks-1) (mkL (HALL (Lex hol-id x) HDOT (Lex hol-id p) HIMP (Lex hol-id q))))
; p /\ q \/ r     -> Unique  ((p/\q)\/r; \/ looser than /\)
(= (hol-toks-2) (mkL ((Lex hol-id p) HAND (Lex hol-id q) HOR (Lex hol-id r))))
; ! x . ! y . p   -> Unique  (nested binders)
(= (hol-toks-3) (mkL (HALL (Lex hol-id x) HDOT HALL (Lex hol-id y) HDOT (Lex hol-id p))))
; \ x . f x       -> Unique  (lambda + application)
(= (hol-toks-4) (mkL (HLAM (Lex hol-id x) HDOT (Lex hol-id f) (Lex hol-id x))))

!(assertEqual (gparse:glr-class (gslt-grammar-path) hol-core-g Term (hol-toks-1)) Unique)
!(assertEqual (shared-result-class (gparse:gll-parse-shared (gslt-grammar-path) hol-core-g Term (hol-toks-1))) Unique)
!(assertEqual
  (unique-cert-valid (hol-core-g) Term (hol-toks-1)
    (gparse:gll-parse-shared (gslt-grammar-path) hol-core-g Term (hol-toks-1))) True)
!(assertEqual (gparse:glr-class (gslt-grammar-path) hol-core-g Term (hol-toks-2)) Unique)
!(assertEqual (gparse:glr-class (gslt-grammar-path) hol-core-g Term (hol-toks-3)) Unique)
!(assertEqual (gparse:glr-class (gslt-grammar-path) hol-core-g Term (hol-toks-4)) Unique)

; ---- (4) Megalodon core ----
; forall x : set , x = x   -> Unique
(= (mega-toks-1) (mkL (FORALL (Lex mega-id x) COLON SET COMMA (Lex mega-id x) MEQ (Lex mega-id x))))
; a -> b -> c              -> Unique  (right assoc)
(= (mega-toks-2) (mkL ((Lex mega-id a) ARROW (Lex mega-id b) ARROW (Lex mega-id c))))
; fun x => x               -> Unique
(= (mega-toks-3) (mkL (FUN (Lex mega-id x) DARROW (Lex mega-id x))))
; A :e F /\ B :e F         -> Unique
(= (mega-toks-4) (mkL ((Lex mega-id A) MIN (Lex mega-id F) MAND (Lex mega-id B) MIN (Lex mega-id F))))

!(assertEqual (gparse:glr-class (gslt-grammar-path) megalodon-core-g Prop (mega-toks-1)) Unique)
!(assertEqual (shared-result-class (gparse:gll-parse-shared (gslt-grammar-path) megalodon-core-g Prop (mega-toks-1))) Unique)
!(assertEqual
  (unique-cert-valid (megalodon-core-g) Prop (mega-toks-1)
    (gparse:gll-parse-shared (gslt-grammar-path) megalodon-core-g Prop (mega-toks-1))) True)
!(assertEqual (gparse:glr-class (gslt-grammar-path) megalodon-core-g Prop (mega-toks-2)) Unique)
!(assertEqual (gparse:glr-class (gslt-grammar-path) megalodon-core-g Prop (mega-toks-3)) Unique)
!(assertEqual (gparse:glr-class (gslt-grammar-path) megalodon-core-g Prop (mega-toks-4)) Unique)
