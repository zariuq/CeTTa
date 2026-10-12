(gslt-presentation-v1 RegularTokenValueCompilerV1
  (signature
    (operator metta-equation 2)
    (operator rtv1:compile 2)
    (operator rtv1:edges 2)
    (operator rtv1:append 2)
    (operator rtv1:answers 2)
    (operator rx-eps 0)
    (operator rx-empty 0)
    (operator rx-any 0)
    (operator rx-eof 0)
    (operator rx-char 1)
    (operator rx-range 2)
    (operator rx-class 1)
    (operator rx-alt 2)
    (operator rx-seq 2)
    (operator rx-star 1)
    (operator rx-minus 2)
    (operator rtv1:difference-edges 3)
    (operator rn-root 1)
    (operator rn-sub 2)
    (operator rn-in 1)
    (operator rn-out 1)
    (operator nfa-start 1)
    (operator nfa-accept 2)
    (operator nfa-epsilon 2)
    (operator nfa-char 3)
    (operator nfa-range 4)
    (operator nfa-class 3)
    (operator nfa-any 2)
    (operator nfa-eof 2)
    (operator compile-span-nfa 2)
    (operator cp 1)
    (operator left 0)
    (operator right 0)
    (operator body 0)
    (operator LNil 0)
    (operator LCons 2))
  (equations)
  (rewrites
    (rule compile
      (head (metta-equation
        (rtv1:compile ?tag ?regex)
        (rtv1:answers ?tag
          (LCons (nfa-start (rn-in (rn-root ?tag)))
            (LCons (nfa-accept (rn-out (rn-root ?tag)) ?tag)
              (rtv1:edges ?regex (rn-root ?tag)))))))
      (body))
    (rule answers-empty
      (head (metta-equation (rtv1:answers ?tag LNil) LNil))
      (body))
    (rule answers-cons
      (head (metta-equation
        (rtv1:answers ?tag (LCons ?edge ?tail))
        (LCons (compile-span-nfa ?tag ?edge) (rtv1:answers ?tag ?tail))))
      (body))
    (rule append-empty
      (head (metta-equation (rtv1:append LNil ?tail) ?tail))
      (body))
    (rule append-cons
      (head (metta-equation
        (rtv1:append (LCons ?head ?rest) ?tail)
        (LCons ?head (rtv1:append ?rest ?tail))))
      (body))
    (rule edges-epsilon
      (head (metta-equation (rtv1:edges rx-eps ?path)
        (LCons (nfa-epsilon (rn-in ?path) (rn-out ?path)) LNil)))
      (body))
    (rule edges-empty-language
      (head (metta-equation (rtv1:edges rx-empty ?path) LNil))
      (body))
    (rule edges-char
      (head (metta-equation (rtv1:edges (rx-char ?cp) ?path)
        (LCons (nfa-char (rn-in ?path) ?cp (rn-out ?path)) LNil)))
      (body))
    (rule edges-class
      (head (metta-equation (rtv1:edges (rx-class ?class) ?path)
        (LCons (nfa-class (rn-in ?path) ?class (rn-out ?path)) LNil)))
      (body))
    (rule edges-range
      (head (metta-equation (rtv1:edges (rx-range ?low ?high) ?path)
        (LCons (nfa-range (rn-in ?path) ?low ?high (rn-out ?path)) LNil)))
      (body))
    (rule edges-difference
      (head (metta-equation (rtv1:edges (rx-minus ?left ?right) ?path)
        (rtv1:difference-edges ?path ?left ?right)))
      (body))
    (rule edges-any
      (head (metta-equation (rtv1:edges rx-any ?path)
        (LCons (nfa-any (rn-in ?path) (rn-out ?path)) LNil)))
      (body))
    (rule edges-eof
      (head (metta-equation (rtv1:edges rx-eof ?path)
        (LCons (nfa-eof (rn-in ?path) (rn-out ?path)) LNil)))
      (body))
    (rule edges-alternation
      (head (metta-equation (rtv1:edges (rx-alt ?left ?right) ?path)
        (LCons (nfa-epsilon (rn-in ?path) (rn-in (rn-sub ?path left)))
          (LCons (nfa-epsilon (rn-in ?path) (rn-in (rn-sub ?path right)))
            (LCons (nfa-epsilon (rn-out (rn-sub ?path left)) (rn-out ?path))
              (LCons (nfa-epsilon (rn-out (rn-sub ?path right)) (rn-out ?path))
                (rtv1:append
                  (rtv1:edges ?left (rn-sub ?path left))
                  (rtv1:edges ?right (rn-sub ?path right)))))))))
      (body))
    (rule edges-sequence
      (head (metta-equation (rtv1:edges (rx-seq ?left ?right) ?path)
        (LCons (nfa-epsilon (rn-in ?path) (rn-in (rn-sub ?path left)))
          (LCons (nfa-epsilon (rn-out (rn-sub ?path left)) (rn-in (rn-sub ?path right)))
            (LCons (nfa-epsilon (rn-out (rn-sub ?path right)) (rn-out ?path))
              (rtv1:append
                (rtv1:edges ?left (rn-sub ?path left))
                (rtv1:edges ?right (rn-sub ?path right))))))))
      (body))
    (rule edges-star
      (head (metta-equation (rtv1:edges (rx-star ?body) ?path)
        (LCons (nfa-epsilon (rn-in ?path) (rn-out ?path))
          (LCons (nfa-epsilon (rn-in ?path) (rn-in (rn-sub ?path body)))
            (LCons (nfa-epsilon (rn-out (rn-sub ?path body)) (rn-in (rn-sub ?path body)))
              (LCons (nfa-epsilon (rn-out (rn-sub ?path body)) (rn-out ?path))
                (rtv1:edges ?body (rn-sub ?path body))))))))
      (body))))
