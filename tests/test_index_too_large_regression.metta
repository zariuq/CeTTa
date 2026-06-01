!(bind! &stk (new-space stack))
!(assertEqualToResult
   (space-get &stk 4294967296)
   ((Error (space-get &stk 4294967296) IndexOutOfBounds)))
!(assertEqualToResult
   (space-truncate &stk 4294967296)
   ((Error (space-truncate &stk 4294967296) IndexOutOfBounds)))
