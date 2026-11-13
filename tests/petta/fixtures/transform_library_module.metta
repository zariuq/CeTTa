; A module that imports transform's library for the functions it defines.
!(import! &self (library lib_transform))
(= (module-rows $space) (transform $space (n $x) (got $x)))
(= (module-takes $space) (transform $space (take (n $x)) (taken $x) $space))
