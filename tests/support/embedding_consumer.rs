//! A Python-independent consumer of the public embedding ABI.
use std::ffi::{c_char, c_int, c_void};
use std::ptr;

#[repr(C)]
struct QueryStep {
    size: usize,
    state: c_int,
    physical_steps: u64,
    source_steps: u64,
    source_remaining: u64,
    source_limited: u32,
    conditional: u32,
    bindings: *mut c_void,
    delayed: *mut c_void,
}

#[repr(C)]
struct BufferDescriptor {
    size: usize,
    dtype: *const c_char,
    byte_order: c_int,
    allocation: *const c_void,
    allocation_bytes: usize,
    offset: usize,
    rank: usize,
    shape: *const usize,
    strides: *const isize,
}

#[repr(C)]
struct SparseDescriptor {
    size: usize,
    layout: c_int,
    rank: usize,
    shape: *const usize,
    values: *const c_void,
    coordinates: *const c_void,
    indptr: *const c_void,
    indices: *const c_void,
}
#[repr(C)]
struct SparseComponents {
    size: usize,
    values: *mut c_void,
    coordinates: *mut c_void,
    indptr: *mut c_void,
    indices: *mut c_void,
}

unsafe extern "C" {
    fn cetta_embed_abi_version() -> u32;
    fn cetta_embed_engine_new(config: *const c_void, engine: *mut *mut c_void) -> c_int;
    fn cetta_embed_engine_close(engine: *mut c_void);
    fn cetta_embed_engine_release(engine: *mut c_void);
    fn cetta_embed_run(engine: *mut c_void, text: *const c_char, length: usize,
                       options: *const c_void, result: *mut *mut c_void) -> c_int;
    fn cetta_embed_load(engine: *mut c_void, path: *const c_char,
                        options: *const c_void, result: *mut *mut c_void) -> c_int;
    fn cetta_embed_import(engine: *mut c_void, target: *const c_void, spec: *const c_void,
                          options: *const c_void, result: *mut *mut c_void) -> c_int;
    fn cetta_embed_result_count(result: *const c_void) -> usize;
    fn cetta_embed_result_get(result: *const c_void, index: usize, atom: *mut *mut c_void) -> c_int;
    fn cetta_embed_result_release(result: *mut c_void);
    fn cetta_embed_atom_get_integer(atom: *const c_void, integer: *mut i64) -> c_int;
    fn cetta_embed_atom_get_float(atom: *const c_void, value: *mut f64) -> c_int;
    fn cetta_embed_call(engine: *mut c_void, head: *const c_void, arguments: *const *mut c_void,
                        arity: usize, options: *const c_void, result: *mut *mut c_void) -> c_int;
    fn cetta_embed_atom_equal(left: *const c_void, right: *const c_void, equal: *mut c_int) -> c_int;
    fn cetta_embed_atom_release(atom: *mut c_void);
    fn cetta_embed_parse(engine: *mut c_void, text: *const c_char, length: usize,
                         atom: *mut *mut c_void) -> c_int;
    fn cetta_embed_query_open(engine: *mut c_void, source: *const c_void,
                              options: *const c_void, query: *mut *mut c_void) -> c_int;
    fn cetta_embed_query_step(query: *mut c_void, allowance: u64, info: *mut QueryStep,
                              result: *mut *mut c_void) -> c_int;
    fn cetta_embed_query_release(query: *mut c_void);
    fn cetta_embed_space_new(engine: *mut c_void, space: *mut *mut c_void) -> c_int;
    fn cetta_embed_space_add_atom(space: *mut c_void, fact: *const c_void) -> c_int;
    fn cetta_embed_space_count(space: *mut c_void, count: *mut u64) -> c_int;
    fn cetta_embed_space_match(space: *mut c_void, pattern: *const c_void,
        template_value: *const c_void, options: *const c_void, query: *mut *mut c_void) -> c_int;
    fn cetta_embed_space_release(space: *mut c_void);
    fn cetta_embed_buffer_copy(spec: *const BufferDescriptor, buffer: *mut *mut c_void) -> c_int;
    fn cetta_embed_buffer_describe(buffer: *const c_void, spec: *mut BufferDescriptor) -> c_int;
    fn cetta_embed_buffer_release(buffer: *mut c_void);
    fn cetta_embed_atom_buffer(engine: *mut c_void, buffer: *const c_void, atom: *mut *mut c_void) -> c_int;
    fn cetta_embed_buffer_from_atom(atom: *const c_void, buffer: *mut *mut c_void) -> c_int;
    fn cetta_embed_tensor_from_sparse(engine: *mut c_void, spec: *const SparseDescriptor,
                                     duplicates: c_int, copy: c_int, atom: *mut *mut c_void) -> c_int;
    fn cetta_embed_tensor_components(atom: *const c_void, parts: *mut SparseComponents) -> c_int;
}

