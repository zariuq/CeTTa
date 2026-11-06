; Typings kept by shape answer as inference does where a parent's typing
; reads a literal child's value: a native handle's string kind.
(: NativeHandle H)
!(let $state (new-state (NativeHandle "mork-space" 0))
   (let $_ (change-state! $state (NativeHandle "other" 0)) (get-state $state)))
