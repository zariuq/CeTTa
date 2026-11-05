; The bytes of a Megalodon document as Unicode scalars, read whole.  Tokens
; come from the lexical presentation, which follows Megalodon's lexer: its
; longest-match rule cannot be stated by a context-free grammar over a flat
; token stream without lookahead, and a relation states it exactly.
(gslt-presentation-v1 MegalodonSourceV1
  (signature)
  (equations)
  (rewrites
    (rule def-mg-source
      (head (definition mg-source (node mg-source (until eof any))))
      (body))))
