!(prolog:query (quote true) (quote absent))
!(import! &self lib_prolog)
!(prolog:available?)
!(prolog:query (quote (between 1 3 $x)) (quote $x))
