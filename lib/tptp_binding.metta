; Binding utilities for the compact TPTP value language.
;
; Source variables remain symbols.  TPTP's lexical class begins with an
; ASCII upper-case letter; host MeTTa variables are deliberately not used at
; this layer.  All returned collections are ordinary expression lists.

!(import! &self langdef)

(= (tptp-internal:member? $_item ()) False)
(= (tptp-internal:member? $item $items)
   (let ($head $tail) (decons-atom $items)
     (if (== $item $head)
         True
         (tptp-internal:member? $item $tail))))

(= (tptp:source-variable? $atom)
   (case (get-metatype $atom)
     ((Symbol
        (case (langdef:symbol->codepoints $atom)
          (((cons (cp $first) $_rest)
             (if (>= $first 65) (<= $first 90) False))
           ($_ False))))
      ($_ False))))

(= (tptp-internal:binder-name $declaration)
   (case $declaration
     (((: $name $_type) $name)
      ($_ $declaration))))

(= (tptp-internal:binder-extend () $bound) $bound)
(= (tptp-internal:binder-extend $declarations $bound)
   (let ($declaration $rest) (decons-atom $declarations)
     (let $next
          (cons-atom
            (tptp-internal:binder-name $declaration) $bound)
       (tptp-internal:binder-extend $rest $next))))

(= (tptp-internal:binder-type-free () $_bound) ())
(= (tptp-internal:binder-type-free $declarations $bound)
   (let ($declaration $rest) (decons-atom $declarations)
     (let $here
          (case $declaration
            (((: $_name $type)
               (tptp-internal:free-variables-in $type $bound))
             ($_ ())))
       (let $next-bound
            (cons-atom
              (tptp-internal:binder-name $declaration) $bound)
         (let $there
              (tptp-internal:binder-type-free $rest $next-bound)
           (let $combined (union-atom $here $there) $combined))))))

(= (tptp-internal:free-variables-binder
      $declarations $body $bound)
   (let $from-types
        (tptp-internal:binder-type-free $declarations $bound)
     (let $body-bound
          (tptp-internal:binder-extend $declarations $bound)
       (let $from-body
            (tptp-internal:free-variables-in $body $body-bound)
         (let $combined (union-atom $from-types $from-body) $combined)))))

(= (tptp-internal:free-variables-items () $_bound) ())
(= (tptp-internal:free-variables-items $items $bound)
   (let ($head $tail) (decons-atom $items)
     (let $here (tptp-internal:free-variables-in $head $bound)
       (let $there (tptp-internal:free-variables-items $tail $bound)
         (let $combined (union-atom $here $there) $combined)))))

(= (tptp-internal:free-variables-expression $expression $bound)
   (case $expression
     (((∀ $declarations $body)
        (tptp-internal:free-variables-binder
          $declarations $body $bound))
      ((∃ $declarations $body)
        (tptp-internal:free-variables-binder
          $declarations $body $bound))
      ((λ $declarations $body)
        (tptp-internal:free-variables-binder
          $declarations $body $bound))
      ((Π $declarations $body)
        (tptp-internal:free-variables-binder
          $declarations $body $bound))
      ((Σ $declarations $body)
        (tptp-internal:free-variables-binder
          $declarations $body $bound))
      ((choice $declarations $body)
        (tptp-internal:free-variables-binder
          $declarations $body $bound))
      ((definite $declarations $body)
        (tptp-internal:free-variables-binder
          $declarations $body $bound))
      ($_ (tptp-internal:free-variables-items $expression $bound)))))

(= (tptp-internal:free-variables-in $atom $bound)
   (case (get-metatype $atom)
     ((Expression
        (tptp-internal:free-variables-expression $atom $bound))
      (Symbol
        (if (tptp:source-variable? $atom)
            (if (tptp-internal:member? $atom $bound)
                ()
                (cons-atom $atom ()))
            ()))
      ($_ ()))))

(= (tptp:free-variables $formula)
   (let $variables (tptp-internal:free-variables-in $formula ())
     (let $unique (unique-atom $variables) $unique)))

(= (tptp-internal:all-variables-items ()) ())
(= (tptp-internal:all-variables-items $items)
   (let ($head $tail) (decons-atom $items)
     (let $here (tptp-internal:all-variables-in $head)
       (let $there (tptp-internal:all-variables-items $tail)
         (let $combined (union-atom $here $there) $combined)))))

