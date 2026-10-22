#!/usr/bin/env python3
"""Actual NumPy capsules against the optional, copying CPU DLPack provider."""
import argparse
import ctypes as C
import gc
from pathlib import Path


class Device(C.Structure):
    _fields_=[('kind',C.c_int32),('index',C.c_int32)]


class DType(C.Structure):
    _fields_=[('code',C.c_uint8),('bits',C.c_uint8),('lanes',C.c_uint16)]


class Tensor(C.Structure):
    _fields_=[('data',C.c_void_p),('device',Device),('ndim',C.c_int32),('dtype',DType),
              ('shape',C.POINTER(C.c_int64)),('strides',C.POINTER(C.c_int64)),('offset',C.c_uint64)]


class Version(C.Structure):
    _fields_=[('major',C.c_uint32),('minor',C.c_uint32)]


class Managed(C.Structure):
    pass


Managed._fields_=[('version',Version),('context',C.c_void_p),
                 ('deleter',C.CFUNCTYPE(None,C.POINTER(Managed))),
                 ('flags',C.c_uint64),('tensor',Tensor)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,required=True)
    args=parser.parse_args()
    import numpy as np
    lib=C.CDLL(str(args.library.resolve()))
    lib.cetta_tensor_dlpack_copy.argtypes=[C.POINTER(Tensor),C.c_void_p,C.c_size_t,C.POINTER(C.c_void_p)]
    lib.cetta_tensor_dlpack_copy.restype=C.c_int
    lib.cetta_tensor_dlpack_export_copy.argtypes=[C.c_void_p,C.POINTER(C.POINTER(Managed))]
    lib.cetta_tensor_dlpack_export_copy.restype=C.c_int
    lib.cetta_tensor_release.argtypes=[C.c_void_p]
    capsule_pointer=C.pythonapi.PyCapsule_GetPointer
    capsule_pointer.argtypes=[C.py_object,C.c_char_p]
    capsule_pointer.restype=C.c_void_p
    capsule_new=C.pythonapi.PyCapsule_New
    capsule_new.argtypes=[C.c_void_p,C.c_char_p,C.c_void_p]
    capsule_new.restype=C.py_object

    class Export:
        def __init__(self,tensor):
            self.tensor=tensor

        def __dlpack_device__(self):
            return (1,0)

        def __dlpack__(self,stream=None,max_version=None,dl_device=None,copy=None):
            assert stream is None and max_version and max_version[0]>=1
            if copy is False:
                raise BufferError('this immutable provider exports independent copies')
            managed=C.POINTER(Managed)()
            assert lib.cetta_tensor_dlpack_export_copy(self.tensor,C.byref(managed))==0
            # Test-only immediate handoff: NumPy takes the managed deleter.
            return capsule_new(C.cast(managed,C.c_void_p),b'dltensor_versioned',None)

    arrays=[]
    for dtype in (np.int64,np.uint64,np.uint8,np.float32,np.float64,np.bool_):
        base=np.arange(24,dtype=np.uint8).astype(dtype).reshape(4,6)
        arrays.extend((base,base[::-1,::2],base.T,np.broadcast_to(base[:1,:1],(3,4))))
    arrays += [np.asarray(7,dtype=np.int64),np.empty((0,3),dtype=np.float64),
               np.array([-0.0,0.0,np.inf,-np.inf,np.nan],dtype=np.float64)]
    checks=0
    for array in arrays:
        expected=array.copy()
        capsule=array.__dlpack__(max_version=(1,0))
        pointer=capsule_pointer(capsule,b'dltensor_versioned')
        producer=C.cast(pointer,C.POINTER(Managed))
        low,high=np.lib.array_utils.byte_bounds(array)
        tensor=C.c_void_p()
        assert lib.cetta_tensor_dlpack_copy(C.byref(producer.contents.tensor),C.c_void_p(low),high-low,C.byref(tensor))==0
        # Keep the capsule with its producer; this borrowing call did not
        # consume it. The independent immutable tensor survives its release.
        del producer,capsule
        if array.flags.writeable and array.size:
            array.flat[0]=False if array.dtype==np.bool_ else 99
        got=np.from_dlpack(Export(tensor),copy=True)
        assert got.dtype==expected.dtype and got.shape==expected.shape
        assert got.tobytes(order='C')==expected.tobytes(order='C')
        # A second export remains independent of the first output's bytes.
        again=np.from_dlpack(Export(tensor),copy=True)
        lib.cetta_tensor_release(tensor)
        assert again.tobytes(order='C')==expected.tobytes(order='C')
        del got,again
        gc.collect()
        checks+=3
    print(f'TensorDLPackNumPy {checks} 0')


if __name__=='__main__':
    main()
