; Experimental producers for ModuleSource.InductiveSyntax.Family.
; This is syntax inspection. Positivity, dependent formation and generated
; eliminators remain the existing indexed-family admission's responsibility.
(= (decl:term-shape (quote $term))
   (if (== (get-metatype $term) Symbol) True
     (case (quote $term){$term}
       (((quote ($function $argument))
         (and (decl:term-shape (quote $function){$function})
              (decl:term-shape (quote $argument){$argument})))
        ((quote (-> $domain $result))
         (and (decl:term-shape (quote $domain){$domain})
              (decl:term-shape (quote $result){$result})))
        ($_ False)))))
(= (decl:binders-shape (quote $binders))
   (if (== (get-metatype $binders) Expression)
     (if (== $binders ()) True
       (let ($first $rest) (decons-atom $binders)
         (case (quote $first){$first}
           (((quote (: $name $type))
             (and (== (get-metatype $name) Symbol)
               (and (decl:term-shape (quote $type){$type})
                    (decl:binders-shape (quote $rest){$rest}))))
            ($_ False)))))
     False))
(= (decl:constructors-shape (quote $constructors))
   (if (== (get-metatype $constructors) Expression)
     (if (== $constructors ()) True
       (let ($first $rest) (decons-atom $constructors)
         (case (quote $first){$first}
           (((quote (: ($name (fields $fields)) $result))
             (and (== (get-metatype $name) Symbol)
               (and (decl:binders-shape (quote $fields){$fields})
                 (and (decl:term-shape (quote $result){$result})
                      (decl:constructors-shape (quote $rest){$rest})))))
            ($_ False)))))
     False))

(= (decl:family $name (quote $parameters) (quote $indices) (quote $sort) (quote $constructors))
   (quote (Family $name (parameters $parameters) (indices $indices) $sort
     (constructors $constructors))){$name $parameters $indices $sort $constructors})

(= (decl:read-family $code)
   (case $code
     (((quote (Family $name (parameters $parameters) (indices $indices) $sort
          (constructors $constructors)))
       (if (and (== (get-metatype $name) Symbol)
             (and (decl:binders-shape (quote $parameters){$parameters})
               (and (decl:binders-shape (quote $indices){$indices})
                 (and (decl:term-shape (quote $sort){$sort})
                      (decl:constructors-shape (quote $constructors){$constructors})))))
         (Some $code) None))
      ($_ None))))

(= (decl:read-block $code)
   (case $code
     (((quote (Declarations
         (: (Family $declared (parameters $parameters) (indices $indices)) $sort)
         (= $defined (data $constructors))))
       (if (=alpha $declared $defined)
         (let $family (decl:family $declared (quote $parameters){$parameters}
               (quote $indices){$indices} (quote $sort){$sort} (quote $constructors){$constructors})
           (decl:read-family $family))
         None))
      ($_ None))))

(= (decl:nat $name $zero $succ)
   (decl:family $name (quote ()) (quote ()) (quote Type)
     (quote ((: ($zero (fields ())) $name)
             (: ($succ (fields ((: previous $name)))) $name))){$zero $succ $name}))
(= (decl:vec $name $nat $zero $succ)
   (decl:family $name (quote ((: A Type))) (quote ((: length $nat))){$nat} (quote Type)
     (quote ((: (Nil (fields ())) (($name A) $zero))
             (: (Cons (fields ((: n $nat) (: head A) (: tail (($name A) n)))))
                (($name A) ($succ n))))){$name $nat $zero $succ}))
