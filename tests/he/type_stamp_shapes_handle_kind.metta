; Typings kept by shape answer as inference does where a parent's typing
; reads a child symbol itself: a native handle's kind.
(: NativeHandle H)
(: mork-space K)
(: other K)
!(let $state (new-state (NativeHandle mork-space 0))
   (let $_ (change-state! $state (NativeHandle other 0)) (get-state $state)))
