; The retained zero-add proof is stored as text addressed by its sha256.
; A consumer opens that text and checks the hash. The hash alone is not the
; article. A missing, altered, wrong-index, or stale address does not open it.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(pf:equality &self num refl@num subst@num)
!(set:known-proposition &self num-ind)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(set:theorem &self zero-add
   (all num (lam k (eq num (add zero k) k)))
   (pf:imp-elim
     (pf:imp-elim
       (pf:all-elim (pf:known num-ind) (lam k (eq num (add zero k) k)))
       (pf:all-elim (pf:known refl@num) zero))
     (pf:fix k
       (pf:assume ih
         (pf:imp-elim
           (pf:imp-elim
             (pf:all-elim
               (pf:all-elim
                 (pf:all-elim (pf:known subst@num)
                   (lam z (eq num (suc (add zero k)) (suc z))))
                 (add zero k))
               k)
             ih)
           (pf:all-elim (pf:known refl@num) (suc (add zero k))))))))

(= (article-verdict $index $digest $revision)
   (let $at-index (collapse (match &self
         (MegalodonSharedArticleV1 $index $digest $revision $text) $text))
     (if (== $at-index ())
         (let $by-digest (collapse (match &self
               (MegalodonSharedArticleV1 $i $digest $r $text) ($i $r $text)))
           (if (== $by-digest ())
               (Missing)
               (let ($i $r $text) (car-atom $by-digest)
                 (if (== $i $index)
                     (if (== (sha256 $text) $digest) (Stale) (Altered))
                     (WrongIndex)))))
         (let $text (car-atom $at-index)
           (if (== $text $digest)
               (DigestOnly)
               (if (== (sha256 $text) $digest)
                   (Checked Opened)
                   (Altered)))))))

!(let $package (set:native-proof zero-add)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $text (repr $term)
   (let $article-digest (sha256 $text)
    (let $_ (add-atom &self
             (MegalodonSharedArticleV1 0 $article-digest "rev-zero-add" $text))
     (let $resolved (article-verdict 0 $article-digest "rev-zero-add")
      (let $opened (collapse (match &self
             (MegalodonSharedArticleV1 0 $article-digest "rev-zero-add" $body)
             $body))
       (let $article (car-atom $opened)
        (let $use (set:native-use $package (Lam $type (idx 0)))
         (let (SetNativeUseV1 $checked $consumer $result $result-type) $use
          (ResolvedArticle $resolved
            (and (== $checked $package) (== $article (repr $term)))
            (size-atom $assumptions))))))))))))

!(article-verdict 0 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" "rev-zero-add")
!(let $_ (add-atom &self (MegalodonSharedArticleV1 2 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" "rev-zero-add" "(tampered article)"))
  (article-verdict 2 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" "rev-zero-add"))
!(let $digest (match &self (MegalodonSharedArticleV1 0 $d "rev-zero-add" $t) $d)
  (article-verdict 1 $digest "rev-zero-add"))
!(let $digest (match &self (MegalodonSharedArticleV1 0 $d "rev-zero-add" $t) $d)
  (article-verdict 0 $digest "rev-stale"))
!(let $only (sha256 "not-the-article")
  (let $hits (collapse (match &self (MegalodonSharedArticleV1 $i $only $r $text) $text))
   (if (== $hits ()) (DigestOnly) (Checked (car-atom $hits)))))

; A matching hash of some other text is not the checked term. Acceptance
; requires the opened article to be that term.
!(let $package (set:native-proof zero-add)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $text "(not-a-proof-term)"
   (let $article-digest (sha256 $text)
    (let $_ (add-atom &self
             (MegalodonSharedArticleV1 7 $article-digest "rev-zero-add" $text))
     (let $opened (collapse (match &self
            (MegalodonSharedArticleV1 7 $article-digest "rev-zero-add" $body) $body))
      (let $article (car-atom $opened)
       (ArticleNotEvidence (== $article (repr $term))
         (== (sha256 $article) $article-digest)))))))))

!(let $origins (collapse (match &self (set:known $n $p $origin $tree $depends $revision $signature) $origin))
  (Inventory $origins))
