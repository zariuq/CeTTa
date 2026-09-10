!(import! &self (library lib_memo))
!(translatePredicate (set_prolog_flag occurs_check True))

(: Sized (-> $a Type))
(: MkSized (-> $n $a (Sized $a)))

(= (obc-gtz $s (: ax1 (imp $phi (imp $psi $phi))))
   (MkSized 1 (: ax1 (imp $phi (imp $psi $phi)))))
(= (obc-gtz $s
       (: ax2
          (imp (imp $phi (imp $psi $chi))
               (imp (imp $phi $psi) (imp $phi $chi)))))
   (MkSized 1
      (: ax2
         (imp (imp $phi (imp $psi $chi))
              (imp (imp $phi $psi) (imp $phi $chi))))))
(= (obc-gtz $s (: ax3 (imp (imp (neg $phi) (neg $psi))
                              (imp $psi $phi))))
   (MkSized 1 (: ax3 (imp (imp (neg $phi) (neg $psi))
                            (imp $psi $phi)))))
(= (obc-gtz $s (: (mp $f $x) $b))
   (if (< 2 $s)
       (let* (((MkSized $fs (: $f (imp $a $b)))
                 (obc (- $s 2) (: $f (imp $a $b))))
              ((MkSized $xs (: $x $a))
                 (obc (- (- $s 1) $fs) (: $x $a))))
         (MkSized (+ (+ $fs $xs) 1) (: (mp $f $x) $b)))
       (empty)))
(= (obc $s (: $x $a))
   (if (< 0 $s)
       (obc-gtz $s (: $x $a))
       (empty)))

!(memoize obc)
!(obc 13 (: $proof
   (imp (imp (imp phi psi) chi) (imp psi chi))))
