!(import! &self (library lib_tabling))
!(tabled (adaptive-stable $key))
!(tabled (adaptive-grow $key))

; An initially empty read selects one dependency. Its later answer selects
; another; retained answers keep the order in which rounds discover them.
(= (adaptive-stable 0)
   (case (collapse (adaptive-stable 1))
     ((() (adaptive-stable 2)) ($items (adaptive-stable 3)))))
(= (adaptive-stable 1) (adaptive-stable 0))
(= (adaptive-stable 2) (adaptive-stable 0))
(= (adaptive-stable 2) (seed 2))
(= (adaptive-stable 3) (seed 3))

; The newly selected dependency joins the running recursive component.
(= (adaptive-grow 0)
   (case (collapse (adaptive-grow 1))
     ((() (adaptive-grow 2)) ($items (adaptive-grow 3)))))
(= (adaptive-grow 1) (adaptive-grow 0))
(= (adaptive-grow 2) (adaptive-grow 0))
(= (adaptive-grow 2) (seed 2))
(= (adaptive-grow 3) (adaptive-grow 0))
(= (adaptive-grow 3) (seed 3))

!(adaptive-stable 0)
!(adaptive-stable 1)
!(adaptive-grow 0)
!(adaptive-grow 1)
