; Only computation that admission published enters conversion.
!(set:inductive &self colour (u 0) (: red colour) (: blue colour))
!(type:eq &self red blue)

; A rule written as a raw atom was never checked, so it computes nothing.
(type:rule red 0 () (DeclConst blue))
!(type:eq &self red blue)
!(type:check &self (refl blue) (id colour red blue))

; A raw rule that would unfold forever is inert as well.
(type:rule blue 0 () (DeclConst blue))
!(type:eq &self blue blue)

; A definition record copied out of a tried definition was never published.
!(let (Established (SetPublish $record $declaration $first $second))
      (try (set:define &self flip (-> colour colour)
             (= (flip red) blue)
             (= (flip blue) red)))
   (let $a (add-atom &self $record)
     (let $b (add-atom &self $declaration)
       (let $c (add-atom &self $first)
         (add-atom &self $second)))))
!(type:eq &self (flip red) blue)
!(set:eq &self (flip red) blue)

; A published definition computes.
!(set:define &self swap (-> colour colour)
   (= (swap red) blue)
   (= (swap blue) red))
!(type:eq &self (swap red) blue)
!(type:eq &self (swap red) red)
!(set:eq &self (swap blue) red)
