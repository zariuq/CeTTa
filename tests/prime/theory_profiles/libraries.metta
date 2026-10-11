; A library supplies the same explicit declaration in a fresh named space.
!(import! &logic ../../../lib/profiles/hol.metta)
!(import! &hotg ../../../lib/profiles/megalodon_hotg.metta)
!(set:proves &logic (pf:imp-intro (pf:hyp 0)) (imp Falsum Falsum))
!(set:of &hotg Empty)
!(let $r (set:of Empty) (car-atom $r))
