; The launcher pins the complete input and runs this profile in fresh processes.
; Load the ordinary ic_pln_poc support before these experiment-only operations.
(= (pln-profile:pinned? (Syntax:Document $version $reader $forms))
   (let $selected (filter-atom $forms $form
          (case $form
            (((Syntax:Form $names (Syntax:Expr ($marker $left $right)))
              (and (or (source-library:literal-symbol? $marker "=")
                       (source-library:literal-symbol? $marker ":"))
                   (source-library:literal-symbol? (rule-heads:head $left) "/safe")))
             ($other False))))
     (case (syntax:read "(= (/safe $A $B) (if (> $B 0.0) (/ $A $B) (empty)))")
       (((Syntax:Document $v $r $expected) (== $selected $expected))))))

(= (pln-profile:learn $input $suffix $split $output $helpers $limit)
   (case (library-stack:view (pln-poc:read $input))
     (((Library:StackValue $layers $document $original)
       (if (pln-profile:pinned? $document)
         (pln-poc:learn $input $suffix $split $output $helpers $limit)
         (println! (PLN:Fault DefinitionChanged))))
      ($invalid (println! (PLN:Fault InvalidInput))))))

(= (pln-profile:controls)
   (let* (($expected "(= (/safe $A $B) (if (> $B 0.0) (/ $A $B) (empty)))")
          ($a (println! (ProfileCheck Original
               (pln-profile:pinned? (syntax:read $expected)))))
          ($b (println! (ProfileCheck ChangedDefinition
               (not (pln-profile:pinned? (syntax:read "(= (/safe $A $B) (/ $A $B))"))))))
          ($c (println! (ProfileCheck ChangedArity
               (not (pln-profile:pinned? (syntax:read "(= (/safe $A) $A)"))))))
          ($d (println! (ProfileCheck AddedType
               (not (pln-profile:pinned? (syntax:read
                  (str:concat $expected "\n(: /safe (-> Number Number Atom))"))))))))
     (println! (ProfileChecksFinished))))
