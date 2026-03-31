; Space values now expose their concrete space identity through get-type.

!(get-type &self)
!(bind! &kb (new-space))
!(get-type &kb)
!(bind! &seen (new-space hash))
!(get-type &seen)
!(bind! &q (new-space queue))
!(get-type &q)
!(bind! &ws (new-space mork))
!(get-type &ws)
