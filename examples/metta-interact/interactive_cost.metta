(site model-v1 economical (question $x) (candidate $x) (s z))
(site model-v1 thorough (question $x) (candidate $x) (s (s (s z))))

; Inspection exposes both authorized events and their occurrence-level costs.
(! (events model-v1 (question q1)))

; A budget of one admits only the economical event.
(! (fire model-v1 (s z) (question q1) economical))
(! (fire model-v1 (s z) (question q1) thorough))
