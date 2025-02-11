from setuptools import setup, Extension
import pybind11


# run python setup.py build_ext --inplace


ext_modules = [
    Extension(
        "vcd_parser",  # Module name
        ["src/parser.cpp"],  # C++ source file
        include_dirs=[pybind11.get_include()],
        extra_compile_args=["-O3", "-Wall", "-std=c++20", '-D PYBIND', '-march=native'],
        language="c++",
    ),
]

setup(
    name="vcd_parser",
    ext_modules=ext_modules,
)