(= (tptp-internal:all-variables-in $atom)
   (case (get-metatype $atom)
     ((Expression (tptp-internal:all-variables-items $atom))
      (Symbol
        (if (tptp:source-variable? $atom)
            (cons-atom $atom ())
            ()))
      ($_ ()))))

(= (tptp:all-source-variables $formula)
   (let $variables (tptp-internal:all-variables-in $formula)
     (let $unique (unique-atom $variables) $unique)))

(= (tptp-internal:binder-names () ) ())
(= (tptp-internal:binder-names $declarations)
   (let ($declaration $rest) (decons-atom $declarations)
     (let $names (tptp-internal:binder-names $rest)
       (let $result
            (cons-atom
              (tptp-internal:binder-name $declaration) $names)
         $result))))

(= (tptp-internal:fresh-candidate $candidate $avoid)
   (if (tptp-internal:member? $candidate $avoid)
       (let $next (langdef:append-symbol $candidate Z)
         (tptp-internal:fresh-candidate $next $avoid))
       $candidate))

(= (tptp:fresh-source-variable $like $avoid)
   (if (tptp:source-variable? $like)
       (let $candidate (langdef:append-symbol $like Z)
         (tptp-internal:fresh-candidate $candidate $avoid))
       (TPTP:NotSourceVariable $like)))

(= (tptp-internal:rebuild-binder $head $declarations $body)
   (let $tail (cons-atom $body ())
     (let $arguments (cons-atom $declarations $tail)
       (let $result (cons-atom $head $arguments) $result))))

(= (tptp-internal:rename-bound-items () $_old $_new) ())
(= (tptp-internal:rename-bound-items $items $old $new)
   (let ($head $tail) (decons-atom $items)
     (let $renamed-head
          (tptp-internal:rename-bound-in $head $old $new)
       (let $renamed-tail
            (tptp-internal:rename-bound-items $tail $old $new)
         (let $result (cons-atom $renamed-head $renamed-tail) $result)))))

(= (tptp-internal:rename-bound-binder
      $head $declarations $body $old $new)
   (let $names (tptp-internal:binder-names $declarations)
     (if (tptp-internal:member? $old $names)
         (let $renamed-types
              (tptp-internal:rename-shadowing-types
                $declarations $old $new True)
           (tptp-internal:rebuild-binder
             $head $renamed-types $body))
         (let $renamed-declarations
              (tptp-internal:rename-shadowing-types
                $declarations $old $new True)
           (let $renamed-body
                (tptp-internal:rename-bound-in $body $old $new)
             (tptp-internal:rebuild-binder
               $head $renamed-declarations $renamed-body))))))

(= (tptp-internal:rename-bound-expression $expression $old $new)
   (case $expression
     (((∀ $declarations $body)
        (tptp-internal:rename-bound-binder
          ∀ $declarations $body $old $new))
      ((∃ $declarations $body)
        (tptp-internal:rename-bound-binder
          ∃ $declarations $body $old $new))
      ((λ $declarations $body)
        (tptp-internal:rename-bound-binder
          λ $declarations $body $old $new))
      ((Π $declarations $body)
        (tptp-internal:rename-bound-binder
          Π $declarations $body $old $new))
      ((Σ $declarations $body)
        (tptp-internal:rename-bound-binder
          Σ $declarations $body $old $new))
      ((choice $declarations $body)
        (tptp-internal:rename-bound-binder
          choice $declarations $body $old $new))
      ((definite $declarations $body)
        (tptp-internal:rename-bound-binder
          definite $declarations $body $old $new))
      ($_
        (let $renamed
             (map-atom $expression $item
               (tptp-internal:rename-bound-in $item $old $new))
          $renamed)))))

(= (tptp-internal:rename-bound-in $atom $old $new)
   (case (get-metatype $atom)
     ((Expression
        (tptp-internal:rename-bound-expression $atom $old $new))
      (Symbol (if (== $atom $old) $new $atom))
      ($_ $atom))))

(= (tptp-internal:rename-declaration $declaration $old $new $active)
   (case $declaration
     (((: $name $type)
        (let $renamed-type
             (if $active
                 (tptp-internal:rename-bound-in $type $old $new)
                 $type)
          (: (if (== $name $old) $new $name) $renamed-type)))
      ($_ (if (== $declaration $old) $new $declaration)))))

