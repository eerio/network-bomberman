/* This file contains definitions of types and message formats used through-
 * out the project in communication between client, server and GUI server.
 * The order of members of the structs below and the order of these types
 * in related std::variants is important:
 * -the order of members determines the order in which they will be
 *  serialized.
 * -the order of types corresponds directly to the message ids which they
 *  will be assigned
 * Name of the 'msg_id' member is not to be changed - objects are tested
 * for presence of such a member in the serialization code
 */

#ifndef BOMBERMAN_MESSAGES_HPP
#define BOMBERMAN_MESSAGES_HPP

#include <cstdint> // uint8_t, ...
#include <string>
#include <vector>
#include <map>
#include <variant>

// Definitions of the most primitive types ---------------------------------
using bomb_id_t = uint32_t;
using bomb_timer_t = uint16_t;
// we could use an enum, but this is just more convenient for serialization
using direction_t = uint8_t;
using explosion_radius_t = uint16_t;
using game_length_t = uint16_t;
using initial_blocks_t = uint16_t;
using msg_id_t = uint8_t;
using player_id_t = uint8_t;
using players_count_t = uint8_t;
using pos_t = uint16_t;
using score_t = uint32_t;
using server_name_t = std::string;
using strlen_t = uint8_t;
using turn_duration_t = uint64_t;
using turn_t = uint16_t;

struct Position {
  pos_t x;
  pos_t y;
  auto operator<=>(const Position&) const = default;
};

struct Bomb {
  Position position;
  bomb_timer_t timer;
};

struct Player {
  std::string name;
  std::string address;
};

// Definitions of events ---------------------------------------------------
struct EventBombPlaced {
  static constexpr uint8_t msg_id = 0;
  bomb_id_t bomb_id;
  Position position;
};

struct EventBombExploded {
  static constexpr uint8_t msg_id = 1;
  bomb_id_t bomb_id;
  std::vector<player_id_t> robots_destroyed;
  std::vector<Position> blocks_destroyed;
};

struct EventPlayerMoved {
  static constexpr uint8_t msg_id = 2;
  player_id_t player_id;
  Position position;
};

struct EventBlockPlaced {
  static constexpr uint8_t msg_id = 3;
  Position position;
};

using Event = std::variant<
  EventBombPlaced, 
  EventBombExploded, 
  EventPlayerMoved, 
  EventBlockPlaced
>;

// Definitions of messages from client to server ---------------------------
struct ClientMessageJoin {
  static constexpr uint8_t msg_id = 0;
  std::string name;
};

struct ClientMessagePlaceBomb {
  static constexpr uint8_t msg_id = 1;
};

struct ClientMessagePlaceBlock {
  static constexpr uint8_t msg_id = 2;
};

struct ClientMessageMove {
  static constexpr uint8_t msg_id = 3;
  direction_t direction;
};

using ClientMessage = std::variant<
  ClientMessageJoin, 
  ClientMessagePlaceBomb,
  ClientMessagePlaceBlock,
  ClientMessageMove
>;

// Definitions of messages from server to client ---------------------------
struct ServerMessageHello {
  static constexpr uint8_t msg_id = 0;
  std::string server_name;
  players_count_t players_count;
  pos_t size_x;
  pos_t size_y;
  game_length_t game_length;
  explosion_radius_t explosion_radius;
  bomb_timer_t bomb_timer;
};

struct ServerMessageAcceptedPlayer {
  static constexpr uint8_t msg_id = 1;
  player_id_t player_id;
  Player player;
};

struct ServerMessageGameStarted {
  static constexpr uint8_t msg_id = 2;
  std::map<player_id_t, Player> players;
};

struct ServerMessageTurn {
  static constexpr uint8_t msg_id = 3;
  game_length_t turn;
  std::vector<Event> events;
};

struct ServerMessageGameEnded {
  static constexpr uint8_t msg_id = 4;
  std::map<player_id_t, score_t> scores;
};

using ServerMessage = std::variant<
  ServerMessageHello, 
  ServerMessageAcceptedPlayer, 
  ServerMessageGameStarted, 
  ServerMessageTurn, 
  ServerMessageGameEnded
>;

// Definitions of messages from client to GUI server -----------------------
struct DrawMessageLobby {
  static constexpr uint8_t msg_id = 0;
  std::string server_name;
  players_count_t players_count;
  pos_t size_x;
  pos_t size_y;
  game_length_t game_length;
  explosion_radius_t explosion_radius;
  bomb_timer_t bomb_timer;
  std::map<player_id_t, Player> players;
};

struct DrawMessageGame {
  static constexpr uint8_t msg_id = 1;
  std::string server_name;
  pos_t size_x;
  pos_t size_y;
  game_length_t game_length;
  turn_t turn;
  std::map<player_id_t, Player> players;
  std::map<player_id_t, Position> player_positions;
  std::vector<Position> blocks;
  std::vector<Bomb> bombs;
  std::vector<Position> explosions;
  std::map<player_id_t, score_t> scores;
};

using DrawMessage = std::variant<DrawMessageLobby, DrawMessageGame>;

// Definitions of messages from GUI server to client -----------------------
struct InputMessagePlaceBomb {
  static constexpr uint8_t msg_id = 0;
};

struct InputMessagePlaceBlock {
  static constexpr uint8_t msg_id = 1;
};

struct InputMessageMove {
  static constexpr uint8_t msg_id = 2;
  direction_t direction;
};

using InputMessage = std::variant<
  InputMessagePlaceBomb,
  InputMessagePlaceBlock,
  InputMessageMove
>;

#endif // BOMBERMAN_MESSAGES_HPP
