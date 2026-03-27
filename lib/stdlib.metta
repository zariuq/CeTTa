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
(: select (-> Atom %Undefined%))
(: collect (-> Atom Expression))
(: once (-> Atom %Undefined%))
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
  (@desc "HE-extended committed choice over a nondeterministic stream. The primary form (select stream) returns the first non-empty result. The extended form (select k stream) returns up to k results, with k=0 yielding ().")
  (@params (
    (@param "Stream expression to evaluate")))
  (@return "Selected result, or a bounded packet in the two-argument extended form"))

(@doc collect
  (@desc "HE-extended stream collector paired with select. Evaluates a nondeterministic stream and packages all non-empty results into one expression.")
  (@params (
    (@param "Stream expression to evaluate")))
  (@return "Collected expression of surviving results"))

(@doc once
  (@desc "Compatibility alias for select. Prefer select in new code.")
  (@params (
    (@param "Stream expression to evaluate")))
  (@return "First non-empty result from the stream"))

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
