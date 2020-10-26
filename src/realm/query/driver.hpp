#ifndef DRIVER_HH
#define DRIVER_HH
#include <string>
#include <map>
#include "realm/query/query_bison.hpp"

// Give Flex the prototype of yylex we want ...
#define YY_DECL yy::parser::symbol_type yylex(realm::ParserDriver& drv)
// ... and declare it for the parser's sake.
YY_DECL;

namespace realm {
// Conducting the whole scanning and parsing of Calc++.
class ParserDriver {
public:
    ParserDriver(TableRef t)
        : base_table(t)
    {
    }

    Query result;
    TableRef base_table;

    // Run the parser on file F.  Return 0 on success.
    int parse(const std::string& str);

    // Handling the scanner.
    void scan_begin();
    void scan_end();

    void error(const std::string& err)
    {
        error_string = err;
        parse_error = true;
    }

    // Whether to generate parser debug traces.
    bool trace_parsing = false;
    // Whether to generate scanner debug traces.
    bool trace_scanning = false;
    // The token's location used by the scanner.
    yy::location location;
private:
    // The string being parsed.
    std::string parse_string;
    std::string error_string;
    void* scan_buffer = nullptr;
    bool parse_error = false;
};
}
#endif // ! DRIVER_HH
