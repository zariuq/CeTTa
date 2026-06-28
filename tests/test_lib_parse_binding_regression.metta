!(import! &self lib_parse)

(= (g-lam) (mkGram (
   (VarD x term)(VarD y term)(VarD z term)
   (Prod lam term ((Tm LAM)(HlB term term)(Tm DOT)(Hl term))) )))

(= (g-rho-gap) (mkGram (
   (VarD x rho_id)(VarD y rho_id)
   (Prod pnil  proc ((Tm ZERO)))
   (Prod pdrop proc ((Tm STAR)(Hl name)))
   (Prod precv proc ((Tm FOR)(Tm LP)(Tm DOLLAR)(HlB rho_id proc)(Tm FROM)(Hl name)(Tm RP)(Tm LBR)(Hl proc)(Tm RBR)))
   (Prod nquot name ((Tm AT)(Tm LBR)(Hl proc)(Tm RBR)))
   (Prod nvar  name ((Tm DOLLAR)(Hl rho_id))) )))

!(assertEqual
  (lp-parse-raw (g-lam) term (mkL (LAM x DOT x)))
  (Unique (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))))
!(assertEqual
  (lp-parse (g-lam) term (mkL (LAM x DOT x)))
  (Unique (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil)))))
!(assertEqual
  (lp-parse (g-lam) term (mkL (LAM y DOT y)))
  (Unique (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil)))))
!(assertEqual
  (== (lp-parse (g-lam) term (mkL (LAM x DOT x)))
      (lp-parse (g-lam) term (mkL (LAM y DOT y))))
  True)
!(assertEqual
  (lp-lower (g-lam)
    (Nd lam (Cons (Lf x)
      (Cons (Nd lam (Cons (Lf y) (Cons (Lf x) Nil))) Nil))))
  (Nd lam (Cons (Bd term term)
    (Cons (Nd lam (Cons (Bd term term) (Cons (Bv 1) Nil))) Nil))))
!(assertEqual
  (lp-lower (g-lam)
    (Nd lam (Cons (Lf x)
      (Cons (Nd lam (Cons (Lf x) (Cons (Lf x) Nil))) Nil))))
  (Nd lam (Cons (Bd term term)
    (Cons (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil))) Nil))))
!(assertEqual
  (lp-print (g-lam) (pu-tree (lp-parse (g-lam) term (mkL (LAM x DOT x)))))
  (Rejected bad-tree))
!(assertEqual
  (lp-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (pu-tree (lp-parse (g-lam) term (mkL (LAM x DOT x)))))
  False)
!(assertEqual
  (lp-lower-cert
    (g-lam)
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil))))
  True)
!(assertEqual
  (lp-lower-cert
    (g-lam)
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Lf x) Nil))))
  False)
!(assertEqual
  (lp-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Bv 0) Nil))))
  True)
!(assertEqual
  (lp-parse-cert
    (g-lam)
    term
    (mkL (LAM x DOT x))
    (Nd lam (Cons (Lf x) (Cons (Lf x) Nil)))
    (Nd lam (Cons (Bd term term) (Cons (Lf x) Nil))))
  False)
!(assertEqual
  (lp-parse (g-rho-gap) proc (mkL (FOR LP DOLLAR x FROM AT LBR ZERO RBR RP LBR STAR DOLLAR x RBR)))
  (Unique
    (Nd precv
      (Cons (Bd rho_id proc)
        (Cons (Nd nquot (Cons (Nd pnil Nil) Nil))
          (Cons (Nd pdrop (Cons (Nd nvar (Cons (Bv 0) Nil)) Nil)) Nil))))))
!(assertEqual
  (lp-parse (g-rho-gap) proc (mkL (FOR LP DOLLAR y FROM AT LBR ZERO RBR RP LBR STAR DOLLAR y RBR)))
  (Unique
    (Nd precv
      (Cons (Bd rho_id proc)
        (Cons (Nd nquot (Cons (Nd pnil Nil) Nil))
          (Cons (Nd pdrop (Cons (Nd nvar (Cons (Bv 0) Nil)) Nil)) Nil))))))
!(assertEqual
  (== (lp-parse (g-rho-gap) proc (mkL (FOR LP DOLLAR x FROM AT LBR ZERO RBR RP LBR STAR DOLLAR x RBR)))
      (lp-parse (g-rho-gap) proc (mkL (FOR LP DOLLAR y FROM AT LBR ZERO RBR RP LBR STAR DOLLAR y RBR))))
  True)
!(assertEqual
  (lp-lower (g-rho-gap)
    (Nd precv
      (Cons (Lf x)
        (Cons (Nd nquot (Cons (Nd pnil Nil) Nil))
          (Cons (Nd pdrop (Cons (Nd nvar (Cons (Lf x) Nil)) Nil)) Nil)))))
  (Nd precv
    (Cons (Bd rho_id proc)
      (Cons (Nd nquot (Cons (Nd pnil Nil) Nil))
        (Cons (Nd pdrop (Cons (Nd nvar (Cons (Bv 0) Nil)) Nil)) Nil)))))
