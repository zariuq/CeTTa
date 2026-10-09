!(== (mod-space! top) &self)
!(case (mod-space! module-top-probe-missing) (((Error $call $why) refused) ($ok unexpected)))
!(case (mod-space!) (((Error $call $why) refused) ($ok unexpected)))
