; Tiny seam reproducer for the WM-PLN PeTTa flat-loader split.
; Positive example:
; import each module into a fresh named space, then batch-copy atoms into the
; target space you want to query.
;
; Negative example:
; try to import twice into the same named space and assume every engine accepts
; that as an append operation.

!(import! &merge-space ./support/git_module_seed/module.metta)
!(match &merge-space (git-root $x) $x)
!(import! &merge-source ./support/git_module_seed/module.metta)
!(add-atoms &merge-space (collapse (get-atoms &merge-source)))
!(match &merge-space (git-root $x) $x)

!(import! &repeat-space ./support/git_module_seed/module.metta)
!(import! &repeat-space ./support/git_module_seed/module.metta)
