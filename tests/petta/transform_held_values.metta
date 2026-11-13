; transform's native step answers as its library does where a value is held:
; a matched atom, an instance or a let's value that holds code stays a value,
; a branch fires when it is continued and re-checks its conditions against the
; store the branches before it left, a let's code binds the variables it
; binds, and an error a let's code raises is the caller's to catch.
; The authored library (lib_transform_swi) answers the same expected output.
!(import! &self (library lib_transform))
(= (seen1 $x) (saw $x))
(= (same $a $a) True)
(= (made2 $v) (m2 $v))

; A matched atom that holds code reaches a template that calls a function as
; it is stored, at the top and in a function's body; match answers alike.
!(bind! &held (new-space))
!(add-atom &held (arith (made a (+ 41 1))))
!(add-atom &held (arith (made b c)))
(= (seen-in $s) (transform $s (arith $b) (seen1 $b)))
!(matched-atom-call (sort-atom (collapse (transform &held (arith $b) (seen1 $b)))))
!(matched-atom-body (sort-atom (collapse (seen-in &held))))
!(matched-atom-match (sort-atom (collapse (match &held (arith $b) (seen1 $b)))))
; control: code written in the template itself runs, once per branch
!(template-code-runs (collapse (transform &held (arith $b) (seen1 (+ 1 1)))))

; A row read in a function's body is the row, as at the top.
!(bind! &rows (new-space))
!(add-atom &rows (kept (+ 41 1)))
(= (rows-of $space) (transform $space (read $atom) $atom))
!(row-top (collapse (transform &rows (read $atom) $atom)))
!(row-body (collapse (rows-of &rows)))
; a template that is a source variable bound to a list of clauses holding code
!(add-atom &rows (c ((p 1) (q 2))))
!(add-atom &rows (c ((+ 1 2) (q 2))))
(= (clauses-of $space) (transform $space (c $cand) $cand))
!(clause-lists (sort-atom (collapse (clauses-of &rows))))
!(clause-lists-let (sort-atom (collapse (let $x (clauses-of &rows) (k $x)))))

; A function destination receives each instance as it is; a parameter
; declared Atom keeps it unevaluated.
!(bind! &dest (new-space))
(: keepf (-> Atom %Undefined%))
(= (keepf $x) (add-atom &dest (got $x)))
!(fn-dest (collapse (transform &dest (let $new (quote (r (+ 1 2)))) $new keepf)))
!(fn-dest-template (collapse (transform &dest (let $new (quote (u 7))) (w (+ 1 2) $new) keepf)))
!(fn-dest-stored (sort-atom (collapse (get-atoms &dest))))
; control: a function that evaluates its argument runs the code it holds
!(bind! &ran (new-space))
(= (runf $x) (let $v (eval $x) (add-atom &ran (ran $v))))
!(fn-dest-runs (collapse (transform &ran (let $new (quote (+ 1 2))) $new runf)))
!(fn-dest-ran (collapse (get-atoms &ran)))
; the first answer alone routes its instance as the whole transform does
(= (keepu $x) (got-u $x))
!(once-fn-dest (once (transform &dest (let $new (quote (r (+ 1 2)))) $new keepu)))
!(once-fn-dest-row (once (transform &rows (read (kept $v)) (w $v) keepu)))
!(once-caller-row (once (transform &rows (read (kept $v)) (seen1 $v))))

; A let's value from a parameter declared Atom stays the value.
!(bind! &param (new-space))
(: param-made (-> Atom %Undefined%))
(= (param-made $atom) (transform &param (let $new (quote $atom)) (made $new)))
(: param-kept (-> Atom %Undefined%))
(= (param-kept $atom)
   (let (made $value) (transform &param (let $new (quote $atom)) (made $new)) (kept $value)))
!(param-let-value (collapse (param-made (r (+ 1 2)))))
!(param-let-bound (collapse (param-kept (r (+ 1 2)))))
; control: a parameter's value written into a caller template as code, outside
; any source, is evaluated with it (only source variables are held)
(: param-written (-> Atom %Undefined%))
(= (param-written $atom) (transform &param () (made $atom)))
!(param-written-code (collapse (param-written (+ 1 2))))

