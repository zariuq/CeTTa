; Negative declaration information must be discarded after space changes.
!(get-type (untyped-box $x))
!(add-atom &self (: (untyped-box 7) Box))
!(let $type (get-type (untyped-box $x)) ($x $type))
!(remove-atom &self (: (untyped-box 7) Box))
!(get-type (untyped-box $x))

; Function inference must retain correlated argument and result declarations.
(: typed-box (-> Number Box))
!(get-type (typed-box 5))

; An extensible classifier still owns nested type judgments and their effects.
(= (get-type classified-name) (let $done (println! classifier-ran) Number))
!(get-type (typed-box classified-name))
