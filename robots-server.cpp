#include <chrono>
#include <iostream>
#include <functional>
#include <random>
#include <thread>

#include <boost/asio.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/program_options.hpp>

#include "common.hpp"
#include "debug.hpp"
#include "messages.hpp"
#include "streamable-buffer.hpp"
#include "serialization.hpp"
#include "safe-queue.hpp"

namespace po = boost::program_options;

namespace ip = boost::asio::ip;
using ip::tcp;
using ip::udp;

using seed_t = uint32_t;

std::ostream& operator<<(std::ostream& os, unsigned char c) {
  return os << +c;
}

struct ServerParams {
  bomb_timer_t bomb_timer;
  players_count_t players_count;
  turn_duration_t turn_duration;
  explosion_radius_t explosion_radius;
  initial_blocks_t initial_blocks;
  game_length_t game_length;
  server_name_t server_name;
  pos_t size_x;
  pos_t size_y;
};

class Server {
  // max 25 tcp connections
  static constexpr size_t max_clients = 25;
  // max 100 unreceived messages
  static constexpr size_t max_queue_size = 100;
  const ServerParams params;
  const port_t port;
  std::minstd_rand random;

  struct ClientInfo {
    std::shared_ptr<tcp::socket> sock;
    ClientMessage last_msg;
    std::optional<player_id_t> player_id;
  };

  std::map<tcp::endpoint, ClientInfo> clients;
  std::mutex mutex_clients;

  struct PlayerInfo {
    std::string name;
    Position pos;
    ClientMessage msg;
    Player player;

    friend std::ostream& operator<<(std::ostream& os, PlayerInfo x) {
      return os << x.name << x.pos;
    }
  };

  std::condition_variable cond_players;
  std::mutex mutex_players;
  std::map<player_id_t, PlayerInfo> players;
  
  enum class State { Lobby, Maintenance, Playing };
  std::atomic<State> state;

  boost::asio::io_service io_service;

  std::vector<ServerMessageTurn> turns;
  std::mutex mutex_turns;

  std::vector<Event> turn_events;
  
  const ServerMessageHello hello = ServerMessageHello {
    .server_name      = params.server_name,
    .players_count    = params.players_count,
    .size_x           = params.size_x,
    .size_y           = params.size_y,
    .game_length      = params.game_length,
    .explosion_radius = params.explosion_radius,
    .bomb_timer       = params.bomb_timer
  };

  void client_connected(std::shared_ptr<tcp::socket> sock) {
    ip::tcp::endpoint client_endpoint = sock->remote_endpoint();
    println("Connected:", client_endpoint);

    std::scoped_lock lock {mutex_clients};
    assert(clients.size() < max_clients);
    clients[client_endpoint].sock = sock;
  }

  void client_disconnected(ip::tcp::endpoint client_endpoint) {
    println("Disconnected:", client_endpoint);
    {
      std::scoped_lock lock {mutex_clients};
      auto it = clients.find(client_endpoint);
      assert(it != clients.end());
      try {
        it->second.sock->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
      } catch (const boost::system::system_error& e) {}
      clients.erase(it);
    }

    // if the client has already been accepted, forget them
    // std::scoped_lock lock {mutex_players};
    // auto it = players.find(client_endpoint);
    // if (it != players.end()) {
    //   players.erase(it);
    // }
    // cond_players.notify_one();
  }

  void await_players() {
    std::unique_lock lock {mutex_players};
    cond_players.wait(lock,
      [this] { return players.size() == params.players_count; }
    );
  }

  void init_game() {
    println("Generating new board...");
    std::scoped_lock lock {mutex_players};
    
    for (auto& [player_id, player] : players) {
      player.pos = {
        .x = static_cast<pos_t>(random() % params.size_x),
        .y = static_cast<pos_t>(random() % params.size_y)
      };
      turn_events.push_back(EventPlayerMoved {.player_id = player_id, .position = player.pos});
    }

    for (size_t i=0; i < params.initial_blocks; ++i) {
      Position pos = {
        .x = static_cast<pos_t>(random() % params.size_x),
        .y = static_cast<pos_t>(random() % params.size_y)
      };
      turn_events.push_back(EventBlockPlaced {.position = pos});
    }

    cond_players.notify_one();
  }

