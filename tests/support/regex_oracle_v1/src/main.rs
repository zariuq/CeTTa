use regex::Regex;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 4 {
        eprintln!("usage: cetta-regex-oracle-v1 search|full|all PATTERN INPUT");
        std::process::exit(2);
    }
    let pattern = match args[1].as_str() {
        "search" | "all" => args[2].clone(),
        "full" => format!(r"\A(?:{})\z", args[2]),
        _ => std::process::exit(2),
    };
    let re = match Regex::new(&pattern) {
        Ok(re) => re,
        Err(_) => { println!("{{\"outcome\":\"invalid-pattern\",\"groups\":[]}}"); return; }
    };
    if args[1] == "all" {
        print!("{{\"matches\":[");
        for (i, captures) in re.captures_iter(&args[3]).enumerate() {
            if i != 0 { print!(","); }
            print!("[");
            for (j, capture) in captures.iter().enumerate() {
                if j != 0 { print!(","); }
                match capture {
                    None => print!("null"),
                    Some(m) => print!("[{},{}]", m.start(), m.end()),
                }
            }
            print!("]");
        }
        println!("]}}");
        return;
    }
    match re.captures(&args[3]) {
        None => println!("{{\"outcome\":\"no-match\",\"groups\":[]}}"),
        Some(captures) => {
            print!("{{\"outcome\":\"match\",\"groups\":[");
            for (i, capture) in captures.iter().enumerate() {
                if i != 0 { print!(","); }
                match capture {
                    None => print!("null"),
                    Some(m) => print!("[{},{}]", m.start(), m.end()),
                }
            }
            println!("]}}");
        }
    }
}
