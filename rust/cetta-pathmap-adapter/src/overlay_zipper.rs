//GOAT, Internal discussion about the API we eventually want.
//
//It strikes me that "overlay" is *almost* join.  The main difference being the treatment of values. One
// possible direction is to embrace this and upgrade this to a full "JoinZipper" that performs a join
// on-the-fly.
//
//However, with the rearchitecture of the algebraic ops towards support for policies and subtrie algebra,
// that might make this too complicated.  In either case, I'd prefer to wait until that change fully shakes
// out before trying to mess with this zipper.  Also the algebraic traits are (and will be) defined with
// the expectations that new trie storage can be created, and thus it seems tricky to shoe-horn that into
// a zipper API.
//
//A half-step might be to change the mapping function into a "merging" function,
// e.g. `Fn(Option<&VBase>, Option<&VOverlay>) -> VOverlay`  Although this also breaks the trait contract
// with ZipperValues, etc. because we don't have a place to store the newly created value
//

use fast_slice_utils::find_prefix_overlap;
use pathmap::utils::{BitMask, ByteMask};
use pathmap::zipper::{PathObserver, Zipper, ZipperIteration, ZipperMoving, ZipperPath, ZipperValues, ZipperValuesAt};

/// Zipper that traverses a virtual trie formed by fusing the tries of two other zippers
pub struct OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    a: AZipper,
    b: BZipper,
    mapping: Mapping,
    _marker: core::marker::PhantomData<(AV, BV, OutV)>,
}

fn identity_ref<'a, V>(a_val: Option<&'a V>, b_val: Option<&'a V>) -> Option<&'a V> {
    a_val.or(b_val)
}

impl<V, AZipper, BZipper>
    OverlayZipper<
        V,
        V,
        V,
        AZipper,
        BZipper,
        for<'a> fn(Option<&'a V>, Option<&'a V>) -> Option<&'a V>,
    >
