!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (test:extended-bnf-document
      (TPTP:StageValue extended-bnf-ast
        (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))

(= (test:extended-bnf-source ($source $_document))
   $source)
(= (test:extended-bnf-document-value ($_source $document))
   $document)

(= (test:row-counts (tptp-ebnf-v1:entries-nil))
   (0 0 0 0))
(= (test:row-counts
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:syntax-row $_name $_rhs $_span) $tail))
   (let ($syntax $semantic $token $character)
        (test:row-counts $tail)
     ((+ $syntax 1) $semantic $token $character)))
(= (test:row-counts
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:semantic-row $_name $_rhs $_span) $tail))
   (let ($syntax $semantic $token $character)
        (test:row-counts $tail)
     ($syntax (+ $semantic 1) $token $character)))
(= (test:row-counts
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:token-row $_name $_rhs $_span) $tail))
   (let ($syntax $semantic $token $character)
        (test:row-counts $tail)
     ($syntax $semantic (+ $token 1) $character)))
(= (test:row-counts
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:character-row $_name $_rhs $_span) $tail))
   (let ($syntax $semantic $token $character)
        (test:row-counts $tail)
     ($syntax $semantic $token (+ $character 1))))
(= (test:row-counts
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:comment $_text $_span) $tail))
   (test:row-counts $tail))
(= (test:row-counts
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:blank $_span) $tail))
   (test:row-counts $tail))

!(bind! &read
   (case (system:arg 0)
     (($path (tptp:extended-bnf-file->ast $path)))))
!(bind! &source-and-document
   (test:extended-bnf-document &read))
!(bind! &source
   (test:extended-bnf-source &source-and-document))
!(bind! &document
   (test:extended-bnf-document-value &source-and-document))

!(assertEqual
   &source
   "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
!(assertEqual
   (test:row-counts
     (tptp:extended-bnf-document-entry-list &document))
   (232 67 18 38))

!(println! (TptpOfficialExtendedBnfFullReaderV1Summary 2 2 0))
