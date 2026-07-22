; PeTTa filereader.pl strip/3 plus top_forms//2 as scannerless GSLT data.
; This compatibility presentation intentionally toggles quotes without escape handling.
(gslt-presentation-v1 PeTTaDocumentSplitterSyntaxV1
  (signature)
  (equations)
  (rewrites
    (rule def-petta-splitter-comment-line (head (definition petta-splitter-comment-line (right (char (cp 59)) (left (star (class petta-splitter-comment-body-scalar)) (char (cp 10)))))) (body))
    (rule def-petta-splitter-comment-eof (head (definition petta-splitter-comment-eof (right (char (cp 59)) (left (star (class petta-splitter-comment-body-scalar)) eof)))) (body))
    (rule def-petta-splitter-comment (head (definition petta-splitter-comment (alt (ref petta-splitter-comment-line) (ref petta-splitter-comment-eof)))) (body))
    (rule def-petta-splitter-quoted (head (definition petta-splitter-quoted (node naive-quoted (seq (char (cp 34)) (seq (star (class petta-splitter-nonquote-scalar)) (char (cp 34))))))) (body))
    (rule def-petta-splitter-balanced-item (head (definition petta-splitter-balanced-item (alt (ref petta-splitter-balanced) (alt (ref petta-splitter-quoted) (alt (right (ref petta-splitter-comment-line) (eps nil)) (class petta-splitter-ordinary-scalar)))))) (body))
    (rule def-petta-splitter-balanced (head (definition petta-splitter-balanced (node balanced (seq (char (cp 40)) (seq (star (ref petta-splitter-balanced-item)) (char (cp 41))))))) (body))
    (rule def-petta-splitter-top-skip-unit (head (definition petta-splitter-top-skip-unit (alt (class petta-splitter-blank-scalar) (right (ref petta-splitter-comment) (eps nil))))) (body))
    (rule def-petta-splitter-top-skip (head (definition petta-splitter-top-skip (star (ref petta-splitter-top-skip-unit)))) (body))
    (rule def-petta-splitter-top-form (head (definition petta-splitter-top-form (alt (node runnable (right (char (cp 33)) (ref petta-splitter-balanced))) (node form (ref petta-splitter-balanced))))) (body))
    (rule def-petta-document (head (definition petta-document (node document (right (ref petta-splitter-top-skip) (left (star (left (ref petta-splitter-top-form) (ref petta-splitter-top-skip))) eof))))) (body))
  ))
