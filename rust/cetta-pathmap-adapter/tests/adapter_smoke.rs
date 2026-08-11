use cetta_pathmap_adapter::{OverlayZipper, ZipperSnapshotExt};
use pathmap::PathMap;
use pathmap::zipper::{Zipper, ZipperMoving, ZipperValues};

#[test]
fn snapshot_extension_roots_focus_value() {
    let mut map = PathMap::new();
    map.set_val_at(b"a", ());
    map.set_val_at(b"ab", ());
    map.create_path(b"ac");

    let snapshot = map
        .read_zipper_at_path(b"a")
        .try_make_snapshot_map_ext()
        .expect("focus should expose a concrete subtrie");

    assert!(snapshot.get_val_at([]).is_some());
    assert!(snapshot.get_val_at(b"b").is_some());
    assert!(snapshot.path_exists_at(b"c"));
}

#[test]
fn overlay_zipper_child_count_follows_virtual_overlay_at_focus() {
    let mut base = PathMap::new();
    base.set_val_at([1u8, 7u8], ());
    base.set_val_at([2u8, 8u8], ());

    let mut overlay = PathMap::new();
    overlay.set_val_at([1u8, 9u8], ());
    overlay.set_val_at([3u8, 5u8], ());

    let mut oz = OverlayZipper::new(base.read_zipper(), overlay.read_zipper());
    assert_eq!(oz.child_count(), 3);

    oz.descend_to([1u8]);
    assert_eq!(oz.path(), &[1u8]);
    assert_eq!(oz.child_count(), 2);
}

#[test]
fn overlay_zipper_value_lookup_follows_virtual_overlay_below_focus() {
    let mut base = PathMap::new();
    base.set_val_at([1u8, 7u8], 17u8);
    base.set_val_at([2u8, 8u8], 28u8);

    let mut overlay = PathMap::new();
    overlay.set_val_at([1u8, 7u8], 97u8);
    overlay.set_val_at([3u8, 9u8], 39u8);

    let oz = OverlayZipper::new(base.read_zipper(), overlay.read_zipper());
    assert_eq!(oz.val_at([1u8, 7u8]), Some(&17u8));
    assert_eq!(oz.val_at([2u8, 8u8]), Some(&28u8));
    assert_eq!(oz.val_at([3u8, 9u8]), Some(&39u8));
    assert_eq!(oz.val_at([4u8, 0u8]), None);
}
