; One program, four identity profiles.  Run with
;   --profile identity-j | identity-scoped | identity-uip | identity-univalence
; The kernel is the same under every profile: elimination with one iota rule
; and every route retained, so definitional equality is a congruence
; everywhere.  A profile only decides which uniqueness or guest assumptions
; the language makes available, as explicit declarations used through
; elimination.  Every difference below is observable from inside a program.

!(id:policy)

!(bind! &s (new-space))
!(add-atom &s (: Nat (u 0)))
!(add-atom &s (: z Nat))
!(add-atom &s (: s (-> Nat Nat)))
!(add-atom &s (: F (-> (id Nat z z) Nat)))
; A route from z to z that is not reflexivity: evidence from elsewhere.
!(add-atom &s (: loop (id Nat z z)))

; 1. Definitionally, two routes are never the same route, under any profile,
;    and the same holds inside any context: conversion is a congruence.
!(type:eq &s (refl z) loop)
!(type:eq &s (F (refl z)) (F loop))

; 2. Declaring Nat a region publishes the explicit assumption
;    id:assumed-unique@Nat : Π a b (p q : id Nat a b). id (id Nat a b) p q
;    into the space, where the profile admits regions.
!(id:region &s Nat)
!(type:check &s (id:assumed-unique@Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))
; Under the profile with uniqueness everywhere, the assumption is global.
!(type:check &s (id:assumed-unique Nat z z (refl z) loop) (id (id Nat z z) (refl z) loop))
; Definitional equality is unchanged by either assumption.
!(type:eq &s (refl z) loop)

; 3. Two independent routes p1 and p2 between the same lengths, and a vector
;    transported along each.  The transports are distinct terms everywhere;
;    where uniqueness is assumed, elimination on the uniqueness witness proves
;    them equal, so an application consumer may exchange them by proof, while
;    an inspector still sees two routes.
!(add-atom &s (: Vec (-> Nat (u 0))))
!(add-atom &s (: v (Vec z)))
!(add-atom &s (: p1 (id Nat z z)))
!(add-atom &s (: p2 (id Nat z z)))
!(type:of &s (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1))
!(type:eq &s (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1)
             (id:eliminate Nat z (lam y (lam e (Vec y))) v z p2))
!(type:check &s
   (id:eliminate (id Nat z z) p1
     (lam q (lam w (id (Vec z) (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1)
                                (id:eliminate Nat z (lam y (lam e (Vec y))) v z q))))
     (refl (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1))
     p2
     (id:assumed-unique@Nat z z p1 p2))
   (id (Vec z) (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1)
               (id:eliminate Nat z (lam y (lam e (Vec y))) v z p2)))
!(type:check &s
   (id:eliminate (id Nat z z) p1
     (lam q (lam w (id (Vec z) (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1)
                                (id:eliminate Nat z (lam y (lam e (Vec y))) v z q))))
     (refl (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1))
     p2
     (id:assumed-unique Nat z z p1 p2))
   (id (Vec z) (id:eliminate Nat z (lam y (lam e (Vec y))) v z p1)
               (id:eliminate Nat z (lam y (lam e (Vec y))) v z p2)))

; 4. Paths between types.  Only the univalence profile declares `equiv` and
;    `ua`; elsewhere these stay inert.  Transport along the univalence path
;    is well typed but does not compute: the guest has no computation rule.
!(add-atom &s (: Bool (u 0)))
!(add-atom &s (: true Bool))
!(add-atom &s (: false Bool))
!(add-atom &s (: swap (equiv Bool Bool)))
!(type:of &s (ua Bool Bool swap))
!(type:of &s (id:eliminate (u 0) Bool (lam B (lam e B)) true Bool (ua Bool Bool swap)))
!(type:eq &s (id:eliminate (u 0) Bool (lam B (lam e B)) true Bool (ua Bool Bool swap)) true)

; 5. Adversarial: declare the universe itself a region.  Where regions are
;    admitted this publishes uniqueness of paths between types, and under the
;    univalence guest the swap path is then provably equal to reflexivity.
;    The incompatibility of uniqueness on the universe with a non-trivial
;    path is exhibited as an inhabited equation, not asserted.
!(id:region &s (u 0))
!(type:check &s (id:assumed-unique@u0 Bool Bool (ua Bool Bool swap) (refl Bool))
             (id (id (u 0) Bool Bool) (ua Bool Bool swap) (refl Bool)))
; The assumption is a visible atom of the space and can be withdrawn.
!(match &s (: id:assumed-unique@u0 $t) (remove-atom &s (: id:assumed-unique@u0 $t)))
!(type:check &s (id:assumed-unique@u0 Bool Bool (ua Bool Bool swap) (refl Bool))
             (id (id (u 0) Bool Bool) (ua Bool Bool swap) (refl Bool)))
