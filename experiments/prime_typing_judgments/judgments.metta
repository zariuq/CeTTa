; Experimental judgment adapter over the existing Prime checker.
; |- is inspectable data, not an assertion. Context binders are ordered
; declarations with distinct symbol names in this prototype.
(= (context-name-present $name $names)
   (case $names
     ((() False)
      ($_ (let ($head $rest) (decons-atom $names)
            (if (=alpha $name $head) True (context-name-present $name $rest)))))))
(= (context-shape $context $seen)
   (case $context
     ((() Ready)
      ($_ (let ($binding $rest) (decons-atom $context)
          (case $binding
            (((: $name $domain)
              (if (== (get-metatype $name) Symbol)
                (if (context-name-present $name $seen)
                  (Unsupported duplicate-display-name)
                  (let $next (cons-atom $name $seen) (context-shape $rest $next)))
                (Unsupported non-symbol-binder)))
             ($_ (Unsupported context-binding)))))))))
(= (abstract-term $context $term)
   (case $context
     ((() (quote $term){$term})
      ($_ (let ($binding $rest) (decons-atom $context)
          (let (: $name $type) $binding
            (let (quote $body) (abstract-term $rest $term)
              (quote (lam ($name : $type) $body)){$name $type $body})))))))
(= (abstract-type $context $type)
   (case $context
     ((() (quote $type){$type})
      ($_ (let ($binding $rest) (decons-atom $context)
          (let (: $name $domain) $binding
            (let (quote $body) (abstract-type $rest $type)
              (quote (-> ($name : $domain) $body)){$name $domain $body})))))))
(= (close-judgment $context $term $type)
   (case (context-shape $context ())
     ((Ready
       (let (quote $closed-term) (abstract-term $context $term)
         (let (quote $closed-type) (abstract-type $context $type)
           (quote (type:check $closed-term $closed-type)){$closed-term $closed-type})))
      ($other $other))))
(= (judgment-call (quote (|- $context (: $term $type))))
   (close-judgment $context $term $type))
(= (judgment-call (quote (|- $context (formed $type $universe))))
   (close-judgment $context $type $universe))
; A conversion request checks reflexivity at the exact requested endpoints;
; it does not search for an arbitrary propositional equality proof.
(= (judgment-call (quote (|- $context (convert $left $right $type))))
   (let $witness (refl $left)
     (let $claim (id $type $left $right)
       (close-judgment $context $witness $claim))))
(= (judge $statement)
   (case (judgment-call $statement)
     (((quote $call) (eval $call))
      ($other $other))))
(= (judgment-outcome $statement)
   (case (judgment-call $statement)
     (((quote $call)
       (let $args (cons-atom $call ())
         (let $request (cons-atom try $args) (eval $request))))
      ($other $other))))
(= (quote-value $value)
   (let $args (cons-atom $value ()) (cons-atom quote $args)))
(= (inspect-judgment $statement)
   (function
     (case (judgment-call $statement)
       (((quote $call)
         (let $args (cons-atom $call ())
           (let $request (cons-atom type:kernel-query $args)
             (let $view (eval $request)
               (let $quoted-call (quote-value $call)
                 (let $quoted-view (quote-value $view)
                   (return (JudgmentView $statement $quoted-call $quoted-view))))))))
        ($other (return $other))))))
(= (publish-judgment $name $statement)
   (case (judgment-call $statement)
     (((quote (type:check $term $type))
       (type:theorem $name $type $term))
      ($other $other))))

; Existing : and = atoms are the input. This proposes native admission;
; the native definition checker, not this builder, grants it.
(= (definition-call (quote (: $name $type)) (quote $equations))
   (let $with-type (cons-atom $type $equations)
     (let $with-name (cons-atom $name $with-type)
       (let $call (cons-atom hol:define $with-name)
         (quote $call){$call}))))
(= (admit-definition $signature $equations)
   (let (quote $call) (definition-call $signature $equations)
     (eval $call)))
; Names chosen by this source builder avoid the complete supplied syntax.
; Native binding still uses the checker's existing scope elaboration. This
; small builder does not replace the general declared binding interface.
(= (name-occurs $name (quote $term))
   (case (get-metatype $term)
     ((Symbol (=alpha $name $term))
      (Expression
        (if (== (size-atom $term) 0) False
          (let ($head $tail) (decons-atom $term)
            (if (name-occurs $name (quote $head){$head}) True
                (name-occurs $name (quote $tail){$tail})))))
      ($_ False))))
