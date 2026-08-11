; Ordinary authored control calls share one revision-pinned generated program.
; The witness deliberately contains no compiler package, plan, or admission
; declaration: the runtime derives control semantics from the LanguageDef.

!(bind! &control-cache:columns (new-space))
!(add-atom &control-cache:columns (column 1))
!(add-atom &control-cache:columns (column 2))
!(add-atom &control-cache:columns (column 3))
!(add-atom &control-cache:columns (column 4))

(= (control-cache:safe $column $placed $distance)
   (if (== $placed ())
       True
       (let ($head $tail) (decons-atom $placed)
         (if (== $column $head)
             False
             (if (== (abs-math (- $column $head)) $distance)
                 False
                 (control-cache:safe
                   $column $tail (+ $distance 1)))))))

(= (control-cache:place $remaining $placed)
   (if (<= $remaining 0)
       $placed
       (match &control-cache:columns (column $column)
         (case (control-cache:safe $column $placed 1)
           ((True
             (control-cache:place
               (- $remaining 1)
               (cons-atom $column $placed))))))))

!(collapse (control-cache:place 4 ()))

; Negative/laziness witnesses: compiling the branch schedule must never force
; the unselected faulting arm.
!(if True selected (+ "unselected-branch" 1))
!(if False (+ "unselected-branch" 1) selected)
