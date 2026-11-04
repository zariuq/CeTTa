!(import! &self lang)

(Edge "a" "b")
(Edge "b" "c")

!(let $dl (lang:load Datalog)
   (lang:run $dl &self r#"
.decl Edge(x:symbol, y:symbol)
.decl Reach(x:symbol, y:symbol)
.input Edge
.output Reach

Reach(X,Y) :- Edge(X,Y).
Reach(X,Z) :- Reach(X,Y), Edge(Y,Z).
"#))

!(collapse (match &self (Reach $x $y) ($x $y)))
