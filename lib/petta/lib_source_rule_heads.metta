; First-order constructor patterns in untyped equation heads. Constructor
; boundaries are traversed only for declaration-only documents, and only when
; the constructor has no intrinsic meaning, equation or type declaration.
; Constructor roots are not themselves offered as code-abstraction sites.
!(import! &self (library lib_ic_library))
!(import! &self (library lib_source_context))

(= (rule-heads:head $term)
   (case $term
     (((Syntax:Expr $children) (if (== $children ()) $term (car-atom $children)))
      ($atom $atom))))
(= (rule-heads:declared? $name $forms)
   (foldl-atom $forms False $found $form
     (or $found
       (case $form
         (((Syntax:Form $names (Syntax:Expr ($tag $left $right)))
           (and (or (source-library:literal-symbol? $tag "=")
                    (source-library:literal-symbol? $tag ":"))
                (source-library:literal-symbol? (rule-heads:head $left) $name)))
          ($other False))))))
(= (rule-heads:declarations-only? $forms)
   (foldl-atom $forms True $valid $form
     (and $valid
       (case $form
         (((Syntax:Form $names (Syntax:Expr ($tag $left $right)))
           (and (or (source-library:literal-symbol? $tag "=")
                    (source-library:literal-symbol? $tag ":"))
                (case (rule-heads:head $left) (((Syntax:Literal Symbol $bytes) True) ($other False)))))
          ($other False))))))
(= (rule-heads:collect $term)
   (case $term
     (((Syntax:Expr $children)
       (if (== $children ()) ()
         (let $nested (foldl-atom (cdr-atom $children) () $rows $child
                        (union-atom $rows (rule-heads:collect $child)))
           (case (car-atom $children)
             (((Syntax:Literal Symbol $bytes)
               (cons-atom ((binary:from-utf8 $bytes) (- (size-atom $children) 1)) $nested))
              ($other $nested))))))
      ($atom ()))))
(= (rule-heads:safe? $forms $reader ($name $arity))
   (and (not (syntax:intrinsic-head? (binary:from-symbol-utf8 (binary:utf8 $name))))
     (and (not (source-context:binding-head? $name))
       (and (not (rule-heads:declared? $name $forms))
         (and (not (source-context:declaration-at? $reader "=" $name $arity False))
           (and (not (source-context:declaration-at? $reader "=" $name 0 True))
             (and (not (source-context:declaration-at? $reader ":" $name 0 True))
                  (not (source-context:declaration-at? $reader ":" $name $arity False)))))))))
(= (rule-heads:constructors (Syntax:Document $version $reader $forms))
   (if (rule-heads:declarations-only? $forms)
     (let* (($rows (foldl-atom $forms () $rows $form
               (case $form
                 (((Syntax:Form $names (Syntax:Expr ($equals (Syntax:Expr $left) $body)))
                   (if (and (source-library:literal-symbol? $equals "=") (not (== $left ())))
                     (union-atom $rows (foldl-atom (cdr-atom $left) () $out $arg
                       (union-atom $out (rule-heads:collect $arg)))) $rows))
                  ($other $rows)))))
            ($unique (foldl-atom $rows () $out $row
               (if (binary-schema:member? $row $out) $out (cons-atom $row $out)))))
       (filter-atom $unique $row (rule-heads:safe? $forms $reader $row))) ()))
(= (rule-heads:pattern? $constructors $term)
   (case $term
     (((Syntax:Variable $index) True)
      ((Syntax:Literal $kind $bytes) True)
      ((Syntax:Expr ()) True)
      ((Syntax:Expr $children)
       (case (car-atom $children)
         (((Syntax:Literal Symbol $bytes)
           (and (binary-schema:member? ((binary:from-utf8 $bytes) (- (size-atom $children) 1)) $constructors)
             (foldl-atom (cdr-atom $children) True $valid $child
               (and $valid (rule-heads:pattern? $constructors $child)))))
          ($other False))))
      ($other False))))
(= (rule-heads:clause $constructors $form)
   (case $form
     (((Syntax:Form $names (Syntax:Expr ($equals (Syntax:Expr $left) $body)))
       (if (and (source-library:literal-symbol? $equals "=") (not (== $left ())))
         (let ($head $arguments) (decons-atom $left)
           (case $head
             (((Syntax:Literal Symbol $bytes)
               (let* (($bound (source-context:variables (Syntax:Expr $arguments)))
                      ($ports (foldl-atom $bound 0 $count $index (max $count (+ $index 1)))))
                 (if (and (not (syntax:intrinsic-head? (binary:from-symbol-utf8 $bytes)))
                       (and (source-library:plain-names? $names)
                         (and (<= $ports (size-atom $names))
                           (foldl-atom $arguments True $valid $arg
                             (and $valid (rule-heads:pattern? $constructors $arg))))))
                   (IC:Some (Ctx:Rule $head $body $bound $ports)) IC:None)))
              ($other IC:None)))) IC:None))
      ($other IC:None))))
