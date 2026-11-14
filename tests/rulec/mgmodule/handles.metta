; Handles of the compiled Megalodon kernel module (mgmodule): a theory kept once and extended through
; call-hold.  The extended theory answers for the name it admitted; the old handles keep answering for
; the old theories and never for the newer one.  A handle of the wrong sort, a released handle, a handle
; never issued and every handle made before a reset are refused; a hold after the reset gets a new number.
!(import! &self mgmodule)
; T0: no primitives, reserved names, declarations or selections
!(mgm:hold (thy (prims.nil) (names.nil) (ds.nil) (ss.nil)))
; select, then admit, the parameter 1 of type prop: T1, T2
!(mgm:call-hold (thy.select-param (mgm:handle 0) (nm.i (nm.nil)) (Tp.prop)))
!(mgm:call-hold (thy.admit-param (mgm:handle 1) (nm.i (nm.nil)) (Tp.prop)))
; T2 knows the parameter; T0 and T1 do not
!(mgm:call (thy.term (mgm:handle 2) (nm.i (nm.nil))))
!(mgm:call (thy.term (mgm:handle 0) (nm.i (nm.nil))))
!(mgm:call (thy.term (mgm:handle 1) (nm.i (nm.nil))))
; the name is fresh in T0, not in T2
!(mgm:call (thy.fresh-term (mgm:handle 0) (nm.i (nm.nil))))
!(mgm:call (thy.fresh-term (mgm:handle 2) (nm.i (nm.nil))))
; a handle where a name is expected
!(mgm:call (thy.term (nm.i (nm.nil)) (mgm:handle 2)))
; a kept value read back
!(mgm:view (mgm:handle 0))
; released, then used
!(mgm:release (mgm:handle 0))
!(mgm:call (thy.term (mgm:handle 0) (nm.i (nm.nil))))
; never issued
!(mgm:call (thy.term (mgm:handle 99) (nm.i (nm.nil))))
; a reset: every earlier handle is refused, a new hold gets a new number
!(mgm:reset)
!(mgm:call (thy.term (mgm:handle 2) (nm.i (nm.nil))))
!(mgm:hold (thy (prims.nil) (names.nil) (ds.nil) (ss.nil)))
!(mgm:call (thy.fresh-term (mgm:handle 3) (nm.i (nm.nil))))
