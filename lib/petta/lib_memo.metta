; Native PeTTa memoization capability.  The familiar import spelling is
; retained for source compatibility, while CeTTa registers and executes
; these controls entirely inside the native PeTTa realization.

!(import_prolog_function
  (superpose
    (memoize
     config-memoize
     get-memoize-config
     clear-memoize
     invalidate-memoize
     is-memoized
     get-memoize-stats
     clear-memoize-stats)))
