:- multifile 'things'/3.
:- discontiguous 'things'/3.

'things'(item,"a b",1,2.5).
'things'(pair,[x,y],z).
'things'(pair,[x,[y,w]],"q").
