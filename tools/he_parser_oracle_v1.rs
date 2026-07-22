use hyperon::metta::text::{SExprParser, SyntaxNode, SyntaxNodeType, Tokenizer};
use hyperon_atom::Atom;
use std::env;
use std::fs;
use std::io::{self, Cursor, Write};
use std::path::Path;

fn put_u64(out: &mut Vec<u8>, value: u64) {
    out.extend_from_slice(&value.to_be_bytes());
}

fn put_bytes(out: &mut Vec<u8>, value: &[u8]) {
    put_u64(out, value.len() as u64);
    out.extend_from_slice(value);
}

fn put_optional_bytes(out: &mut Vec<u8>, value: Option<&str>) {
    match value {
        Some(value) => {
            out.push(1);
            put_bytes(out, value.as_bytes());
        }
        None => out.push(0),
    }
}

fn encode_atom(atom: &Atom, out: &mut Vec<u8>) {
    match atom {
        Atom::Symbol(symbol) => {
            out.push(b'S');
            put_bytes(out, symbol.name().as_bytes());
        }
        Atom::Variable(variable) => {
            out.push(b'V');
            put_bytes(out, variable.name().as_bytes());
        }
        Atom::Expression(expression) => {
            out.push(b'E');
            put_u64(out, expression.children().len() as u64);
            for child in expression.children() {
                encode_atom(child, out);
            }
        }
        Atom::Grounded(grounded) => {
            out.push(b'G');
            put_bytes(out, grounded.to_string().as_bytes());
        }
    }
}

fn syntax_kind(kind: SyntaxNodeType) -> u8 {
    match kind {
        SyntaxNodeType::Comment => 0,
        SyntaxNodeType::VariableToken => 1,
        SyntaxNodeType::StringToken => 2,
        SyntaxNodeType::WordToken => 3,
        SyntaxNodeType::OpenParen => 4,
        SyntaxNodeType::CloseParen => 5,
        SyntaxNodeType::Whitespace => 6,
        SyntaxNodeType::LeftoverText => 7,
        SyntaxNodeType::ExpressionGroup => 8,
        SyntaxNodeType::ErrorGroup => 9,
    }
}

fn encode_syntax(node: &SyntaxNode, out: &mut Vec<u8>) {
    out.push(b'N');
    out.push(syntax_kind(node.node_type));
    put_u64(out, node.src_range.start as u64);
    put_u64(out, node.src_range.end as u64);
    out.push(u8::from(node.is_complete));
    put_optional_bytes(out, node.parsed_text.as_deref());
    put_optional_bytes(out, node.message.as_deref());
    put_u64(out, node.sub_nodes.len() as u64);
    for child in &node.sub_nodes {
        encode_syntax(child, out);
    }
}

fn hex(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        out.push(DIGITS[(byte >> 4) as usize] as char);
        out.push(DIGITS[(byte & 0x0f) as usize] as char);
    }
    out
}

fn emit_error(stdout: &mut dyn Write, error: &str) -> io::Result<()> {
    writeln!(stdout, "error\t{}", hex(error.as_bytes()))
}

fn emit_atoms(input: Vec<u8>, stdout: &mut dyn Write) -> io::Result<()> {
    let tokenizer = Tokenizer::new();
    let mut parser = SExprParser::new(Cursor::new(input));
    loop {
        match parser.parse(&tokenizer) {
            Ok(Some(atom)) => {
                let mut encoded = Vec::new();
                encode_atom(&atom, &mut encoded);
                writeln!(stdout, "atom\t{}", hex(&encoded))?;
            }
            Ok(None) => break,
            Err(error) => {
                emit_error(stdout, &error)?;
                break;
            }
        }
    }
    Ok(())
}

fn emit_syntax(input: Vec<u8>, stdout: &mut dyn Write) -> io::Result<()> {
    let mut parser = SExprParser::new(Cursor::new(input));
    loop {
        match parser.parse_to_syntax_tree() {
            Ok(Some(node)) => {
                let mut encoded = Vec::new();
                encode_syntax(&node, &mut encoded);
                writeln!(stdout, "syntax\t{}", hex(&encoded))?;
            }
            Ok(None) => break,
            Err(error) => {
                emit_error(stdout, &error)?;
                break;
            }
        }
    }
    Ok(())
}

fn usage(program: &str) -> ! {
    eprintln!("usage: {program} atoms|syntax INPUT");
    std::process::exit(2);
}

fn main() -> io::Result<()> {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() != 3 {
        usage(
            arguments
                .first()
                .map(String::as_str)
                .unwrap_or("he-parser-oracle-v1"),
        );
    }
    let mode = &arguments[1];
    let path = Path::new(&arguments[2]);
    let input = fs::read(path)?;
    let mut stdout = io::BufWriter::new(io::stdout().lock());
    writeln!(stdout, "he-parser-oracle-v1")?;
    writeln!(stdout, "mode\t{mode}")?;
    match mode.as_str() {
        "atoms" => emit_atoms(input, &mut stdout)?,
        "syntax" => emit_syntax(input, &mut stdout)?,
        _ => usage(&arguments[0]),
    }
    writeln!(stdout, "end")?;
    Ok(())
}
