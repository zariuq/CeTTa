; Experimental declaration-driven intentional-scope construction.
;
; This compares the zero-new-C construction route with the native ABT parser.
; It is deliberately not imported by a language profile.  The constructor is
; driven by the same AbtSignature data as shift, substitution, and scope check.

(= (abtc:ok $value) (ABTConstructionOK $value))
(= (abtc:reject $reason $detail)
   (ABTConstructionRejected $reason $detail))

(= (abtc:list-length $items)
   (if (== $items ())
       0
       (let ($_head $tail) (decons-atom $items)
         (+ 1 (abtc:list-length $tail)))))

(= (abtc:append $left $right)
   (if (== $left ())
       $right
       (let ($head $tail) (decons-atom $left)
         (let $rest (abtc:append $tail $right)
           (cons-atom $head $rest)))))

(= (abtc:same-length $left $right)
   (if (== $left ())
       (== $right ())
       (if (== $right ())
           False
           (let ($_left-head $left-tail) (decons-atom $left)
             (let ($_right-head $right-tail) (decons-atom $right)
               (abtc:same-length $left-tail $right-tail))))))

(= (abtc:contains $needle $items)
   (if (== $items ())
       False
       (let ($head $tail) (decons-atom $items)
         (if (== $needle $head)
             True
             (abtc:contains $needle $tail)))))

(= (abtc:unique $items)
   (if (== $items ())
       True
       (let ($head $tail) (decons-atom $items)
         (if (abtc:contains $head $tail)
             False
             (abtc:unique $tail)))))

; The selected surface domain keeps the two stages explicit:
;   * a bare symbol is a human binder name;
;   * (quote closed-expression) is an explicit structural name.
; Canonical (idx Nat), malformed nonnumeric idx forms, unquoted compound terms,
; and open quoted syntax are not binder names.  The recursive closedness check
; is presentation-level data; it does not reinterpret a name as a string.

(= (abtc:closed-items $items)
   (if (== $items ())
       True
       (let ($head $tail) (decons-atom $items)
         (if (abtc:closed $head)
             (abtc:closed-items $tail)
             False))))

(= (abtc:closed $atom)
   (let $meta (get-metatype $atom)
     (if (== $meta Variable)
         False
         (if (== $meta Expression)
             (abtc:closed-items $atom)
             True))))

(= (abtc:name $name)
   (if (== (get-metatype $name) Symbol)
       True
   (case $name
     (((quote $payload) (abtc:closed $payload))
      ($_ False)))))

(= (abtc:all-names $names)
   (if (== $names ())
       True
       (let ($name $rest) (decons-atom $names)
         (if (abtc:name $name)
             (abtc:all-names $rest)
             False))))

(= (abtc:signature-decls $signatures)
   (if (== (get-metatype $signatures) Expression)
       (let ($tag $decls) (decons-atom $signatures)
         (if (== $tag AbtSignatures)
             (abtc:ok $decls)
             (abtc:reject MalformedSignatures $signatures)))
       (abtc:reject MalformedSignatures $signatures)))

; Native ABT operations implicitly admit the compiled default declaration
; set and then the supplied extension set.  The library reads that exact
; compiled default set through its capability, so lookup and execution still
; share one declaration source.
(= (abtc:all-declarations $extensions)
   (case (abtc:signature-decls
           (__cetta_abt_default_signatures AbtDefaultsV1))
     (((ABTConstructionOK $defaults)
       (case (abtc:signature-decls $extensions)
         (((ABTConstructionOK $extra)
           (let $combined (abtc:append $defaults $extra)
             (abtc:ok $combined)))
          ((ABTConstructionRejected $reason $detail)
           (abtc:reject $reason $detail)))))
      ((ABTConstructionRejected $reason $detail)
       (abtc:reject $reason $detail)))))

(= (abtc:field-tail $fields)
   (if (== (get-metatype $fields) Expression)
       (let ($tag $tail) (decons-atom $fields)
         (if (== $tag Fields)
             (abtc:ok $tail)
             (abtc:reject MalformedFields $fields)))
       (abtc:reject MalformedFields $fields)))

