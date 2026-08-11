(= seed (hypothesis noisy))
(= (hypothesis noisy) (hypothesis refined))
(= (hypothesis refined) (explanation compact))

(! (seek (s (s (s z))) (explanation $kind) seed))
