!(import! &self ../../../lib/lib_tptp.metta)

(= (compact-test:texts $policy $reader $texts)
   (if (== $texts ())
       ()
       (let $head (tptp:read-text $reader $policy (car-atom $texts))
         (let $tail (compact-test:texts $policy $reader (cdr-atom $texts))
           (cons-atom $head $tail)))))

(= (compact-test:files $policy $reader $paths)
   (if (== $paths ())
       ()
       (let $head (tptp:read-file $reader $policy (car-atom $paths))
         (let $tail (compact-test:files $policy $reader (cdr-atom $paths))
           (cons-atom $head $tail)))))

(= (compact-test:profile $policy $profile $texts $files)
   (let $reader (tptp:open-reader $profile)
     (let $from-texts (compact-test:texts $policy $reader $texts)
       (let $from-files (compact-test:files $policy $reader $files)
         (quote (TptpCompactV1 $policy $profile $from-texts $from-files))))))

(= (compact-test:strict-texts)
   ("fof(fof_formula,axiom,![X]:(p(X)=>?[Y]:q(Y)))."
    "cnf(c1,axiom,(~lives(X)|X=agatha))."
    "thf(mix_type,type,mix:beverage>syrup>beverage)."
    "thf(hot_mix,axiom,![B:beverage,S:syrup]:(hot@(mix@B@S)))."
    "thf(right_application,axiom,f@(g@X))."
    "thf(left_application,axiom,(f@g)@X)."
    "cnf(c1,axiom,~lives(X)|X=agatha,inference(res,[status(thm)],[a,b]))."
    "fof('X',axiom,p('X',X,$i,'$i',\"distinct value\",1/3,-2.5,2,123456789012345678901234567890)).fof(`Y,axiom,q)."
    "fof(cat,axiom,cat = 'cat').fof(q,axiom,p('a\\'b','a\\\\b',\"x\\\"y\")).fof(numbers,axiom,p(2/4,-6/4,0/5,+7,1.020,1.5E1,0.0000001,123.456e78,0.30000000000000004,1E-400,1E400,-0.0))."
    "fof(a,plain,p,unknown).fof(a,unknown,p,unknown).fof(unknown,axiom,p,inference(unknown,[unknown],[unknown:unknown]))."
    "fof(and,axiom,and(X,n0)).fof(let,axiom,let(X,p,q)).fof(include,axiom,include(a))."
    "fof(t,axiom,p(True,False,PI,EXP,Empty,X)).fof(w,axiom,p(true,false,empty,file,rat,word,'?True','a b','Cat','cat',cat)).fof(r,axiom,rat(1,2) = 1/2).fof(d,axiom,p($i,$$sys,$))."))

(= (compact-test:strict-files)
   ("tests/langdef/tptp/official_syntax_families_v9200.p"
    "tests/langdef/tptp/official_syntax_every_form_v9200.p"))

(= (compact-test:compatible-texts)
   ("cnf(c,axiom,(X = Y) | p(X) | (X != Z) | (Y = Z))."
    "fof(i,plain,p,introduced(definition,[new_symbols(def,[q])]))."
    "fof(f,axiom,![X]:p(X))."
    "cnf(u,axiom,p(X))."))

(= (compact-test:compatible-files)
   ("tests/langdef/tptp/official_internal_source_compatibility_v9200.p"
    "tests/langdef/tptp/official_syntax_parenthesized_cnf_v9200.p"))


!(compact-test:profile tagged strict (compact-test:strict-texts) (compact-test:strict-files))
!(compact-test:profile tagged corpus-compatible (compact-test:compatible-texts) (compact-test:compatible-files))
!(compact-test:profile mixed strict (compact-test:strict-texts) (compact-test:strict-files))
!(compact-test:profile mixed corpus-compatible (compact-test:compatible-texts) (compact-test:compatible-files))
!(compact-test:profile native strict (compact-test:strict-texts) (compact-test:strict-files))
!(compact-test:profile native corpus-compatible (compact-test:compatible-texts) (compact-test:compatible-files))