(= (tptp-internal:rename-declarations () $_old $_new $_active) ())
(= (tptp-internal:rename-declarations
      $declarations $old $new $active)
   (let ($declaration $rest) (decons-atom $declarations)
     (let $renamed
          (tptp-internal:rename-declaration
            $declaration $old $new $active)
       (let $next-active
            (if $active
                True
                (== (tptp-internal:binder-name $declaration) $old))
         (let $renamed-rest
              (tptp-internal:rename-declarations
                $rest $old $new $next-active)
           (let $result (cons-atom $renamed $renamed-rest) $result))))))

(= (tptp-internal:rename-shadowing-types
      () $_old $_new $_active) ())
(= (tptp-internal:rename-shadowing-types
      $declarations $old $new $active)
   (let ($declaration $rest) (decons-atom $declarations)
     (let $renamed
          (case $declaration
            (((: $name $type)
               (let $renamed-type
                    (if $active
                        (tptp-internal:rename-bound-in $type $old $new)
                        $type)
                 (: $name $renamed-type)))
             ($_ $declaration)))
       (let $next-active
            (if $active
                (if (== (tptp-internal:binder-name $declaration) $old)
                    False
                    True)
                False)
         (let $renamed-rest
              (tptp-internal:rename-shadowing-types
                $rest $old $new $next-active)
           (let $result (cons-atom $renamed $renamed-rest) $result))))))

(= (tptp-internal:alpha-rename-binder
      $head $declarations $body $old $new)
   (let $names (tptp-internal:binder-names $declarations)
     (if (tptp-internal:member? $old $names)
         (let $all
              (tptp:all-source-variables
                (tptp-internal:rebuild-binder
                  $head $declarations $body))
           (if (if (== $old $new)
                   False
                   (tptp-internal:member? $new $all))
               (TPTP:AlphaRenameCapture $new)
               (let $renamed-declarations
                    (tptp-internal:rename-declarations
                      $declarations $old $new False)
                 (let $renamed-body
                      (tptp-internal:rename-bound-in $body $old $new)
                   (tptp-internal:rebuild-binder
                     $head $renamed-declarations $renamed-body)))))
         (TPTP:BinderDoesNotDeclare $old))))

(= (tptp:alpha-rename $binder $old $new)
   (if (tptp:source-variable? $new)
       (case $binder
         (((∀ $declarations $body)
            (tptp-internal:alpha-rename-binder
              ∀ $declarations $body $old $new))
          ((∃ $declarations $body)
            (tptp-internal:alpha-rename-binder
              ∃ $declarations $body $old $new))
          ((λ $declarations $body)
            (tptp-internal:alpha-rename-binder
              λ $declarations $body $old $new))
          ((Π $declarations $body)
            (tptp-internal:alpha-rename-binder
              Π $declarations $body $old $new))
          ((Σ $declarations $body)
            (tptp-internal:alpha-rename-binder
              Σ $declarations $body $old $new))
          ((choice $declarations $body)
            (tptp-internal:alpha-rename-binder
              choice $declarations $body $old $new))
          ((definite $declarations $body)
            (tptp-internal:alpha-rename-binder
              definite $declarations $body $old $new))
          ($_ (TPTP:NotBinder $binder))))
       (TPTP:NotSourceVariable $new)))

(= (tptp-internal:intersection () $_right) ())
(= (tptp-internal:intersection $left $right)
   (let ($head $tail) (decons-atom $left)
     (let $rest (tptp-internal:intersection $tail $right)
       (if (tptp-internal:member? $head $right)
           (let $result (cons-atom $head $rest) $result)
           $rest))))

(= (tptp-internal:substitute-declarations
      () $_target $_replacement) ())
(= (tptp-internal:substitute-declarations
      $declarations $target $replacement)
   (let ($declaration $rest) (decons-atom $declarations)
     (let $changed
          (case $declaration
            (((: $name $type)
               (: $name
                  (tptp-internal:substitute-in
                    $type $target $replacement)))
             ($_ $declaration)))
       (let $changed-rest
            (tptp-internal:substitute-declarations
              $rest $target $replacement)
         (let $result (cons-atom $changed $changed-rest) $result)))))

(= (tptp-internal:substitute-shadowing-types
      () $_target $_replacement $_active) ())
