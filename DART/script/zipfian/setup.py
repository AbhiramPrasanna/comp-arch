from setuptools import setup, Extension
import pybind11

cpp_module = Extension(
    'cpp_zipfian',
    sources=['zipf_wrapper.cpp'],
    include_dirs=[pybind11.get_include()], 
    language='c++',
    extra_compile_args=['-std=c++11', '-O3'],
)

setup(
    name='cpp_zipfian',
    version='1.0',
    description='A Python wrapper for a C++ Zipfian generator',
    ext_modules=[cpp_module],
)
