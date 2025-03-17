# Fast-VCD
High-Performance Python VCD Parser built with PyBind.

# History

This was initially developed during the Winter 2025 Semester of EECS470 (University of Michigan, Computer Architecture) to accelerate our internal debugging tools. 

All are free to use.

# Installing

You will need:

- A compiler that supports C++20
- Python3 

Install the python dependencies:
```bash
pip3 install -r requirements.txt
```

Build the C++ backend:
```bash
python3 setup.py build_ext --inplace
```

# Using

```python
import vcd_parser

if __name__ == "__main__":
    # Create an instance of VCDParser with a filename
    parser = vcd_parser.VCDParser("p3_cpu.vcd")

    # Query a row
    row = parser.query_row(0)

    print(row)
```
