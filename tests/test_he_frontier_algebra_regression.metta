!(import! &self lts:he)

; Regression for the he_c3 overlap shape: a generic Empty-producing branch
; must not stop the surviving branch from being evaluated applicatively.
(= (min $a $b) (if (< $a $b) $a $b))
(= (s-tv (stv $s $c)) $s)
(.tv aa (stv 0.5 0.8))
(= (TV $x) (match &self (.tv $x $stv) $stv))
(= (TV (And $a $b)) (stv (min (s-tv (TV $a)) 0.1) 0.2))

!(assertEqualToResult
  (TV (And aa aa))
  ((stv 0.1 0.2)))

!(assertIncludes
  (lts:he:transitions (TV (And aa aa)))
  ((quote (stv (min (s-tv (TV aa)) 0.1) 0.2))))

; Reduced he_d4 result-algebra shape: Empty is the zero of a visible
; result/frontier set when a non-Empty branch survives.
(= (choice) Empty)
(= (choice) T)

!(assertEqualToResult
  (choice)
  (T))

!(assertEqualToResult
  (collapse (lts:he:transitions (choice)))
  (((quote T))))

; Bare Empty is the bottom result/frontier, not a visible answer.
!(assertEqualToResult
  Empty
  ())

!(assertEqualToResult
  (empty)
  ())

!(assertEqual
  (collapse (lts:he:transitions Empty))
  ())

(: Entity Type)
(: Plato Entity)
(: Human (-> Entity Type))
(: Mortal (-> Entity Type))
(: HumansAreMortal (-> (Human $t) (Mortal $t)))
(: PlatoIsHuman (Human Plato))
(: T Type)
(= (= $x $x) T)
(= (= $type T) (match &self (: $x $type) T))
(= (= $type T)
   (match &self (: $impl (-> $cause $type))
      (if (== $cause $type) (empty) (= $cause T))))

!(assertEqualToResult
  (= (Mortal Plato) T)
  (T))
