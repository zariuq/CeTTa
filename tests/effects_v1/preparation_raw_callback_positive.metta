!(import! &self str)
!(function
  (chain (eval (map-atom (" a " " b ") $item
      (metta (str:trim $item) %Undefined% &self))) $parts
    (return $parts)))
