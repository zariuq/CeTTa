; State operations — new-state, change-state!, get-state
!(bind! &count (new-state 0))
!(assertEqual (get-state &count) 0)
!(change-state! &count 1)
!(assertEqual (get-state &count) 1)
