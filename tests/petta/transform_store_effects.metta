!(import! &self (library lib_transform))
(= (show $s) (sort-atom (collapse (match $s $a $a))))

; An open stored row keeps its variables in the guided take.  Its completed
; selection is retained for publication; the result and store stay ordinary.
!(bind! &open (new-space))
!(add-atom &open (item $stored))
!(open-result (collapse (transform &open (take (item $x)) (got $x))))
!(open-left (show &open))

; Ground duplicates use the ordinary removal route and preserve multiplicity.
!(bind! &ground (new-space))
!(add-atom &ground (item a))
!(add-atom &ground (item a))
!(ground-result (collapse (transform &ground (take (item $x)) (got $x))))
!(ground-left (show &ground))

; A read and a take of one occurrence need two copies.
!(bind! &single (new-space))
!(add-atom &single (item a))
!(read-take-refused (collapse (transform &single (, (read (item $x)) (take (item $x))) (got $x))))
!(read-take-refused-left (show &single))

!(bind! &accepted (new-space))
!(add-atom &accepted (item a))
!(add-atom &accepted (item a))
!(read-take-accepted (collapse (transform &accepted (, (read (item $x)) (take (item $x))) (got $x))))
!(read-take-accepted-left (show &accepted))

!(bind! &empty (new-space))
!(empty-result (collapse (transform &empty (take (item $x)) (got $x))))
!(empty-left (show &empty))
