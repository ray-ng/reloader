#pragma once

#include <unistd.h>
#include <stdio.h>
#include <thread>
#include <utility>
#include <chrono>
#include <map>
#include <iomanip>  // std::put_time
#include <ctime>    // std::time_t, struct std::tm, std::localtime
#include <atomic>
#include <type_traits>
#include <mutex>
#include <functional>

namespace reloader {

struct ReloadBase {
  virtual ~ReloadBase() = default;
  virtual bool Reload() = 0;
  virtual void ReloadInfo() = 0;
};

template <typename T, typename std::enable_if<std::is_base_of<ReloadBase, T>{}, int>::type = 0>
class DataLoader {
 public:
  DataLoader() = default;
  void Init(int sleep_time = 7200) {
    std::call_once(init_flag_, std::bind(&DataLoader::Reload, this, std::placeholders::_1), sleep_time);
  }

 private:
  void Loop() {
    int count = 0;
    running_.store(true, std::memory_order_relaxed);
    while (running_.load(std::memory_order_relaxed)) {
      if (count == sleep_time_) {
        count = 0;
        int32_t idx = index_.load(std::memory_order_relaxed);
        T *p = &data_[(idx + 1) % 2];
        p = new (p) T();
        bool success = p->Reload();
        p->ReloadInfo();
        if (success) {
          wcount_[(idx + 1) % 2].store(0, std::memory_order_relaxed);
          index_.store((idx + 1) % 2, std::memory_order_seq_cst);
          int expected = 0;
          while (!wcount_[idx].compare_exchange_weak(expected, -1, std::memory_order_relaxed)) {
          }
          data_[idx].~T();
        } else {
          data_[(idx + 1) % 2].~T();
        }
      }
      sleep(1);
      ++count;
    }
  }
  void Reload(int sleep_time) {
    sleep_time_ = sleep_time;
    for (int i = 0; i < 2; ++i) {
      wcount_[i].store(0, std::memory_order_relaxed);
    }
    index_.store(0, std::memory_order_relaxed);
    data_[index_].Reload();
    thread_ = std::move(std::thread(&DataLoader::Loop, this));
  }

 public:
  struct PointWrapper {
    DataLoader<T> *loader;
    int32_t index;
    const T *operator->() {
      return &loader->data_[index];
    }
    PointWrapper(DataLoader<T> *l, int32_t i) : loader(l), index(i) {}
    ~PointWrapper() {
      loader->wcount_[index].fetch_sub(1, std::memory_order_relaxed);
    }
  };

  PointWrapper GetPoint() {
    while (true) {
      int32_t idx = index_.load(std::memory_order_acquire);
      int32_t expected = wcount_[idx].load(std::memory_order_relaxed);
      if (expected != -1 &&
          wcount_[idx].compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed)) {
        return {this, idx};
      }
    }
  }

  void Stop() {
    running_.store(false, std::memory_order_relaxed);
  }

  ~DataLoader() {
    running_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  DataLoader(const DataLoader &) = delete;
  DataLoader &operator=(const DataLoader &) = delete;

 private:
  std::thread thread_;
  int sleep_time_;
  std::atomic_bool running_;
  std::once_flag init_flag_;

  T data_[2];
  std::atomic_int32_t wcount_[2];
  std::atomic_int32_t index_;
};

}  // namespace reloader
