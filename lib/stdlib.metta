; CeTTa Standard Library — precompiled into binary at build time.
; These equations are loaded into &self before user code.
; They are introspectable via (match &self (= ...) ...).

; ── Type declarations ─────────────────────────────────────────────────
(: id (-> $t $t))
(: if (-> Bool Atom Atom $t))
(: noeval (-> Atom Atom))
(: atom-subst (-> Atom Variable Atom Atom))
(: car-atom (-> Expression Atom))
(: cdr-atom (-> Expression Expression))
(: ErrorType Type)
(: Error (-> Atom Atom ErrorType))
(: if-decons-expr (-> Expression Variable Variable Atom Atom %Undefined%))
(: if-error (-> Atom Atom Atom %Undefined%))
(: return-on-error (-> Atom Atom %Undefined%))
(: filter-atom (-> Expression Variable Atom Expression))
(: map-atom (-> Expression Variable Atom Expression))
(: empty (-> Atom))
(: unique (-> Atom %Undefined%))
(: union (-> Atom Atom %Undefined%))
(: intersection (-> Atom Atom %Undefined%))
(: subtraction (-> Atom Atom %Undefined%))
(: unique-atom (-> Expression Expression))
(: union-atom (-> Expression Expression Expression))
(: intersection-atom (-> Expression Expression Expression))
(: subtraction-atom (-> Expression Expression Expression))
(: =alpha (-> Atom Atom Bool))
(: if-equal (-> Atom Atom Atom Atom %Undefined%))
(: sealed (-> Atom Atom Atom))
(: capture (-> Atom Atom))
(: quote (-> Atom Atom))
(: unquote (-> %Undefined% %Undefined%))
(: switch-internal (-> Atom Expression Atom))
(: import! (-> Atom Atom (->)))
(: select (-> Atom %Undefined%))
(: collect (-> Atom Expression))
(: fold (-> Atom Atom Variable Variable Atom %Undefined%))
(: fold-by-key (-> Atom Atom Variable Variable Atom Atom %Undefined%))
(: reduce (-> Atom Atom Variable Variable Atom %Undefined%))
(: once (-> Atom %Undefined%))
(: search-policy (-> Atom Atom))
(: new-space (-> SpaceType))
(: new-space (-> Atom SpaceType))
(: space-set-backend! (-> SpaceType Atom (->)))
(: space-set-match-backend! (-> SpaceType Atom (->)))
(: space-len (-> SpaceType Number))
(: space-push (-> SpaceType Atom (->)))
(: space-peek (-> SpaceType %Undefined%))
(: space-pop (-> SpaceType %Undefined%))
(: space-get (-> SpaceType Number %Undefined%))
(: space-truncate (-> SpaceType Number (->)))
(: step! (-> SpaceType Number))
(: step! (-> SpaceType Number Number))
(: is-function (-> Type Bool))
(: type-cast (-> Atom Type SpaceType %Undefined%))
(: match-types (-> Type Type Atom Atom %Undefined%))
(: first-from-pair (-> Atom Atom))
(: match-type-or (-> Bool Type Type Bool))
(: get-type-space (-> SpaceType Atom Atom))
(: _minimal-foldl-atom (-> Expression Atom Variable Variable Atom SpaceType %Undefined%))
(: _collapse-add-next-atom-from-collapse-bind-result (-> Expression Expression Atom))
(: foldl-atom-in-space (-> Expression Atom Variable Variable Atom SpaceType %Undefined%))
(: foldl-atom (-> Expression Atom Variable Variable Atom %Undefined%))
(: add-reducts (-> SpaceType %Undefined% (->)))
(: add-atoms (-> SpaceType Expression (->)))
(: @doc (-> Atom DocDescription DocInformal))
(: @doc (-> Atom DocDescription DocParameters DocReturnInformal DocInformal))
(: @desc (-> String DocDescription))
(: @param (-> String DocParameterInformal))
(: @param (-> DocType DocDescription DocParameter))
(: @return (-> String DocReturnInformal))
(: @return (-> DocType DocDescription DocReturn))
(: @doc-formal (-> DocItem DocKindFunction DocType DocDescription DocParameters DocReturn DocFormal))
(: @doc-formal (-> DocItem Atom DocType DocDescription DocFormal))
(: @kind (-> Atom %Undefined%))
(: @item (-> Atom DocItem))
(: @type (-> Type DocType))
(: @params (-> Expression DocParameters))
(: get-doc (-> Atom %Undefined%))
(: get-doc (-> SpaceType Atom %Undefined%))
(: get-doc-single-atom (-> SpaceType Atom %Undefined%))
(: get-doc-function (-> SpaceType Atom Type %Undefined%))
(: undefined-doc-function-type (-> Expression Type))
(: get-doc-params (-> Expression Atom Expression (Expression Atom)))
(: get-doc-atom (-> SpaceType Atom %Undefined%))
(: help! (-> Atom (->)))
(: help! (-> (->)))
(: help-space! (-> SpaceType (->)))
(: help-param! (-> Atom (->)))
(: module-space-no-deps (-> SpaceType SpaceType))
(: for-each-in-atom (-> Expression Atom (->)))
(: noreduce-eq (-> Atom Atom Bool))
(: sort-strings (-> Expression Expression))
(: assertAlphaEqual (-> Atom Atom (->)))
(: assertAlphaEqualMsg (-> Atom Atom Atom (->)))
(: assertAlphaEqualToResultMsg (-> Atom Atom Atom (->)))
(: max-atom (-> %Undefined% Number))
(: min-atom (-> %Undefined% Number))
(: MathDomainError (-> Number Atom ErrorType))
(: ModuleImportCycle (-> String ErrorType))
(: ModuleParseFailed (-> String ErrorType))
(: pow-math (-> Number Number Number))
(: sqrt-math (-> Number Number))
(: abs-math (-> Number Number))
(: log-math (-> Number Number Number))
(: trunc-math (-> Number Number))
(: ceil-math (-> Number Number))
(: floor-math (-> Number Number))
(: round-math (-> Number Number))
(: sin-math (-> Number Number))
(: asin-math (-> Number Number))
(: cos-math (-> Number Number))
(: acos-math (-> Number Number))
(: tan-math (-> Number Number))
(: atan-math (-> Number Number))
(: isnan-math (-> Number Bool))
(: isinf-math (-> Number Bool))
(: size (-> %Undefined% Number))
(: size-atom (-> Expression Number))
(: index-atom (-> Expression Number Atom))

