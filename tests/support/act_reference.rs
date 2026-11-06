// Differential format oracle against the separately pinned PathMap crate.
use pathmap::{PathMap, arena_compact::ArenaCompactTree};
use std::{env, fs};
fn main() {
    let args: Vec<String> = env::args().collect();
    if args[1] == "write" {
        let text = fs::read_to_string(&args[2]).unwrap();
        let entries = text.lines().map(|line| {
            let (hex, value) = line.rsplit_once(' ').unwrap();
            let key: Vec<u8> = (0..hex.len()).step_by(2)
                .map(|i| u8::from_str_radix(&hex[i..i+2], 16).unwrap()).collect();
            (key, value.parse::<u64>().unwrap())
        });
        let map = PathMap::from_iter(entries);
        let tree = ArenaCompactTree::from_zipper(map.read_zipper(), |v| *v);
        fs::write(&args[3], tree.get_data()).unwrap();
    } else {
        let tree = ArenaCompactTree::open_mmap(&args[2]).unwrap();
        for (key, value) in tree.iter() {
            for byte in key { print!("{byte:02x}"); }
            println!(" {value}");
        }
    }
}
