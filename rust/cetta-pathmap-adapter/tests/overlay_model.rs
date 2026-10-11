use cetta_pathmap_adapter::OverlayZipper;
use pathmap::zipper::{Zipper, ZipperIteration, ZipperMoving, ZipperPath};
use pathmap::PathMap;
use std::collections::BTreeSet;

fn children(keys: &[Vec<u8>], path: &[u8]) -> BTreeSet<u8> {
    keys.iter()
        .filter(|key| key.len() > path.len() && key.starts_with(path))
        .map(|key| key[path.len()])
        .collect()
}

fn is_value(keys: &[Vec<u8>], path: &[u8]) -> bool {
    keys.iter().any(|key| key == path)
}

// The model uses only the input keys, independently of trie nodes and zippers.
fn descend_model(keys: &[Vec<u8>], start: &[u8], limit: usize) -> Vec<u8> {
    let mut path = start.to_vec();
    for _ in 0..limit {
        let next = children(keys, &path);
        if next.len() != 1 {
            break;
        }
        path.push(*next.first().unwrap());
        if is_value(keys, &path) {
            break;
        }
    }
    path
}

fn check_descent(
    base_keys: &[Vec<u8>],
    overlay_keys: &[Vec<u8>],
    start: &[u8],
    limit: Option<usize>,
) {
    let (mut base, mut overlay) = (PathMap::new(), PathMap::new());
    for key in base_keys {
        base.set_val_at(key, ());
    }
    for key in overlay_keys {
        overlay.set_val_at(key, ());
    }
    let all: Vec<_> = base_keys.iter().chain(overlay_keys).cloned().collect();
    let mut zipper = OverlayZipper::new(base.read_zipper(), overlay.read_zipper());
    zipper.descend_to(start);
    let mut observed = Vec::new();
    let moved = match limit {
        Some(bytes) => zipper.descend_until_max_bytes_observed(bytes, &mut observed),
        None => zipper.descend_until_observed(&mut observed),
    };
    // A descent cannot exceed the longest key. This bound is a property of the
    // finite model, not a search limit imposed on the implementation.
    let bound = all.iter().map(Vec::len).max().unwrap_or(0);
    let expected = descend_model(&all, start, limit.unwrap_or(bound));
    assert_eq!(zipper.path(), expected, "start={start:?}, limit={limit:?}");
    assert_eq!(moved, expected.len() > start.len());
    assert_eq!(observed, expected[start.len()..]);
    assert_eq!(zipper.is_val(), is_value(&all, &expected));
    assert_eq!(zipper.child_count(), children(&all, &expected).len());
    assert_eq!(
        zipper.child_mask().iter().collect::<BTreeSet<_>>(),
        children(&all, &expected)
    );
    assert_eq!(
        zipper.path_exists(),
        expected.is_empty() || all.iter().any(|key| key.starts_with(&expected))
    );
}

#[test]
fn one_sided_descent_crosses_every_chunk_boundary() {
    for len in [47, 48, 49, 95, 96, 97, 145, 257] {
        let key = vec![b'x'; len];
        for swapped in [false, true] {
            let (base, overlay) = if swapped {
                (vec![], vec![key.clone()])
            } else {
                (vec![key.clone()], vec![])
            };
            for limit in [
                None,
                Some(0),
                Some(1),
                Some(48),
                Some(60),
                Some(96),
                Some(300),
            ] {
                check_descent(&base, &overlay, &[], limit);
            }
        }
    }
}

#[test]
fn shared_descent_stops_at_values_on_chunk_boundaries() {
    for len in [47, 48, 49, 96, 144] {
        let value = vec![b'x'; len];
        let mut continuation = value.clone();
        continuation.push(b'x');
        for swapped in [false, true] {
            let (base, overlay) = if swapped {
                (
                    vec![value.clone()],
                    vec![value.clone(), continuation.clone()],
                )
            } else {
                (
                    vec![value.clone(), continuation.clone()],
                    vec![value.clone()],
                )
            };
            check_descent(&base, &overlay, &[], None);
            check_descent(&base, &overlay, &[], Some(len + 1));
            // Starting on the value permits movement to the next value.
            check_descent(&base, &overlay, &value, None);
        }
    }
}

