!(import! &self ../../hyperon/PeTTa/lib/lib_he)

!(assertEqual (unquote (quote (+ 1 2))) 3)
!(assertEqual (repr (unquote 42)) "(unquote 42)")
!(assertEqual (noreduce-eq (+ 1 2) (+ 1 2)) True)
!(assertEqual (noreduce-eq (+ 1 2) 3) False)