fn main() {
    unsafe {
        assert_eq!(cetta_embed_abi_version(), 1);
        let mut engine = ptr::null_mut();
        assert_eq!(cetta_embed_engine_new(ptr::null(), &mut engine), 0);
        let source = b"(= (double $x) (* $x 2)) !(double 21)";
        let mut result = ptr::null_mut();
        assert_eq!(cetta_embed_run(engine, source.as_ptr().cast(), source.len(), ptr::null(), &mut result), 0);
        assert_eq!(cetta_embed_result_count(result), 1);
        let mut atom = ptr::null_mut();
        assert_eq!(cetta_embed_result_get(result, 0, &mut atom), 0);
        cetta_embed_result_release(result);
        let directory = std::env::temp_dir().join(format!("cetta-abi-modules-{}-{}",
            std::process::id(), std::time::SystemTime::now().duration_since(
                std::time::UNIX_EPOCH).unwrap().as_nanos()));
        std::fs::create_dir(&directory).unwrap();
        std::fs::write(directory.join("helper.metta"), "(= (triple $x) (* $x 3))").unwrap();
        let path = directory.join("main.metta");
        std::fs::write(&path, "!(import! &self \"./helper.metta\") !(triple 14)").unwrap();
        let filename = std::ffi::CString::new(path.as_os_str().as_encoded_bytes()).unwrap();
        assert_eq!(cetta_embed_load(engine, filename.as_ptr(), ptr::null(), &mut result), 0);
        let mut loaded = ptr::null_mut();
        assert_eq!(cetta_embed_result_get(result, cetta_embed_result_count(result) - 1, &mut loaded), 0);
        let mut value = 0;
        assert_eq!(cetta_embed_atom_get_integer(loaded, &mut value), 0);
        assert_eq!(value, 42);
        cetta_embed_atom_release(loaded);
        cetta_embed_result_release(result);
        let spec_source = b"(library tensor)";
        let mut spec = ptr::null_mut();
        assert_eq!(cetta_embed_parse(engine, spec_source.as_ptr().cast(), spec_source.len(), &mut spec), 0);
        assert_eq!(cetta_embed_import(engine, ptr::null(), spec, ptr::null(), &mut result), 0);
        cetta_embed_atom_release(spec);
        cetta_embed_result_release(result);
        std::fs::remove_dir_all(directory).unwrap();
        let query_source = b"(superpose (1 2))";
        let mut expression = ptr::null_mut();
        assert_eq!(cetta_embed_parse(engine, query_source.as_ptr().cast(), query_source.len(),
                                    &mut expression), 0);
        let mut query = ptr::null_mut();
        assert_eq!(cetta_embed_query_open(engine, expression, ptr::null(), &mut query), 0);
        cetta_embed_atom_release(expression);
        let mut count = 0;
        for _ in 0..1000 {
            let mut info = QueryStep {size: std::mem::size_of::<QueryStep>(), state: 0,
                physical_steps: 0, source_steps: 0, source_remaining: 0,
                source_limited: 0, conditional: 0, bindings: ptr::null_mut(), delayed: ptr::null_mut()};
            assert_eq!(cetta_embed_query_step(query, 1, &mut info, &mut result), 0);
            assert!(info.physical_steps <= 1);
            count += cetta_embed_result_count(result);
            cetta_embed_result_release(result);
            cetta_embed_atom_release(info.bindings);
            cetta_embed_atom_release(info.delayed);
            if info.state == 2 { break; }
        }
        assert_eq!(count, 2);
        cetta_embed_query_release(query);
        let mut space = ptr::null_mut();
        assert_eq!(cetta_embed_space_new(engine, &mut space), 0);
        assert_eq!(cetta_embed_space_add_atom(space, atom), 0);
        assert_eq!(cetta_embed_space_add_atom(space, atom), 0);
        let mut occurrences = 0;
        assert_eq!(cetta_embed_space_count(space, &mut occurrences), 0);
        assert_eq!(occurrences, 2);
        let pattern = b"$value";
        assert_eq!(cetta_embed_parse(engine, pattern.as_ptr().cast(), pattern.len(), &mut expression), 0);
        assert_eq!(cetta_embed_space_match(space, expression, ptr::null(), ptr::null(), &mut query), 0);
        cetta_embed_atom_release(expression);
        cetta_embed_space_release(space);
        let mut info = QueryStep {size: std::mem::size_of::<QueryStep>(), state: 0,
            physical_steps: 0, source_steps: 0, source_remaining: 0,
            source_limited: 0, conditional: 0, bindings: ptr::null_mut(), delayed: ptr::null_mut()};
        assert_eq!(cetta_embed_query_step(query, u64::MAX, &mut info, &mut result), 0);
        assert_eq!(info.state, 0);
        assert_eq!(cetta_embed_result_count(result), 1);
        assert!(!info.bindings.is_null());
        cetta_embed_result_release(result);
        cetta_embed_atom_release(info.bindings);
        cetta_embed_atom_release(info.delayed);
        let values = [1.0_f64, 2.0, 3.0, 4.0];
        let shape = [2_usize, 2];
        let mut descriptor = BufferDescriptor {size: std::mem::size_of::<BufferDescriptor>(),
            dtype: c"f64".as_ptr(), byte_order: 0, allocation: values.as_ptr().cast(),
            allocation_bytes: std::mem::size_of_val(&values), offset: 0, rank: 2,
            shape: shape.as_ptr(), strides: ptr::null()};
        let mut buffer = ptr::null_mut();
        assert_eq!(cetta_embed_buffer_copy(&descriptor, &mut buffer), 0);
        let mut tensor = ptr::null_mut();
        assert_eq!(cetta_embed_atom_buffer(engine, buffer, &mut tensor), 0);
        cetta_embed_buffer_release(buffer);
        assert_eq!(cetta_embed_buffer_from_atom(tensor, &mut buffer), 0);
        cetta_embed_atom_release(tensor);
        let raw: Vec<u8> = values.iter().flat_map(|value|
            if cfg!(target_endian = "little") { value.to_be_bytes() } else { value.to_le_bytes() }).collect();
        let mut foreign_spec = BufferDescriptor {size: std::mem::size_of::<BufferDescriptor>(),
            dtype: c"f64".as_ptr(), byte_order: if cfg!(target_endian = "little") { 2 } else { 1 },
            allocation: raw.as_ptr().cast(), allocation_bytes: raw.len(), offset: 0, rank: 2,
            shape: shape.as_ptr(), strides: ptr::null()};
        let mut foreign_buffer = ptr::null_mut();
        assert_eq!(cetta_embed_buffer_copy(&foreign_spec, &mut foreign_buffer), 0);
        drop(raw);
        let order = foreign_spec.byte_order;
        assert_eq!(cetta_embed_buffer_describe(foreign_buffer, &mut foreign_spec), 0);
        assert_eq!(foreign_spec.byte_order, order);
        assert_eq!(cetta_embed_atom_buffer(engine, foreign_buffer, &mut tensor), 0);
        cetta_embed_buffer_release(foreign_buffer);
        let sum_name = b"tensor:sum";
        let mut sum_head = ptr::null_mut();
        assert_eq!(cetta_embed_parse(engine, sum_name.as_ptr().cast(), sum_name.len(), &mut sum_head), 0);
        assert_eq!(cetta_embed_call(engine, sum_head, [tensor].as_ptr(), 1, ptr::null(), &mut result), 0);
        let mut sum = ptr::null_mut();
        assert_eq!(cetta_embed_result_get(result, 0, &mut sum), 0);
        let mut total = 0.0;
        assert_eq!(cetta_embed_atom_get_float(sum, &mut total), 0);
        assert_eq!(total, 10.0);
        cetta_embed_atom_release(sum);
        cetta_embed_atom_release(sum_head);
        cetta_embed_atom_release(tensor);
        cetta_embed_result_release(result);
        let sparse_values = [0.0_f64, 7.0];
        let sparse_coordinates = [0_u64, 4];
        let component_shape = [2_usize];
        let mut component = BufferDescriptor {size: std::mem::size_of::<BufferDescriptor>(),
            dtype: c"f64".as_ptr(), byte_order: 0, allocation: sparse_values.as_ptr().cast(),
            allocation_bytes: 16, offset: 0, rank: 1, shape: component_shape.as_ptr(), strides: ptr::null()};
        let mut value_buffer = ptr::null_mut();
        let mut key_buffer = ptr::null_mut();
        assert_eq!(cetta_embed_buffer_copy(&component, &mut value_buffer), 0);
        component.dtype = c"index".as_ptr(); component.allocation = sparse_coordinates.as_ptr().cast();
        assert_eq!(cetta_embed_buffer_copy(&component, &mut key_buffer), 0);
        let sparse_shape = [2_usize, 3];
        let sparse_spec = SparseDescriptor {size: std::mem::size_of::<SparseDescriptor>(), layout: 1,
            rank: 2, shape: sparse_shape.as_ptr(), values: value_buffer, coordinates: key_buffer,
            indptr: ptr::null(), indices: ptr::null()};
        let mut sparse_atom = ptr::null_mut();
        assert_eq!(cetta_embed_tensor_from_sparse(engine, &sparse_spec, 0, 2, &mut sparse_atom), 0);
        cetta_embed_buffer_release(value_buffer);
        cetta_embed_buffer_release(key_buffer);
        cetta_embed_engine_close(engine);
        let mut parts = SparseComponents {size: std::mem::size_of::<SparseComponents>(),
            values: ptr::null_mut(), coordinates: ptr::null_mut(), indptr: ptr::null_mut(), indices: ptr::null_mut()};
        assert_eq!(cetta_embed_tensor_components(sparse_atom, &mut parts), 0);
        cetta_embed_atom_release(sparse_atom);
        cetta_embed_query_release(query);
        cetta_embed_engine_release(engine);
        let mut integer = 0;
        assert_eq!(cetta_embed_atom_get_integer(atom, &mut integer), 0);
        assert_eq!(integer, 42);
        let mut equal = 0;
        assert_eq!(cetta_embed_atom_equal(atom, atom, &mut equal), 0);
        assert_eq!(equal, 1);
        cetta_embed_atom_release(atom);
        assert_eq!(cetta_embed_buffer_describe(buffer, &mut descriptor), 0);
        assert_eq!(descriptor.rank, 2);
        assert_eq!(*descriptor.shape.add(1), 2);
        assert_eq!(*descriptor.strides, 16);
        assert_eq!(*(descriptor.allocation.cast::<f64>()).add(3), 4.0);
        cetta_embed_buffer_release(buffer);
        let mut sparse_info = BufferDescriptor {size: std::mem::size_of::<BufferDescriptor>(),
            dtype: ptr::null(), byte_order: 0, allocation: ptr::null(), allocation_bytes: 0,
            offset: 0, rank: 0, shape: ptr::null(), strides: ptr::null()};
        assert_eq!(cetta_embed_buffer_describe(parts.coordinates, &mut sparse_info), 0);
        assert_eq!(*sparse_info.shape, 2);
        assert_eq!(*(sparse_info.allocation.cast::<u64>()).add(1), 4);
        assert_eq!(cetta_embed_buffer_describe(parts.values, &mut sparse_info), 0);
        assert_eq!(*(sparse_info.allocation.cast::<f64>()), 0.0);
        assert_eq!(*(sparse_info.allocation.cast::<f64>()).add(1), 7.0);
        cetta_embed_buffer_release(parts.values);
        cetta_embed_buffer_release(parts.coordinates);
        cetta_embed_buffer_release(parts.indptr);
        cetta_embed_buffer_release(parts.indices);
    }
    println!("Rust embedding consumer: sparse/foreign-order buffers, file/module loading, spaces, sliced queries and retained data after engine close");
}
