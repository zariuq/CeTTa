; metta.md marks call-native as an internal instruction that cannot be called
; directly from a MeTTa program. HE surfaces direct user calls as an error.
!(call-native + (1 2))
