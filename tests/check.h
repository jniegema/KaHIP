#ifndef KAHIP_TEST_CHECK_H
#define KAHIP_TEST_CHECK_H

#include <stdexcept>
#include <string>

inline void require(bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
}

#endif
