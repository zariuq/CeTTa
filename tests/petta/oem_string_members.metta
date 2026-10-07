!(import! &self str)

!(collapse (is-member "a" ("a" "b" "a" "c")))
!(collapse (is-member "absent" ("a" "b" "a" "c")))
!(collapse (let true (is-member $item ("a" "b" "a")) $item))
!(collapse (let true (is-member "a" ("b" $item "c")) $item))
!(collapse (let true (is-member "a" ("a" $item "b")) (Held $item $item)))
!(collapse (let true (is-member "a" ("a" "b")) (superpose (first second))))
!(collapse (let true (is-member "a" ("a" "b" "a" "c"))
            (let $effect (println! member_effect) kept)))

!(let* (($value (str:from-codepoints (65 0 66))))
   (collapse (is-member $value ($value "A" $value "other"))))
!(let* (($value (str:from-codepoints (955 0 90))))
   (collapse (is-member $value ($value "different" $value "other"))))

(= (member:loop $remaining)
   (if (< $remaining 1) done
     (if (is-member "a" ("a" "z"))
       (member:loop (- $remaining 1)) wrong)))
!(member:loop 1000)
