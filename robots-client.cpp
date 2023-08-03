#include <variant>
#include <iostream>
#include <optional>

#include <boost/program_options.hpp>
#include <boost/asio.hpp>

#include "resolve-address.hpp"
#include "streamable-buffer.hpp"
#include "serialization.hpp"
#include "messages.hpp"

#include "debug.hpp"

namespace po = boost::program_options;

namespace ip = boost::asio::ip;

enum ClientState {
  Lobby,
  Playing,
  Finish
};

volatile std::sig_atomic_t client_state = ClientState::Lobby;
std::string player_name;

ip::udp::resolver::results_type gui_endpoints;

void send(streamable_buffer& stream, ip::udp::socket& sock) {
  auto buffer = stream.get_buffer();
  const std::vector<unsigned char>& data { buffer.begin(), buffer.end() };
  sock.send_to(boost::asio::buffer(data), *gui_endpoints);
  stream.clear();
}

struct game_state_t {
  std::string server_name;
  players_count_t players_count;
  pos_t size_x;
  pos_t size_y;
  game_length_t game_length;
  explosion_radius_t explosion_radius;
  bomb_timer_t bomb_timer;
  turn_t turn;

  std::map<player_id_t, Player> players;
  std::map<player_id_t, bool> killed;
  std::map<player_id_t, Position> player_positions;
  std::vector<Position> blocks;
  std::set<Position> blocks_destroyed;
  std::vector<Bomb> bombs;
  std::map<bomb_id_t, Position> bombs_positions;
  std::vector<Position> explosions;
  std::map<player_id_t, score_t> scores;
} game_state;

void handle_event(const EventBombPlaced& e) {
  println("Bomb placed:", e.position);
  game_state.bombs.push_back(Bomb {e.position, game_state.bomb_timer});
  game_state.bombs_positions[e.bomb_id] = e.position;
}

void handle_event(const EventBombExploded& e) {
  pos_t x0 = game_state.bombs_positions[e.bomb_id].x;
  pos_t y0 = game_state.bombs_positions[e.bomb_id].y;
  println("Bomb exploded at:", x0, y0);

  game_state.explosions.push_back(Position {.x=x0, .y=y0});
  
  std::vector<std::pair<int, int>> diffs = {
    { 0, -1},
    { 0,  1},
    { 1,  0},
    {-1,  0}
  };
  for (auto [dx, dy] : diffs) {
    int x = x0;
    int y = y0;

    // if the explosion's arm encountered a block on its way,
    // don't propagate further
    bool blocked = false;

    for (pos_t t = 0; t <= game_state.explosion_radius && !blocked; ++t) {
      Position pos = Position {
        .x = static_cast<pos_t>(x),
        .y = static_cast<pos_t>(y)
      };

      bool init = dx == 0 && dy == -1;
      if (!(init && t==0)) {
        game_state.explosions.push_back(pos);
      }

      for (const Position& block_pos : game_state.blocks) {
        if (block_pos == pos) { blocked = true; break; }
      }

      x += dx;
      y += dy;
      if (x < 0 || x >= game_state.size_x) { break; }
      if (y < 0 || y >= game_state.size_y) { break; }
    }
  };

  for (const player_id_t& player_id : e.robots_destroyed) {
    game_state.killed[player_id] = true;
  }

  for (const Position& pos : e.blocks_destroyed) {
    game_state.blocks_destroyed.insert(pos);
  }

  // remove the bomb
  // find the bomb's position
  auto bomb_position_it = game_state.bombs_positions.find(e.bomb_id);
  if (bomb_position_it == game_state.bombs_positions.end()) { return; }
  Position pos = bomb_position_it->second;

  // find the bomb
  auto bomb_it = std::find_if(
    game_state.bombs.begin(),
    game_state.bombs.end(),
    [pos](auto& x) { return x.position == pos; }
  );
  if (bomb_it == game_state.bombs.end()) { return; }
  game_state.bombs.erase(bomb_it);
}

void handle_event(const EventPlayerMoved& e) {
  println("Player moved to:", e.position);
  game_state.player_positions[e.player_id] = e.position;
}

void handle_event(const EventBlockPlaced& e) {
  println("Block placed at:", e.position);
  game_state.blocks.push_back(e.position);
}

void prepare_draw_message(DrawMessage& msg) {
  std::visit (
    [](auto& msg) {
      msg.server_name = game_state.server_name;
      msg.size_x = game_state.size_x;
      msg.size_y = game_state.size_y;
      msg.game_length = game_state.game_length;
      msg.players = game_state.players;
    },
    msg
  );
}

