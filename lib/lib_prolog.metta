; Generic Prolog helper surface layered over CeTTa's foreign seam.
; This is intended for library-tier interoperability, not as an alternate
; implementation substrate for the core language semantics.

(: prolog-consult! (-> Atom (->)))
(= (prolog-consult! $prologfile)
   (pl-consult $prologfile))

(: prolog-use-module! (-> Atom (->)))
(= (prolog-use-module! $prologfile)
   (pl-use-module $prologfile))

(: prolog-import! (-> Atom (->)))
(= (prolog-import! $function)
   (pl-import $function))

(: prolog-atom (-> Atom Atom))
(= (prolog-atom $function)
   (pl-atom $function))

(: prolog-call (-> Atom Bool))
(= (prolog-call $goal)
   (pl-call $goal))

(: prolog-assertz (-> Atom Bool))
(= (prolog-assertz $goal)
   (assertzPredicate $goal))

(: prolog-asserta (-> Atom Bool))
(= (prolog-asserta $goal)
   (assertaPredicate $goal))

(: prolog-retract (-> Atom Bool))
(= (prolog-retract $goal)
   (retractPredicate $goal))

(: import-prolog-functions (-> Expression (->)))
(= (import-prolog-functions $importfunctions)
   (for-each-in-atom $importfunctions prolog-import!))

(: import-prolog-functions-from-file (-> Atom Expression (->)))
(= (import-prolog-functions-from-file $prologfile $importfunctions)
   (let $_ (prolog-consult! $prologfile)
     (import-prolog-functions $importfunctions)))

(: import-prolog-functions-from-module (-> Atom Expression (->)))
(= (import-prolog-functions-from-module $prologfile $importfunctions)
   (let $_ (prolog-use-module! $prologfile)
     (import-prolog-functions $importfunctions)))