(@doc sort-strings
  (@desc "Sorts expression which contains strings in alphabet order")
  (@params (
    (@param "List of strings")))
  (@return "Sorted list of strings"))

(@doc _collapse-add-next-atom-from-collapse-bind-result
  (@desc "Adds atom from collapse-bind result to the passed list")
  (@params (
    (@param "List to add an atom into")
    (@param "collapse-bind result to extract atom from, should be a pair of atom and bindings")))
  (@return "List with new atom as a head"))

(@doc select
  (@desc "HE-extended committed choice over nondeterministic results. The primary form (select results) returns the first non-empty result encountered under the active traversal policy. The extended forms (select k results), (select (search-policy lane) results), and (select k (search-policy lane) results) expose bounded selection and explicit search-lane choice.")
  (@params (
    (@param "Result expression to evaluate")))
  (@return "Selected result, or a bounded packet in the two-argument extended form"))

(@doc collect
  (@desc "Compatibility collector over nondeterministic results. Evaluates the result fan-out, packages all non-empty results into one expression, and preserves the current traversal order. Prefer collapse in new code when you do not need explicit search-policy or result-order control. The policy form (collect (search-policy lane) results) keeps the lane/order choice explicit.")
  (@params (
    (@param "Result expression to evaluate")))
  (@return "Collected expression of surviving results in current traversal order"))

(@doc fold
  (@desc "HE-extended fold over nondeterministic results. Evaluates a result fan-out, skips Empty results, and folds the surviving items into one accumulator without first packaging them into an expression. The primary form is (fold results init $acc $item step). The policy form (fold (search-policy lane) results init $acc $item step) keeps the same lane and ordering controls as select and collect. Each fold step must produce exactly one non-empty result. If the step is order-sensitive, the final value follows engine/policy traversal order rather than a semantic ordering guarantee.")
  (@params (
    (@param "Result expression to evaluate")
    (@param "Initial accumulator value")
    (@param "Accumulator binder")
    (@param "Current item binder")
    (@param "Step expression that yields the next accumulator")))
  (@return "Final accumulator value after folding the surviving results"))

