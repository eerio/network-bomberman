#ifndef BOMBERMAN_STREAMABLE_BUFFER_HPP
#define BOMBERMAN_STREAMABLE_BUFFER_HPP

#include <deque>
#include <iostream>
#include <optional>
#include <type_traits> // std::unsigned_integral

#include <boost/endian/conversion.hpp>

class streamable_buffer {
  std::deque<unsigned char> buffer;

  unsigned char byte_mask = std::numeric_limits<unsigned char>::max();

  using provider_t = std::function<std::vector<unsigned char>(size_t n)>;
  std::optional<provider_t> provider;

public:
  class underflow_error : public std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  class buffer_underflow : public std::exception {
  public:
    size_t missing;
    buffer_underflow(size_t missing) : missing(missing) {}
    virtual ~buffer_underflow() noexcept = default;
  };

  streamable_buffer() = default;

  streamable_buffer(std::vector<unsigned char>::iterator begin, std::vector<unsigned char>::iterator end) {
    buffer = { begin, end };
  }

  bool empty() { return buffer.empty(); }

  void set_provider(provider_t&& provider) {
    this->provider = provider;
  }

  template <std::unsigned_integral T>
  streamable_buffer& operator<<(T t) {
    t = boost::endian::native_to_big(t);
    for (size_t i=0; i < sizeof(T); ++i) {
      unsigned char byte = static_cast<unsigned char>((t >> (i * CHAR_BIT)) & (T)byte_mask);
      buffer.push_back(byte);
    }
    return *this;
  }

  template <std::unsigned_integral T>
  streamable_buffer& operator>>(T& t) {
    if (buffer.size() < sizeof(T)) {
      if (provider) {
        auto data = (*provider)(sizeof(T) - buffer.size());
        for (auto e : data) { *this << e; }
      } else {
        //throw underflow_error("Buffer underflow");
        throw buffer_underflow(sizeof(T) - buffer.size());
      }
    }

    t = 0;
    for (size_t i=0; i < sizeof(T); ++i) {
      unsigned char byte = buffer.front();
      buffer.pop_front();
      t = static_cast<unsigned char>((t << CHAR_BIT) | byte);
    }
    t = boost::endian::endian_reverse(t);
    t = boost::endian::big_to_native(t);
    return *this;
  }

  const decltype(buffer)& get_buffer() {
    return buffer;
  }

  void clear() {
    buffer.clear();
  }

  friend std::ostream& operator<<(std::ostream&, const streamable_buffer&);
};

#endif // BOMBERMAN_STREAMABLE_BUFFER_HPP