  void broadcast_message(ServerMessage&& msg) {
    std::scoped_lock lock {mutex_clients};
    streamable_buffer sbuffer;
    for (auto& [key, client] : clients) {
      // std::visit(
      //   [&sbuffer] (auto&&x) { sbuffer << x; },
      //   msg
      // );
      sbuffer << msg;
      try {
        send(sbuffer, *client.sock);
      } catch (const boost::system::system_error& e) {
        println("Error writing to client!");
      }
      assert(sbuffer.empty());
    }
  }

  void broadcast_turn(turn_t turn) {
    println("Broadcasting current state for turn:", turn);
    std::scoped_lock lock {mutex_turns};
    auto msg = ServerMessageTurn {
      .turn = turn,
      .events = turn_events
    };
    turns.push_back(msg);
    broadcast_message(msg);
    turn_events.clear();
  }

  std::optional<Event> get_event([[maybe_unused]]player_id_t player_id, [[maybe_unused]]ClientMessageJoin msg) {
    return {};
  }

  std::optional<Event> get_event([[maybe_unused]]player_id_t player_id, [[maybe_unused]]ClientMessagePlaceBlock msg) {
    return {};
  }

  std::optional<Event> get_event([[maybe_unused]]player_id_t player_id, [[maybe_unused]]ClientMessagePlaceBomb msg) {
    return {};
  }

  std::optional<Event> get_event([[maybe_unused]]player_id_t player_id, [[maybe_unused]]ClientMessageMove msg) {
    PlayerInfo& player = players[player_id];
    Position new_pos = player.pos;
    if (player.pos.x > 0 && msg.direction == 3) { new_pos.x--; }
    else if (player.pos.x < params.size_x - 1 && msg.direction == 1) { new_pos.x++; }
    else if (player.pos.y < params.size_y - 1 && msg.direction == 0) { new_pos.y++; }
    else if (player.pos.y > 0 && msg.direction == 2) { new_pos.y--; }
    
    return EventPlayerMoved { .player_id = player_id, .position = new_pos };
  }

  void apply_player_moves() {
    std::scoped_lock lock {mutex_turns, mutex_players};
    for (auto elt : players) {
      player_id_t player_id = elt.first;
      PlayerInfo& player = elt.second;
      ClientMessage msg = player.msg;
      std::optional<Event> event = std::visit(
        [this, player_id] (auto x) { return get_event(player_id, x); },
        msg
      );
      if (event) { turn_events.push_back(*event); }
    }
  }

  void finish_game() {
    println("Cleaning up...");
    {
      std::scoped_lock lock {mutex_clients};
      for (auto& [_, client] : clients) {
        client.player_id = std::nullopt;
      }
    }
    {
      std::scoped_lock lock {mutex_players};
      players = {};
      cond_players.notify_one();
    }

    broadcast_message(ServerMessageGameEnded {});
    println("Broadcasting GameEnded finished!");
  }

  void send_past_turns(tcp::endpoint client_endpoint) {
    // lock so that broadcast doesn't send anything
    //std::scoped_lock lock {mutex_clients, mutex_turns};
    println("Ktos sie spoznil - wyslij stare kolejki");
    return;
    
    auto it = clients.find(client_endpoint);
    assert(it != clients.end());
    ClientInfo& client = it->second;

    streamable_buffer sbuffer;
    for (ServerMessageTurn turn : turns) {
      sbuffer << turn; 
      try {
        send(sbuffer, *client.sock);
      } catch (const boost::system::system_error& e) {
        println("Error writing to client!");
      }
      assert(sbuffer.empty());
    }
  }

  void send_players(tcp::endpoint client_endpoint) {
    std::scoped_lock lock {mutex_players};
    streamable_buffer sbuffer;
    for (auto [player_id, player] : players) {
      sbuffer << ServerMessageAcceptedPlayer {
        .player_id = player_id,
        .player = player.player
      };
      try {
        auto it = clients.find(client_endpoint);
        assert(it != clients.end());
        ClientInfo& client = it->second;
        send(sbuffer, *client.sock);
      } catch (const boost::system::system_error& e) {
        println("Error writing to client!");
      }
      assert(sbuffer.empty());
    }
  }

