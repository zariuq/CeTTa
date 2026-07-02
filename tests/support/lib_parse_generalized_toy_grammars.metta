!(import! &self lib_parse)

(= (toy-sss-g) (mkGram (
  (VarD b S)
  (Prod s2 S ((Hl S)(Hl S)))
  (Prod s1 S ((Hl b))) )))

(= (toy-plus-g) (mkGram (
  (VarD x A)
  (Prod e2 E ((Hl E)(Tm PLUS)(Hl A)))
  (Prod e1 E ((Hl A)))
  (Prod a A ((Hl x))) )))

(= (toy-op-ambig-g) (mkGram (
  (VarD a E)
  (Prod p E ((Hl E)(Tm PLUS)(Hl E)))
  (Prod m E ((Hl E)(Tm STAR)(Hl E)))
  (Prod a1 E ((Hl a))) )))

(= (toy-nullable-g) (mkGram (
  (VarD a a_tok)(VarD b b_tok)(VarD c c_tok)
  (Prod s S ((Hl a_tok)(Hl B)(Hl c_tok)))
  (Prod b1 B ((Hl b_tok)))
  (Prod b0 B ()) )))

(= (toy-lex-g) (mkGram (
  (LexD WORD W)
  (Prod s S ((Hl W)(Hl W))) )))
