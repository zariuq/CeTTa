; Typings kept by shape answer as inference does where a parent's typing
; reads a child symbol itself: a tuple part's own annotations, which its
; subtype-closed typing does not determine.
(: h H)
(: x A)
(: y A)
(: y B)
(: z B)
(:< A B)
!(let $ignored (new-state (h y))
   (let $state (new-state (h z))
     (let $_ (change-state! $state (h x)) (get-state $state))))