DrawMessageLobby prepare_message(DrawMessageLobby&& msg) {
  msg.server_name = game_state.server_name;
  msg.size_x = game_state.size_x;
  msg.size_y = game_state.size_y;
  msg.game_length = game_state.game_length;
  msg.players = game_state.players;
  msg.players_count = game_state.players_count;
  msg.explosion_radius = game_state.explosion_radius;
  msg.bomb_timer = game_state.bomb_timer;

  return msg;
}

DrawMessageGame prepare_message(DrawMessageGame&& msg) {
  msg.server_name = game_state.server_name;
  msg.size_x = game_state.size_x;
  msg.size_y = game_state.size_y;
  msg.game_length = game_state.game_length;
  msg.players = game_state.players;
  msg.turn = game_state.turn;
  msg.player_positions = game_state.player_positions;
  msg.blocks = game_state.blocks;
  msg.bombs = game_state.bombs;
  msg.explosions = game_state.explosions;
  msg.scores = game_state.scores;

  return msg;
}

void send_lobby(ip::udp::socket& gui_socket) {
  streamable_buffer s;
  s << prepare_message(DrawMessageLobby {});
  send(s, gui_socket);
}

void send_game(ip::udp::socket& gui_socket) {
  streamable_buffer s;
  s << prepare_message(DrawMessageGame {});
  send(s, gui_socket);
  game_state.explosions = {};
}

void handle_server_msg(
  const ServerMessageHello& msg,
  ip::udp::socket& gui_socket
) {
  println("Hello!");
  game_state.server_name = msg.server_name;
  game_state.players_count = msg.players_count;
  game_state.size_x = msg.size_x;
  game_state.size_y = msg.size_y;
  game_state.game_length = msg.game_length;
  game_state.explosion_radius = msg.explosion_radius;
  game_state.bomb_timer = msg.bomb_timer;

  send_lobby(gui_socket);
}

void handle_server_msg(
  const ServerMessageAcceptedPlayer& msg,
  ip::udp::socket& gui_socket
) {
  println("Accepted player:", msg.player.name);
  game_state.players[msg.player_id] = msg.player;
  game_state.scores[msg.player_id] = 0;
  send_lobby(gui_socket);
}

void handle_server_msg(
  [[maybe_unused]]const ServerMessageGameStarted& msg,
  [[maybe_unused]]ip::udp::socket& gui_socket
) {
  println("Game started");
  client_state = ClientState::Playing;
  game_state.players = msg.players;
  for (auto [player_id, player] : msg.players) {
    game_state.scores[player_id] = 0;
  }
}

void handle_server_msg(
  const ServerMessageTurn& msg,
  ip::udp::socket& gui_socket
) {
  println("Turn:", msg.turn);
  for (Bomb& bomb : game_state.bombs) {
    if(bomb.timer) bomb.timer--;
  }

  for (const Event& e : msg.events) {
    std::visit([](auto& x){ handle_event(x); }, e);
  }

  for (auto& [player_id, killed] : game_state.killed) {
    if (killed) { game_state.scores[player_id]++; }
    killed = false; // important: we bind by reference
  }
  
  for (const Position& pos : game_state.blocks_destroyed) {
    auto it = std::find(game_state.blocks.begin(), game_state.blocks.end(), pos);
    if (it == game_state.blocks.end()) { continue; }
    game_state.blocks.erase(it);
  }

  game_state.blocks_destroyed = {};
  game_state.turn = msg.turn;

  std::sort(game_state.explosions.begin(), game_state.explosions.end());
  auto last = std::unique(game_state.explosions.begin(), game_state.explosions.end());
  game_state.explosions.erase(last, game_state.explosions.end());

  send_game(gui_socket);
}

void handle_server_msg(
  [[maybe_unused]]const ServerMessageGameEnded& msg,
  ip::udp::socket& gui_socket
) {
  println("Game ended");
  client_state = ClientState::Lobby;
  game_state.turn = 0;
  game_state.players = {};
  game_state.killed = {};
  game_state.player_positions = {};
  game_state.blocks = {};
  game_state.bombs = {};
  game_state.bombs_positions = {};
  send_lobby(gui_socket);
}

ClientMessagePlaceBomb get_client_action(
  [[maybe_unused]]const InputMessagePlaceBomb& msg
) {
  return ClientMessagePlaceBomb {};
}

ClientMessagePlaceBlock get_client_action(
  [[maybe_unused]]const InputMessagePlaceBlock& msg
) {
  return ClientMessagePlaceBlock {};
}

