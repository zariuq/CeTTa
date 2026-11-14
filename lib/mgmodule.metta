; The Megalodon kernel stated as a rule module (MegalodonKernel.module), compiled to procedures by
; rulec.  Import with (import! &self mgmodule).
; (mgm:call TERM): TERM = (JUDGMENT ARG ...), each argument module data in the module's constructors,
;   (CTOR KID ...), (CTOR) or CTOR for a nullary one.  The judgment's value as (mgm:value V), (mgm:none)
;   when no rule applies, or (mgm:error "message") when the call cannot be made.
; (mgm:hold TERM): module data kept in the module's store across calls: (mgm:handle N), which stands for
;   the value wherever module data is expected in a call or a hold.  (mgm:call-hold TERM): as mgm:call,
;   the value kept: (mgm:value (mgm:handle M)).  (mgm:view H): (mgm:value V).  (mgm:release H): the value
;   is no longer kept.  (mgm:reset): every handle is refused, the store freed.  A released handle, or one made before a reset,
;   is refused ("stale handle N"); a value is never replaced under a handle.
; (mgm:stats): (mgm:stats CALLS READ-NS BUILD-NS MODULE-NS VIEW-NS NODES-READ NODES-VIEWED HOLDS HELD)
;   since the process started (the boundary's costs: reading atoms (calls and holds), building module
;   data, the module, results; HOLDS the holds made, HELD the values kept now).
; (mgm:set-relation NAME ROWS): ROWS = ((A ...) ...), the rows of a relation the module's rules query
;   (the module's "identity"), for the calls that follow; () empties it.  (mgm:rows N) or
;   (mgm:error "message").

(: mgm:call (-> Atom Atom))
(: __cetta_lib_mgm_call (-> Atom Atom))
(= (mgm:call $term)
   (function (chain (eval (__cetta_lib_mgm_call $term)) $result (return $result))))

(: mgm:set-relation (-> %Undefined% Atom Atom))
(: __cetta_lib_mgm_set-relation (-> Atom Atom Atom))
(= (mgm:set-relation $name $rows)
   (function (chain (eval (__cetta_lib_mgm_set-relation $name $rows)) $result (return $result))))

(: mgm:stats (-> Atom))
(: __cetta_lib_mgm_stats (-> Atom))
(= (mgm:stats)
   (function (chain (eval (__cetta_lib_mgm_stats)) $result (return $result))))

(: mgm:hold (-> Atom Atom))
(: __cetta_lib_mgm_hold (-> Atom Atom))
(= (mgm:hold $term)
   (function (chain (eval (__cetta_lib_mgm_hold $term)) $result (return $result))))

(: mgm:call-hold (-> Atom Atom))
(: __cetta_lib_mgm_call-hold (-> Atom Atom))
(= (mgm:call-hold $term)
   (function (chain (eval (__cetta_lib_mgm_call-hold $term)) $result (return $result))))

(: mgm:view (-> Atom Atom))
(: __cetta_lib_mgm_view (-> Atom Atom))
(= (mgm:view $handle)
   (function (chain (eval (__cetta_lib_mgm_view $handle)) $result (return $result))))

(: mgm:release (-> Atom Atom))
(: __cetta_lib_mgm_release (-> Atom Atom))
(= (mgm:release $handle)
   (function (chain (eval (__cetta_lib_mgm_release $handle)) $result (return $result))))

(: mgm:reset (-> Atom))
(: __cetta_lib_mgm_reset (-> Atom))
(= (mgm:reset)
   (function (chain (eval (__cetta_lib_mgm_reset)) $result (return $result))))
