; A compiled call can end in a goal it hands to the host: here a call to a
; function whose parameters are held (Atom), made after an earlier step of
; the same body added an equation.  The host answers it through the machine,
; and the call resumes with no continuation of its own, so the answer is the
; call's.  Rules are kept in a space and fired by transform; the body of the
; rule that fires adds a dispatch equation, then makes the held call.
!(import! &self (library lib_transform))
(= (list4 $a $b $c $d) (cons-atom $a (cons-atom $b (cons-atom $c (cons-atom $d ())))))
(= (rule! $name $patterns $key)
   (let $rule (list4 rule $name (cons-atom args (plain-each $patterns)) $key)
     (let $made (transform &rules (let $new (quote $rule)) $new &rules) True)))
(= (plain-each $patterns)
   (if (== $patterns ()) ()
     (let ($pattern $more) (decons-atom $patterns)
       (let $tail (plain-each $more) (cons-atom $pattern $tail)))))
(= (apply-rules $name $values)
   (let $arguments (cons-atom args $values)
     (let (fired $key) (transform &rules (rule $name $arguments $k) (fired $k))
       (rule-body $key $arguments))))
(= (ensure-dispatch $name)
   (let $known (collapse (match &notes (defined $name) known))
     (if (== $known ())
         (let $noted (add-atom &notes (defined $name))
           (add-atom &self (= (dispatch $name) $name)))
         known)))
(= (call-made $x) (apply-rules call-made (cons-atom $x ())))
(= (rule-body (key call-made 0) (args $x))
   (let $done (ensure-dispatch made) (held made ($y) (key made 1))))
!(rule! call-made ($x) (key call-made 0))
!(rule! call-made ($x) (key call-made 1))
(: held (-> Atom Atom Atom %Undefined%))
(= (held $name $patterns $key) (kept $name $patterns $key))
!(println! (out (collapse (call-made (made $x)))))
