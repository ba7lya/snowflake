///
/// @file snowflake.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief
/// @version 0.1
/// @date 2026-05-14
/// @copyright Copyright (c) 2026
///

#pragma once

#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace ba7lya::snowflake {

///
/// @brief 雪花算法
///
enum class algo {
    SHIFT,   // 漂移算法
    ORIGINAL // 传统算法
};

struct options {
    // 雪花计算方法,(1-漂移算法|2-传统算法)
    // 默认1
    algo algo { algo::SHIFT };

    // 基础时间(ms单位), 不能超过当前系统时间
    uint64_t base_time { 1582136402000 };

    // 机器码, 必须由外部设定
    // 最大值 2^worker_id_bit_len-1
    uint32_t worker_id { 0 };

    // 机器码位长, 默认值6
    // 要求：序列数位长+机器码位长不超过22
    // 取值范围 [1, 15]
    uint8_t worker_id_bit_len { 6 };

    // 序列数位长, 默认值6
    // 要求：序列数位长+机器码位长不超过22
    // 取值范围 [3, 21]
    uint8_t seq_bit_len { 6 };

    // 最大序列数(含)
    // 设置范围 [min_seq_num, 2^seq_bit_len-1]
    // 默认值0, 表示最大序列数取最大值(2^seq_bit_len-1])
    uint32_t max_seq_num { 0 };

    // 最小序列数(含)
    // 默认值5, 取值范围 [5, max_seq_num]
    // 每毫秒的前5个序列数对应编号0-4是保留位, 0是手工新值预留位, 其中1-4是时间回拨相应预留位
    uint32_t min_seq_num { 5 };

    // 最大漂移次数(含), 默认2000, 推荐范围 500-20000(与计算能力有关)
    uint32_t top_over_cost_cnt { 2000 };
};

inline static int64_t get_curr_time() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

class worker {
public:
    int64_t get_curr_time_tick() { return get_curr_time() - (int64_t)opts_.base_time; }

    int64_t get_next_time_tick() {
        int64_t time_tick = get_curr_time_tick();
        while (time_tick <= last_time_tick_) {
            std::this_thread::sleep_for(1ms);
            time_tick = get_curr_time_tick();
        }
        return time_tick;
    }

    void end_over_cost_action(int64_t useTimeTick) {
        // if (term_idx_ > 10000) {
        //     term_idx_ = 0;
        // }
    }

    int64_t next_over_cost_id() {
        int64_t curr_time_tick = get_curr_time_tick();
        if (curr_time_tick > last_time_tick_) {
            end_over_cost_action(curr_time_tick);
            last_time_tick_ = curr_time_tick;
            curr_seq_num_ = opts_.min_seq_num;
            is_over_cost_ = false;
            over_cost_cnt_in_one_term_ = 0;
            gen_cnt_in_one_term_ = 0;
            return calc_id();
        }
        if (over_cost_cnt_in_one_term_ > opts_.top_over_cost_cnt) {
            end_over_cost_action(curr_time_tick);
            last_time_tick_ = get_next_time_tick();
            curr_seq_num_ = opts_.min_seq_num;
            is_over_cost_ = false;
            over_cost_cnt_in_one_term_ = 0;
            gen_cnt_in_one_term_ = 0;
            return calc_id();
        }
        if (curr_seq_num_ > opts_.max_seq_num) {
            last_time_tick_++;
            curr_seq_num_ = opts_.min_seq_num;
            is_over_cost_ = true;
            over_cost_cnt_in_one_term_++;
            gen_cnt_in_one_term_++;
            return calc_id();
        }

        gen_cnt_in_one_term_++;
        return calc_id();
    }

    int64_t next_normal_id() {
        int64_t curr_time_tick = get_curr_time_tick();
        if (curr_time_tick < last_time_tick_) {
            if (turn_back_time_tick_ < 1) {
                turn_back_time_tick_ = last_time_tick_ - 1;
                turn_back_idx_++;
                // 每毫秒序列数的前 5 位是预留位, 0 用于手工新值, 1-4 是时间回拨次序
                // 支持 4 次回拨次序(避免回拨重叠导致 ID 重复), 可无限次回拨(次序循环使用)。
                if (turn_back_idx_ > 4) { turn_back_idx_ = 1; }
            }

            // std::this_thread::sleep_for(std::chrono::milliseconds(1));;  // 暂停1ms
            return CalcTurnBackId();
        }

        if (turn_back_time_tick_ > 0) { turn_back_time_tick_ = 0; }

        if (curr_time_tick > last_time_tick_) {
            last_time_tick_ = curr_time_tick;
            curr_seq_num_ = opts_.min_seq_num;
            return calc_id();
        }

        if (curr_seq_num_ > opts_.max_seq_num) {
            term_idx_++;
            last_time_tick_++;
            curr_seq_num_ = opts_.min_seq_num;
            is_over_cost_ = true;
            over_cost_cnt_in_one_term_ = 1;
            gen_cnt_in_one_term_ = 1;
            return calc_id();
        }

        return calc_id();
    }

    int64_t calc_id() {
        uint64_t result = (last_time_tick_ << ts_shift_) | (opts_.worker_id << opts_.seq_bit_len)
                        | (curr_seq_num_);
        curr_seq_num_++;
        return result;
    }

    int64_t CalcTurnBackId() {
        uint64_t result = (turn_back_time_tick_ << ts_shift_)
                        | (opts_.worker_id << opts_.seq_bit_len) | (turn_back_idx_);
        turn_back_time_tick_--;
        return result;
    }

