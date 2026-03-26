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
(: filter-atom (-> Expression Variable Atom Expression))
(: map-atom (-> Expression Variable Atom Expression))
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
(: get-type-space (-> SpaceType Atom Atom))
(: _minimal-foldl-atom (-> Expression Atom Variable Variable Atom SpaceType %Undefined%))
(: foldl-atom-in-space (-> Expression Atom Variable Variable Atom SpaceType %Undefined%))
(: foldl-atom (-> Expression Atom Variable Variable Atom %Undefined%))
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
