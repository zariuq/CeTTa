!(assertEqual (if-decons-expr (alpha beta) $head $tail $head fallback) alpha)
!(assertEqual (if-decons-expr () $head $tail $head fallback) fallback)

!(assertEqual (if-error (Error boom fail) yes no) yes)
!(assertEqual (if-error boom yes no) no)

!(assertEqual (function (return-on-error Empty later)) Empty)
!(assertEqual (function (return-on-error (Error boom fail) later)) (Error boom fail))
!(assertEqual (function (chain (return-on-error ok later) $x (return $x))) later)

!(assertEqual (is-function (-> A B)) True)
!(assertEqual (is-function Atom) False)

!(assertEqual (match-types Number Number yes no) yes)
!(assertEqual (match-types Number Bool yes no) no)
!(assertEqual (match-types Atom Bool yes no) yes)

!(assertEqual (first-from-pair (left right)) left)
!(assertEqualToResult (first-from-pair single)
  ((Error (first-from-pair single) "incorrect pair format")))

!(assertEqual (match-type-or False Number Number) True)
!(assertEqual (match-type-or False Number Bool) False)
!(assertEqual (noreduce-eq (+ 1 2) (+ 1 2)) True)
!(assertEqual (noreduce-eq (+ 1 2) 3) False)

!(bind! &tmp (new-space))
!(assertEqual (add-atoms &tmp (a b)) ())
!(assertEqual (collapse (match &tmp $x $x)) (a b))