(@doc fold-by-key
  (@desc "HE-extended grouped fold over nondeterministic results. Evaluates a result fan-out, skips Empty results, computes one grouping key per surviving item, and folds each group into its own accumulator without first packaging the full result set by key in user code. The primary form is (fold-by-key results init $acc $item key step). The policy form (fold-by-key (search-policy lane) results init $acc $item key step) keeps the same lane and ordering controls as select, collect, and fold. The key expression must produce exactly one non-empty key per item. The step expression must produce exactly one non-empty next accumulator. Results are emitted as two-element expressions (key accumulator) in first-encounter order under the active traversal policy.")
  (@params (
    (@param "Result expression to evaluate")
    (@param "Initial accumulator value for each key")
    (@param "Accumulator binder")
    (@param "Current item binder")
    (@param "Key expression that yields the grouping key for the current item")
    (@param "Step expression that yields the next accumulator for the current item's key")))
  (@return "Grouped fold results as two-element expressions of key and final accumulator"))

(@doc reduce
  (@desc "Compatibility alias for fold. Prefer (fold results init $acc $item step) in new code. The policy form (reduce (search-policy lane) results init $acc $item step) remains supported for legacy call sites.")
  (@params (
    (@param "Result expression to evaluate")
    (@param "Initial accumulator value")
    (@param "Accumulator binder")
    (@param "Current item binder")
    (@param "Step expression that yields the next accumulator")))
  (@return "Final accumulator value after folding the surviving results"))

(@doc once
  (@desc "Compatibility alias for select. Prefer select in new code. The policy form (once (search-policy lane) results) is supported for legacy call sites that still want explicit search-lane choice.")
  (@params (
    (@param "Result expression to evaluate")))
  (@return "First non-empty result encountered under the active traversal policy"))

(@doc search-policy
  (@desc "Search-policy literal constructor for explicit controlled-search calls. The current native CeTTa lane is recursive-dependent-proof, and it supports result-order control through (search-policy recursive-dependent-proof), (search-policy recursive-dependent-proof (order reverse)), (search-policy recursive-dependent-proof (order lex)), and (search-policy recursive-dependent-proof (order shortlex)). The lex mode orders results by canonical rendered-atom bytes. The shortlex mode orders results by canonical rendered-atom length first, then bytewise lexical order. ATP and solver lanes are named here for future explicit oracle work, but are not executed by the native evaluator yet.")
  (@params (
    (@param "Search lane name such as recursive-dependent-proof")))
  (@return "Opaque policy atom consumed by select, fold, fold-by-key, collect, or once"))

(@doc with-space-snapshot
  (@desc "CeTTa / HE-extended snapshot-binding surface. Binds a frozen copy of a space to the first parameter while evaluating the body, so reads see the snapshot while writes can still target the live space explicitly.")
  (@params (
    (@param "Binder or pattern that receives the snapshot space")
    (@param "Live source space to snapshot")
    (@param "Body evaluated with the snapshot binding in scope")))
  (@return "Results produced by evaluating the body against the snapshot-bound space"))

(@doc new-space
  (@desc "Creates a fresh mutable space. The HE-compatible form is (new-space). The he_extended/he_prime form accepts semantic kinds such as stack, queue, or hash, and also explicit engine selectors such as native or pathmap. Stack and queue produce ordered spaces with explicit peek/pop/get/truncate behavior under their chosen discipline. Hash-space keeps unordered atomspace semantics but adds efficient exact-membership and nodup support for stable closed atoms. The MORK lane is no longer constructed through generic new-space: use (mork:new-space) from lib/mork.metta when you want the explicit MORK API.")
  (@params (
    (@param "Optional selector such as stack, queue, hash, native, or pathmap in extension profiles")))
  (@return "Fresh space value"))

(@doc space-set-backend!
  (@desc "Preferred compact spelling for the low-level backend mutation surface.")
  (@params (
    (@param "Space to mutate")
    (@param "Backend symbol such as native, pathmap, or the diagnostic lane native-candidate-exact")))
  (@return "Unit on success"))

(@doc space-set-match-backend!
  (@desc "Legacy longer spelling for the low-level backend mutation surface. This remains available for compatibility, but new code should prefer space-set-backend!. The MORK lane is no longer reachable here: use lib/mork.metta for explicit MORK handles instead.")
  (@params (
    (@param "Space to mutate")
    (@param "Backend symbol such as native, pathmap, or the diagnostic lane native-candidate-exact")))
  (@return "Unit on success"))

