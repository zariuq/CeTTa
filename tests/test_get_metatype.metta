; HE-backed: get-metatype returns the structural metatype of an atom.
!(assertEqual (get-metatype foo) Symbol)
!(assertEqual (get-metatype $x) Variable)
!(assertEqual (get-metatype (foo bar)) Expression)
!(assertEqual (get-metatype 1) Grounded)
