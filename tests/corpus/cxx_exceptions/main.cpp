/*
 * File: tests/corpus/cxx_exceptions/main.cpp
 * Purpose: Throw and catch C++ exceptions across frames, with destructors, rethrow and nested handlers
 * Author: Akilesh
 * Date: 2026-10-29
 */

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

constexpr int DEPTH = 6;

struct Tracker {
    explicit Tracker(const char *label) : label_(label) { std::printf("enter %s\n", label_); }
    ~Tracker() { std::printf("leave %s\n", label_); }
    Tracker(const Tracker &) = delete;
    Tracker &operator=(const Tracker &) = delete;

private:
    const char *label_;
};

struct CodeError : std::runtime_error {
    CodeError(const std::string &message, int code) : std::runtime_error(message), code(code) {}
    int code;
};

void descend(int level)
{
    Tracker tracker(level % 2 == 0 ? "even" : "odd");
    if (level == DEPTH) {
        throw CodeError("bottom reached", level);
    }
    descend(level + 1);
}

int catch_by_type(void)
{
    try {
        descend(0);
    } catch (const std::logic_error &) {
        return -1;
    } catch (const CodeError &error) {
        std::printf("caught %s (%d)\n", error.what(), error.code);
        return error.code;
    }
    return 0;
}

void rethrow_through(void)
{
    try {
        throw std::invalid_argument("inner");
    } catch (...) {
        std::printf("rethrowing\n");
        throw;
    }
}

int catch_everything(void)
{
    try {
        rethrow_through();
    } catch (const std::exception &error) {
        std::printf("outer caught %s\n", error.what());
        return 1;
    }
    return 0;
}

int throw_primitive(void)
{
    try {
        throw 42;
    } catch (int value) {
        std::printf("caught int %d\n", value);
        return value;
    }
}

}  // namespace

int main()
{
    int total = catch_by_type() + catch_everything() + throw_primitive();
    std::printf("total %d\n", total);
    return total == DEPTH + 1 + 42 ? EXIT_SUCCESS : EXIT_FAILURE;
}
