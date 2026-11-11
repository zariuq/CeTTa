; Learning from failures on one of Popper's tasks: TASK the task's directory,
; POPPER_ROOT a Popper checkout (its generator encoding, popper/lp/alan.pl),
; MAX_VARS and MAX_BODY Popper's settings.
!(import! &self clingo)
!(import! &self asp)
!(import! &self fs)
!(import! &self str)
!(import! &self system)
!(import! &self "../../lib/ilp/popper.metta")
!(let* (($dir (system:getenv-or-default "TASK" ""))
        ($root (system:getenv-or-default "POPPER_ROOT" ""))
        ($gen (fs:read-text (str:concat $root "/popper/lp/alan.pl")))
        ($mv (parse (system:getenv-or-default "MAX_VARS" "6")))
        ($mb (parse (system:getenv-or-default "MAX_BODY" "4"))))
   (popper:learn $dir $gen (settings $mv $mb)))
