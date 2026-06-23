; ===========================================================================
; lib/rho/patterns.metta  --  derived rho-calculus interaction patterns.
;
; Reusable rho-process combinators built from STRICT-CORE rho constructors only:
; rho:send / rho:recv / rho:par / rho:drop / rho:quote / rho:nil.  These are
; ENCODINGS over the core -- no engine primitive, no extended profile, no nested
; rhometta:run.  lib/rho.metta stays constructor-only; derived forms live here.
;
; A caller may still pass any rho process as an argument, including a
; rho:eval-payload produced by rhometta:eval in a Rhometta program.  That does
; not make the pattern depend on the Rhometta bridge; it is just part of the
; caller-supplied continuation.
;
; Import:  !(import! &self rho:patterns)
; ===========================================================================

!(import! &self rho)

; ---------------------------------------------------------------------------
; rho:patterns:!recv  --  REPLICATED RECEIVE (persistent input).
;
; The faithful lazy, input-guarded replication of Meredith & Radestock 2005,
; "A Reflective Higher-Order Calculus" (ENTCS 141), Section 3:
;
;     D(x)     = x(y).( x[y] | *y )            where  x[y]  =  x<*y>
;     !u(v).P  = x< u(v).(D(x) | P) >  |  D(x)
;
; Replication is DERIVED from reflection (quote/drop); the core needs no
; primitive replication / persistent receive.  Name output x[y] is the dropped
; name lifted again: (rho:send x (rho:drop y)), which the COMM re-quotes.
;
;   (rho:patterns:!recv $ctrl $chan $var $body)
;     replicated receive on $chan, written like the rho input !chan(var).body:
;     for EACH message on $chan, bind it to $var and run $body, re-arming after
;     each.  $body is a rho-process term (rho: constructors / rhometta:eval) and
;     may mention $var.  Keep anything reducible inside $body under rhometta:eval
;     so it fires at COMM, not eagerly at construction (a bare reducible body
;     would evaluate with $var still unbound).  To close over context, just let
;     $body mention the surrounding channels/values in scope at the call site.
;     The caller supplies the PRIVATE control channel $ctrl (freshness is not
;     hidden globally -- derive it from your own channel scheme, distinct from
;     $chan and any sub-channels).
;
; PERSISTENCE IS INTENTIONALLY NON-QUIESCENT.  After the last message the
; residual still contains the expected ARMED LISTENER: an `rho:recv` on $chan
; (and the replicator's control-channel `rho:send`/`rho:recv` pair) waiting for a
; message that may never come.  That is correct behaviour, not a stuck
; computation.  A bounded/quiescent variant is future work.
;
; Correctness criterion for a consuming demo (per Meredith-Radestock + the test
; contract):
; all expected outputs are produced, bad branches vanish, and the only leftover
; live process is the expected armed listener.  Do NOT use a generic
; "no rho:eval-payload anywhere" gate for every consumer: an armed listener may
; legitimately carry a deferred continuation supplied by the caller.  Instead,
; project the completed values your protocol promises and separately classify
; any residual sends/receives that are not the listener you intentionally left
; armed.  That observer is protocol-specific and lives in the consuming demo,
; not in this library.
; ---------------------------------------------------------------------------

; internal duplicator  D(x) = x(y).( x[y] | *y ),  x[y] = x<*y>.
(= (rho:patterns:_D $ctrl)
   (rho:recv $ctrl $y
     (rho:par (rho:send $ctrl (rho:drop $y))
              (rho:drop $y))))

(= (rho:patterns:!recv $ctrl $chan $var $body)
   (rho:par
     (rho:send $ctrl
       (rho:recv $chan $var (rho:par (rho:patterns:_D $ctrl) $body)))
     (rho:patterns:_D $ctrl)))
