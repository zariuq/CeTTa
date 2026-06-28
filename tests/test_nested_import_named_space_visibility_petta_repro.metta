; Minimal repro for the current WM-PLN-style nested named-space import seam.
; Positive example:
; direct import into a fresh named space from the top-level file.
;
; Negative example:
; import a helper module that itself binds a named space, then assume the
; importing file can query that named space identically on every engine.

!(import! &direct-top-space ./support/git_module_seed/module.metta)
!(collapse (match &direct-top-space $x $x))

!(import! &self ./support/named_space_import_bridge/module_parent.metta)
!(collapse (match &nested-bridge-space $x $x))
