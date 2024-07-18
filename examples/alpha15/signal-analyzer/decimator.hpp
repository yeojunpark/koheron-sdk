/// Pulse driver
///
/// (c) Koheron

#ifndef __DRIVERS_DECIMATOR_HPP__
#define __DRIVERS_DECIMATOR_HPP__

#include <context.hpp>

#include <array>
#include <atomic>
#include <thread>
#include <mutex>

#include <scicpp/core.hpp>
#include <scicpp/signal.hpp>

#include <server/drivers/fifo.hpp>
#include <boards/alpha15/drivers/clock-generator.hpp>

namespace {
    namespace sci = scicpp;
    namespace sig = scicpp::signal;
    namespace win = scicpp::signal::windows;
}

class Decimator
{
  public:
    Decimator(Context& ctx_)
    : ctx(ctx_)
    , ctl(ctx.mm.get<mem::control>())
    , sts(ctx.mm.get<mem::status>())
    , ps_ctl(ctx.mm.get<mem::ps_control>())
    , fifo(ctx)
    , clk_gen(ctx.get<ClockGenerator>())
    {
        fs_adc = clk_gen.get_adc_sampling_freq()[0];
        // set_cic_rate(prm::cic_decimation_rate_default);
        set_cic_rate(32);
        psd.resize(1 + n_pts / 2);
        psd_int.resize(1 + n_pts / 2);
        spectrum.window(win::Hann, n_pts);
        start_acquisition();
    }

    void set_cic_rate(uint32_t rate) {
        if (rate < prm::cic_decimation_rate_min ||
            rate > prm::cic_decimation_rate_max) {
            ctx.log<ERROR>("Decimator: CIC rate out of range\n");
            return;
        }

        cic_rate = rate;
        fs = fs_adc / (2.0f * cic_rate); // Sampling frequency (factor of 2 because of FIR)
        ctx.log<INFO>("Decimator: Sampling frequency = %f Hz\n", double(fs));
        fifo_transfer_duration = n_pts / fs;
        ctx.log<INFO>("Decimator: FIFO transfer duration = %f s\n",
                      double(fifo_transfer_duration));
        spectrum.fs(fs);
        ps_ctl.write<reg::cic_rate>(cic_rate);
    }

    auto get_control_parameters() {
        return std::tuple{fs, fifo_transfer_duration, cic_rate, n_pts};
    }

    auto read_adc() const {
        return adc_data;
    }

    auto spectral_density() const {
        return psd;
    }

  private:
    // static constexpr uint32_t n_pts = 8192;
    static constexpr uint32_t n_fifo = 16384; // Total size of the FIFO buffer
    static constexpr uint32_t n_segs = 8;
    static constexpr uint32_t n_pts = n_fifo / n_segs;

    Context& ctx;
    Memory<mem::control>& ctl;
    Memory<mem::status>& sts;
    Memory<mem::ps_control>& ps_ctl;
    Fifo<mem::adc_fifo> fifo;

    ClockGenerator& clk_gen;

    uint32_t cic_rate;
    float fs_adc, fs;
    float fifo_transfer_duration;

    std::array<double, n_fifo> adc_data;
    std::array<double, n_pts> seg_data;

    // Data acquisition thread
    std::thread acq_thread;
    std::mutex mutex;
    std::atomic<bool> acquisition_started{false};
    void acquisition_thread();
    void start_acquisition();

    // Spectrum analyzer
    sig::Spectrum<double> spectrum;
    std::vector<double> psd_int; // Internal psd buffer
    std::vector<double> psd;
}; // Decimator

inline void Decimator::start_acquisition() {
    if (! acquisition_started) {
        adc_data.fill(0);
        acq_thread = std::thread{&Decimator::acquisition_thread, this};
        acq_thread.detach();
    }
}

// inline void Decimator::acquisition_thread() {
//     acquisition_started = true;

//     while (acquisition_started) {
//         using namespace sci::operators;

//         psd_int = sci::zeros<double>(1 + n_pts / 2);
        
//         for (uint32_t cnt=0; cnt<psd_navg; ++cnt) {
//             fifo.wait_for_data(n_pts, fs);
//             const double vrange = 2.048; // TODO Use calibration and range
//             constexpr double nmax = 262144.0; // 2^18

//             for (uint32_t i = 0; i < n_pts; i++) {
//                 adc_data[i] = vrange * static_cast<int32_t>(fifo.read()) / nmax / 4096.0;
//             }

//             psd_int = psd_int + spectrum.periodogram<sig::DENSITY, false>(adc_data);
//         }

//         {
//             std::lock_guard<std::mutex> lock(mutex);
//             psd = psd_int / double(psd_navg);
//         }
//     }
// }

inline void Decimator::acquisition_thread() {
    acquisition_started = true;

    while (acquisition_started) {
        using namespace sci::operators;

        psd_int = sci::zeros<double>(1 + n_pts / 2);
        uint32_t seg_cnt = 0;
        
        fifo.wait_for_data(n_fifo, fs);
        const double vrange = 2.048; // TODO Use calibration and range
        constexpr double nmax = 262144.0; // 2^18

        for (uint32_t i = 0; i < n_fifo; i++) {
            const auto value = vrange * static_cast<int32_t>(fifo.read()) / nmax / 4096.0;
            adc_data[i] = value;

            seg_data[seg_cnt] = value;
            ++seg_cnt;

            if (seg_cnt == n_pts) {
                seg_cnt = 0;
                psd_int = psd_int + spectrum.periodogram<sig::DENSITY, false>(seg_data);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            psd = psd_int / double(n_segs);
        }
    }
}

#endif // __DRIVERS_DECIMATOR_HPP__
