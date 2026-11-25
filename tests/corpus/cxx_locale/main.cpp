// File: tests/corpus/cxx_locale/main.cpp
// Purpose: Exercises the C++ locale machinery that libstdc++ builds on the C library: facets, case conversion, stream imbuing, a global locale
//          from the environment, and the exception thrown for an unknown locale name
// Author: Akilesh
// Date: 2026-11-24

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void show_case_conversion(const std::locale &loc)
{
    const auto &facet = std::use_facet<std::ctype<char>>(loc);
    std::string text = "Hello, World 123";
    std::string lower = text;
    std::string upper = text;
    facet.tolower(&lower[0], &lower[0] + lower.size());
    facet.toupper(&upper[0], &upper[0] + upper.size());
    std::cout << "lower [" << lower << "] upper [" << upper << "]\n";
    std::cout << "classes: " << facet.is(std::ctype_base::alpha, 'x') << facet.is(std::ctype_base::digit, '7') << facet.is(std::ctype_base::space, ' ')
              << facet.is(std::ctype_base::upper, 'a') << '\n';
    std::cout << "single: " << facet.tolower('Q') << facet.toupper('q') << " " << std::tolower('Z', loc) << std::toupper('z', loc) << '\n';
}

void show_number_formatting(const std::locale &loc)
{
    std::ostringstream out;
    out.imbue(loc);
    out << 1234567 << ' ' << std::fixed << std::setprecision(3) << 3.14159 << ' ' << std::hex << 255 << ' ' << std::boolalpha << true;
    std::cout << "formatted [" << out.str() << "]\n";
    std::istringstream in("42 2.5 word");
    in.imbue(loc);
    int whole = 0;
    double fraction = 0;
    std::string word;
    in >> whole >> fraction >> word;
    std::cout << "parsed " << whole << ' ' << fraction << ' ' << word << '\n';
}

void show_unknown_locale()
{
    try {
        std::locale bad("xx_YY.not_a_locale");
        std::cout << "unexpectedly built a locale named " << bad.name() << '\n';
    } catch (const std::runtime_error &error) {
        std::cout << "caught: " << error.what() << '\n';
    }
}

}  // namespace

int main()
{
    show_case_conversion(std::locale::classic());
    show_number_formatting(std::locale::classic());
    std::locale::global(std::locale(""));
    std::cout << "global name [" << std::locale().name() << "]\n";
    show_case_conversion(std::locale());
    show_number_formatting(std::locale());
    show_unknown_locale();
    std::string sorted = "delta alpha charlie bravo";
    std::sort(sorted.begin(), sorted.end());
    std::cout << "sorted [" << sorted << "]\n";
    return 0;
}
