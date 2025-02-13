import vcd_parser

if __name__ == "__main__":
    # Create an instance of VCDParser with a filename
    parser = vcd_parser.VCDParser("p3_cpu.vcd")

    # Query a row
    results = parser.fetch_all_cycles()
    print(len(results["pos_result"]))
        
        

