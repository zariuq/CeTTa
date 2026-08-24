; Proof-relevant relational lifting for the native Prime family `list`.
; Three parameters select the endpoint types and relation.  The two List
; endpoints are indices, so evidence retains the complete elementwise path.

(: map-rel
  (-> (source : (u $source-level))
      (target : (u $target-level))
      (relation :
        (-> (source-value : source)
            (target-value : target)
            (u $evidence-level)))
      (source-list : (list source))
      (target-list : (list target))
      (u $evidence-level)))

(: map-rel:nil
  (-> (source : (u $source-level))
      (target : (u $target-level))
      (relation :
        (-> (source-value : source)
            (target-value : target)
            (u $evidence-level)))
      (map-rel source target relation
        (list:nil source) (list:nil target))))

(: map-rel:cons
  (-> (source : (u $source-level))
      (target : (u $target-level))
      (relation :
        (-> (source-value : source)
            (target-value : target)
            (u $evidence-level)))
      (source-head : source)
      (target-head : target)
      (source-tail : (list source))
      (target-tail : (list target))
      (head-evidence : (relation source-head target-head))
      (tail-evidence :
        (map-rel source target relation source-tail target-tail))
      (map-rel source target relation
        (list:cons source source-head source-tail)
        (list:cons target target-head target-tail))))

(: map-rel:eliminate
  (-> (source : (u $source-level))
      (target : (u $target-level))
      (relation :
        (-> (source-value : source)
            (target-value : target)
            (u $evidence-level)))
      (motive :
        (-> (source-list : (list source))
            (target-list : (list target))
            (evidence :
              (map-rel source target relation
                source-list target-list))
            (u $motive-level)))
      (nil-case :
        (motive (list:nil source) (list:nil target)
          (map-rel:nil source target relation)))
      (cons-case :
        (-> (source-head : source)
            (target-head : target)
            (source-tail : (list source))
            (target-tail : (list target))
            (head-evidence :
              (relation source-head target-head))
            (tail-evidence :
              (map-rel source target relation
                source-tail target-tail))
            (induction :
              (motive source-tail target-tail tail-evidence))
            (motive
              (list:cons source source-head source-tail)
              (list:cons target target-head target-tail)
              (map-rel:cons source target relation
                source-head target-head source-tail target-tail
                head-evidence tail-evidence))))
      (source-list : (list source))
      (target-list : (list target))
      (evidence :
        (map-rel source target relation source-list target-list))
      (motive source-list target-list evidence)))
