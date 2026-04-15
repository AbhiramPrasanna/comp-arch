#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <vector>
#include <cstdint>
#include <stdexcept>

#include "zipf.h"

namespace py = pybind11;

std::vector<uint64_t> generate_zipf_cpp(uint64_t count, uint64_t n, double theta, uint64_t seed) {
    if (n == 0) {
        throw std::invalid_argument("Number of items (n) cannot be zero.");
    }

    struct zipf_gen_state state;
    mehcached_zipf_init(&state, n, theta, seed);

    std::vector<uint64_t> results;
    results.reserve(count);

    for (uint64_t i = 0; i < count; ++i) {
        uint64_t val = mehcached_zipf_next(&state);
        
        if (val >= n) {
            val = 0;
        }
        results.push_back(val);
    }

    return results;
}

PYBIND11_MODULE(cpp_zipfian, m) {
    m.doc() = "A C++ Zipfian generator wrapped for Python using pybind11";
    m.def(
        "generate", &generate_zipf_cpp, "Generates a list of Zipfian random numbers",
        py::arg("count"),
        py::arg("n"),
        py::arg("theta"),
        py::arg("seed")
    );
}
