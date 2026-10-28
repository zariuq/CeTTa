; Signature projection from the existing authored LanguageDef carrier.
; The projection describes finite AST operators; it does not execute or
; authorize any source equation, rewrite, premise, effect or concrete syntax.
; Plain registration supplies the very same Scoped:Family values directly.
!(import! &self (library lib_scoped_templates))

(= (scoped-module:types $types)
   (case $types
     ((LNil (IC:Some ()))
      ((LCons (TypeDecl $name CarrierAst) $tail)
       (if (binary-schema:text? $name)
         (case (scoped-module:types $tail)
           (((IC:Some $rest) (let $sorts (cons-atom $name $rest) (IC:Some $sorts))) ($other $other)))
         (Scoped:Refused InvalidSort)))
      ($other (Scoped:Refused UnsupportedCarrier)))))

(= (scoped-module:parameter $parameter)
   (case $parameter
     (((TermSimple $name (TBase $sort)) (IC:Some (Scoped:Slot () $sort)))
      ((TermAbstraction $name (TBase $sort)) (IC:Some (Scoped:Slot ($sort) $sort)))
      ((TermAbstractionNamed $binder $name (TBase $sort)) (IC:Some (Scoped:Slot ($sort) $sort)))
      ((TermAbstraction $name (TArrow (TBase $domain) (TBase $codomain)))
       (IC:Some (Scoped:Slot ($domain) $codomain)))
      ((TermAbstractionNamed $binder $name (TArrow (TBase $domain) (TBase $codomain)))
       (IC:Some (Scoped:Slot ($domain) $codomain)))
      ($other (Scoped:Refused UnsupportedParameter)))))

(= (scoped-module:slots $parameters)
   (case $parameters
     ((LNil (IC:Some ()))
      ((LCons $parameter $tail)
       (case ((scoped-module:parameter $parameter) (scoped-module:slots $tail))
         ((((IC:Some $slot) (IC:Some $rest))
           (let $slots (cons-atom $slot $rest) (IC:Some $slots)))
          (($failure (IC:Some $rest)) $failure)
          (($first $failure) $failure))))
      ($other (Scoped:Refused InvalidParameters)))))

(= (scoped-module:operator $row)
   (case $row
     (((GrammarRule $name $sort $parameters $syntax $evaluation)
       (case (scoped-module:slots $parameters)
         (((IC:Some $slots) (IC:Some (Scoped:Operator $name $sort $slots))) ($other $other))))
      ((GrammarRuleV2 $name $sort $parameters $syntax $evaluation $algebra)
       (case (scoped-module:slots $parameters)
         (((IC:Some $slots) (IC:Some (Scoped:Operator $name $sort $slots))) ($other $other))))
      ($other (Scoped:Refused UnsupportedConstructor)))))

(= (scoped-module:operators $rows)
   (case $rows
     ((LNil (IC:Some ()))
      ((LCons $row $tail)
       (case ((scoped-module:operator $row) (scoped-module:operators $tail))
         ((((IC:Some $operator) (IC:Some $rest))
           (let $operators (cons-atom $operator $rest) (IC:Some $operators)))
          (($failure (IC:Some $rest)) $failure)
          (($first $failure) $failure))))
      ($other (Scoped:Refused InvalidConstructors)))))

(= (scoped-module:family $types $grammar $parameters)
   (case ((scoped-module:types $types) (scoped-module:operators $grammar))
     ((((IC:Some $sorts) (IC:Some $operators))
       (let $family (Scoped:Family $sorts $operators $parameters)
         (if (scoped:family? $family) (IC:Some $family) (Scoped:Refused InvalidFamily))))
      (($failure (IC:Some $operators)) $failure)
      (($first $failure) $failure))))

(= (scoped:family-from-language $language $parameters)
   (case $language
     (((GSLTLanguageDefWireV1 $name $types $grammar $rewrites $equations)
       (scoped-module:family $types $grammar $parameters))
      ((LanguageDefV2 $name $types $grammar $rewrites $equations)
       (scoped-module:family $types $grammar $parameters))
      ($other (Scoped:Refused UnsupportedLanguage)))))
