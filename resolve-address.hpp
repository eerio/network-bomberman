#ifndef BOMBERMAN_RESOLVE_ADDRESS_HPP
#define BOMBERMAN_RESOLVE_ADDRESS_HPP

#include <cstdint> // uint16_t
#include <stdexcept> // std::runtime_error
#include <string>
#include <utility> // std::pair

#include <boost/asio.hpp> // io_service

#include "common.hpp" // parse_uint

class addr_resolution_error : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Convert str "host:port" to {str host, uint16_t port}
std::pair<std::string, uint16_t> split_port(const std::string& address_spec) {
  size_t last_colon = address_spec.rfind(':');
  if (last_colon == std::string::npos) {
    throw addr_resolution_error("Port not specified");
  }

  size_t port_len = address_spec.size() - (last_colon + 1);

  std::string port_str = address_spec.substr(last_colon + 1, port_len);
  uint16_t port;
  try {
    port = parse_uint<uint16_t>(port_str.c_str(), port_str.c_str() + port_len);
  } catch (const invalid_number& e) {
    throw addr_resolution_error("Invalid port number: " + port_str);
  }

  return { address_spec.substr(0, last_colon), port };
}

// Resolve address of the form "<ipv4/ipv6/hostname>:<port>" to a list of
// endpoints. Throw exception on failure. The `io_service` argument is required
// because we perform DNS lookups for hostname resolution here
template <typename resolver_t>
typename resolver_t::results_type resolve_address(
    const std::string& addr,
    boost::asio::io_service& io_service
) {
  auto [host, port] = split_port(addr);
  resolver_t resolver { io_service };

  try {                                                                      
    return resolver.resolve(host, std::to_string(port));
  } catch (const boost::system::system_error& err) {                         
    throw addr_resolution_error("Unable to resolve address: " + addr);
  }                                                                          
} 

#endif // BOMBERMAN_RESOLVE_ADDRESS_HPP

