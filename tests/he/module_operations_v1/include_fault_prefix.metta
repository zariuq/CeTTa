!(let $result (collapse (include include_fault_source)) done)
!(match &self (include-fault-before $x) $x)
!(match &self (include-fault-after $x) $x)
