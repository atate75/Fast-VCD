import vcd_parser

if __name__ == "__main__":
    # Create an instance of VCDParser with a filename
    parser = vcd_parser.VCDParser("bfsvpd.vcd")

    # Query a row
    row = parser.query_row(0)

    print(row)