  void handle_client_msg(tcp::endpoint client_endpoint, const ClientMessageJoin& msg) {
    println("Client wants to join");
    if (state != State::Lobby) {
      send_players(client_endpoint);
      send_past_turns(client_endpoint);
      return;
    }
    {
      std::scoped_lock lock {mutex_clients};
      auto it = clients.find(client_endpoint);
      if (it == clients.end()) { return; }
      if (it->second.player_id) { return; }
    }

    player_id_t player_id;
    Player player;
    {
      std::scoped_lock lock {mutex_players};
      player_id = static_cast<player_id_t>(players.size());
      std::stringstream ss;
      ss << client_endpoint;
      std::string addr = ss.str();

      player = Player { .name = msg.name, .address = addr };

      auto [_, inserted] = players.insert({
        player_id,
        PlayerInfo {
          .name = msg.name,
          .pos = {},
          .msg = {},
          .player = player
        }
      });
      if (!inserted) { return; }
    }
    {
      std::scoped_lock lock {mutex_clients};
      clients[client_endpoint].player_id = player_id;
    }
    cond_players.notify_one();

    println("Client joins:", client_endpoint);

    broadcast_message(
      ServerMessageAcceptedPlayer {
        .player_id = player_id,
        .player = player
      }
    );
    println("Current players:", players);
  }

  std::optional<player_id_t> get_player_id(tcp::endpoint client_endpoint) {
    std::scoped_lock lock {mutex_clients};
    return clients[client_endpoint].player_id;
  }

  void set_input(tcp::endpoint client_endpoint, const ClientMessage& msg) {
    if (state != State::Playing) { return; }
    auto player_id = get_player_id(client_endpoint);
    if (!player_id) { return; };
    std::scoped_lock lock {mutex_players};
    players[*player_id].msg = msg;
  }

  void handle_client_msg(tcp::endpoint client_endpoint, const ClientMessagePlaceBomb& msg) {
    println("Client wants to place a bomb");
    set_input(client_endpoint, msg);
  }

  void handle_client_msg(tcp::endpoint client_endpoint, const ClientMessagePlaceBlock& msg) {
    println("Client wants to place a block!!");
    set_input(client_endpoint, msg);
  }

  void handle_client_msg(tcp::endpoint client_endpoint, const ClientMessageMove& msg) {
    println("Client wants to move to:", msg.direction);
    set_input(client_endpoint, msg);
  }

public:
  Server(ServerParams params, port_t port, seed_t seed)
    : params(params),
      port(port),
      random(seed)
    {}

  void accept_clients() {
    tcp::acceptor a (io_service, tcp::endpoint(tcp::v6(), port));

    while (true) {
      // println("Waiting for client...");
      std::shared_ptr<tcp::socket> sock {new tcp::socket(io_service)};
      a.accept(*sock);
      sock->set_option(ip::tcp::no_delay(true));
      std::thread handler {
        [this, sock] { this->handle_session(sock); }
      };
      handler.detach();
    }
  }

  void start() {
    std::thread acceptor {[this] { accept_clients(); }};

    std::chrono::duration<turn_duration_t, std::milli> turn_duration {params.turn_duration};
    while (true) {
      println("Lobby.");
      state = State::Lobby;
      await_players();
      init_game();
      broadcast_message(ServerMessageGameStarted {});
      for (game_length_t turn = 0; turn < params.game_length; ++turn) {
        broadcast_turn(turn);
        state = State::Playing;
        std::this_thread::sleep_for(turn_duration);
        apply_player_moves();
        println("End of turn:", turn);
      }
      finish_game();
      println("End of game!");
    }

    acceptor.join();
  }