where
    AZipper: ZipperMoving,
    BZipper: ZipperMoving,
{
    /// Create a new `OverlayZipper` from two other zippers, using a default value mapping function
    ///
    /// In cases where both source zippers supply a value, the value from `AZipper` will be supplied by
    /// the `OverlayZipper`.
    pub fn new(a: AZipper, b: BZipper) -> Self {
        Self::with_mapping(a, b, identity_ref)
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: ZipperMoving,
    BZipper: ZipperMoving,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    /// Create a new `OverlayZipper` from two other zippers, using a the supplied value mapping function
    pub fn with_mapping(mut a: AZipper, mut b: BZipper, mapping: Mapping) -> Self {
        a.reset();
        b.reset();
        Self {
            a,
            b,
            mapping,
            _marker: core::marker::PhantomData,
        }
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: ZipperPath + ZipperValues<AV>,
    BZipper: ZipperPath + ZipperValues<BV>,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    fn to_sibling(&mut self, next: bool) -> Option<u8> {
        let path = self.path();
        let Some(&last) = path.last() else {
            return None;
        };
        self.ascend(1);
        let child_mask = self.child_mask();
        let maybe_child = if next {
            child_mask.next_bit(last)
        } else {
            child_mask.prev_bit(last)
        };
        let Some(child) = maybe_child else {
            self.descend_to_byte(last);
            return None;
        };
        self.descend_to_byte(child);
        Some(child)
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> ZipperValues<OutV>
    for OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: ZipperValues<AV>,
    BZipper: ZipperValues<BV>,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    fn val(&self) -> Option<&OutV> {
        (self.mapping)(self.a.val(), self.b.val())
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> ZipperValuesAt<OutV>
    for OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: ZipperValuesAt<AV>,
    BZipper: ZipperValuesAt<BV>,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    fn val_at<K: AsRef<[u8]>>(&self, path: K) -> Option<&OutV> {
        let path = path.as_ref();
        (self.mapping)(self.a.val_at(path), self.b.val_at(path))
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> Zipper
    for OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: Zipper + ZipperValues<AV>,
    BZipper: Zipper + ZipperValues<BV>,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    fn path_exists(&self) -> bool {
        self.a.path_exists() || self.b.path_exists()
    }
    fn is_val(&self) -> bool {
        //NOTE: the mapping function has the ability to nullify the value, so we need ZipperValues to implement this correctly
        // self.a.is_val() || self.b.is_val()
        self.val().is_some()
    }
    fn child_count(&self) -> usize {
        self.child_mask().count_bits()
    }
    fn child_mask(&self) -> ByteMask {
        self.a.child_mask() | self.b.child_mask()
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> ZipperMoving
    for OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: ZipperPath + ZipperValues<AV>,
    BZipper: ZipperPath + ZipperValues<BV>,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    fn depth(&self) -> usize {
        debug_assert_eq!(self.a.depth(), self.b.depth());
        self.a.depth()
    }

    fn focus_byte(&self) -> Option<u8> {
        self.path().last().copied()
    }

    fn at_root(&self) -> bool {
        self.a.at_root() || self.b.at_root()
    }

    fn reset(&mut self) {
        self.a.reset();
        self.b.reset();
    }

    fn val_count(&self) -> usize {
        todo!()
    }

    fn descend_to<P: AsRef<[u8]>>(&mut self, path: P) {
        let path = path.as_ref();
        self.a.descend_to(path);
        self.b.descend_to(path);
    }

    fn descend_to_existing<P: AsRef<[u8]>>(&mut self, path: P) -> usize {
        let path = path.as_ref();
        let depth_a = self.a.descend_to_existing(path);
        let depth_b = self.b.descend_to_existing(path);
        if depth_a > depth_b {
            self.b.descend_to(&path[depth_b..depth_a]);
            depth_a
        } else if depth_b > depth_a {
            self.a.descend_to(&path[depth_a..depth_b]);
            depth_b
        } else {
            depth_a
        }
    }

    fn descend_to_val<K: AsRef<[u8]>>(&mut self, path: K) -> usize {
        let path = path.as_ref();
        let mut descended = 0;
        while descended < path.len() {
            let remaining = &path[descended..];
            let depth_a = self.a.descend_to_val(remaining);
            let depth_b = self.b.descend_to_val(remaining);
            // A value at the starting focus is not a newly encountered value.
            let advanced = if depth_a < depth_b {
                if depth_a > 0 && self.a.is_val() {
                    self.b.ascend(depth_b - depth_a);
                    depth_a
                } else {
                    self.a.descend_to(&remaining[depth_a..depth_b]);
                    depth_b
                }
            } else if depth_b < depth_a {
                if depth_b > 0 && self.b.is_val() {
                    self.a.ascend(depth_a - depth_b);
                    depth_b
                } else {
                    self.b.descend_to(&remaining[depth_b..depth_a]);
                    depth_a
                }
            } else {
                depth_a
            };
            descended += advanced;
            if advanced == 0 || self.is_val() {
                break;
            }
        }
        descended
    }

    fn descend_to_byte(&mut self, k: u8) {
        self.a.descend_to(&[k]);
        self.b.descend_to(&[k]);
    }

    fn descend_first_byte(&mut self) -> Option<u8> {
        self.descend_indexed_byte(0)
    }

    fn descend_indexed_byte(&mut self, idx: usize) -> Option<u8> {
        let child_mask = self.child_mask();
        let Some(byte) = child_mask.indexed_bit::<true>(idx) else {
            return None;
        };
        self.descend_to_byte(byte);
        Some(byte)
    }

    fn descend_until_observed<Obs: PathObserver>(&mut self, obs: &mut Obs) -> bool {
        let start_depth = self.a.path().len();
        let desc_a = self.a.descend_until();
        let desc_b = self.b.descend_until();
        let path_a = &self.a.path()[start_depth..];
        let path_b = &self.b.path()[start_depth..];
        if !desc_a && !desc_b {
            return false;
        }
        if !desc_a && desc_b {
            if self.a.child_count() == 0 {
                self.a.descend_to(path_b);
                obs.descend_to(&self.a.path()[start_depth..]);
                return true;
            } else {
                self.b.ascend(self.b.path().len() - start_depth);
                return false;
            }
        }
        if desc_a && !desc_b {
            if self.b.child_count() == 0 {
                self.b.descend_to(path_a);
                obs.descend_to(&self.a.path()[start_depth..]);
                return true;
            } else {
                self.a.ascend(self.a.path().len() - start_depth);
                return false;
            }
        }
        let overlap = find_prefix_overlap(path_a, path_b);
        if path_a.len() > overlap {
            self.a.ascend(path_a.len() - overlap);
        }
        if path_b.len() > overlap {
            self.b.ascend(path_b.len() - overlap);
        }
        obs.descend_to(&self.a.path()[start_depth..]);
        overlap > 0
    }

    fn ascend(&mut self, steps: usize) -> usize {
        let a = self.a.ascend(steps);
        let b = self.b.ascend(steps);
        debug_assert_eq!(a, b);
        a
    }

    fn ascend_byte(&mut self) -> bool {
        self.ascend(1) == 1
    }

    fn ascend_until(&mut self) -> usize {
        let start_depth = self.depth();
        // A branch can be formed by one child from each source even when neither
        // source branches there. Stop according to the virtual trie, not either source.
        while self.ascend_byte() {
            if self.child_count() > 1 || self.is_val() {
                break;
            }
        }
        start_depth - self.depth()
    }

    fn ascend_until_branch(&mut self) -> usize {
        let start_depth = self.depth();
        while self.ascend_byte() {
            if self.child_count() > 1 {
                break;
            }
        }
        start_depth - self.depth()
    }

    fn to_next_sibling_byte(&mut self) -> Option<u8> {
        self.to_sibling(true)
    }

    fn to_prev_sibling_byte(&mut self) -> Option<u8> {
        self.to_sibling(false)
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> ZipperPath
    for OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: ZipperPath + ZipperValues<AV>,
    BZipper: ZipperPath + ZipperValues<BV>,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    fn path(&self) -> &[u8] {
        self.a.path()
    }
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> ZipperIteration
    for OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    AZipper: ZipperPath + ZipperValues<AV>,
    BZipper: ZipperPath + ZipperValues<BV>,
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
}

impl<AV, BV, OutV, AZipper, BZipper, Mapping> core::fmt::Debug
    for OverlayZipper<AV, BV, OutV, AZipper, BZipper, Mapping>
where
    Mapping: for<'a> Fn(Option<&'a AV>, Option<&'a BV>) -> Option<&'a OutV>,
{
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("OverlayZipper").finish_non_exhaustive()
    }
}
