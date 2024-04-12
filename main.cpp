#include <chrono>
#include <coroutine>
#include <deque>
#include <iostream>
#include <thread>

struct Executor {
  void spawn(std::coroutine_handle<> coro) { m_readyQueue.push_back(coro); }

  void block() {
    while (true) {
      if (!m_readyQueue.empty()) {
        auto coro = m_readyQueue.front();
        m_readyQueue.pop_front();
        coro.resume();
      }
    }
  }

  std::deque<std::coroutine_handle<>> m_readyQueue;
};

static Executor executor;

// Return object for a async task returning void
struct AsyncVoidTask {
  struct promise_type {
    AsyncVoidTask get_return_object() {
      return {.m_handle =
                  std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { std::terminate(); }
    // When the coroutine is finished (or returns) we have to resume the parent
    // coroutine, thus spawn it on the executor
    void return_void() noexcept {
      if (m_parent) {
        executor.spawn(m_parent);
      }
    }

    std::coroutine_handle<> m_parent;
  };

  auto operator co_await() {
    // As the `VoidTask` is lazy, we have to explicitely resume it to the next
    // suspension point when it is co_awaited
    m_handle.resume();

    struct Awaiter {
      // return `false`, so we always suspend at the reached suspension point
      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> parent) noexcept {
        m_task.m_handle.promise().m_parent = parent;
      }
      void await_resume() const noexcept {}

      AsyncVoidTask &m_task;
    };
    return Awaiter{.m_task = *this};
  }

  std::coroutine_handle<promise_type> m_handle;

  // Implicitely convert the coroutines return object to a `coroutine_handle`
  operator std::coroutine_handle<promise_type>() const { return m_handle; }
  operator std::coroutine_handle<>() const { return m_handle; }
};

// An asnychronous timer serving as an example for an async `leaf` task
class AsyncTimer {
public:
  AsyncTimer(int seconds) : m_duration{seconds} {}

  auto operator co_await() {
    struct Awaiter {

      // return `false`, so we always suspend and schedule the timer in
      // `await_suspend`
      bool await_ready() const noexcept { return false; }

      // called on first suspend, when `await_ready` returns false there we
      // start the timer on a different thread and resume the awaiter when it
      // has finished
      void await_suspend(std::coroutine_handle<> awaiter) noexcept {
        std::cout << "Scheduling an AsyncTimer(" << this << ") for "
                  << m_duration << " seconds" << std::endl;
        std::jthread timer_thread{[awaiter, duration = m_duration] {
          std::this_thread::sleep_for(std::chrono::seconds{duration});
          executor.spawn(awaiter);
        }};
        timer_thread.detach();
      }

      void await_resume() const noexcept {
        std::cout << "AsyncTimer(" << this << ") finished after " << m_duration
                  << " seconds!" << std::endl;
      }
      int m_duration;
    };
    return Awaiter{.m_duration = m_duration};
  }

private:
  int m_duration;
};

AsyncVoidTask inner() {
  std::cout << "Inner starts\n";
  co_await AsyncTimer(3);
  std::cout << "Inner finished\n";
}

AsyncVoidTask outter() {
  std::cout << "Outter starts\n";
  co_await inner();
  std::cout << "Now we schedule another timer\n";
  co_await AsyncTimer{2};
  std::cout << "Now we schedule another timer\n";
  co_await AsyncTimer{4};
  std::cout << "Now we schedule another timer\n";
  co_await AsyncTimer{1};
  std::cout << "Outter finished\n";
}
AsyncVoidTask anotherCoro() {
  std::cout << "Another coro starts\n";
  co_await AsyncTimer{7};
  std::cout << "Another coro finishes!!!!\n";
}

int main() {
  std::jthread timerThread{[] {
    // delay the timer for 10ms, so we get nicer outputs, as two threads are
    // trying to write to cout at more or less the same time
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    for (int i = 0;; ++i) {
      std::cout << "----->" << i << "sec\n";
      std::this_thread::sleep_for(std::chrono::seconds{1});
    }
  }};
  executor.spawn(outter());
  executor.spawn(anotherCoro());

  executor.block();

  return 0;
}