#[test]
fn branch_leaf_absent_and_empty_positions_do_not_invent_movement() {
    let base = vec![b"abc".to_vec(), b"abd".to_vec()];
    let overlay = vec![b"axy".to_vec()];
    for start in [b"".as_slice(), b"a", b"ab", b"abc", b"missing"] {
        check_descent(&base, &overlay, start, None);
        check_descent(&base, &overlay, start, Some(0));
    }
    check_descent(&[], &[], &[], None);
    check_descent(&[], &[], b"missing", None);
}

struct Rng(u64);
impl Rng {
    fn next(&mut self) -> usize {
        self.0 = self.0.wrapping_add(0x9e3779b97f4a7c15);
        let mut value = self.0;
        value = (value ^ (value >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)).wrapping_mul(0x94d049bb133111eb);
        (value ^ (value >> 31)) as usize
    }

    fn keys(&mut self) -> Vec<Vec<u8>> {
        const BYTES: [u8; 6] = [0, 1, 2, 7, 128, 255];
        let count = self.next() % 18;
        (0..count)
            .map(|_| {
                let len = self.next() % 6;
                let mut key: Vec<_> = (0..len).map(|_| BYTES[self.next() % BYTES.len()]).collect();
                if self.next() % 3 == 0 {
                    let extra = self.next() % 150;
                    let byte = BYTES[self.next() % BYTES.len()];
                    key.extend(std::iter::repeat_n(byte, extra));
                }
                key
            })
            .collect()
    }
}

#[test]
fn randomized_overlay_descent_matches_the_key_set_model() {
    let mut rng = Rng(0x5eed_2026_1009);
    for _ in 0..4096 {
        let base = rng.keys();
        let mut overlay = rng.keys();
        if let Some(key) = base.first() {
            overlay.push(key.clone());
        }
        let all: Vec<_> = base.iter().chain(&overlay).collect();
        let start = if all.is_empty() {
            vec![]
        } else {
            let key = all[rng.next() % all.len()];
            key[..rng.next() % (key.len() + 1)].to_vec()
        };
        for limit in [None, Some(0), Some(48), Some(60), Some(rng.next() % 200)] {
            check_descent(&base, &overlay, &start, limit);
        }
    }
}

#[derive(Clone, Debug)]
enum Movement {
    Reset,
    Ascend(usize),
    DescendExistingByte(u8),
    DescendIndex(usize),
    DescendFirst,
    DescendLast,
    DescendUntil(Option<usize>),
    AscendUntil(bool),
    Sibling(bool),
    NextStep,
    DescendTo(Vec<u8>, bool),
}

fn sibling_model(keys: &[Vec<u8>], path: &mut Vec<u8>, next: bool) -> bool {
    let Some(last) = path.pop() else { return false };
    let replacement = children(keys, path).into_iter().filter(|byte| {
        if next { *byte > last } else { *byte < last }
    });
    let replacement = if next { replacement.min() } else { replacement.max() };
    path.push(replacement.unwrap_or(last));
    replacement.is_some()
}