(= (space-set-match-backend! $space $backend)
  (space-set-backend! $space $backend))

(@doc space-len
  (@desc "Returns the current number of stored items in a space. Unlike count-atoms, this is an explicit structured-space surface.")
  (@params (
    (@param "Space to inspect")))
  (@return "Current item count as a Number"))

(@doc size
  (@desc "Returns the size of a concrete expression or space. On expressions it matches size-atom; on spaces it returns the current number of stored items. This is CeTTa's clean extension spelling; size-atom remains the HE-compatible expression alias, while space-len and count-atoms remain transitional space aliases.")
  (@params (
    (@param "Expression or space to inspect")))
  (@return "Element count for expressions, or stored item count for spaces"))

(@doc space-push
  (@desc "Adds one atom to an ordered space. For stack-spaces this is LIFO push; for queue-spaces this is FIFO enqueue.")
  (@params (
    (@param "Target ordered space")
    (@param "Atom to add")))
  (@return "Unit on success"))

(@doc space-peek
  (@desc "Reads the current next element of an ordered space without mutating it. Stack-spaces peek at the top; queue-spaces peek at the front. Empty ordered spaces return a kind-specific runtime error.")
  (@params (
    (@param "Target ordered space")))
  (@return "Current top/front element"))

(@doc space-pop
  (@desc "Removes and returns the current next element of an ordered space. Stack-spaces pop from the top; queue-spaces dequeue from the front. Empty ordered spaces return a kind-specific runtime error.")
  (@params (
    (@param "Target ordered space")))
  (@return "Removed top/front element"))

(@doc space-get
  (@desc "Reads the element at a zero-based index from an ordered space's storage. Out-of-bounds indices return an IndexOutOfBounds runtime error.")
  (@params (
    (@param "Target ordered space")
    (@param "Zero-based index")))
  (@return "Stored element at that index"))

(@doc space-truncate
  (@desc "Drops all ordered-space elements at indices greater than or equal to the given zero-based length, preserving the prefix [0, len).")
  (@params (
    (@param "Target ordered space")
    (@param "New retained length")))
  (@return "Unit on success"))

(@doc step!
  (@desc "Low-level stepping surface. The one-argument form performs one step. The two-argument form performs up to the given non-negative step count. Ordinary native/pathmap spaces report NoStepSemantics. Explicit MORK workflows should use mork:step! from lib/mork.metta by default; bare step! on Space mork stays guarded unless you enable (pragma! mork-space-sugar allow).")
  (@params (
    (@param "Target space")
    (@param "Optional non-negative step count")))
  (@return "Number of steps actually performed"))

(@doc include
  (@desc "Loads a module into a space transactionally. The one-argument form (include spec) merges into &self. The two-argument form (include &dst spec) merges into an already-existing named space; on failure the target space is rolled back unchanged. Relative specs may resolve to .metta, .act, or raw .mm2 files. Including a .mm2 file lowers its surface into ordinary atoms; those atoms stay plain data unless the destination space is already an explicit MorkSpace.")
  (@params (
    (@param "Module spec or path to include, or an explicit destination space reference followed by the module spec")))
  (@return "Unit on success, or an Error value on failure"))

(@doc import!
  (@desc "Loads a module into &self or a fresh &name. CeTTa keeps imports transactional when materializing into existing spaces; fresh compiled .act imports may stay attached as compiled PathMap spaces instead of reparsing, and raw .mm2 imports are stored as ordinary atoms unless the destination space is already an explicit MorkSpace. Loader failures can surface ModuleImportCycle or ModuleParseFailed.")
  (@params (
    (@param "Destination space reference: &self or a fresh &name")
    (@param "Module spec or path to import")))
  (@return "Unit on success, or an Error value on failure"))

(@doc MathDomainError
  (@desc "Structured runtime error constructor for invalid real-domain math calls. The first argument is the 1-based argument index, and the second is a symbolic domain constraint tag.")
  (@params (
    (@param "1-based argument index that violated the domain")
    (@param "Symbolic domain constraint tag, such as NonNegativeReal or PositiveRealNotOne")))
  (@return "ErrorType payload describing the violated real-domain contract"))

(@doc ModuleImportCycle
  (@desc "Structured runtime error constructor for cyclic module imports.")
  (@params (
    (@param "Resolved module path, shown relative to the active script or project root when possible")))
  (@return "ErrorType payload describing the detected import cycle"))

