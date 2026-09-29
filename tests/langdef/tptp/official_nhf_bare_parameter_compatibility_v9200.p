thf(bare_symbol,axiom,{$necessary(agent)} @ p).
thf(bare_function,axiom,{$knows(f(a))} @ p).
thf(bare_variable,axiom,! [X: $i] : ({$box(X)} @ p)).
thf(bare_number,axiom,{$box(1)} @ p).
thf(bare_with_index,axiom,{$box(agent,#1)} @ p).
thf(bare_with_definition,axiom,{$box(agent,$agent == b)} @ p).