(= (abtc:find-signature $head $values ())
   (abtc:reject UndeclaredConstructor $head))

(= (abtc:find-signature $head $values $declarations)
   (let ($declaration $rest) (decons-atom $declarations)
     (case $declaration
       (((AbtSignature $declared-head $fields $binding)
         (if (== $head $declared-head)
             (if (== (size-atom $values) (- (size-atom $fields) 1))
                 (case (abtc:field-tail $fields)
                   (((ABTConstructionOK $field-names)
                     (ABTConstructionOK
                       (ABTConstructionSignature $field-names $binding)))
                    ((ABTConstructionRejected $reason $detail)
                     (abtc:reject $reason $detail))))
                 (abtc:find-signature $head $values $rest))
             (abtc:find-signature $head $values $rest)))
        ($_ (abtc:reject MalformedSignature $declaration))))))

(= (abtc:bindfields-depth $field $bindings)
   (case $bindings
     ((BNil (abtc:reject MissingFieldDepth $field))
      ((BCons (BField $candidate $depth) $rest)
       (if (== $field $candidate)
           (if (== (get-metatype $depth) Grounded)
               (if (>= $depth 0)
                   (abtc:ok $depth)
                   (abtc:reject MalformedFieldDepth $depth))
               (abtc:reject MalformedFieldDepth $depth))
           (abtc:bindfields-depth $field $rest)))
      ($_ (abtc:reject MalformedBindFields $bindings)))))

(= (abtc:field-depth $field $binding)
   (case $binding
     ((Bind0 (abtc:ok 0))
      ((Bind1 $body)
       (let $depth (if (== $field $body) 1 0)
         (abtc:ok $depth)))
      ((BindPi $domain $codomain)
       (if (== $field $domain)
           (abtc:ok 0)
           (if (== $field $codomain)
               (abtc:ok 1)
               (abtc:reject MissingPiField $field))))
      ((BindFields $bindings)
       (abtc:bindfields-depth $field $bindings))
      ($_ (abtc:reject MalformedBindingDeclaration $binding)))))

(= (abtc:max $left $right)
   (if (> $left $right) $left $right))

(= (abtc:max-depth $field-names $binding)
   (if (== $field-names ())
       (abtc:ok 0)
       (let ($field $rest) (decons-atom $field-names)
         (case (abtc:field-depth $field $binding)
           (((ABTConstructionOK $depth)
             (case (abtc:max-depth $rest $binding)
               (((ABTConstructionOK $rest-max)
                 (let $maximum (abtc:max $depth $rest-max)
                   (abtc:ok $maximum)))
                ((ABTConstructionRejected $reason $detail)
                 (abtc:reject $reason $detail)))))
            ((ABTConstructionRejected $reason $detail)
             (abtc:reject $reason $detail)))))))

; Add one binder around an already canonical/named field.  Existing loose
; indices move outward before the new name is closed at index zero.  This is
; intentionally different from the unsound shift(-1, close(...)) repair.
(= (abtc:add-binder $signatures $name $term)
   (__cetta_abt_bind $signatures $name $term))

(= (abtc:close-prefix $signatures $names $depth $term)
   (if (== $depth 0)
       (abtc:ok $term)
       (if (== $names ())
           (abtc:reject MissingBinder $depth)
           (let ($name $rest) (decons-atom $names)
             (let $next (abtc:add-binder $signatures $name $term)
               (abtc:close-prefix
                 $signatures $rest (- $depth 1) $next))))))

