!(import! &self (library str))

(= (native-word $tag)
   (case $tag ((go (str:string->symbol "Empty")))))

!(str:string->symbol "Empty")
!(native-word go)
!(collapse (native-word (superpose (go go))))
!(once (native-word (superpose (go go))))
!(== (native-word go) Empty)
