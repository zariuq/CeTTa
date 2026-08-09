; Semantic closure of Nil Geisweiller's optimized obfc-xp.mm2 search.
; Source revision: ngeiswei/chaining opt-bfc-mm2 @ 9a3287210880ca162beed379942a036a6f3cf8a6.
; The MM2 program's decFn-first physical join plan remains MORK-specific.
; HE represents computation zero here with an empty superposition.

(= (ofc $depth $hypcnt $target (: $proof $type))
   (if (< 0 $hypcnt)
       (if (< 0 $depth)
           (if (<= $hypcnt $depth)
               (ofc-step (- $depth 1) (- $hypcnt 1)
                         $target (: $proof $type))
               (superpose ()))
           (superpose ()))
       (let (: $proof $type) $target (: $proof $type))))

(= (ofc-step $depth $hypcnt $target
             (: $proof (-> (→ $𝜑 (→ $𝜓 $𝜑)) $body)))
   (ofc $depth $hypcnt $target (: ($proof ax₁) $body)))

(= (ofc-step $depth $hypcnt $target
             (: $proof
                (-> (→ (→ $𝜑 (→ $𝜓 $𝜒))
                       (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))
                    $body)))
   (ofc $depth $hypcnt $target (: ($proof ax₂) $body)))

(= (ofc-step $depth $hypcnt $target
             (: $proof
                (-> (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))
                    $body)))
   (ofc $depth $hypcnt $target (: ($proof ax₃) $body)))

(= (ofc-step $depth $hypcnt $target (: $proof (-> $antecedent $body)))
   (ofc $depth (+ $hypcnt 2) $target
        (: (mpⁱ $proof) (-> (→ $fresh $antecedent) (-> $fresh $body)))))

(= (obfc $depth (: $proof $type))
   (ofc $depth 1 (: $proof $type) (: I (-> $type $type))))

!(obfc 13 (: $proof (→ (→ (→ 𝜑 𝜓) 𝜒) (→ 𝜓 𝜒))))

; Negative bound canary: depth 12 produces no result.
; !(obfc 12 (: $proof (→ (→ (→ 𝜑 𝜓) 𝜒) (→ 𝜓 𝜒))))
