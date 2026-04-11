use pathmap::PathMap;
use pathmap::alloc::{Allocator, GlobalAlloc};
use pathmap::zipper::{ZipperInfallibleSubtries, ZipperSubtries};

pub trait ZipperSnapshotExt<V: Clone + Send + Sync, A: Allocator = GlobalAlloc>:
    ZipperSubtries<V, A>
{
    fn try_make_snapshot_map_ext(&self) -> Option<PathMap<V, A>>
    where
        V: Unpin,
    {
        self.try_make_map().map(|mut map| {
            if let Some(val) = self.val() {
                map.set_val_at([], val.clone());
            }
            map
        })
    }
}

impl<T, V, A> ZipperSnapshotExt<V, A> for T
where
    T: ZipperSubtries<V, A>,
    V: Clone + Send + Sync,
    A: Allocator,
{
}

pub trait ZipperInfallibleSnapshotExt<V: Clone + Send + Sync, A: Allocator = GlobalAlloc>:
    ZipperInfallibleSubtries<V, A>
{
    fn make_snapshot_map_ext(&self) -> PathMap<V, A>
    where
        V: Unpin,
    {
        let mut map = self.make_map();
        if let Some(val) = self.val() {
            map.set_val_at([], val.clone());
        }
        map
    }
}

impl<T, V, A> ZipperInfallibleSnapshotExt<V, A> for T
where
    T: ZipperInfallibleSubtries<V, A>,
    V: Clone + Send + Sync,
    A: Allocator,
{
}
