!(bind! &stk (new-space stack))
!(assertEqualToResult
   (space:stack:get &stk 4294967296)
   ((Error (space:stack:get &stk 4294967296) IndexOutOfBounds)))
!(assertEqualToResult
   (space:stack:truncate &stk 4294967296)
   ((Error (space:stack:truncate &stk 4294967296) IndexOutOfBounds)))
