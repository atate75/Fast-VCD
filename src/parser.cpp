#ifdef PYBIND
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#endif

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#ifdef PYBIND
namespace py = pybind11;
#endif

constexpr std::string_view INTERNAL_DELIM = ".";

// Generic std::string_view stream since C++23 isn't well supported.
class StringViewStream {
   public:
	explicit StringViewStream(const std::string_view sv) : input_(sv) {}
	StringViewStream& operator>>(std::string_view& output) {
		input_.remove_prefix(std::min(input_.find_first_not_of(' '), input_.size()));
		if (input_.empty()) {
			output = {};
			return *this;
		}
		size_t pos = input_.find(' ');
		if (pos == std::string_view::npos) {
			output = input_;
			input_ = {};
		} else {
			output = input_.substr(0, pos);
			input_.remove_prefix(pos + 1);
		}
		return *this;
	}

   private:
	std::string_view input_;
};

// Convert binary to hex.
inline std::string bin2hex(const std::string_view binary_ascii) {
	if (binary_ascii.find('x') != std::string_view::npos) {
		return "x\0";
	}
	if (binary_ascii.find('z') != std::string_view::npos) {
		return "z\0";
	}
	static constexpr char hex_table[] = "0123456789abcdef";
	size_t length = binary_ascii.size();
	size_t remainder = length % 4;
	size_t padding = (remainder == 0) ? 0 : (4 - remainder);
	std::string hex;
	hex.reserve((length + padding) / 4);
	int value = 0;
	int bit_count = 0;
	for (size_t i = 0; i < length + padding; ++i) {
		char bit = (i < padding) ? '0' : binary_ascii[i - padding];
		value = (value << 1) | (bit - '0');
		++bit_count;
		if (bit_count == 4) {
			hex.push_back(hex_table[value]);
			value = 0;
			bit_count = 0;
		}
	}
	return hex;  // null terminator not needed when storing length.
}

class Parser {
   public:
	explicit Parser(const std::string& file) {
		std::ios_base::sync_with_stdio(false);
		file_stream.open(file);
		if (!file_stream.is_open()) {
			std::cerr << "Could not open file " << file << std::endl;
			return;
		}
		parse_header();
		parse_data();
		// Initialize data_block with an empty string at offset 0.
		data_block.push_back('\0');
		decompress();
	}

	// Return a map from column name to string for a given row.
	std::unordered_map<std::string, std::string> fetch_row(const size_t row) const {
		std::unordered_map<std::string, std::string> result;
		if (row >= time_steps.size()) {
			return result;
		}
		size_t num_cols = column_names.size();
		const char* base = data_block.data();
		for (size_t i = 0; i < num_cols; ++i) {
			unsigned int off = db[row * num_cols + i];
			result[column_names[i]] = std::string(base + off);
		}
		return result;
	}

	std::vector<int> get_rows() const {
		return time_steps;
	}

	std::vector<std::string> get_columns() const {
		return column_names;
	}

	// Get the total number of positive clocks
	unsigned int get_pos_clock_numbers() const {
		return time_steps.size() >> 1;
	}

	// Get the total number of clocks in cluding neg edge
	unsigned int get_neg_clock_numbers() const {
		return time_steps.size();
	}

   private:
	std::fstream file_stream;
	std::unordered_map<std::string, std::string> symbol_table;
	// db now holds offsets into data_block.
	std::vector<unsigned int> db;
	std::vector<std::unordered_map<std::string_view, std::string>> raw_data;
	std::vector<std::string> column_names;
	std::vector<int> time_steps;
	// data_block holds concatenated string data.
	std::vector<char> data_block;

	// HEADER PARSING
	void parse_var(const std::string_view line, const std::string_view path) {
		StringViewStream ss(line);
		std::string_view type, size, symbol, name, junk;
		ss >> junk >> type >> size >> symbol >> name >> junk;
		// Variable also needs a delimiter
		symbol_table[std::string(symbol)] = std::string(path) + std::string(INTERNAL_DELIM) + std::string(name);
	}

	static std::string_view parse_scope_name(const std::string_view line) {
		std::string_view junk, next_scope;
		StringViewStream ss(line);
		ss >> junk >> junk >> next_scope;
		return next_scope;
	}

	void parse_scope(const std::string_view path) {
		std::string line;
		while (std::getline(file_stream, line)) {
			if (line.starts_with("$upscope $end")) {
				break;
			}
			if (line.starts_with("$scope")) {
				std::string_view next_scope = parse_scope_name(line);
				parse_scope(std::string(path) + std::string(INTERNAL_DELIM) + std::string(next_scope));
			} else if (line.starts_with("$var")) {
				parse_var(line, path);
			}
		}
	}

