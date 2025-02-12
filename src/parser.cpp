#ifdef PYBIND
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#endif

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#ifdef PYBIND
namespace py = pybind11;
#endif

constexpr std::string_view INTERNAL_DELIM = ".";

// Generic std::string_view stream since C++23 isn't well supported
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
			input_ = std::string_view{};
		} else {
			output = input_.substr(0, pos);
			input_.remove_prefix(pos + 1);
		}
		return *this;
	}

   private:
	std::string_view input_;
};

// Convert binary to hex
std::string bin2hex(const std::string_view binary_ascii) {
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
	hex.reserve((length + padding) / 4 + 1);  // Pre-allocate exact size

	int value = 0;
	int bit_count = 0;

	// Process input while considering the left padding dynamically
	for (size_t i = 0; i < length + padding; ++i) {
		char bit = (i < padding) ? '0' : binary_ascii[i - padding];

		value = (value << 1) | (bit - '0');
		bit_count++;

		if (bit_count == 4) {
			hex.push_back(hex_table[value]);
			value = 0;
			bit_count = 0;
		}
	}

	return hex + "\0";
}

class Parser {
   public:
	explicit Parser(const std::string& file) {
		std::ios_base::sync_with_stdio(false);
		file_stream.open(file);
		if (!file_stream.is_open()) {
			std::cerr << "Could not open file " << file << std::endl;
		}
		parse_header();
		parse_data();
		decompress();
	}

	std::unordered_map<std::string, std::string> fetch_row(const size_t row) const {
		if (row >= time_steps.size()) {
			return {};
		}
		std::unordered_map<std::string, std::string> result;
		for (size_t i = 0; i < column_names.size(); i++) {
			const char* ptr = db[row * column_names.size() + i];
			result[column_names[i]] = (ptr) ? std::string(ptr) : std::string("");
		}
		return result;
	}

	std::vector<int> get_rows() {
		return time_steps;
	}

	std::vector<std::string> get_columns() {
		return column_names;
	}

	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> get_all_cycles(
		bool include_neg = false) {
		std::unordered_map<std::string, std::unordered_map<std::string, std::string>> aggregate_result;
		size_t i = 0;
		while (i < time_steps.size()) {
			if (include_neg || i % 2 == 0) {
				std::string cur_cycle = std::to_string(aggregate_result.size());
				std::unordered_map<std::string, std::string> cycle_result = fetch_row(i);
				aggregate_result[cur_cycle] = cycle_result;
			}
			i++;
		}
		return aggregate_result;
	}

   private:
	std::fstream file_stream;
	std::unordered_map<std::string, std::string> symbol_table;
	std::vector<char*> db;
	std::vector<std::unordered_map<std::string_view, std::string>> raw_data;
	std::vector<std::string> column_names;
	std::vector<int> time_steps;

	/// HEADER PARSING STUFF
	void parse_var(const std::string_view line, const std::string_view path) {
		StringViewStream ss(line);
		std::string_view type;
		std::string_view size;
		std::string_view symbol;
		std::string_view name;
		std::string_view junk;
		ss >> junk >> type >> size >> symbol >> name >> junk;
		symbol_table[std::string(symbol)] = std::string(path) + std::string(name);
	}

	static std::string_view parse_scope_name(const std::string_view line) {
		std::string_view junk;
		std::string_view next_scope;
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
				const std::string_view next_scope = parse_scope_name(line);
				parse_scope(next_scope);
			}
		}
	}

	void parse_data_line(const std::string_view line) {
		if (line.size() == 0) {
			return;
		}
		if (line[0] == 'b' or line[0] == 'B') {
			StringViewStream ss(line);
			std::string_view data;
			std::string_view symbol;
			ss >> data >> symbol;
			const std::string_view logic_name = symbol_table[symbol.data()];
			raw_data.back()[logic_name] = bin2hex(data.substr(1));
		} else if (line[0] == 'x' or line[0] == 'z' or line[0] == '0' or line[0] == '1') {
			char data = line[0];
			const std::string_view symbol = line.substr(1);
			const std::string_view logic_name(symbol_table.at(symbol.data()));
			raw_data.back()[logic_name] += data;
		} else {
			std::cout << "Unrecognized data line: " << line << std::endl;
		}
	}

	void parse_data() {
		std::string line;
		while (std::getline(file_stream, line)) {
			if (line.starts_with("#")) {
				time_steps.push_back(std::stoi(line.substr(1)));
				raw_data.emplace_back();
			} else {
				parse_data_line(line);
			}
		}
	}

	void decompress() {
		db.resize(raw_data.size() * symbol_table.size());

		std::unordered_map<std::string_view, int> column_map;
		{
			int i = 0;
			for (const auto& [_, value] : symbol_table) {
				column_map[value] = i++;
				column_names.push_back(value);
			}
		}
		for (size_t i = 0; i < raw_data.size(); ++i) {
			if (i == 0) {
				for (auto& [name, value] : raw_data[i]) {
					db[i * symbol_table.size() + column_map.at(name)] = raw_data[i][name].data();
					assert(db[i * symbol_table.size() + column_map[name]]);
				}
			} else {
				for (size_t j = 0; j < raw_data[0].size(); ++j) {
					db[i * symbol_table.size() + j] = db[(i - 1) * symbol_table.size() + j];
				}
				for (auto& [name, value] : raw_data[i]) {
					db[i * symbol_table.size() + column_map.at(name)] = raw_data[i][name].data();
					assert(db[i * symbol_table.size() + column_map.at(name)]);
				}
			}
		}
	}
};

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "Usage: " << argv[0] << " <file>" << std::endl;
	}
	std::cout << "Parsing " << argv[1] << std::endl;
	const Parser parser(argv[1]);

	parser.fetch_row(0);
}

#ifdef PYBIND
PYBIND11_MODULE(vcd_parser, m) {
	m.doc() = "VCD Parser Module: Loads and queries VCD files.";
	py::class_<Parser>(m, "VCDParser", "A parser for VCD files, allowing queries by row and column.")
		.def(py::init<const std::string&>(), "Constructor that loads a VCD file.", py::arg("filename"))
		.def("query_row", &Parser::fetch_row, "Fetch a row by index from the VCD file.", py::arg("row_index"))
		.def("get_rows", &Parser::get_rows, "Return the names of rows in the VCD file (time steps).")
		.def("get_columns", &Parser::get_columns, "Return the column names in the VCD file.")
		.def("get_all_cycles", &Parser::get_all_cycles,
		"Return the aggregate information about all cycles in VCD file, with option to include negative edge",
		py::arg("include_neg") = false);
}

#endif