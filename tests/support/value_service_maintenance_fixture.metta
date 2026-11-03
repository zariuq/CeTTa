!(import! &self (library system))
(= (maintenance-host:configuration)
  (let* (($socket (system:getenv-or-default "CETTA_KB_SOCKET" ""))
         ($mode (system:getenv-or-default "CETTA_TEST_MAINTENANCE_MODE" "normal")))
    (ValueServiceV2 $socket $mode
      ((ServiceRouteV1 Echo Write maintenance-host:echo))
      (ServiceLimitsV1 8 16 1048576 30000 16777216 100000)
      (ServiceMaintenanceV1 maintenance-host:step 0 1))))
(= (maintenance-host:echo (ServiceCallV1 $data $peer $resource $argument)) $argument)
(= (maintenance-host:step (ServiceMaintenanceCallV1 $mode $state $work))
  (if (== $mode "malformed") UnexpectedMaintenanceResult
    (if (< $state 5)
      (ServiceMaintenanceMoreV1 (+ $state 1) (MaintenanceCounterV1 (+ $state 1)))
      (ServiceMaintenanceIdleV1 $state (MaintenanceCounterV1 $state)))))
