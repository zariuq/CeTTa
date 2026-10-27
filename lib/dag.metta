; Immutable shared forests of Binary:Leaf and Binary:Node byte-payload trees.
; Numeric node ids belong to one owned forest. Portable chronological records
; carry payloads and strictly backward child references, never runtime authority.
(: dag:from-forest (-> %Undefined% %Undefined%))
(: dag:from-records (-> %Undefined% %Undefined%))
(: dag:export (-> %Undefined% %Undefined%))
(: dag:roots (-> %Undefined% %Undefined%))
(: dag:node (-> %Undefined% %Undefined% %Undefined%))
(: dag:unfold (-> %Undefined% %Undefined% %Undefined%))
(: dag:stats (-> %Undefined% %Undefined%))
(: dag:close (-> %Undefined% %Undefined%))

; Calls each algebra operation once per unique node, in postorder. Agreement
; with folding unfolded trees requires a pure deterministic algebra. For effects
; or choice this API explicitly observes the unique-node schedule, not a tree fold.
(= (dag:fold-unique $forest $leaf $node)
   (case (dag:export $forest)
     (((Dag:Forest $roots $records)
       (let $values (dag:fold-records $records () $leaf $node)
         (map-atom $roots $root (index-atom $values (- (- (size-atom $values) 1) $root)))))
      ($other $other))))

(= (dag:fold-records $records $values $leaf $node)
   (if (== $records ()) $values
     (let ($record $tail) (decons-atom $records)
       (let $value
         (case $record
           (((Binary:Leaf $bytes) ($leaf $bytes))
            ((Binary:Node $tag $children)
             (let $arguments
               (map-atom $children $child (index-atom $values (- (- (size-atom $values) 1) $child)))
               ($node $tag $arguments)))))
         (let $next (cons-atom $value $values)
           (dag:fold-records $tail $next $leaf $node))))))
