#ifndef BOMBERMAN_COMMON_HPP
#define BOMBERMAN_COMMON_HPP

#include <charconv> // std::from_chars
#include <concepts> // std::unsigned_integral
#include <stdexcept> // std::runtime_error
#include <system_error> // std::errc

#include <boost/asio.hpp>

#include "streamable-buffer.hpp"

// The maximal size of data in a UDP packet is the maximal size of an UDP
// packet (1 << 16 bytes) decreased by the size of an IP header (20 bytes)
// and an UDP header (8 bytes).
constexpr size_t MAX_UDP_MESSAGE_SIZE = (1 << 16) - 20 - 8;

using port_t = uint16_t;

class invalid_number : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Parse unsigned, base-10 integral from the given C string.
// Throw invalid_number on any non-standardiness such as a leading sign.
template <std::unsigned_integral T>
T parse_uint(const char* first, const char* last) {
  T result;
  std::from_chars_result conv = std::from_chars(
    first,
    last,
    result
  );

  if (conv.ec != std::errc {}) {
    throw invalid_number("Invalid format for an unsigned integer");
  }

  return result;
}

// to nie powinno byc w cpp - to powinno byc jako template z tcp/udp

void send(streamable_buffer& stream, boost::asio::ip::tcp::socket& sock) {
  auto buffer = stream.get_buffer();
  const std::vector<unsigned char>& data { buffer.begin(), buffer.end() };
  sock.send(boost::asio::buffer(data));
  stream.clear();
}

std::vector<uint8_t> read(boost::asio::ip::tcp::socket& sock, size_t n) {
  std::vector<uint8_t> buffer (n);
  size_t rec = boost::asio::read(sock, boost::asio::buffer(buffer));
  assert(rec == n);
  return buffer;
}

#endif // BOMBERMAN_COMMON_HPP

