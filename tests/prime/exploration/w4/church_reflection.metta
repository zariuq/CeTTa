; Proof by reflection on the native simple fragment: Church arithmetic over a
; base type, decided by the fragment's conversion checker.  A judgment is a
; computation on the codes of two terms; an answer, when there is one, is a
; proof or a refutation, and too little budget gives no answer at all.
;
;   two  = lam f x. f (f x)
;   four = lam f x. f (f (f (f x)))
;   add  = lam m n f x. m f (n f x)

(: Base (u 0))
(: s (-> Base Base))
(: z Base)

; 1. 2 + 2 converts to 4, decided by computation on the codes (two_plus_two).
!(type:eq ((((lam (m : (-> (-> Base Base) (-> Base Base)))
               (lam (n : (-> (-> Base Base) (-> Base Base)))
                 (lam (f : (-> Base Base)) (lam (x : Base) ((m f) ((n f) x))))))
             (lam (f : (-> Base Base)) (lam (x : Base) (f (f x)))))
            (lam (f : (-> Base Base)) (lam (x : Base) (f (f x))))))
          (lam (f : (-> Base Base)) (lam (x : Base) (f (f (f (f x)))))))

; 2. The same theorem read at a carrier and a function: add 2 2 applied to s
;    and z is four-fold application (two_plus_two_denote).
!(type:eq (((((lam (m : (-> (-> Base Base) (-> Base Base)))
                (lam (n : (-> (-> Base Base) (-> Base Base)))
                  (lam (f : (-> Base Base)) (lam (x : Base) ((m f) ((n f) x))))))
              (lam (f : (-> Base Base)) (lam (x : Base) (f (f x)))))
             (lam (f : (-> Base Base)) (lam (x : Base) (f (f x))))) s) z)
          (s (s (s (s z)))))

; 3. With too little budget the checker gives no answer, never a wrong one
;    (two_plus_two_needs_fuel): the judgment stays as it was written.
!(type:eq ((((lam (m : (-> (-> Base Base) (-> Base Base)))
               (lam (n : (-> (-> Base Base) (-> Base Base)))
                 (lam (f : (-> Base Base)) (lam (x : Base) ((m f) ((n f) x))))))
             (lam (f : (-> Base Base)) (lam (x : Base) (f (f x)))))
            (lam (f : (-> Base Base)) (lam (x : Base) (f (f x))))))
          (lam (f : (-> Base Base)) (lam (x : Base) (f (f (f (f x))))))
          1)

; 4. Some budget decides every pair (conversionDecision_complete), and an
;    answer is always the right one (ReflectiveDecision.prove), so a large
;    budget answers as the unbudgeted judgment does.
!(type:eq ((((lam (m : (-> (-> Base Base) (-> Base Base)))
               (lam (n : (-> (-> Base Base) (-> Base Base)))
                 (lam (f : (-> Base Base)) (lam (x : Base) ((m f) ((n f) x))))))
             (lam (f : (-> Base Base)) (lam (x : Base) (f (f x)))))
            (lam (f : (-> Base Base)) (lam (x : Base) (f (f x))))))
          (lam (f : (-> Base Base)) (lam (x : Base) (f (f (f (f x))))))
          1000000)
