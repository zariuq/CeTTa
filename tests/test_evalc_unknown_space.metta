; HE does not silently fall back to &self when evalc receives a non-space.
; The runtime surfaces this as a shape error for the whole call.
!(evalc foo &fresh)
