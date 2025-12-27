#include <iostream>
#include <sstream>

// RAII for redirecting cout
class CoutCapture {
  std::ostringstream captured;
  std::streambuf* old_buf;
public:
  // rdbuf overloaded with fetch_set pattern here. 
  CoutCapture() : old_buf(std::cout.rdbuf(captured.rdbuf())) {}
  ~CoutCapture() { std::cout.rdbuf(old_buf); }
  std::string str() const {
    return captured.str();
  }
};