(= (tptp-internal:substitute-shadowing-types
      $declarations $target $replacement $active)
   (let ($declaration $rest) (decons-atom $declarations)
     (let $changed
          (case $declaration
            (((: $name $type)
               (let $changed-type
                    (if $active
                        (tptp-internal:substitute-in
                          $type $target $replacement)
                        $type)
                 (: $name $changed-type)))
             ($_ $declaration)))
       (let $next-active
            (if $active
                (if (== (tptp-internal:binder-name $declaration) $target)
                    False
                    True)
                False)
         (let $changed-rest
              (tptp-internal:substitute-shadowing-types
                $rest $target $replacement $next-active)
           (let $result (cons-atom $changed $changed-rest) $result))))))

(= (tptp-internal:freshen-conflicts
      $binder () $avoid)
   $binder)
(= (tptp-internal:freshen-conflicts
      $binder $conflicts $avoid)
   (let ($conflict $rest) (decons-atom $conflicts)
     (let $fresh (tptp:fresh-source-variable $conflict $avoid)
       (let $renamed (tptp:alpha-rename $binder $conflict $fresh)
         (let $next-avoid (cons-atom $fresh $avoid)
           (tptp-internal:freshen-conflicts
             $renamed $rest $next-avoid))))))

(= (tptp-internal:substitute-binder-direct
      $head $declarations $body $target $replacement)
   (let $changed-declarations
        (tptp-internal:substitute-declarations
          $declarations $target $replacement)
     (let $changed-body
          (tptp-internal:substitute-in $body $target $replacement)
       (tptp-internal:rebuild-binder
         $head $changed-declarations $changed-body))))

(= (tptp-internal:substitute-binder
      $head $declarations $body $target $replacement)
   (let $binder
        (tptp-internal:rebuild-binder $head $declarations $body)
     (let $free-here (tptp:free-variables $binder)
       (if (tptp-internal:member? $target $free-here)
           (let $names (tptp-internal:binder-names $declarations)
             (if (tptp-internal:member? $target $names)
                 (let $changed-types
                      (tptp-internal:substitute-shadowing-types
                        $declarations $target $replacement True)
                   (tptp-internal:rebuild-binder
                     $head $changed-types $body))
                 (let $replacement-free
                      (tptp:free-variables $replacement)
                   (let $conflicts
                        (tptp-internal:intersection
                          $names $replacement-free)
                     (if (== $conflicts ())
                         (tptp-internal:substitute-binder-direct
                           $head $declarations $body $target $replacement)
                         (let $all-binder
                              (tptp:all-source-variables $binder)
                           (let $all-replacement
                                (tptp:all-source-variables $replacement)
                             (let $avoid0
                                  (union-atom $all-binder $all-replacement)
                               (let $avoid1 (cons-atom $target $avoid0)
                                 (let $avoid (unique-atom $avoid1)
                                   (let $freshened
                                        (tptp-internal:freshen-conflicts
                                          $binder $conflicts $avoid)
                                     (tptp-internal:substitute-in
                                       $freshened
                                       $target $replacement))))))))))))
           $binder))))

(= (tptp-internal:substitute-expression
      $expression $target $replacement)
   (case $expression
     (((∀ $declarations $body)
        (tptp-internal:substitute-binder
          ∀ $declarations $body $target $replacement))
      ((∃ $declarations $body)
        (tptp-internal:substitute-binder
          ∃ $declarations $body $target $replacement))
      ((λ $declarations $body)
        (tptp-internal:substitute-binder
          λ $declarations $body $target $replacement))
      ((Π $declarations $body)
        (tptp-internal:substitute-binder
          Π $declarations $body $target $replacement))
      ((Σ $declarations $body)
        (tptp-internal:substitute-binder
          Σ $declarations $body $target $replacement))
      ((choice $declarations $body)
        (tptp-internal:substitute-binder
          choice $declarations $body $target $replacement))
      ((definite $declarations $body)
        (tptp-internal:substitute-binder
          definite $declarations $body $target $replacement))
      ($_
        (let $changed
             (map-atom $expression $item
               (tptp-internal:substitute-in
                 $item $target $replacement))
          $changed)))))

(= (tptp-internal:substitute-in $atom $target $replacement)
   (case (get-metatype $atom)
     ((Expression
        (tptp-internal:substitute-expression
          $atom $target $replacement))
      (Symbol (if (== $atom $target) $replacement $atom))
      ($_ $atom))))

(= (tptp:substitute $formula $target $replacement)
   (if (tptp:source-variable? $target)
       (tptp-internal:substitute-in $formula $target $replacement)
       (TPTP:NotSourceVariable $target)))
