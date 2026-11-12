; A selected operation value is read at the head of a source application.
; Constructed calls and quoted operations remain data until activation.
!(import! &self lts:rho)
(= (service:ping)
   (rho:par (rho:send (rho:quote rho:nil) rho:nil)
            (rho:recv (rho:quote rho:nil) $message rho:nil)))
(: service:apply (-> Proc Atom %Undefined%))
(= (service:apply $operation $argument) ($operation $argument))
!(let $process (service:ping)
   (service:apply __cetta_lib_lts_rho_transitions $process))
!(let $process (service:ping)
   (collapse (service:apply __cetta_lib_lts_rho_transitions $process)))
!(let $process (service:ping)
   (lts:transitions __cetta_lib_lts_rho_transitions $process))
!(let $process (service:ping) (lts:rho:transitions $process))
; A quiescent frontier supplies zero answer occurrences.
!(collapse (service:apply __cetta_lib_lts_rho_transitions rho:nil))
; The observer does not execute a constructed or quoted application.
!(let $head __cetta_lib_lts_rho_transitions
   (let $arguments (rho:nil) (cons-atom $head $arguments)))
!(let $head (quote __cetta_lib_lts_rho_transitions) ($head rho:nil))
; The same carrier rule applies to authored operations and unknown symbols.
(= (service:inc $number) (+ $number 1))
!(service:apply service:inc 4)
!(service:apply service:unknown 4)
; Open constructed syntax keeps its code inert as well.
(= (service:effect $argument) (println! SERVICE_HEAD_MUST_STAY_DATA))
!(let $head service:effect
   (let $arguments ($unbound)
     (let $data (cons-atom $head $arguments)
       (== (car-atom $data) service:effect))))
; A matched operation value can be applied by the caller's written body.
!(bind! &operations (new-space))
!(add-atom &operations (operation service:inc))
!(match &operations (operation $head) ($head 4))
; A grammar rejection is a diagnostic; it is not an empty frontier.
!(service:apply __cetta_lib_lts_rho_transitions (rho:send rho:nil))