(@doc ModuleParseFailed
  (@desc "Structured runtime error constructor for malformed module files that fail CeTTa's file parser checks.")
  (@params (
    (@param "Resolved module path, shown relative to the active script or project root when possible")))
  (@return "ErrorType payload describing the module parse failure"))

(@doc sqrt-math
  (@desc "Square root over the reals. Negative input returns (MathDomainError 1 NonNegativeReal) instead of a silent NaN.")
  (@params (
    (@param "Input real number")))
  (@return "Square root as a Number when the input is within the real domain"))

(@doc log-math
  (@desc "Logarithm over the reals. The base must be positive and not equal to 1, and the input must be positive; otherwise CeTTa returns a MathDomainError.")
  (@params (
    (@param "Logarithm base")
    (@param "Input value")))
  (@return "Logarithm result as a Number when both arguments are in the real domain"))

(@doc asin-math
  (@desc "Inverse sine over the reals. Inputs outside [-1, 1] return (MathDomainError 1 ClosedUnitInterval).")
  (@params (
    (@param "Input real number in the closed unit interval")))
  (@return "Inverse sine as a Number when the input is within the real domain"))

(@doc acos-math
  (@desc "Inverse cosine over the reals. Inputs outside [-1, 1] return (MathDomainError 1 ClosedUnitInterval).")
  (@params (
    (@param "Input real number in the closed unit interval")))
  (@return "Inverse cosine as a Number when the input is within the real domain"))

(@doc pow-math
  (@desc "Exponentiation over the reals. Negative bases require integral exponents, and zero cannot be raised to a negative power; invalid combinations return MathDomainError.")
  (@params (
    (@param "Base")
    (@param "Exponent")))
  (@return "Exponentiation result as a Number when the arguments are within the real domain"))

; ── Equations ─────────────────────────────────────────────────────────

; if: canonical stdlib definition. Lazy branch handling now comes from the
; evaluator following the function type `(-> Bool Atom Atom $t)` plus the
; ordinary equation dispatcher, so these equations are both introspectable
; and executable.
(= (if True $then $else) $then)
(= (if False $then $else) $else)

; `noeval` preserves syntax through ordinary function application, and
; `atom-subst` uses it to substitute into templates without reducing the
; replacement atom first.
(= (noeval $x) $x)
(= (atom-subst $atom $var $templ)
  (function
    (chain (eval (noeval $atom)) $var
      (return $templ))))

; Public fold wrapper: CeTTa keeps the HE surface by calling the compatibility
; alias `_minimal-foldl-atom`, while `foldl-atom-in-space` remains the clearer
; explicit-space extension name over the same grounded engine.
(= (foldl-atom $list $init $a $b $op)
  (function
    (chain (context-space) $space
      (eval (_minimal-foldl-atom $list $init $a $b $op $space)))))

; quote itself is evaluator-backed so it preserves syntax without evaluating
; its body. Once that syntax is preserved, unquote can stay as an ordinary
; stdlib equation.
(= (unquote (quote $atom)) $atom)

(= (if-decons-expr $atom $head $tail $then $else)
  (function
    (eval
      (if-equal $atom ()
        (return $else)
        (chain (decons-atom $atom) $list
          (unify $list ($head $tail)
            (return $then)
            (return $else)))))))

(= (if-error $atom $then $else)
  (function
    (chain (eval (get-metatype $atom)) $meta
      (eval
        (if-equal $meta Expression
          (eval
            (if-equal $atom ()
              (return $else)
              (chain (decons-atom $atom) $list
                (unify $list ($head $tail)
                  (eval (if-equal $head Error (return $then) (return $else)))
                  (return $else)))))
          (return $else))))))

(= (return-on-error $atom $then)
  (function
    (eval
      (if-equal $atom Empty
        (return (return Empty))
        (eval
          (if-error $atom
            (return (return $atom))
            (return $then)))))))

(= (switch-internal $atom (($pattern $template) $tail))
  (function
    (unify $atom $pattern
      (return $template)
      (chain (eval (switch-minimal $atom $tail)) $ret
        (return $ret)))))

(= (is-function $type)
  (let $mtype (get-metatype $type)
    (unify $mtype Expression
      (let $size (size-atom $type)
        (unify $size 0
          False
          (let ($h $t) (decons-atom $type)
            (unify $h -> True False))))
      False)))