fn movement_model(keys: &[Vec<u8>], path: &mut Vec<u8>, op: &Movement) -> usize {
    match op {
        Movement::Reset => { path.clear(); 1 }
        Movement::Ascend(n) => {
            let moved = (*n).min(path.len());
            path.truncate(path.len() - moved);
            moved
        }
        Movement::DescendExistingByte(byte) => {
            if children(keys, path).contains(byte) { path.push(*byte); 1 } else { 0 }
        }
        Movement::DescendIndex(index) => {
            if let Some(byte) = children(keys, path).into_iter().nth(*index) {
                path.push(byte); 1
            } else { 0 }
        }
        Movement::DescendFirst | Movement::DescendLast => {
            let children = children(keys, path);
            let byte = if matches!(op, Movement::DescendFirst) {
                children.first()
            } else { children.last() };
            if let Some(byte) = byte { path.push(*byte); 1 } else { 0 }
        }
        Movement::DescendUntil(limit) => {
            let expected = descend_model(keys, path, limit.unwrap_or_else(|| {
                keys.iter().map(Vec::len).max().unwrap_or(0)
            }));
            let moved = usize::from(expected.len() != path.len());
            *path = expected;
            moved
        }
        Movement::AscendUntil(branch_only) => {
            let before = path.len();
            while path.pop().is_some() {
                if path.is_empty() || children(keys, path).len() > 1
                    || (!branch_only && is_value(keys, path)) { break }
            }
            before - path.len()
        }
        Movement::Sibling(next) => usize::from(sibling_model(keys, path, *next)),
        Movement::NextStep => {
            if let Some(byte) = children(keys, path).first() {
                path.push(*byte);
                return 1;
            }
            loop {
                if sibling_model(keys, path, true) { return 1 }
                if path.pop().is_none() { return 0 }
            }
        }
        Movement::DescendTo(suffix, stop_at_value) => {
            let mut moved = 0;
            for byte in suffix {
                if !children(keys, path).contains(byte) { break }
                path.push(*byte);
                moved += 1;
                if *stop_at_value && is_value(keys, path) { break }
            }
            moved
        }
    }
}

#[test]
fn randomized_cursor_movements_match_the_key_set_model() {
    let mut rng = Rng(0xc075_2026_1009);
    for _ in 0..1024 {
        let base = rng.keys();
        let overlay = rng.keys();
        let all: Vec<_> = base.iter().chain(&overlay).cloned().collect();
        let (mut a, mut b) = (PathMap::new(), PathMap::new());
        for key in &base { a.set_val_at(key, ()); }
        for key in &overlay { b.set_val_at(key, ()); }
        let mut zipper = OverlayZipper::new(a.read_zipper(), b.read_zipper());
        let mut model_path = Vec::new();
        for _ in 0..96 {
            let op = match rng.next() % 15 {
                0 => Movement::Reset,
                1 => Movement::Ascend(rng.next() % 200),
                2 => Movement::DescendExistingByte([0, 1, 2, 7, 128, 255][rng.next() % 6]),
                3 => Movement::DescendIndex(rng.next() % 8),
                4 => Movement::DescendFirst,
                5 => Movement::DescendLast,
                6 => Movement::DescendUntil(None),
                7 => Movement::DescendUntil(Some(rng.next() % 100)),
                8 => Movement::AscendUntil(false),
                9 => Movement::AscendUntil(true),
                10 => Movement::Sibling(false),
                11 => Movement::Sibling(true),
                12 => Movement::NextStep,
                _ => {
                    let suffix = if all.is_empty() { vec![7] } else {
                        let key = &all[rng.next() % all.len()];
                        let overlap = model_path.len().min(key.len());
                        key[overlap..].to_vec()
                    };
                    Movement::DescendTo(suffix, rng.next() % 2 == 0)
                }
            };
            let before = model_path.clone();
            let expected = movement_model(&all, &mut model_path, &op);
            let moved = match &op {
                Movement::Reset => { zipper.reset(); 1 }
                Movement::Ascend(n) => zipper.ascend(*n),
                Movement::DescendExistingByte(byte) => usize::from(zipper.descend_to_existing_byte(*byte)),
                Movement::DescendIndex(index) => usize::from(zipper.descend_indexed_byte(*index).is_some()),
                Movement::DescendFirst => usize::from(zipper.descend_first_byte().is_some()),
                Movement::DescendLast => usize::from(zipper.descend_last_byte().is_some()),
                Movement::DescendUntil(Some(limit)) => usize::from(zipper.descend_until_max_bytes(*limit)),
                Movement::DescendUntil(None) => usize::from(zipper.descend_until()),
                Movement::AscendUntil(true) => zipper.ascend_until_branch(),
                Movement::AscendUntil(false) => zipper.ascend_until(),
                Movement::Sibling(true) => usize::from(zipper.to_next_sibling_byte().is_some()),
                Movement::Sibling(false) => usize::from(zipper.to_prev_sibling_byte().is_some()),
                Movement::NextStep => usize::from(zipper.to_next_step()),
                Movement::DescendTo(suffix, true) => zipper.descend_to_val(suffix),
                Movement::DescendTo(suffix, false) => zipper.descend_to_existing(suffix),
            };
            assert_eq!(zipper.path(), model_path, "before={before:?}, op={op:?}");
            assert_eq!(moved, expected, "before={before:?}, op={op:?}");
            assert_eq!(zipper.depth(), model_path.len());
            assert_eq!(zipper.focus_byte(), model_path.last().copied());
            assert_eq!(zipper.at_root(), model_path.is_empty());
            assert_eq!(zipper.is_val(), is_value(&all, &model_path));
            assert_eq!(zipper.child_mask().iter().collect::<BTreeSet<_>>(), children(&all, &model_path));
        }
    }
}