(= (abtc:construct-fields
      $signatures $names $field-names $values $binding)
   (if (== $field-names ())
       (if (== $values ())
           (abtc:ok ())
           (abtc:reject ExtraFieldValues $values))
       (if (== $values ())
           (abtc:reject MissingFieldValues $field-names)
           (let ($field $field-rest) (decons-atom $field-names)
             (let ($value $value-rest) (decons-atom $values)
               (case (abtc:field-depth $field $binding)
                 (((ABTConstructionOK $depth)
                   (case (abtc:close-prefix
                           $signatures $names $depth $value)
                     (((ABTConstructionOK $scoped)
                       (case (abtc:construct-fields
                               $signatures $names $field-rest
                               $value-rest $binding)
                         (((ABTConstructionOK $rest)
                           (abtc:ok (cons-atom $scoped $rest)))
                          ((ABTConstructionRejected $reason $detail)
                           (abtc:reject $reason $detail)))))
                      ((ABTConstructionRejected $reason $detail)
                       (abtc:reject $reason $detail)))))
                  ((ABTConstructionRejected $reason $detail)
                   (abtc:reject $reason $detail)))))))))

(= (abtc:construct $signatures $head $binders $values)
   (if (== (__cetta_abt_signature_admitted $signatures) True)
       (case (abtc:all-declarations $signatures)
         (((ABTConstructionOK $declarations)
           (if (== (get-metatype $binders) Expression)
               (let ($binder-tag $names) (decons-atom $binders)
                 (if (== $binder-tag Binders)
                     (if (abtc:all-names $names)
                         (if (abtc:unique $names)
                             (case (abtc:find-signature
                                     $head $values $declarations)
                               (((ABTConstructionOK
                                   (ABTConstructionSignature
                                     $field-names $binding))
                                 (case (abtc:max-depth
                                         $field-names $binding)
                                   (((ABTConstructionOK $maximum)
                                     (if (== (abtc:list-length $names)
                                             $maximum)
                                         (case (abtc:construct-fields
                                                 $signatures $names
                                                 $field-names $values $binding)
                                           (((ABTConstructionOK $fields)
                                             (abtc:ok
                                               (cons-atom $head $fields)))
                                            ((ABTConstructionRejected
                                               $reason $detail)
                                             (abtc:reject $reason $detail))))
                                         (let $actual
                                           (abtc:list-length $names)
                                           (abtc:reject BinderCountMismatch
                                             (Expected $maximum
                                               Actual $actual)))))
                                    ((ABTConstructionRejected $reason $detail)
                                     (abtc:reject $reason $detail)))))
                                ((ABTConstructionRejected $reason $detail)
                                 (abtc:reject $reason $detail))))
                             (abtc:reject DuplicateBinder $binders))
                         (abtc:reject InvalidBinderIdentifier $binders))
                     (abtc:reject MalformedBinders $binders)))
               (abtc:reject MalformedBinders $binders)))
          ((ABTConstructionRejected $reason $detail)
           (abtc:reject $reason $detail))))
       (abtc:reject InvalidSignature $signatures)))

(= (abtc:test-signatures)
   (AbtSignatures
     (AbtSignature MultiScope (Fields plain one two three)
       (BindFields
         (BCons (BField plain 0)
           (BCons (BField one 1)
             (BCons (BField two 2)
               (BCons (BField three 3) BNil))))))))

(= (abtc:lam $name $domain $body)
   (abtc:construct
     (abtc:test-signatures) Lam (Binders $name) ($domain $body)))

; Candidate B: an intentionally small, package-specific lexical oracle.  It
; does not call the ABT parser or close/substitution primitives.  Its only job
; is to resolve the Lam/Pi corpus independently so candidates A and C can be
; compared against a structurally different implementation.
(= (abtc:oracle-lookup $name $environment $index)
   (if (== $environment ())
       (ABTCOracleFree $name)
       (let ($candidate $rest) (decons-atom $environment)
         (if (== $name $candidate)
             (ABTCOracleBound $index)
             (abtc:oracle-lookup $name $rest (+ $index 1))))))

(= (abtc:oracle-atom $atom $environment)
   (if (== (get-metatype $atom) Symbol)
       (case (abtc:oracle-lookup $atom $environment 0)
         (((ABTCOracleBound $index) (idx $index))
          ((ABTCOracleFree $free) $free)))
       $atom))

