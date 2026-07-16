!(import! &self ../../lib/lib_wmpln)

; Runnable shadow of PLNContextGuardOSLFDescentBridge.
;
; Lean authority:
;   contextCompleteWM_coexistsWithOSLFSetDescentPolicyBoundary
;   rhoCalcSetExt_set_not_vec_context_policy
;   rhoVecDropWitness_collectionDescent_nil_setExt
;
; Hand model:
;   CANONICAL blocks the concrete SET-context DROP witness.
;   SET-EXT admits the SET-context DROP witness.
;   SET-EXT still blocks the corresponding VEC-context DROP witness.
;
; This is an examples-lane witness, not a proof oracle.  It keeps the boundary
; explicit: WM-PLN context provenance says whether a source is available, while
; OSLF language policy says whether syntactic collection descent is admitted.

(= (wm-context-complete COMPLETE-WM) True)
(= (wm-context-complete MISSING-WM) False)

(= (allows-context-descent CANONICAL SET) False)
(= (allows-context-descent CANONICAL VEC) False)
(= (allows-context-descent SET-EXT SET) True)
(= (allows-context-descent SET-EXT VEC) False)

(= (drop-witness SET) (collection SET (PDrop (NQuote PZero))))
(= (drop-witness VEC) (collection VEC (PDrop (NQuote PZero))))
(= (drop-normal-form SET) (collection SET PZero))
(= (drop-normal-form VEC) (collection VEC PZero))

(= (collection-descent-step $lang $shape)
   (if (allows-context-descent $lang $shape)
       (drop-normal-form $shape)
       NoStep))

(= (wm-oslf-descent-step $wm $lang $shape)
   (if (wm-context-complete $wm)
       (collection-descent-step $lang $shape)
       NoStep))

(= (context-complete-but-descent-blocked $wm $lang $shape)
   (and (wm-context-complete $wm)
        (== (collection-descent-step $lang $shape) NoStep)))

(= (is-set-ext-selective $lang)
   (and (== (collection-descent-step $lang SET) (drop-normal-form SET))
        (== (collection-descent-step $lang VEC) NoStep)))

; Positive: the optional set-extension admits the concrete SET DROP witness
; when the WM context is complete.
!(assertEqualToResult (wm-context-complete COMPLETE-WM) (True))
!(assertEqualToResult (allows-context-descent SET-EXT SET) (True))
!(assertEqualToResult
   (wm-oslf-descent-step COMPLETE-WM SET-EXT SET)
   ((collection SET PZero)))

; Negative: canonical rhoCalc blocks the same SET witness even with complete
; WM context.  Provenance availability is not syntactic descent permission.
!(assertEqualToResult (allows-context-descent CANONICAL SET) (False))
!(assertEqualToResult
   (wm-oslf-descent-step COMPLETE-WM CANONICAL SET)
   (NoStep))
!(assertEqualToResult
   (context-complete-but-descent-blocked COMPLETE-WM CANONICAL SET)
   (True))

; Negative: SET-EXT is selective.  It enables SET contexts, not every
; collection form, so the matching VEC witness is still blocked.
!(assertEqualToResult (allows-context-descent SET-EXT VEC) (False))
!(assertEqualToResult
   (collection-descent-step SET-EXT VEC)
   (NoStep))
!(assertEqualToResult
   (wm-oslf-descent-step COMPLETE-WM SET-EXT VEC)
   (NoStep))
!(assertEqualToResult (is-set-ext-selective SET-EXT) (True))

; Negative: missing WM context blocks the bridge-level step even for the
; language that admits the syntactic SET descent.
!(assertEqualToResult (wm-context-complete MISSING-WM) (False))
!(assertEqualToResult
   (wm-oslf-descent-step MISSING-WM SET-EXT SET)
   (NoStep))