#[test]
fn iteration_reports_the_union_once_and_preserves_observed_paths() {
    let mut rng = Rng(0x17e2_2026_1009);
    for _ in 0..512 {
        let base = rng.keys();
        let overlay = rng.keys();
        let expected: BTreeSet<_> = base.iter().chain(&overlay).cloned().collect();
        let (mut a, mut b) = (PathMap::new(), PathMap::new());
        for key in &base { a.set_val_at(key, ()); }
        for key in &overlay { b.set_val_at(key, ()); }
        let mut zipper = OverlayZipper::new(a.read_zipper(), b.read_zipper());
        let mut observed = Vec::new();
        let mut actual = Vec::new();
        if zipper.is_val() { actual.push(zipper.path().to_vec()); }
        while zipper.to_next_val_observed(&mut observed) {
            assert_eq!(zipper.path(), observed);
            actual.push(zipper.path().to_vec());
        }
        assert_eq!(actual, expected.into_iter().collect::<Vec<_>>());
        assert!(zipper.at_root());
        assert!(observed.is_empty());
    }
}

#[test]
fn source_roots_do_not_leak_into_the_overlay_focus_byte() {
    let (mut a, mut b) = (PathMap::new(), PathMap::new());
    a.set_val_at(b"a/one", 1);
    b.set_val_at(b"b/two", 2);
    let mut zipper = OverlayZipper::new(a.read_zipper_at_path(b"a/"), b.read_zipper_at_path(b"b/"));
    assert_eq!(zipper.path(), b"");
    assert_eq!(zipper.focus_byte(), None);
    assert_eq!(zipper.to_next_sibling_byte(), None);
    assert_eq!(zipper.to_prev_sibling_byte(), None);
    assert!(zipper.to_next_val());
    assert_eq!(zipper.path(), b"one");
    assert!(zipper.to_next_val());
    assert_eq!(zipper.path(), b"two");
    assert!(!zipper.to_next_val());
}

#[test]
fn ascent_stops_at_a_branch_formed_only_by_the_overlay() {
    let (mut a, mut b) = (PathMap::new(), PathMap::new());
    a.set_val_at(b"prefix/left", ());
    b.set_val_at(b"prefix/right", ());
    let mut zipper = OverlayZipper::new(a.read_zipper(), b.read_zipper());
    for branch_only in [false, true] {
        zipper.reset();
        zipper.descend_to(b"prefix/left");
        let moved = if branch_only { zipper.ascend_until_branch() } else { zipper.ascend_until() };
        assert_eq!(moved, 4);
        assert_eq!(zipper.path(), b"prefix/");
        assert_eq!(zipper.child_count(), 2);
        assert_eq!(zipper.ascend_until_branch(), 7);
        assert!(zipper.at_root());
        assert_eq!(zipper.ascend_until(), 0);
    }
}