  void handle_session(std::shared_ptr<tcp::socket> sock) {
    {
      // we do so without getting mutex - because the mutex we use for the
      // whole map and here we modify something that only this thread accesses
      // also, do it before client_connected, because afterwise writes may
      // occur to this socket in another thread
      // println("send hello. game length:", hello.game_length);
      streamable_buffer sbuffer;
      sbuffer << hello;
      try {
        send(sbuffer, *sock);
      } catch (const boost::system::system_error& e) {
        std::cerr << "Error: unable to send hello" << std::endl;
        return;
      }
    }

    ip::tcp::endpoint client_endpoint;

    try {
      client_endpoint = sock->remote_endpoint();
      client_connected(sock);
    } catch (const boost::system::system_error& e) {
      std::cerr << "Error: unable to connect client" << std::endl;
      return;
    }

    streamable_buffer sbuffer;
    sbuffer.set_provider([sock](size_t n){ return read(*sock, n); });

    while (true) {
      ClientMessage msg;
      try {
        sbuffer >> msg;
      } catch (const boost::system::system_error& e) {
        // if (e.code() == boost::asio::error::eof) {
        std::cerr << "Error: unable to read from client" << std::endl;
        client_disconnected(client_endpoint);
        return;
      } catch (const invalid_message& e) {
        std::cerr << "Error: invalid message from client" << std::endl;
        client_disconnected(client_endpoint);
        return;
      }
      // println("Read input from:", client_endpoint);

      if (std::holds_alternative<ClientMessageMove>(msg)) {
        if (std::get<ClientMessageMove>(msg).direction > 3) {
          std::cerr << "Client: Invalid direction value" << std::endl;
          client_disconnected(client_endpoint);
          return;
        }
      }

      std::visit(
        [this, client_endpoint] (auto&& x) {
          handle_client_msg(client_endpoint, x);
        },
        msg
      );
    }
  }
};

int main(int argc, char* argv[]) {
  po::options_description desc("Options");

  static_assert(sizeof(uint32_t) >= sizeof(players_count_t));
  desc.add_options()
    ("help,h", "display help message")
    ("bomb-timer,b", po::value<bomb_timer_t>()->required(), "bomb timer")
    // reading uint8_t with std::cin is tricky, read something wider
    ("players-count,c", po::value<uint32_t>()->required(), "players count")
    ("turn-duration,d", po::value<turn_duration_t>()->required(), "turn duration")
    ("explosion-radius,e", po::value<explosion_radius_t>()->required(), "explosion radius")
    ("initial-blocks,k", po::value<initial_blocks_t>()->required(), "initial blocks")
    ("game-length,l", po::value<game_length_t>()->required(), "game-length")
    ("server-name,n", po::value<std::string>()->required(), "server name")
    ("port,p", po::value<port_t>()->required(), "port")
    (
      "seed,s",
      po::value<seed_t>()->default_value(
        static_cast<seed_t>(std::chrono::system_clock::now().time_since_epoch().count())
      ),
      "seed"
    )
    ("size-x,x", po::value<pos_t>()->required(), "size x")
    ("size-y,y", po::value<pos_t>()->required(), "size y")
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

  uint32_t raw_n_players = vm["players-count"].as<uint32_t>();
  if (raw_n_players > std::numeric_limits<players_count_t>::max()) {
    std::cerr << "Too many players required! Should be 0-255" << std::endl;
    return 1;
  }

  ServerParams params {
    .bomb_timer = vm["bomb-timer"].as<bomb_timer_t>(),
    .players_count = static_cast<players_count_t>(raw_n_players),
    .turn_duration = vm["turn-duration"].as<turn_duration_t>(),
    .explosion_radius = vm["explosion-radius"].as<explosion_radius_t>(),
    .initial_blocks = vm["initial-blocks"].as<initial_blocks_t>(),
    .game_length = vm["game-length"].as<game_length_t>(),
    .server_name = vm["server-name"].as<server_name_t>(),
    .size_x = vm["size-x"].as<pos_t>(),
    .size_y = vm["size-y"].as<pos_t>()
  };

  port_t port = vm["port"].as<port_t>();
  seed_t seed = vm["seed"].as<seed_t>();

  Server server (params, port, seed);
  server.start();

  return 0;
}

