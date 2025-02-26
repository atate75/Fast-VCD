import vcd_parser

if __name__ == "__main__":
    # Create an instance of VCDParser with a filename
    parser = vcd_parser.VCDParser("ready_list_test.vcd")

    # Query a row
    results = parser.get_pos_clock_numbers()
    print(results)
        
        

