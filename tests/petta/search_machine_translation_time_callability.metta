(= (translated-before-install $x) (late-callable $x $x))

!(translated-before-install 1)

!(add-atom &self
   (= (late-callable $left $right) (+ $left $right)))

; An equation translated before a head had equations calls it once an
; equation for it is present, one added by add-atom included, as
; stable-fixed SWI-PeTTa does (swi_differences, 10).
!(translated-before-install 1)
!(late-callable 1 1)