    int64_t WorkerM1NextId() {
        std::lock_guard<std::mutex> lock(mtx_);
        int64_t id = is_over_cost_ ? next_over_cost_id() : next_normal_id();
        return id;
    }

    int64_t WorkerM2NextId() {
        std::lock_guard<std::mutex> lock(mtx_);

        int64_t curr_time_tick = get_curr_time_tick();
        if (last_time_tick_ == curr_time_tick) {
            curr_seq_num_ = (++curr_seq_num_) & opts_.max_seq_num;
            if (curr_seq_num_ == 0) { curr_time_tick = get_next_time_tick(); }
        }
        else { curr_seq_num_ = opts_.min_seq_num; }

        last_time_tick_ = curr_time_tick;
        int64_t id = (int64_t)((curr_time_tick << ts_shift_)
                               | (opts_.worker_id << opts_.seq_bit_len) | curr_seq_num_);

        return id;
    }

    void set_options(options options) {
        // 1.base_time
        if (options.base_time == 0) { opts_.base_time = 1582136402000; }
        else if (options.base_time < 631123200000 || (int64_t)options.base_time > get_curr_time()) {
            throw std::invalid_argument("base_time error.");
        }
        else { opts_.base_time = options.base_time; }

        // 2.worker_id_bit_len
        if (options.worker_id_bit_len <= 0) {
            throw std::invalid_argument("worker_id_bit_len error.(range:[1, 21])");
        }
        if (options.seq_bit_len + options.worker_id_bit_len > 22) {
            throw std::invalid_argument("error: worker_id_bit_len + seq_bit_len <= 22");
        }
        else {
            // worker_id_bit_len = options.worker_id_bit_len;
            opts_.worker_id_bit_len
                = options.worker_id_bit_len <= 0 ? 6 : options.worker_id_bit_len;
        }

        // 3.worker_id
        uint32_t max_worker_id_num = (1 << options.worker_id_bit_len) - 1;
        if (max_worker_id_num == 0) { max_worker_id_num = 63; }
        if (options.worker_id > max_worker_id_num) {
            throw std::invalid_argument(
                "worker_id error. (range:[0, {2^options.worker_id_bit_len-1]}"
            );
        }
        else { opts_.worker_id = options.worker_id; }

        // 4.seq_bit_len
        if (options.seq_bit_len < 2 || options.seq_bit_len > 21) {
            throw std::invalid_argument("seq_bit_len error. (range:[2, 21])");
        }
        else { opts_.seq_bit_len = options.seq_bit_len <= 0 ? 6 : options.seq_bit_len; }

        // 5.max_seq_num
        uint32_t max_seq_num = (1 << options.seq_bit_len) - 1;
        if (max_seq_num == 0) { max_seq_num = 63; }
        if (options.max_seq_num > max_seq_num) {
            throw std::invalid_argument("max_seq_num error. (range:[1, {2^options.seq_bit_len-1}]");
        }
        else { max_seq_num = options.max_seq_num <= 0 ? max_seq_num : options.max_seq_num; }

        // 6.min_seq_num
        if (options.min_seq_num < 5 || options.min_seq_num > max_seq_num) {
            throw std::invalid_argument("min_seq_num error. (range:[5, {options.min_seq_num}]");
        }
        else { opts_.min_seq_num = options.min_seq_num <= 0 ? 5 : options.min_seq_num; }

        // 7.top_over_cost_cnt
        if (options.top_over_cost_cnt > 10000) {
            throw std::invalid_argument("top_over_cost_cnt error. (range:[0, 10000]");
        }
        else {
            // top_over_cost_cnt = options.top_over_cost_cnt <= 0 ? 2000 :
            //  options.top_over_cost_cnt;
            opts_.top_over_cost_cnt = options.top_over_cost_cnt;
        }

        // 8.Others
        ts_shift_ = opts_.worker_id_bit_len + opts_.seq_bit_len;
        curr_seq_num_ = opts_.min_seq_num;

        opts_.algo = options.algo;
        if (options.algo == algo::ORIGINAL) { this->next_id_ = &worker::WorkerM2NextId; }
        else {
            this->next_id_ = &worker::WorkerM1NextId;
            std::this_thread::sleep_for(500ms);
        }
    }

    inline int64_t next_id() { return (this->*next_id_)(); }

protected:
    std::mutex mtx_;
    options opts_;
    int64_t (worker::*next_id_)();

    uint8_t ts_shift_ { 0 };
    uint32_t curr_seq_num_ { 0 };
    int64_t last_time_tick_ { 0 };
    int64_t turn_back_time_tick_ { 0 };
    uint8_t turn_back_idx_ { 0 };
    bool is_over_cost_ { false };
    uint32_t over_cost_cnt_in_one_term_ { 0 };
    uint32_t gen_cnt_in_one_term_ { 0 };
    uint32_t term_idx_ { 0 };
};

class generator {
public:
    // 禁止拷贝构造函数和赋值操作符
    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;

    static void create_instance(uint32_t worker_id) {
        options options;
        options.worker_id = worker_id;
        create_instance(options);
    }

    static void create_instance(options options) {
        static std::once_flag flag;
        std::call_once(flag, [options]() { createInstance(options); });
    }

    static int64_t next_id() {
        assert(inst_ && "Please call create_instance first to create an instance");
        return inst_->next_id();
    }

private:
    generator() {}

    static void createInstance(options options) {
        static generator obj;
        inst_ = &obj;
        inst_->worker_.set_options(options);
    }

private:
    worker worker_;
    static generator* inst_;
};

generator* generator::inst_ = nullptr;

} // namespace ba7lya::snowflake
