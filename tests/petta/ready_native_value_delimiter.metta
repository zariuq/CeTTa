; A native that hands back a completed value through the evaluate path's
; delimiter, (function (return V)), answers V on PeTTa's ready-value path
; too, as on HE.
!(import! &self rhometta)
!(rhometta:values (rho:send (rho:quote rho:nil) 7))
!(collapse (rhometta:values (rho:send (rho:quote rho:nil) 7)))
