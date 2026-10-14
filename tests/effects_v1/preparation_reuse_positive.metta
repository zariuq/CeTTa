!(import! &self str)
(= (trim-many $n)
  (if (== $n 0) done
    (let $parts (map-atom (" a " " b ") $item (str:trim $item))
      (trim-many (- $n 1)))))
!(trim-many 100)