(= (match-types $type1 $type2 $then $else)
  (function
    (eval
      (if-equal $type1 %Undefined%
        (return $then)
        (eval
          (if-equal $type2 %Undefined%
            (return $then)
            (eval
              (if-equal $type1 Atom
                (return $then)
                (eval
                  (if-equal $type2 Atom
                    (return $then)
                    (unify $type1 $type2
                      (return $then)
                      (return $else))))))))))))

(= (first-from-pair $pair)
  (function
    (unify $pair ($first $second)
      (return $first)
      (return (Error (first-from-pair $pair) "incorrect pair format")))))

(= (match-type-or $folded $next $type)
  (function
    (chain (eval (match-types $next $type True False)) $matched
      (chain (eval (or $folded $matched)) $or
        (return $or)))))

; Small public stdlib wrappers that HE exposes over existing tuple/nondeterministic
; primitives. These are lightweight compatibility surfaces rather than new engines.
(= (empty) Empty)

(= (unique $arg)
  (let $c (collapse $arg)
    (let $u (unique-atom $c)
      (superpose $u))))

(= (union $arg1 $arg2)
  (let $c1 (collapse $arg1)
    (let $c2 (collapse $arg2)
      (let $u (union-atom $c1 $c2)
        (superpose $u)))))

(= (intersection $arg1 $arg2)
  (let $c1 (collapse $arg1)
    (let $c2 (collapse $arg2)
      (let $u (intersection-atom $c1 $c2)
        (superpose $u)))))

(= (subtraction $arg1 $arg2)
  (let $c1 (collapse $arg1)
    (let $c2 (collapse $arg2)
      (let $u (subtraction-atom $c1 $c2)
        (superpose $u)))))

(= (assertAlphaEqual $actual $expected)
  (assertAlphaEqualToResult $actual (collapse $expected)))

(= (assertAlphaEqualMsg $actual $expected $msg)
  (assertAlphaEqualToResultMsg $actual (collapse $expected) $msg))

(= (type-cast $atom $type $space)
  (function
    (chain (eval (get-metatype $atom)) $meta
      (eval
        (if-equal $type $meta
          (return $atom)
          (chain (eval (collapse-bind (eval (get-type $atom $space)))) $collapsed
            (chain (eval (map-atom $collapsed $pair (eval (first-from-pair $pair)))) $actual-types
              (chain (eval (foldl-atom $actual-types False $a $b (eval (match-type-or $a $b $type)))) $is-some-comp
                (eval
                  (if $is-some-comp
                    (return $atom)
                    (return (Error $atom BadType))))))))))))

(= (add-reducts $space $tuple)
  (foldl-atom $tuple () $a $b (add-atom $space $b)))

(= (add-atoms $space $tuple)
  (foldl-atom $tuple () $a $b (add-atom $space $b)))

(= (for-each-in-atom $expr $func)
  (if (noreduce-eq $expr ())
    ()
    (let $head (car-atom $expr)
      (let $tail (cdr-atom $expr)
        (let $_ ($func $head)
          (for-each-in-atom $tail $func))))))

(= (noreduce-eq $a $b)
  (== (quote $a) (quote $b)))

(= (module-space-no-deps $space)
  $space)

(= (get-doc $atom)
  (function
    (case (context-space)
      (($space
         (case (eval (get-doc $space $atom))
           ((Empty (return Empty))
            ($doc (return $doc)))))))))

(= (get-doc $space $atom)
  (let $meta-type (get-metatype $atom)
    (case $meta-type (
      (Expression (get-doc-atom $space $atom))
      ($_ (get-doc-single-atom $space $atom)) ))))

(= (get-doc-single-atom $space $atom)
  (let $type (get-type-space $space $atom)
    (if (is-function $type)
      (get-doc-function $space $atom $type)
      (get-doc-atom $space $atom))))

(= (undefined-doc-function-type $params)
  (if (== () $params)
    (%Undefined%)
    (let $params-tail (cdr-atom $params)
      (let $tail (undefined-doc-function-type $params-tail)
        (cons-atom %Undefined% $tail)))))

