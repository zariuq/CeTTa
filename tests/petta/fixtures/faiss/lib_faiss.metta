!(import! &self (library lib_import))

!(import_prolog_functions_from_file
    (library faiss_ffi faiss.pl)
    (faiss_create faiss_add faiss_search faiss_remove))
!(import_prolog_functions_from_file
    (library faiss_ffi embed.pl)
    (embed))

(= (new-atom-vectorspace $space $dimension)
   (bind! $space (new-state ((faiss_create $dimension) $dimension))))

(= (add-atom-vector $space $atom $vector)
   (let ($index $dimension) (get-state $space)
        (faiss_add $index (($atom $vector)))))

(= (add-atom-SRI $space $atom)
   (let ($index $dimension) (get-state $space)
        (faiss_add $index (($atom (embed $atom $dimension))))))

(= (match-k $count $space $query)
   (faiss_search (car-atom (get-state $space))
                 $query $count))

(= (match-sri $count $space $query)
   (let ($index $dimension) (get-state $space)
        (faiss_search $index (embed $query $dimension) $count)))

(= (atom-of $result) (car-atom (car-atom $result)))
