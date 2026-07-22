use hyperon::metta::text::{SExprParser, Tokenizer};
use hyperon_atom::Atom;
use std::env;
use std::fs;
use std::hint::black_box;
use std::io::Cursor;
use std::path::Path;
use std::time::Instant;

const FNV_OFFSET: u64 = 0xcbf29ce484222325;
const FNV_PRIME: u64 = 0x100000001b3;

#[derive(Clone, Copy)]
struct Fingerprint {
    value: u64,
    nodes: u64,
}

impl Fingerprint {
    fn new() -> Self {
        Self {
            value: FNV_OFFSET,
            nodes: 0,
        }
    }

    fn byte(&mut self, value: u8) {
        self.value ^= u64::from(value);
        self.value = self.value.wrapping_mul(FNV_PRIME);
    }

    fn bytes(&mut self, values: &[u8]) {
        self.u64(values.len() as u64);
        for value in values {
            self.byte(*value);
        }
    }

    fn u64(&mut self, value: u64) {
        for byte in value.to_be_bytes() {
            self.byte(byte);
        }
    }

    fn atom(&mut self, atom: &Atom) {
        self.nodes += 1;
        match atom {
            Atom::Symbol(symbol) => {
                self.byte(b'S');
                self.bytes(symbol.name().as_bytes());
            }
            Atom::Variable(variable) => {
                self.byte(b'V');
                self.bytes(variable.name().as_bytes());
            }
            Atom::Expression(expression) => {
                self.byte(b'E');
                self.u64(expression.children().len() as u64);
                for child in expression.children() {
                    self.atom(child);
                }
            }
            Atom::Grounded(grounded) => {
                self.byte(b'G');
                self.bytes(grounded.to_string().as_bytes());
            }
        }
    }
}

fn parse_fingerprint(input: &[u8]) -> Result<(u64, Fingerprint), String> {
    let tokenizer = Tokenizer::new();
    let mut parser = SExprParser::new(Cursor::new(input));
    let mut top_level = 0u64;
    let mut fingerprint = Fingerprint::new();
    while let Some(atom) = parser.parse(&tokenizer)? {
        top_level += 1;
        fingerprint.atom(&atom);
    }
    Ok((top_level, fingerprint))
}

fn parse_timed(input: &[u8]) -> Result<u64, String> {
    let tokenizer = Tokenizer::new();
    let mut parser = SExprParser::new(Cursor::new(input));
    let mut top_level = 0u64;
    while let Some(atom) = parser.parse(&tokenizer)? {
        top_level += 1;
        black_box(&atom);
    }
    Ok(top_level)
}

fn parse_count(raw: &str, name: &str, allow_zero: bool) -> usize {
    match raw.parse::<usize>() {
        Ok(value) if allow_zero || value > 0 => value,
        _ => {
            eprintln!(
                "{name} must be {} integer",
                if allow_zero {
                    "a nonnegative"
                } else {
                    "a positive"
                }
            );
            std::process::exit(2);
        }
    }
}

fn usage(program: &str) -> ! {
    eprintln!("usage: {program} INPUT WARMUP_ITERATIONS MEASURED_ITERATIONS");
    std::process::exit(2);
}

fn main() {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() != 4 {
        usage(
            arguments
                .first()
                .map(String::as_str)
                .unwrap_or("he-parser-parse-only-v1"),
        );
    }
    let input_path = Path::new(&arguments[1]);
    let warmup_iterations = parse_count(&arguments[2], "warmup iterations", true);
    let measured_iterations = parse_count(&arguments[3], "measured iterations", false);
    let input = match fs::read(input_path) {
        Ok(input) => input,
        Err(error) => {
            eprintln!("cannot read input: {error}");
            std::process::exit(1);
        }
    };
    let (expected_top_level, fingerprint) = match parse_fingerprint(&input) {
        Ok(result) => result,
        Err(error) => {
            eprintln!("parse failed: {error}");
            std::process::exit(1);
        }
    };
    for _ in 0..warmup_iterations {
        match parse_timed(&input) {
            Ok(observed) if observed == expected_top_level => {}
            Ok(_) => {
                eprintln!("warmup parse changed the top-level atom count");
                std::process::exit(1);
            }
            Err(error) => {
                eprintln!("warmup parse failed: {error}");
                std::process::exit(1);
            }
        }
    }
    let started = Instant::now();
    let mut total_top_level = 0u64;
    for _ in 0..measured_iterations {
        match parse_timed(&input) {
            Ok(observed) if observed == expected_top_level => {
                total_top_level += observed;
            }
            Ok(_) => {
                eprintln!("measured parse changed the top-level atom count");
                std::process::exit(1);
            }
            Err(error) => {
                eprintln!("measured parse failed: {error}");
                std::process::exit(1);
            }
        }
    }
    let elapsed = started.elapsed().as_nanos();
    black_box(total_top_level);
    println!("he-parser-parse-only-v1");
    println!("input-bytes\t{}", input.len());
    println!("top-level-atoms\t{expected_top_level}");
    println!("atom-nodes\t{}", fingerprint.nodes);
    println!("semantic-fnv64\t{:016x}", fingerprint.value);
    println!("warmup-iterations\t{warmup_iterations}");
    println!("measured-iterations\t{measured_iterations}");
    println!("total-nanoseconds\t{elapsed}");
    println!("end");
}