	void parse_header() {
		std::string line;
		while (std::getline(file_stream, line)) {
			if (line.starts_with("$enddefinitions")) {
				break;
			}
			if (line.starts_with("$scope")) {
				std::string_view next_scope = parse_scope_name(line);
				parse_scope(next_scope);
			}
		}
	}

	void parse_data_line(const std::string_view line) {
		if (line.empty()) {
			return;
		}
		if (line[0] == 'b' || line[0] == 'B') {
			StringViewStream ss(line);
			std::string_view data, symbol;
			ss >> data >> symbol;
			const std::string_view logic_name = symbol_table[symbol.data()];
			// Overwrite or create the value.
			raw_data.back()[logic_name] = bin2hex(data.substr(1));
		} else if (line[0] == 'x' || line[0] == 'z' || line[0] == '0' || line[0] == '1') {
			char ch = line[0];
			std::string_view symbol = line.substr(1);
			const std::string_view logic_name = symbol_table.at(symbol.data());
			raw_data.back()[logic_name] += ch;
		} else {
			std::cout << "Unrecognized data line: " << line << std::endl;
		}
	}

	void parse_data() {
		std::string line;
		int multipler = 1;
		bool fake_clock = false;
		while (std::getline(file_stream, line)) {
			if (line.starts_with("#")) {
				int t = std::stoi(line.substr(1));
				// Ignore the #0
				if (t != 0) {
					// First non-zero, set the clock time
					if (time_steps.empty()) {
						multipler = t;
					}
					if (t % multipler == 0) {
						fake_clock = false;
						time_steps.push_back(t);
						raw_data.emplace_back();
					} else {
						// if t can't be wholy divided by multiplier, then it's a fake clock
						fake_clock = true;
					}
				}
			} else {
				// Only parse data line when time_steps is not empty (after #0) and not a fake clock
				if (!time_steps.empty() && !fake_clock) {
					parse_data_line(line);
				}
			}
		}
	}

	// In decompress, we build db and data_block.
	void decompress() {
		size_t num_rows = raw_data.size();
		size_t num_cols = symbol_table.size();
		db.resize(num_rows * num_cols, 0);  // default offset 0 (empty string)

		// Build column_map and column_names.
		std::unordered_map<std::string_view, int> column_map;
		{
			int idx = 0;
			for (const auto& [_, value] : symbol_table) {
				column_map[value] = idx++;
				column_names.push_back(value);
			}
		}

		// For each row:
		for (size_t i = 0; i < num_rows; ++i) {
			size_t rowStart = i * num_cols;
			// For each column, if not updated in this row, copy previous row's offset.
			if (i > 0) {
				for (size_t j = 0; j < num_cols; ++j) {
					db[rowStart + j] = db[(i - 1) * num_cols + j];
				}
			}
			// For each updated column in raw_data[i]:
			for (const auto& [name, value] : raw_data[i]) {
				int col = column_map.at(name);
				// Store new offset.
				unsigned int offset = static_cast<unsigned int>(data_block.size());
				db[rowStart + col] = offset;
				// Append value (with null terminator) to data_block.
				data_block.insert(data_block.end(), value.begin(), value.end());
				data_block.push_back('\0');
			}
		}
	}
};

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "Usage: " << argv[0] << " <file>" << std::endl;
		return -1;
	}
	std::cout << "Parsing " << argv[1] << std::endl;
	Parser parser(argv[1]);
	auto row0 = parser.fetch_row(0);
	for (const auto& [col, str] : row0) {
		std::cout << col << ": " << str << std::endl;
	}
	return 0;
}

#ifdef PYBIND
PYBIND11_MODULE(vcd_parser, m) {
	m.doc() = "VCD Parser Module: Loads and queries VCD files.";
	py::class_<Parser>(m, "VCDParser", "A parser for VCD files, allowing queries by row and column.")
		.def(py::init<const std::string&>(), "Constructor that loads a VCD file.", py::arg("filename"))
		.def("query_row", &Parser::fetch_row, "Fetch a row by index from the VCD file.", py::arg("row_index"))
		.def("get_rows", &Parser::get_rows, "Return the names of rows in the VCD file (time steps).")
		.def("get_columns", &Parser::get_columns, "Return the column names in the VCD file.")
		.def("get_pos_clock_numbers", &Parser::get_pos_clock_numbers, "Return the number of positive clocks.")
		.def("get_neg_clock_numbers", &Parser::get_neg_clock_numbers,
			"Return the total number of clocks including neg edge.");
}
#endif
