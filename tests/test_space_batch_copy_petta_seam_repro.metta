; Minimal repro for the current PeTTa-side WM-PLN merge seam.
; Positive example:
; copying atoms one-by-one from a source space into a destination space.
;
; Negative example:
; assuming add-atoms over get-atoms materializes the batch on every engine.

!(bind! &dst-direct (new-space))
!(bind! &src-direct (new-space))
!(add-atom &dst-direct (dst direct))
!(add-atom &src-direct (src direct))
!(add-atoms &dst-direct (get-atoms &src-direct))
!(collapse (match &dst-direct $x $x))

!(bind! &dst-collapse (new-space))
!(bind! &src-collapse (new-space))
!(add-atom &dst-collapse (dst collapse))
!(add-atom &src-collapse (src collapse))
!(add-atoms &dst-collapse (collapse (get-atoms &src-collapse)))
!(collapse (match &dst-collapse $x $x))

!(bind! &dst-each (new-space))
!(bind! &src-each (new-space))
!(add-atom &dst-each (dst each))
!(add-atom &src-each (src each))
!(collapse (match &src-each $x (add-atom &dst-each $x)))
!(collapse (match &dst-each $x $x))

(: copy-space-atoms-fold (-> Atom Atom Number))
(= (copy-space-atoms-fold $src $dst)
   (foldall + (match $src $x (let $u (add-atom $dst $x) 0)) 0))

!(bind! &dst-fold (new-space))
!(bind! &src-fold (new-space))
!(add-atom &dst-fold (dst fold))
!(add-atom &src-fold (src fold))
!(copy-space-atoms-fold &src-fold &dst-fold)
!(collapse (match &dst-fold $x $x))

!(bind! &import-copy-dst (new-space))
!(import! &import-copy-src ./support/git_module_seed/module.metta)
!(collapse (match &import-copy-src $x (add-atom &import-copy-dst $x)))
!(collapse (match &import-copy-dst $x $x))

!(bind! &import-fold-dst (new-space))
!(import! &import-fold-src ./support/git_module_seed/module.metta)
!(copy-space-atoms-fold &import-fold-src &import-fold-dst)
!(collapse (match &import-fold-dst $x $x))
