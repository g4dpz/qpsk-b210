#include "ltp_cla/config.h"
#include "ltp_cla/convergence_layer_adapter.h"
#include "ltp_cla/logging.h"
#include "ltp_cla/ltp_engine.h"
#include "ltp_cla/tcp_egress.h"
#include "ltp_cla/tcp_ingress.h"
#include "ltp_cla/timer_manager.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Global shutdown flag
// ---------------------------------------------------------------------------

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) {
    g_running = false;
}

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [--config=<path.json>] [--key=value ...]\n"
              << "\n"
              << "Options:\n"
              << "  --config=<path>   Load configuration from JSON file\n"
              << "  --key=value       Override individual config parameters\n"
              << "                    e.g. --ltp.local_engine_id=1\n"
              << "  --help            Show this help message\n";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Check for --help
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Load config: start with defaults, optionally load JSON, then apply CLI
    ltp::LtpClaConfig config = ltp::LtpClaConfig::defaults();

    // Look for --config=<path>
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg.substr(0, 9) == "--config=") {
            std::string path = arg.substr(9);
            try {
                config = ltp::LtpClaConfig::from_json_file(path);
                spdlog::info("Loaded config from {}", path);
            } catch (const std::exception& e) {
                std::cerr << "Error loading config: " << e.what() << "\n";
                return 1;
            }
            break;
        }
    }

    // Apply CLI overrides
    config.apply_cli_overrides(argc, const_cast<const char* const*>(argv));

    // Set log level
    ltp::set_log_level(config.log_level);

    // Validate
    auto errors = config.validate();
    if (!errors.empty()) {
        std::cerr << "Configuration errors:\n";
        for (const auto& err : errors) {
            std::cerr << "  - " << err << "\n";
        }
        return 1;
    }

    spdlog::info("LTP CLA starting — local engine {} → remote engine {}",
                 config.ltp.local_engine_id, config.ltp.remote_engine_id);
    spdlog::info("  LTP: max_seg={} timeout={}ms max_retx={} max_sessions={}",
                 config.ltp.max_segment_size,
                 config.ltp.retransmission_timeout_ms,
                 config.ltp.max_retransmissions,
                 config.ltp.max_concurrent_sessions);
    spdlog::info("  CLA: TX={}:{} RX={}:{}",
                 config.cla.tx_addr, config.cla.tx_port,
                 config.cla.rx_addr, config.cla.rx_port);
    spdlog::info("  Ingress: {}:{} ({})",
                 config.ingress.bind_addr, config.ingress.port,
                 config.ingress.default_reliable ? "reliable" : "unreliable");
    spdlog::info("  Egress: {}:{} (buffer {}MB)",
                 config.egress.bind_addr, config.egress.port,
                 config.egress.max_buffer_bytes / (1024 * 1024));

    // ---- Initialize components ----

    // Timer manager
    ltp::LtpEngine engine(config.ltp);
    ltp::TimerManager timer_mgr([&engine](const ltp::TimerEvent& evt) {
        engine.handle_timer_expiry(evt);
    });
    engine.set_timer_manager(&timer_mgr);

    // CLA
    ltp::ConvergenceLayerAdapter cla(config.cla);
    cla.set_segment_callback([&engine](std::vector<uint8_t> data) {
        auto seg = ltp::LtpSegment::decode(data.data(), data.size());
        if (seg) {
            engine.receive_segment(*seg);
        } else {
            spdlog::warn("CLA: received malformed LTP segment ({} bytes)", data.size());
        }
    });

    engine.set_send_segment_callback([&cla](std::vector<uint8_t> encoded) {
        cla.send_segment(std::move(encoded));
    });

    // TCP Egress
    ltp::TcpEgress egress(config.egress);
    engine.set_data_arrival_callback([&egress](std::vector<uint8_t> data) {
        spdlog::info("Delivering {} bytes to egress", data.size());
        egress.deliver(std::move(data));
    });

    engine.set_session_complete_callback([](uint64_t sn) {
        spdlog::info("Originator session {} completed", sn);
    });

    engine.set_session_failure_callback([](uint64_t sn, uint8_t reason) {
        spdlog::warn("Session {} failed, reason {}", sn, reason);
    });

    // TCP Ingress
    ltp::TcpIngress ingress(config.ingress);
    bool reliable = config.ingress.default_reliable;
    ingress.set_data_callback([&engine, reliable](std::vector<uint8_t> data) {
        spdlog::info("Ingress received {} bytes, starting LTP session ({})",
                     data.size(), reliable ? "red" : "green");
        engine.start_session(std::move(data), reliable);
    });

    // ---- Start everything ----

    // Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    timer_mgr.run();
    cla.start();
    egress.start();
    ingress.start();

    spdlog::info("LTP CLA running. Press Ctrl+C to stop.");

    // ---- Main loop: wait for shutdown ----

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ---- Graceful shutdown ----

    spdlog::info("Shutting down...");

    ingress.stop();
    spdlog::info("Ingress stopped");

    // Log active sessions before cleanup
    auto diag = engine.get_diagnostics();
    spdlog::info("Active sessions at shutdown: {} originated, {} received",
                 diag.sessions_originated - diag.sessions_completed - diag.sessions_cancelled,
                 diag.sessions_received);

    timer_mgr.stop();
    spdlog::info("Timer manager stopped");

    cla.stop();
    spdlog::info("CLA stopped");

    egress.stop();
    spdlog::info("Egress stopped");

    // Final diagnostics
    diag = engine.get_diagnostics();
    spdlog::info("Final diagnostics:");
    spdlog::info("  Sessions: originated={} received={} completed={} cancelled={}",
                 diag.sessions_originated, diag.sessions_received,
                 diag.sessions_completed, diag.sessions_cancelled);
    spdlog::info("  Segments: red_sent={} red_recv={} green_sent={} green_recv={}",
                 diag.red_segments_sent, diag.red_segments_received,
                 diag.green_segments_sent, diag.green_segments_received);
    spdlog::info("  Handshake: cp_sent={} rpt_recv={} rpt_sent={} rpt_ack_sent={} retx={}",
                 diag.checkpoints_sent, diag.reports_received,
                 diag.reports_sent, diag.report_acks_sent,
                 diag.retransmissions);

    spdlog::info("LTP CLA stopped.");
    return 0;
}