(= (abtc:oracle-parse $surface $environment)
   (case $surface
     (((ABTBind Lam (Binders $binder) $domain $body)
       (Lam
         (abtc:oracle-parse $domain $environment)
         (abtc:oracle-parse $body
           (cons-atom $binder $environment))))
      ((ABTBind Pi (Binders $binder) $domain $codomain)
       (Pi
         (abtc:oracle-parse $domain $environment)
         (abtc:oracle-parse $codomain
           (cons-atom $binder $environment))))
      ((App $function $argument)
       (App
         (abtc:oracle-parse $function $environment)
         (abtc:oracle-parse $argument $environment)))
      ((Pair $left $right)
       (Pair
         (abtc:oracle-parse $left $environment)
         (abtc:oracle-parse $right $environment)))
      ($atom (abtc:oracle-atom $atom $environment)))))

(= (abtc:constructed-value $result)
   (case $result
     (((ABTConstructionOK $value) $value)
      ($rejected $rejected))))

; One shared semantic corpus compares all three routes.  A consumes the
; current ABTBind presentation; B independently resolves the symbol corpus;
; C consumes generated and explicitly quoted structural names.  Equality is
; required only of the resulting canonical ABT, in which names are erased.
(= (abtc:abc-agree $symbol-presentation $candidate-c)
   (let $candidate-a
     (__cetta_abt_parse (abtc:test-signatures) $symbol-presentation)
     (let $candidate-b
       (abtc:oracle-parse $symbol-presentation ())
       (let $candidate-c-value (abtc:constructed-value $candidate-c)
         (if (== $candidate-a $candidate-b)
             (== $candidate-a $candidate-c-value)
             False)))))

!(assertEqual
   (abtc:lam (quote "x") Nat (quote "x"))
   (ABTConstructionOK (Lam Nat (idx 0))))
!(assertEqual
   (abtc:abc-agree
     (ABTBind Lam (Binders x) Nat x)
     (abtc:lam (quote "x") Nat (quote "x")))
   True)
!(assertEqual
   (abtc:abc-agree
     (ABTBind Lam (Binders x) Nat
       (ABTBind Lam (Binders y) Nat x))
     (abtc:lam (quote "x") Nat
       (case (abtc:lam (quote "y") Nat (quote "x"))
         (((ABTConstructionOK $inner) $inner)))))
   True)
!(assertEqual
   (abtc:abc-agree
     (ABTBind Lam (Binders x) Nat
       (ABTBind Lam (Binders x) Nat x))
     (abtc:lam (quote "x") Nat
       (case (abtc:lam (quote "x") Nat (quote "x"))
         (((ABTConstructionOK $inner) $inner)))))
   True)
!(assertEqual
   (abtc:abc-agree
     (ABTBind Pi (Binders x) Nat (App Vec x))
     (abtc:construct
       (abtc:test-signatures) Pi (Binders (quote "x"))
       (Nat (App Vec (quote "x")))))
   True)
!(assertEqual
   (abtc:abc-agree
     (ABTBind Lam (Binders x) Nat (Pair free x))
     (abtc:lam (quote "x") Nat (Pair free (quote "x"))))
   True)
!(assertEqual
   (abtc:lam (quote "x") Nat (quote "x"))
   (abtc:lam (quote "renamed") Nat (quote "renamed")))
!(assertEqual
   (abtc:lam (quote "x") Nat
     (case (abtc:lam (quote "y") Nat (quote "x"))
       (((ABTConstructionOK $inner) $inner))))
   (ABTConstructionOK (Lam Nat (Lam Nat (idx 1)))))
!(assertEqual
   (abtc:lam (quote "x") Nat
     (case (abtc:lam (quote "x") Nat (quote "x"))
       (((ABTConstructionOK $inner) $inner))))
   (ABTConstructionOK (Lam Nat (Lam Nat (idx 0)))))
