; Inventory-backed admin surface: CeTTa currently prints the explicitly loaded
; module graph rather than mirroring HE's builtin module inventory.
!(register-module! support/import_pkg)
!(bind! &m (mod-space! import_pkg:moduleA))
!(print-mods!)