; Each branch fires when it is continued: a later branch whose absence guard
; an earlier answer's continuation broke fires nothing.
!(bind! &r (new-space))
!(add-atom &r (r 1))
!(add-atom &r (r 2))
!(add-atom &r (r 3))
!(bind! &w (new-space))
!(guard-other-space (collapse (let $k (transform &r (, (r $x) (absent &w (stop))) $x)
                                (let $_ (add-atom &w (stop)) $k))))
!(bind! &r2 (new-space))
!(add-atom &r2 (r 1))
!(add-atom &r2 (r 2))
!(add-atom &r2 (r 3))
!(guard-same-space (collapse (let $k (transform &r2 (, (r $x) (absent (stop))) $x)
                               (let $_ (add-atom &r2 (stop)) $k))))
; control: a continuation that leaves the guard holding lets every branch fire
!(bind! &w2 (new-space))
!(guard-kept (sort-atom (collapse (let $k (transform &r (, (r $x) (absent &w2 (stop))) $x)
                                    (let $_ (add-atom &w2 (seen $k)) $k)))))

; A let's code binds what it binds: a function run backwards, and a variable
; a stored atom holds.
!(bind! &n (new-space))
!(add-atom &n (n 3))
!(let-binds (collapse (transform &n (, (read (n $m)) (let True (same $w $m))) (got $w $m))))
!(let-binds-match (collapse (match &n (n $m) (let True (same $w $m) (got $w $m)))))
!(bind! &open (new-space))
!(add-atom &open (cell $z (uses $z)))
!(let-binds-stored (collapse (transform &open (, (take (cell $c $u)) (let True (same $c 7))) (cell2 $c $u) &open)))
!(let-binds-stored-row (collapse (get-atoms &open)))
; controls: code that binds nothing, and a binding the code refuses
!(let-binds-nothing (collapse (transform &n (, (read (n $m)) (let $k (+ $m 1))) (got $k))))
!(let-binding-refused (collapse (transform &n (, (read (n $m)) (let True (same $m 4))) (got $m))))

; An error a let's code raises is the caller's to catch, whatever route the
; transform takes, and the row its firing would have taken stays.
!(bind! &t (new-space))
!(add-atom &t (s 1))
!(let-error-space (catch (collapse (transform &t (, (take (s $x)) (let $v (+ 1 2 3))) (made $v) &t))))
!(let-error-call (catch (collapse (transform &t (, (take (s $x)) (let $v (+ 1 2 3))) (made2 $v)))))
!(let-error-once (catch (once (transform &t (, (take (s $x)) (let $v (+ 1 2 3))) (made $v)))))
!(let-error-bare (catch (let $r (transform &t (, (take (s $x)) (let $v (+ 1 2 3))) (made2 $v)) $r)))
!(let-error-all (catch (collapse (transform &t (all (, (take (s $x)) (let $v (+ 1 2 3)))) (made $v)))))
!(let-error-row-stays (collapse (get-atoms &t)))
; control: code that catches its own error answers it as a value, and fires
!(let-error-caught-inside (collapse (transform &t (, (take (s $x)) (let $v (catch (+ 1 2 3)))) (made $v))))
!(let-error-row-taken (collapse (get-atoms &t)))

; Computed list values unify through their private carrier. An authored
; constructor spelled cons remains data; it does not acquire list meaning.
(= (list-cell $head $tail) (cons-atom $head $tail))
!(bind! &lists (new-space))
!(add-atom &lists (items (1 2 3)))
!(list-value-open (collapse (transform &lists (, (items $list) (let $list (list-cell $h $t))) (parts $h $t))))
!(list-value-decompose (collapse (transform &lists (, (items $list) (let ($h $t) (decons-atom $list))) (parts $h $t))))
!(list-value-wrong-head (collapse (transform &lists (, (items $list) (let $list (list-cell 9 $t))) (parts 9 $t))))
!(list-value-raw-cons (collapse (transform &lists (, (items $list) (let $list (quote (cons 1 (2 3))))) matched)))
