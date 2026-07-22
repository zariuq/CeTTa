; PeTTa per-form DCG recognition as scannerless GSLT data.
; Whole-file comment stripping and balanced-form extraction are a separate authority layer.
(gslt-presentation-v1 PeTTaFormReaderSyntaxV1
  (signature)
  (equations)
  (rewrites
    (rule def-petta-token-boundary (head (definition petta-token-boundary (alt eof (class petta-token-boundary-scalar)))) (body))
    (rule def-petta-skip (head (definition petta-skip (star (class petta-blank-scalar)))) (body))
    (rule def-petta-string-escape (head (definition petta-string-escape (node escape (right (char (cp 92)) any)))) (body))
    (rule def-petta-string-unit (head (definition petta-string-unit (alt (ref petta-string-escape) (class petta-string-plain-scalar)))) (body))
    (rule def-petta-string (head (definition petta-string (node string (right (char (cp 34)) (left (star (ref petta-string-unit)) (char (cp 34))))))) (body))
    (rule def-petta-token-like (head (definition petta-token-like (left (alt (node variable (right (char (cp 36)) (plus (class petta-token-scalar)))) (alt (node dollar-symbol (char (cp 36))) (alt (node quoted-token (right (char (cp 34)) (seq (star (alt (class petta-quoted-token-plain-scalar) (seq (char (cp 92)) (class petta-token-scalar)))) (lit (cons (cp 92) (cons (cp 34) nil)))))) (node token (seq (class petta-token-first-scalar) (star (class petta-token-scalar))))))) (peek (ref petta-token-boundary))))) (body))
    (rule def-petta-expression (head (definition petta-expression (node expression (right (char (cp 40)) (left (right (ref petta-skip) (star (left (ref petta-atom) (ref petta-skip)))) (char (cp 41))))))) (body))
    (rule def-petta-atom (head (definition petta-atom (alt (ref petta-string) (alt (ref petta-expression) (ref petta-token-like))))) (body))
    (rule def-petta-form (head (definition petta-form (node form (right (ref petta-skip) (left (ref petta-atom) (right (ref petta-skip) eof)))))) (body))
  ))
