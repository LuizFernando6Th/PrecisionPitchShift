// PrecisionPitchShift — fixed-capacity realtime-safe ring buffer.
#pragma once

#include <cstddef>
#include <vector>

namespace pps {

class RingBuffer {
public:
    RingBuffer() = default;

    void prepare(std::size_t capacity) {
        data_.assign(capacity > 0 ? capacity : 1, 0.0);
        read_ = write_ = size_ = 0;
    }

    void clear() {
        read_ = write_ = size_ = 0;
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return data_.size(); }
    bool empty() const { return size_ == 0; }

    bool push(double x) {
        if (size_ >= data_.size()) return false;
        data_[write_] = x;
        write_ = (write_ + 1) % data_.size();
        ++size_;
        return true;
    }

    bool pushBlock(const double* src, std::size_t n) {
        if (!src || n > data_.size() - size_) return false;
        for (std::size_t i = 0; i < n; ++i) {
            data_[write_] = src[i];
            write_ = (write_ + 1) % data_.size();
        }
        size_ += n;
        return true;
    }

    double front() const {
        return size_ > 0 ? data_[read_] : 0.0;
    }

    bool pop(double& out) {
        if (size_ == 0) return false;
        out = data_[read_];
        read_ = (read_ + 1) % data_.size();
        --size_;
        return true;
    }

    std::size_t popN(std::size_t n) {
        if (n > size_) n = size_;
        if (data_.empty()) return 0;
        read_ = (read_ + n) % data_.size();
        size_ -= n;
        return n;
    }

    bool copyFrontTo(double* dst, std::size_t n) const {
        if (!dst || n > size_) return false;
        std::size_t idx = read_;
        for (std::size_t i = 0; i < n; ++i) {
            dst[i] = data_[idx];
            idx = (idx + 1) % data_.size();
        }
        return true;
    }

private:
    std::vector<double> data_;
    std::size_t read_ = 0;
    std::size_t write_ = 0;
    std::size_t size_ = 0;
};

} // namespace pps
