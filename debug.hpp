/** This file contains debugging helpers for the project.
 *  Most notably, it contains std::ostream operator<< overloads for most
 *  common types such as selected STL containers and this projects'
 *  message formats for the client-server-gui communication.
 */

#ifndef BOMBERMAN_DEBUG_HPP
#define BOMBERMAN_DEBUG_HPP

#include <deque>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

#include "streamable-buffer.hpp"
#include "messages.hpp"

template<typename u, typename v>
std::ostream& operator<< (std::ostream& out, const std::pair<u, v>& x) {
  out << '<' << x.first << ", " << x.second << '>';
  return out;
}

template<typename u>
std::ostream& operator<< (std::ostream& out, const std::vector<u>& x) {
  out << '[';
  if (x.size() > 0) {
    for (size_t i=0; i < x.size() - 1; ++i) { out << x[i] << ", "; }
    out << x[x.size() - 1];
  }
  out << ']';
  return out;
}

template<typename u>
std::ostream& operator<< (std::ostream& out, const std::deque<u>& x) {
  out << '[';
  if (x.size() > 0) {
    for (size_t i=0; i < x.size() - 1; ++i) { out << x[i] << ", "; }
    out << x[x.size() - 1];
  }
  out << ']';
  return out;
}

template<typename u, typename v>
std::ostream& operator<< (std::ostream& out, const std::map<u, v>& x) {
  out << '[';
  if (x.size() > 0) {
    for (auto it=x.begin(); it != prev(x.end()); ++it) {
      out << *it << ", ";
    }
    out << *std::prev(x.end());
  }
  out << ']';
  return out;
}

std::ostream& operator<<(std::ostream& os, const streamable_buffer& s) {
  os << s.buffer;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Position& pos) {
  return os << std::make_pair(pos.x, pos.y);
}

std::ostream& operator<<(std::ostream& os, const Bomb& bomb) {
  return os << std::make_pair(bomb.position, bomb.timer);
}

std::ostream& operator<< (std::ostream& os, const Player& player) {
  return os << std::make_pair(player.name, player.address);
}

template<typename T, typename ... Args>
void print(T t, Args ... args) {
  std::cout << t;
  ((std::cout << ' ' << args), ...);
}

template<typename ... Args>
void println([[maybe_unused]]Args ... args) {
  print(args...);
  print('\n');
}

template<typename ... Args>
void debug([[maybe_unused]]Args ... args) {
#ifndef NDEBUG
  println(args...);
#endif
}

#endif // BOMBERMAN_DEBUG_HPP
