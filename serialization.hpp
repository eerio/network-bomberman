#ifndef BOMBERMAN_SERIALIZATION_HPP
#define BOMBERMAN_SERIALIZATION_HPP

#include <limits>
#include <variant>

#include "boost/pfr.hpp"

#include "messages.hpp"

class invalid_message : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

template <typename T>
concept is_class = std::is_class<T>::value;

streamable_buffer& operator<<(streamable_buffer& stream, const std::string& s) {
  if (s.size() > std::numeric_limits<uint8_t>::max()) {
    throw invalid_message("string too long");
  }

  stream << (uint8_t) s.size();
  for (uint8_t c : s) { stream << c; };
  return stream;
}

template <typename T>
streamable_buffer& operator<<(streamable_buffer& stream, const std::vector<T>& s) {
  if (s.size() > std::numeric_limits<uint32_t>::max()) {
    throw invalid_message("vector too long");
  }
  stream << (uint32_t)s.size();
  for (const T& c : s) { stream << c; };
  return stream;
}

template <typename T, typename U>
streamable_buffer& operator<<(streamable_buffer& stream, const std::map<T, U>& s) {
  if (s.size() > std::numeric_limits<uint32_t>::max()) {
    throw invalid_message("map too long");
  }
  stream << (uint32_t)s.size();
  for (const auto& [key, val] : s) { stream << key << val; };
  return stream;
}

template <typename ... Ts>
streamable_buffer& operator<<(streamable_buffer& stream, std::variant<Ts ...>& variant) {
  std::visit([&stream](auto&& x){ stream << x; }, variant);
  return stream;
}

template <is_class Compound>
streamable_buffer& operator<<(streamable_buffer& stream, Compound obj) {
  if constexpr (requires {obj.msg_id;}) {
    stream << obj.msg_id;
  }
  boost::pfr::for_each_field(
    obj,
    [&stream](const auto& elt) { stream << elt; }
  );
  return stream;
}

// unfortunately PFR doesn't support members with std::variant type
streamable_buffer& operator<<(streamable_buffer& stream, ServerMessageTurn& msg) {
  stream << msg.msg_id << msg.turn;
  for (const Event& e : msg.events) {
    std::visit([&stream] (auto&& x) { stream << x; }, e);
  }
  return stream;
}


streamable_buffer& operator>>(streamable_buffer& stream, std::string& s) {
  uint8_t size;
  stream >> size;
  s.clear();
  for (size_t i=0; i < size; ++i) {
    uint8_t c;
    stream >> c;
    s += c;
  }
  return stream;
}

template <typename T>
streamable_buffer& operator>>(streamable_buffer& stream, std::vector<T>& s) {
  uint32_t size;
  stream >> size;
  s.clear();
  for (size_t i=0; i < size; ++i) {
    T c;
    stream >> c;
    s.push_back(c);
  }
  return stream;
}

template <typename T, typename U>
streamable_buffer& operator>>(streamable_buffer& stream, std::map<T, U>& s) {
  uint32_t size;
  stream >> size;
  s.clear();
  for (size_t i=0; i < size; ++i) {
    T key;
    U val;
    stream >> key >> val;
    s[key] = val;
  }
  return stream;
}

template <is_class Compound>
streamable_buffer& operator>>(streamable_buffer& stream, Compound& obj) {
  obj = {};
  boost::pfr::for_each_field(
    obj,
    [&stream](auto& elt) { stream >> elt; }
  );
  return stream;
}

template <typename Variant, size_t I = 0, typename Stream>
Variant get(Stream& s, size_t msg_id) {
  if constexpr (I >= std::variant_size_v<Variant>) {
    throw invalid_message("unknown message id");
  } else if (I == msg_id) {
    typename std::variant_alternative_t<I, Variant> obj;
    boost::pfr::for_each_field(
      obj,
      [&s](auto& elt) { s >> elt; }
    );
    return obj;
  } else {
    return get<Variant, I + 1, Stream>(s, msg_id); 
  }
}

template <typename ... Ts>
streamable_buffer& operator>>(streamable_buffer& stream, std::variant<Ts ...>& variant) {
  uint8_t msg_id;
  stream >> msg_id;
  variant = get<std::variant<Ts ...>>(stream, msg_id);
  return stream;
}

#endif // BOMBERMAN_SERIALIZATION_HPP

