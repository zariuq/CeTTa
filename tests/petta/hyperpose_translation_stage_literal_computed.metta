!(msort
   (let $_ (add-atom &self (= (literal-stage-fresh) seen))
      (collapse
         (hyperpose
            ((literal-stage-fresh)
             companion)))))

!(msort
   (let $_ (add-atom &self (= (computed-stage-fresh) seen))
      (let $branches ((computed-stage-fresh) companion)
         (collapse (hyperpose $branches)))))