!(assertEqual
   (abtc:lam (quote "x") Nat (Pair x (quote "x")))
   (ABTConstructionOK (Lam Nat (Pair x (idx 0)))))
!(assertEqual
   (abtc:lam (quote "x") Nat (App (idx 0) (quote "x")))
   (ABTConstructionOK (Lam Nat (App (idx 1) (idx 0)))))
!(assertEqual
   (abtc:construct
     (abtc:test-signatures) Pi (Binders (quote "x"))
     (Nat (Vec (quote "x"))))
   (ABTConstructionOK (Pi Nat (Vec (idx 0)))))
!(assertEqual
   (abtc:construct
     (abtc:test-signatures) MultiScope
     (Binders (quote "x") (quote "y") (quote "z"))
     (free-x (quote "x")
       (App (quote "x") (quote "y"))
       (Triple (quote "x") (quote "y") (quote "z"))))
   (ABTConstructionOK
     (MultiScope
       free-x
       (idx 0)
       (App (idx 1) (idx 0))
       (Triple (idx 2) (idx 1) (idx 0)))))
!(assertEqual
   (abtc:construct
     (abtc:test-signatures) Lam
     (Binders (quote "x") (quote "x")) (Nat (quote "x")))
   (ABTConstructionRejected DuplicateBinder
     (Binders (quote "x") (quote "x"))))
!(assertEqual
   (abtc:construct
     (abtc:test-signatures) Lam
     (Binders (compound x)) (Nat (compound x)))
   (ABTConstructionRejected InvalidBinderIdentifier
     (Binders (compound x))))
!(assertEqual
   (abtc:construct
     (abtc:test-signatures) Lam (Binders (idx 1)) (Nat (idx 1)))
   (ABTConstructionRejected InvalidBinderIdentifier
     (Binders (idx 1))))
!(assertEqual
   (abtc:construct
     (abtc:test-signatures) Lam (Binders x) (Nat x))
   (ABTConstructionOK (Lam Nat (idx 0))))
!(assertEqual
   (abtc:lam
     (quote (mm-var "ph")) Nat (quote (mm-var "ph")))
   (ABTConstructionOK (Lam Nat (idx 0))))
!(assertEqual
   (abtc:lam (quote "arg-1") Nat (quote "arg-1"))
   (ABTConstructionOK (Lam Nat (idx 0))))
!(assertEqual
   (__cetta_abt_parse
     (abtc:test-signatures)
     (ABTBind Lam (Binders (quote "arg-1")) Nat (quote "arg-1")))
   (Lam Nat (idx 0)))
!(assertEqual
   (abtc:name (quote (mm-var $open)))
   False)
!(assertEqual
   (__cetta_abt_bind (abtc:test-signatures) x (quote x))
   (quote x))
!(assertEqual
   (abtc:construct
     (abtc:test-signatures) Lam (Binders) (Nat body))
   (ABTConstructionRejected BinderCountMismatch (Expected 1 Actual 0)))
!(assertEqual
   (__cetta_abt_subst
     (abtc:test-signatures) 0 (idx 0) (Lam Nat (idx 1)))
   (Lam Nat (idx 1)))
!(assertEqual
   (__cetta_abt_shift
     (abtc:test-signatures) -1 0
     (__cetta_abt_close
       (abtc:test-signatures) (quote (mm-var "x"))
       (Lam Nat (App (quote (mm-var "x")) (idx 0)))))
   (Lam Nat (App (idx 0) (idx 0))))
!(assertEqual
   (__cetta_abt_scope_check
     (abtc:test-signatures) 0
     (__cetta_abt_shift
       (abtc:test-signatures) -1 0
       (__cetta_abt_close
         (abtc:test-signatures) (quote (mm-var "x"))
         (Lam Nat (App (quote (mm-var "x")) (idx 0))))))
   True)
!(assertEqual
   (__cetta_abt_signature_admitted
     (AbtSignatures
       (AbtSignature Bad (Fields body body) Bind0)))
   False)

!(println! (ABTScopeConstructionCandidateSummary 27 27 0))
