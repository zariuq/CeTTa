!(import! &self (library str))

(= (native-truth-case $text)
   (let $predicate (|-> ($value) (str:starts-with $value "/"))
     (let $answer ($predicate $text)
       (case $answer ((True yes) (False no))))))

(= (native-truth-pair $text)
   (let $pair (|-> ($value) (Pair (str:starts-with $value "/") $value))
     (case ($pair $text)
       (((Pair True $value) (absolute $value))
        ((Pair False $value) (relative $value))))))

(= (native-truth-string $text)
   (let $copy (|-> ($value) (str:concat $value ""))
     (case ($copy $text) ((True boolean) ("True" string) ($other other)))))

(= (native-truth-effects $text)
   (let $predicate
       (|-> ($value)
            (progn (add-atom &self (truth-effect $value))
                   (str:starts-with $value "/")))
     (case ($predicate $text) ((True yes) (False no)))))

!(native-truth-case "/a")
!(native-truth-case "a")
!(collapse (native-truth-case "/b"))
!(native-truth-pair "/c")
!(native-truth-pair "c")
!(native-truth-string "True")
!(native-truth-effects "/once")
!(collapse (match &self (truth-effect $value) $value))
!(collapse (let $text (superpose ("/d" "d" "/d")) (native-truth-case $text)))
