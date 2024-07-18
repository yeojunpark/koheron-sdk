/// FFT driver
///
/// (c) Koheron

#ifndef __DRIVERS_FFT_HPP__
#define __DRIVERS_FFT_HPP__

#include <context.hpp>

#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <cmath>
#include <limits>
#include <array>

#include <scicpp/core.hpp>
#include <scicpp/signal.hpp>

#include <boards/alpha15/drivers/clock-generator.hpp>

namespace {
    namespace sci = scicpp;
    namespace win = scicpp::signal::windows;
}

class FFT
{
  public:
    FFT(Context& ctx_)
    : ctx(ctx_)
    , ctl(ctx.mm.get<mem::control>())
    , sts(ctx.mm.get<mem::status>())
    , ps_ctl(ctx.mm.get<mem::ps_control>())
    , ps_sts(ctx.mm.get<mem::ps_status>())
    , psd_map(ctx.mm.get<mem::psd>())
    , demod_map(ctx.mm.get<mem::demod>())
    , clk_gen(ctx.get<ClockGenerator>())
    {
        set_offsets(0, 0);
        select_adc_channel(0);
        set_operation(0);
        set_scale_sch(0);
        set_fft_window(1);
        start_psd_acquisition();
    }

    //////////////////////////////////////
    // Power Spectral Density
    //////////////////////////////////////

    void set_offsets(uint32_t off0, uint32_t off1) {
        ctl.write<reg::channel_offset0>(off0);
        ctl.write<reg::channel_offset1>(off1);
    }

    void select_adc_channel(uint32_t channel) {
        ctx.log<INFO>("FFT: Select channel %u", channel);

        if (channel == 0) {
            ctl.clear_bit<reg::channel_select, 0>();
            ctl.set_bit<reg::channel_select, 1>();
        } else if (channel == 1) {
            ctl.set_bit<reg::channel_select, 0>();
            ctl.clear_bit<reg::channel_select, 1>();
        } else if (channel == 2) { // Diff or sum of channels
            ctl.set_bit<reg::channel_select, 0>();
            ctl.set_bit<reg::channel_select, 1>();
        } else {
            ctx.log<ERROR>("FFT: Invalid input channel");
            return;
        }

        input_channel = channel;
    }

    void set_operation(uint32_t operation) {
        // operation:
        // 0 : Substration
        // 1 : Addition

        ctx.log<INFO>("FFT: Select operation %u", operation);

        operation == 0 ? ctl.clear_bit<reg::channel_select, 2>()
                       : ctl.set_bit<reg::channel_select, 2>();
        input_operation = operation;
    }

    void set_scale_sch(uint32_t scale_sch) {
        // LSB at 1 for forward FFT
        ps_ctl.write<reg::ctl_fft>(1 + (scale_sch << 1));
    }

    void set_fft_window(uint32_t window_id) {
        switch (window_id) {
          case 0:
            window = win::boxcar<double, prm::fft_size>();
            break;
          case 1:
            window = win::hann<double, prm::fft_size>();
            break;
          case 2:
            window = win::flattop<double, prm::fft_size>();
            break;
          case 3:
            window = win::blackmanharris<double, prm::fft_size>();
            break;
          default:
            ctx.log<ERROR>("FFT: Invalid window index\n");
            return;
        }

        set_window_buffer();
        window_index = window_id;
    }

    // Read averaged spectrum data
    auto read_psd_raw() {
        std::lock_guard<std::mutex> lock(mutex);
        return psd_buffer_raw;
    }

    // Return the PSD in W/Hz
    auto read_psd() {
        std::lock_guard<std::mutex> lock(mutex);
        return psd_buffer;
    }

    uint32_t get_number_averages() const {
        return prm::n_cycles;
    }

    uint32_t get_fft_size() const {
        return prm::fft_size;
    }

    auto get_window_index() const {
        return window_index;
    }

    auto get_control_parameters() {
        return std::tuple{fs_adc, input_channel, input_operation, W1, W2};
    }

 private:
    Context& ctx;
    Memory<mem::control>& ctl;
    Memory<mem::status>& sts;
    Memory<mem::ps_control>& ps_ctl;
    Memory<mem::ps_status>& ps_sts;
    Memory<mem::psd>& psd_map;
    Memory<mem::demod>& demod_map;
    ClockGenerator& clk_gen;

    double fs_adc; // ADC sampling rate (Hz)
    std::array<float, 2> calibration; // Conversion to V/Hz

    std::array<double, prm::fft_size> window;
    double W1, W2; // Window correction factors
    uint32_t window_index;

    uint32_t input_channel = 0;
    uint32_t input_operation = 0;

    std::array<float, prm::fft_size/2> psd_buffer;
    std::array<float, prm::fft_size/2> psd_buffer_raw;
    std::thread psd_thread;
    std::mutex mutex;
    std::atomic<bool> psd_acquisition_started{false};
    std::atomic<uint32_t> acq_cycle_index{0};
    void psd_acquisition_thread();
    void start_psd_acquisition();

    // Vectors to convert PSD raw data into V²/Hz
    void set_calibs() {
        fs_adc = clk_gen.get_adc_sampling_freq()[0];

        std::array<double, 2> vin = {2.048, 2.048}; // TODO Update with actual ADC range + Use calibration

        calibration[0] =  (vin[0] / (2 << 21)) * (vin[0] / (2 << 21)) / prm::n_cycles / fs_adc / W2;
        calibration[1] =  (vin[1] / (2 << 21)) * (vin[1] / (2 << 21)) / prm::n_cycles / fs_adc / W2;
    }

    void set_window_buffer() {
        demod_map.write_array(sci::map([](auto w){
            return uint32_t(((int32_t(32768 * w) + 32768) % 65536) + 32768);
        }, window));

        W1 = win::s1(window) / prm::fft_size / prm::fft_size;
        W2 = win::s2(window) / prm::fft_size;
        set_calibs();
    }

    uint32_t get_cycle_index() {
        return ps_sts.read<reg::cycle_index>();
    }
}; // class FFT

inline void FFT::start_psd_acquisition() {
    if (! psd_acquisition_started) {
        psd_buffer.fill(0);
        psd_thread = std::thread{&FFT::psd_acquisition_thread, this};
        psd_thread.detach();
    }
}

inline void FFT::psd_acquisition_thread() {
    using namespace std::chrono_literals;

    psd_acquisition_started = true;

    while (psd_acquisition_started) {
        uint32_t cycle_index = get_cycle_index();
        uint32_t previous_cycle_index = cycle_index;

        // Wait for data
        while (cycle_index >= previous_cycle_index) {
            // 1/15 MHz = 66.7 ns
            const auto acq_period = std::chrono::nanoseconds(int32_t(std::ceil(1E9 / fs_adc)));
            const auto sleep_time = (prm::n_cycles - cycle_index) * prm::fft_size * acq_period;
            // const auto sleep_time = std::chrono::nanoseconds((prm::n_cycles - cycle_index) * prm::fft_size * 67);

            if (sleep_time > 1ms) {
                std::this_thread::sleep_for(sleep_time);
            }

            previous_cycle_index = cycle_index;
            cycle_index = get_cycle_index();
        }

        {
            using namespace sci::operators;

            std::lock_guard<std::mutex> lock(mutex);
            psd_buffer_raw = psd_map.read_array<float, prm::fft_size/2, 0>();
            psd_buffer = psd_buffer_raw * calibration[input_channel];
        }

        acq_cycle_index = get_cycle_index();
    }
}

#endif // __DRIVERS_FFT_HPP__