(= (get-doc-params $params $ret $types)
  (let $head-type (car-atom $types)
    (let $tail-types (cdr-atom $types)
      (if (== () $params)
        (let (@return $ret-desc) $ret
          (() (@return (@type $head-type) (@desc $ret-desc))))
        (let (@param $param-desc) (car-atom $params)
          (let $tail-params (cdr-atom $params)
            (let ($params' $result-ret) (get-doc-params $tail-params $ret $tail-types)
              (let $result-params (cons-atom (@param (@type $head-type) (@desc $param-desc)) $params')
                ($result-params $result-ret)))))))))

(= (get-doc-function $space $name $type)
  (let $docs
    (collapse
      (match $space (@doc $name $desc (@params $params) $ret)
        (let $type' (if (== $type %Undefined%) (undefined-doc-function-type $params) (cdr-atom $type))
          (let ($params' $ret') (get-doc-params $params $ret $type')
            (@doc-formal (@item $name) (@kind function) (@type $type) $desc (@params $params') $ret')))))
    (if (== $docs ())
      Empty
      (car-atom $docs))))

(= (get-doc-atom $space $atom)
  (let $type (get-type-space $space $atom)
    (let $docs
      (collapse
        (match $space (@doc $atom $desc)
          (@doc-formal (@item $atom) (@kind atom) (@type $type) $desc)))
      (if (== $docs ())
        (let $fallback (get-doc-function $space $atom %Undefined%)
          (if (== $fallback Empty) Empty $fallback))
        (car-atom $docs)))))

(= (help! $atom)
  (case (get-doc $atom) (
    ((@doc-formal (@item $item) (@kind function) (@type $type) (@desc $descr)
                  (@params $params)
                  (@return (@type $ret-type) (@desc $ret-desc)))
      (let () (println! (format-args "Function {}: {} {}" (noeval ($item $type $descr))))
        (let () (println! (format-args "Parameters:" ()))
          (let () (for-each-in-atom $params help-param!)
            (let () (println! (format-args "Return: (type {}) {}" (noeval ($ret-type $ret-desc))))
              ())))))
    ((@doc-formal (@item $item) (@kind function) (@type $type) (@desc $descr))
      (let () (println! (format-args "Function {}: {} {}" (noeval ($item $type $descr))))
        ()))
    ((@doc-formal (@item $item) (@kind atom) (@type $type) (@desc $descr))
      (let () (println! (format-args "Atom {}: {} {}" (noeval ($item $type $descr))))
        ()))
    (Empty (println! (format-args "No documentation found for {}" (noeval ($atom)))))
    ($other (Error $other "Cannot match @doc-formal structure")) )))

(= (help!)
  (function
    (chain (context-space) $space
      (chain (eval (help-space! $space)) $done
        (return $done)))))

(= (help-space! $space)
  (let $_
    (collapse
      (match $space (@doc $name (@desc $desc) $params $ret)
        (let () (println! (format-args "{}\n\t{}" (noeval ($name $desc))))
          Empty)))
    (let $_
      (collapse
        (match $space (@doc $name (@desc $desc))
          (let () (println! (format-args "{}\n\t{}" (noeval ($name $desc))))
            Empty)))
      ())))

(= (help-param! $param)
  (let (@param (@type $type) (@desc $desc)) $param
    (println! (format-args "  {} {}" (noeval ($type $desc))))))

; Higher-order list combinators from HE stdlib. These depend on `sealed` so
; recursive calls do not accidentally capture the user variable.
(= (filter-atom $list $var $filter)
  (function (chain (decons-atom $list) $ht
    (unify ($head $tail) $ht
        (chain (eval (sealed ($var) $filter)) $sealedfilter
          (chain (eval (filter-atom $tail $var $sealedfilter)) $tail-filtered
            (chain (eval (atom-subst $head $var $sealedfilter)) $filter-expr
              (chain $filter-expr $is-filtered
                (eval (if $is-filtered
                  (chain (cons-atom $head $tail-filtered) $res (return $res))
                  (return $tail-filtered) ))))))
      (return ()) ))))

(= (map-atom $list $var $map)
  (function (chain (decons-atom $list) $ht
    (unify ($head $tail) $ht
      (chain (eval (sealed ($var) $map)) $sealedmap
        (chain (eval (map-atom $tail $var $sealedmap)) $tail-mapped
          (chain (eval (atom-subst $head $var $sealedmap)) $map-expr
            (chain $map-expr $head-mapped
              (chain (cons-atom $head-mapped $tail-mapped) $res (return $res)) ))))
      (return ()) ))))
