!(collapse
   (horn:prove-v1
     (GroundResolve
       ground-resolution:positive-left
       p
       (ground-resolution:cons
         (ground-resolution:positive p)
         ground-resolution:nil)
       (ground-resolution:cons
         (ground-resolution:negative p)
         ground-resolution:nil)
       ground-resolution:nil)))
