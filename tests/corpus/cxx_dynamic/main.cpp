/*
 * File: tests/corpus/cxx_dynamic/main.cpp
 * Purpose: Typical C++ standard library use through the dynamic libstdc++: streams, strings, containers, threads and exceptions
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int THREADS = 4;
constexpr int INCREMENTS = 1000;

void print_strings_and_numbers()
{
    std::string text = "crossport";
    text += " runs c++";
    std::ostringstream stream;
    stream << text << ' ' << 42 << ' ' << std::fixed << std::setprecision(3) << 3.14159 << ' ' << std::hex << 255;
    std::cout << stream.str() << '\n';
    std::cout << std::to_string(12345) << ' ' << std::stod("2.5") << ' ' << std::stoi("-17") << '\n';
}

void print_containers()
{
    std::vector<int> numbers = {5, 3, 9, 1, 7};
    std::sort(numbers.begin(), numbers.end());
    for (int value : numbers) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
    std::map<std::string, int> ordered = {{"pear", 3}, {"apple", 1}, {"fig", 2}};
    for (const auto &entry : ordered) {
        std::cout << entry.first << '=' << entry.second << ' ';
    }
    std::cout << '\n';
    std::unordered_map<int, std::string> hashed = {{1, "one"}, {2, "two"}};
    std::cout << hashed.at(1) << ' ' << hashed.at(2) << ' ' << hashed.count(3) << '\n';
}

void use_functions_and_pointers()
{
    auto shared = std::make_shared<std::vector<int>>(3, 7);
    std::function<int(int)> twice = [shared](int value) { return value * 2 + static_cast<int>(shared->size()); };
    std::cout << twice(10) << ' ' << shared.use_count() << '\n';
}

void count_in_threads()
{
    std::mutex lock;
    int total = 0;
    std::vector<std::thread> workers;
    for (int i = 0; i < THREADS; i++) {
        workers.emplace_back([&] {
            for (int n = 0; n < INCREMENTS; n++) {
                std::lock_guard<std::mutex> guard(lock);
                total++;
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    std::cout << "total " << total << '\n';
}

void throw_and_catch()
{
    try {
        std::vector<int> empty;
        (void)empty.at(3);
    } catch (const std::out_of_range &) {
        std::cout << "caught out_of_range\n";
    }
    try {
        std::stoi("not a number");
    } catch (const std::invalid_argument &error) {
        std::cout << "caught invalid_argument: " << error.what() << '\n';
    }
}

}  // namespace

int main()
{
    print_strings_and_numbers();
    print_containers();
    use_functions_and_pointers();
    count_in_threads();
    throw_and_catch();
    return 0;
}
