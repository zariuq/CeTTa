!(bind! state (new-state start))

(= (effect) (change-state! state done))

!(effect)
!(get-state state)
