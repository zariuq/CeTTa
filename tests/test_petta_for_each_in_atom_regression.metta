!(import! &self ../../hyperon/PeTTa/lib/lib_he)

!(assertEqual (for-each-in-atom (1 2) println!) (true true))
!(assertEqual (True False) (true false))
