#pragma once

#include <stdio.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <ctime>  // std::time_t, struct std::tm, std::localtime
#include <functional>
#include <iomanip>  // std::put_time
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

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
        data_[(idx + 1) % 2].reset(new T());
        bool success = data_[(idx + 1) % 2]->Reload();
        data_[(idx + 1) % 2]->ReloadInfo();
        if (success) {
          wcount_[(idx + 1) % 2].store(0, std::memory_order_relaxed);
          index_.store((idx + 1) % 2, std::memory_order_acq_rel);
          int expected = 0;
          while (!wcount_[idx].compare_exchange_weak(expected, -1, std::memory_order_relaxed)) {
          }
          data_[idx].reset();
        } else {
          data_[(idx + 1) % 2].reset();
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
    data_[index_].reset(new T());
    data_[index_]->Reload();
    data_[index_]->ReloadInfo();
    thread_ = std::move(std::thread(&DataLoader::Loop, this));
  }

 public:
  const std::shared_ptr<T> GetPoint() {
    while (true) {
      int32_t idx = index_.load(std::memory_order_acquire);
      int32_t expected = wcount_[idx].load(std::memory_order_relaxed);
      if (expected != -1 &&
          wcount_[idx].compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed)) {
        const std::shared_ptr<T> temp = data_[idx];
        wcount_[idx].fetch_sub(1, std::memory_order_relaxed);
        return temp;
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

  std::shared_ptr<T> data_[2];
  std::atomic_int32_t wcount_[2];
  std::atomic_int32_t index_;
};

}  // namespace reloader
