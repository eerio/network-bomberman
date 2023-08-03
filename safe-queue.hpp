#ifndef BOMBERMAN_SAFE_QUEUE_HPP
#define BOMBERMAN_SAFE_QUEUE_HPP

#include <deque>
#include <thread>

template <typename T>
class safe_queue {
  const size_t max_size;
  
  std::deque<T> que;
  bool destroying = false;
  std::condition_variable cond_que;
  std::mutex mutex_que;

public:
  safe_queue(size_t max_size) : max_size(max_size) {}

  void interrupt() {
    std::scoped_lock lock {mutex_que};
    destroying = true;
    cond_que.notify_all();
  }

  void push(T t) {
    std::scoped_lock lock {mutex_que};
    if (que.size() == max_size) {
      throw std::runtime_error("Queue full");
    }
    que.push_back(t);
    cond_que.notify_one();
  }

  T pop() {
    std::unique_lock lock {mutex_que};
    if (que.empty()) {
      cond_que.wait(lock,
        [this] { return !que.empty() || destroying; }
      );
    }
    if (destroying) {
      throw std::runtime_error("Queue destroyed");
    }
    T val = que.front();
    que.pop_front();
    return val;
  }

  T try_pop() {
    std::unique_lock lock {mutex_que};
    if (que.empty()) { throw std::runtime_error("Queue empty"); }
    T val = que.front();
    que.pop_front();
    return val;
  }
};

#endif //BOMBERMAN_SAFE_QUEUE_HPP