(= (builder-name $prefix $avoid $index)
   (let* (($tail (cons-atom $index ()))
          ($parts (cons-atom $prefix $tail))
          ($text (format-args "rule-{}-{}" $parts))
          ($name (parse $text)))
     (if (name-occurs $name $avoid)
       (builder-name $prefix $avoid (+ $index 1))
       $name)))
; Generates an actual proof program for a derived rule. All binders are
; chosen away from the supplied function and carrier expressions.
(= (congruence-judgment $f $A $B)
   (function
     (let* (($avoid (quote ($f $A $B)){$f $A $B})
            ($x (builder-name x $avoid 0))
            ($y (builder-name y $avoid 0))
            ($e (builder-name e $avoid 0))
            ($result (builder-name result $avoid 0))
            ($proof (builder-name proof $avoid 0))
            ($code
              (quote (|- ((: $x $A) (: $y $A) (: $e (id $A $x $y)))
                (: (id:eliminate $A $x
                     (lam $result (lam $proof (id $B ($f $x) ($f $result))))
                     (refl ($f $x)) $y $e)
                   (id $B ($f $x) ($f $y))))){$f $A $B $x $y $e $result $proof}))
       (return $code))))

; Generating a datatype changes formation and constructor declarations
; together. The existing native datatype checker admits the result.
(= (box-declaration $T $make $A)
   (quote (hol:inductive $T (u 0) (: $make (-> $A $T)))){$T $make $A})
(= (admit-datatype (quote (hol:inductive $T $sort $constructor)))
   (hol:inductive $T $sort $constructor))
(= (projection-signature $name $T $A)
   (quote (: $name (-> $T $A))){$name $T $A})
(= (projection-equations $name $make)
   (quote ((= ($name ($make $value)) $value))){$name $make})

; One quoted block uses the familiar : and = forms. The existing native
; definition operation checks and publishes the proposed program.
(= (block-call (quote $declarations))
   (let ($signature $equations) (decons-atom $declarations)
     (let (: $name $type) $signature
       (let $equation-code (quote-value $equations)
         (definition-call (quote (: $name $type)){$name $type} $equation-code)))))
(= (admit-block $block)
   (let (quote $call) (block-call $block) (eval $call)))
(= (block-outcome $block)
   (let (quote $call) (block-call $block)
     (let $args (cons-atom $call ())
       (let $request (cons-atom try $args) (eval $request)))))

(= (increment-signature $name) (quote (: $name (-> Nat Nat))){$name})
(= (increment-equations $name) (quote ((= ($name $argument) (S $argument)))){$name})

; A derived rule is usable as an ordinary function without publishing a
; global name. Each application goes through the existing native checker.
(= (rule-code $statement)
   (case (judgment-call $statement)
     (((quote (type:check $term $type))
       (quote (RuleProgram $term $type)){$term $type})
      ($other $other))))
(= (apply-arguments (quote $function) (quote $arguments))
   (if (== (size-atom $arguments) 0)
     (quote $function){$function}
     (let ($argument $rest) (decons-atom $arguments)
       (let $application (quote (app $function $argument)){$function $argument}
         (apply-arguments $application (quote $rest){$rest})))))
(= (rule-application (quote (RuleProgram $term $type)) $arguments)
   (apply-arguments (quote $term){$term} $arguments))
(= (rule-check (quote (RuleProgram $term $type)))
   (type:check $term $type))
(= (run-rule $rule $arguments)
   (function
     (case (rule-check $rule)
       ((True
         (let (quote $application) (rule-application $rule $arguments)
           (let $args (cons-atom $application ())
             (let $call (cons-atom type:normalize $args)
               (let $result (eval $call) (return $result))))))
        (False (return (Refuted rule-program-type)))
        ($other (return (Undetermined rule-program-type)))))))
(= (rule-outcome $rule $arguments)
   (function
     (case (rule-check $rule)
       ((True
         (let (quote $application) (rule-application $rule $arguments)
           (let $args (cons-atom $application ())
             (let $normalize (cons-atom type:normalize $args)
               (let $one (cons-atom $normalize ())
                 (let $call (cons-atom try $one)
                   (let $result (eval $call) (return $result))))))))
        (False (return (Refuted rule-program-type)))
        ($other (return (Undetermined rule-program-type)))))))