ClientMessageMove get_client_action(const InputMessageMove& msg) {
  return ClientMessageMove {msg.direction};
}

void handle_gui(ip::tcp::socket& server_socket, ip::udp::socket& gui_socket) {
  std::vector<unsigned char> raw_buffer (MAX_UDP_MESSAGE_SIZE);

  while (client_state != ClientState::Finish) {
    streamable_buffer sbuffer;

    // receive the GUI message
    try {
      size_t received = gui_socket.receive(boost::asio::buffer(raw_buffer));
      sbuffer = streamable_buffer {
        raw_buffer.begin(),
        raw_buffer.begin() + received
      };
    } catch (const boost::system::system_error& e) {
      std::cerr << "UDP read failed\n";
      client_state = ClientState::Finish;
      return;
    }

    // parse the GUI message
    InputMessage msg;
    try {
      sbuffer >> msg;
      if (!sbuffer.empty()) {
        std::cerr << "GUI: Trailing data\n";
        continue;
      }
    } catch (const streamable_buffer::buffer_underflow& e) {
      std::cerr << "GUI: Message incomplete\n";
      continue;
    } catch (const invalid_message& e) {
      std::cerr << "GUI: Message invalid\n";
      continue;
    }

    // handle the message
    ClientMessage response;
    std::visit(
      [&response](const auto& msg) {
        if (client_state == ClientState::Lobby) {
          response = ClientMessageJoin {player_name};
        } else {
          response = get_client_action(msg);
        }
      },
      msg
    );
    sbuffer << response;

    // pass the communicate to the server
    try {
      send(sbuffer, server_socket);
    } catch (const boost::system::system_error& e) {
      std::cerr << "TCP write failed\n";
      client_state = ClientState::Finish;
      return;
    }
  }
}

void handle_server(ip::tcp::socket& server_socket, ip::udp::socket& gui_socket) {
  auto provider = [&server_socket] (size_t n) -> std::vector<uint8_t> {
    return read(server_socket, n);
  };

  streamable_buffer sbuffer;
  sbuffer.set_provider(provider);

  while (client_state != ClientState::Finish) {
    ServerMessage msg;

    try {
      sbuffer >> msg;
    } catch (const invalid_message& e) {
      std::cerr << "Received invalid message from the server!" << std::endl;
      client_state = ClientState::Finish;
      std::exit(1);
    } catch (const boost::system::system_error& e) {
      std::cerr << "TCP read failed!" << std::endl;
      client_state = ClientState::Finish;
      std::exit(1);
    }
    sbuffer.clear();

    std::visit(
      [&gui_socket](auto&& x) { handle_server_msg(x, gui_socket); },
      msg
    );
  }
}

int main(int argc, char* argv[]) {
  po::options_description desc("Options");
  desc.add_options()
    ("help,h", "display help message")
    ("gui-address,d",   po::value<std::string>()->required(), "GUI server address <hostname|IPv4|IPv6[:port]>")
    ("player-name,n",   po::value<std::string>()->required(), "player name")
    ("port,p",          po::value<port_t>()->required(), "port to listen to GUI messages")
    ("server-address,s",po::value<std::string>()->required(), "game server address <hostname|IPv4|IPv6[:port]>")
    ;

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
  } catch (const po::error& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return 0;
  }

  try {
    po::notify(vm);    
  } catch (const po::error& e) {
    std::cerr << e.what() << std::endl; 
    return 1;
  }

  const std::string server_addr = vm["server-address"].as<std::string>();
  const std::string gui_addr = vm["gui-address"].as<std::string>();
  const uint16_t gui_port = vm["port"].as<uint16_t>();
  player_name = vm["player-name"].as<std::string>();

  boost::asio::io_service io_service;
  ip::tcp::socket server_socket(io_service);
  ip::udp::socket gui_socket(
    io_service,
    ip::udp::endpoint {ip::udp::v6(), gui_port}
  );

  try {
    auto server_endpoints = resolve_address<ip::tcp::resolver>(
        server_addr,
        io_service
    );
    gui_endpoints = resolve_address<ip::udp::resolver>(
        gui_addr,
        io_service
    );

    boost::asio::connect(server_socket, server_endpoints);
    server_socket.set_option(ip::tcp::no_delay(true));
    println("TCP connection bound to:", server_socket.remote_endpoint());
  } catch (const addr_resolution_error& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  } catch (const boost::system::system_error& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
 
  std::thread gui_thread {
    handle_gui, std::ref(server_socket), std::ref(gui_socket)
  };
  handle_server(server_socket, gui_socket);
  gui_thread.join();
  
  return 0;
}
