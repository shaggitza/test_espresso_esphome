#include "mock_pump.h"
#include "esphome/core/hal.h"
#include <cmath>

namespace esphome {
namespace espresso_machine_mock_pump {

static const char *const TAG = "mock_pump";

// ---------------------------------------------------------------------------
// MockPumpNumber
// ---------------------------------------------------------------------------
void MockPumpNumber::setup() {
  if (!parent_) return;

  // Publish the initial value from the parent
  float initial_value = 0.0f;
  switch (param_type_) {
    case ParamType::NOMINAL_FLOW:
      initial_value = parent_->get_nominal_flow();
      break;
    case ParamType::PUCK_TIME_CONSTANT:
      initial_value = parent_->get_puck_time_constant();
      break;
    case ParamType::PUCK_DENSITY:
      initial_value = parent_->get_puck_density();
      break;
    case ParamType::PUMP_MAX_PRESSURE:
      initial_value = parent_->get_pump_max_pressure();
      break;
    case ParamType::INTERNAL_VOLUME:
      initial_value = parent_->get_internal_volume();
      break;
    case ParamType::PUCK_ABSORPTION_ML:
      initial_value = parent_->get_puck_absorption();
      break;
    case ParamType::PUCK_EXTRACTION_TAU:
      initial_value = parent_->get_puck_extraction_tau();
      break;
  }
  this->publish_state(initial_value);
}

void MockPumpNumber::control(float value) {
  if (!parent_) return;

  switch (param_type_) {
    case ParamType::NOMINAL_FLOW:
      parent_->update_nominal_flow(value);
      ESP_LOGD(TAG, "Nominal flow updated to %.1f mL/s", value);
      break;
    case ParamType::PUCK_TIME_CONSTANT:
      parent_->update_puck_time_constant(value);
      ESP_LOGD(TAG, "Puck time constant updated to %.1f s", value);
      break;
    case ParamType::PUCK_DENSITY:
      parent_->update_puck_density(value);
      ESP_LOGD(TAG, "Puck density updated to %.0f", value);
      break;
    case ParamType::PUMP_MAX_PRESSURE:
      parent_->update_pump_max_pressure(value);
      ESP_LOGD(TAG, "Pump max pressure updated to %.1f bar", value);
      break;
    case ParamType::INTERNAL_VOLUME:
      parent_->update_internal_volume(value);
      ESP_LOGD(TAG, "Internal volume updated to %.1f mL", value);
      break;
    case ParamType::PUCK_ABSORPTION_ML:
      parent_->update_puck_absorption(value);
      ESP_LOGD(TAG, "Puck absorption updated to %.1f mL", value);
      break;
    case ParamType::PUCK_EXTRACTION_TAU:
      parent_->update_puck_extraction_tau(value);
      ESP_LOGD(TAG, "Puck extraction tau updated to %.1f s", value);
      break;
  }
  this->publish_state(value);
}

// ---------------------------------------------------------------------------
// MockPump
// ---------------------------------------------------------------------------
void MockPump::setup() {
  last_update_ms_ = millis();
  ESP_LOGI(TAG, "Mock pump initialized:");
  ESP_LOGI(TAG, "  Nominal flow: %.1f mL/s (max unimpeded, D=1)", nominal_flow_);
  ESP_LOGI(TAG, "  Pump stall pressure: %.1f bar", pump_max_pressure_bar_);
  ESP_LOGI(TAG, "  Puck density: %.0f (1=open, 100=blocked)", puck_density_);
  ESP_LOGI(TAG, "  Puck time constant: %.1f s (at D=100)", puck_time_constant_);
  ESP_LOGI(TAG, "  Internal volume: %.1f mL (τ_decay = %.1f s)",
           internal_volume_ml_,
           (nominal_flow_ > 0.0f) ? internal_volume_ml_ / nominal_flow_ : 0.0f);
  ESP_LOGI(TAG, "  Puck absorption capacity: %.1f mL", puck_absorption_ml_);
  if (puck_extraction_tau_ > 0.0f) {
    ESP_LOGI(TAG, "  Puck extraction tau: %.1f s (flow increases as puck degrades)", puck_extraction_tau_);
  } else {
    ESP_LOGI(TAG, "  Puck extraction tau: disabled (constant puck density)");
  }
}

void MockPump::loop() {
  uint32_t now = millis();
  uint32_t dt_ms = now - last_update_ms_;

  // Update every ~10 ms for smooth simulation (same rate as mock heater)
  static constexpr float PRESSURE_RISE_TAU = 1.5f;  // Time constant for pressure rise [s]
  if (dt_ms < 10) {
    return;
  }
  last_update_ms_ = now;

  float dt_s = dt_ms / 1000.0f;

  // -------------------------------------------------------------------------
  // Puck density model:
  //   flow_fraction = (101 − D) / 100   →  1.0 at D=1,  0.01 at D=100
  //   Q_ss          = nominal_flow × flow_fraction
  //   P_equilibrium = pump_max_pressure × (D − 1) / 100
  //                   →  0 bar at D=1 (no restriction),  ~P_max at D=100 (blocked)
  //
  // Open valve mode: When pumping through steam/purge valve instead of a puck,
  // there's virtually no resistance. In this mode we force D=1, which gives:
  //   - Max flow (nominal_flow)
  //   - Zero pressure
  //   - No wetting delay
  // -------------------------------------------------------------------------
  float effective_density = puck_density_;

  // In open valve mode, bypass puck physics — no resistance
  if (open_valve_mode_) {
    effective_density = 1.0f;
  } else if (running_ && puck_extraction_tau_ > 0.0f) {
    // Puck extraction degradation: effective density decreases from puck_density_
    // toward 1 with time constant puck_extraction_tau_. This represents coffee
    // solubles dissolving and the puck structure weakening over the shot, causing
    // flow to increase monotonically as time progresses. Disabled when tau = 0.
    effective_density = 1.0f + (puck_density_ - 1.0f) * std::exp(-run_time_ / puck_extraction_tau_);
  }

  float flow_fraction = (101.0f - effective_density) / 100.0f;
  float Q_ss = nominal_flow_ * flow_fraction;
  float p_equilibrium = pump_max_pressure_bar_ * (effective_density - 1.0f) / 100.0f;

  if (running_) {
    run_time_ += dt_s;

    // -----------------------------------------------------------------------
    // Puck wetting model
    //
    // Wetting time constant scales linearly with density so a denser puck
    // takes proportionally longer before flow breaks through. The effective
    // density (which decreases over time due to extraction) is used here so
    // that wetting also speeds up as the puck degrades.
    //   effective_τ = puck_time_constant × (D_eff / 100)
    //   wetted_fraction(t) = 1 − exp(−t / effective_τ)
    //   Q(t) = Q_ss × wetted_fraction(t)
    // -----------------------------------------------------------------------
    float effective_tau = puck_time_constant_ * (effective_density / 100.0f);

    float wetted_fraction;
    if (effective_tau <= 0.0f) {
      wetted_fraction = 1.0f;  // Immediate full flow (low density or τ = 0)
    } else {
      wetted_fraction = 1.0f - std::exp(-run_time_ / effective_tau);
    }

    current_flow_rate_ = Q_ss * wetted_fraction;

    // -----------------------------------------------------------------------
    // Puck absorption model (realistic nozzle flow)
    //
    // Coffee grounds absorb water during extraction. The absorption follows
    // the same wetting curve — water is absorbed as it penetrates the puck.
    //
    //   absorbed_total(t) = puck_absorption_ml × wetted_fraction(t)
    //   absorption_rate   = d(absorbed_total)/dt
    //                     = puck_absorption_ml × d(wetted_fraction)/dt
    //                     = puck_absorption_ml × (1/τ_eff) × exp(-t/τ_eff)
    //
    // Nozzle flow = pump flow - absorption rate
    //             = Q(t) - absorption_rate
    //
    // This means early in the shot, much of the water is absorbed by the puck
    // and little comes out the nozzle. As the puck saturates, nozzle flow
    // approaches pump flow.
    // -----------------------------------------------------------------------
    float absorption_rate = 0.0f;
    if (puck_absorption_ml_ > 0.0f && effective_tau > 0.0f) {
      // Derivative of wetted_fraction: d/dt(1 - exp(-t/τ)) = (1/τ) × exp(-t/τ)
      float exp_term = std::exp(-run_time_ / effective_tau);
      absorption_rate = (puck_absorption_ml_ / effective_tau) * exp_term;
      
      // Update absorbed volume
      absorbed_volume_ = puck_absorption_ml_ * wetted_fraction;
    }
    
    // Nozzle flow is pump flow minus what's being absorbed by the puck
    // Cannot be negative (absorption rate can exceed pump flow briefly at start)
    nozzle_flow_rate_ = std::max(0.0f, current_flow_rate_ - absorption_rate);

    // Accumulate volumes
    total_volume_ += current_flow_rate_ * dt_s;
    nozzle_total_volume_ += nozzle_flow_rate_ * dt_s;

    // -----------------------------------------------------------------------
    // Pressure model: fast first-order rise toward P_equilibrium.
    //
    // At D=1 (open puck): P_eq = 0 bar — virtually no pressure builds.
    // At D=50 (medium):   P_eq = ~7.4 bar — pressure stabilises mid-range.
    // At D=100 (blocked): P_eq = ~15 bar — pressure climbs to pump stall.
    //
    // τ_rise = 1.5 s  →  ~95% of equilibrium is reached within ~4.5 s.
    // -----------------------------------------------------------------------
    float alpha = 1.0f - std::exp(-dt_s / PRESSURE_RISE_TAU);
    system_pressure_bar_ += (p_equilibrium - system_pressure_bar_) * alpha;

    // Update sensors periodically (~4 Hz is fine for display)
    static uint32_t last_sensor_publish_ms = 0;
    if (now - last_sensor_publish_ms > 250) {
      last_sensor_publish_ms = now;

      // Update 3-second rolling average (12 samples × 250 ms = 3 s window)
      avg_buf_[avg_buf_idx_] = current_flow_rate_;
      avg_buf_idx_ = (avg_buf_idx_ + 1) % AVG_WINDOW_SIZE;
      if (avg_buf_count_ < AVG_WINDOW_SIZE)
        avg_buf_count_++;
      float sum = 0.0f;
      for (uint8_t i = 0; i < avg_buf_count_; i++)
        sum += avg_buf_[i];
      avg_rate_3s_ = sum / static_cast<float>(avg_buf_count_);

      if (rate_sensor_) {
        rate_sensor_->publish_state(current_flow_rate_);
      }
      if (total_sensor_) {
        total_sensor_->publish_state(total_volume_);
      }
      if (pressure_sensor_) {
        pressure_sensor_->publish_state(system_pressure_bar_);
      }
      if (nozzle_rate_sensor_) {
        nozzle_rate_sensor_->publish_state(nozzle_flow_rate_);
      }
      if (nozzle_total_sensor_) {
        nozzle_total_sensor_->publish_state(nozzle_total_volume_);
      }
      if (avg_rate_sensor_) {
        avg_rate_sensor_->publish_state(avg_rate_3s_);
      }
    }
  } else {
    // -----------------------------------------------------------------------
    // Pump is off — model residual-pressure-driven flow
    //
    // When the pump stops, the internal volume of tubing and piping remains
    // pressurised. This trapped pressure continues to drive water through the
    // puck/valve until it bleeds off, causing a gradual flow decay instead of
    // an instant drop to zero.
    //
    // Model:
    //   Q_residual(t) = Q_ss × (P_system / P_equilibrium)
    //   P_system decays: dP/dt = −P / τ_decay
    //   τ_decay = internal_volume_ml / nominal_flow    [s]
    //
    // With internal_volume_ml = 0 the model is disabled and flow decays
    // immediately (legacy behaviour preserved).
    // -----------------------------------------------------------------------
    if (internal_volume_ml_ > 0.0f && system_pressure_bar_ > 0.01f &&
        p_equilibrium > 0.1f) {
      // Residual flow proportional to remaining system pressure
      current_flow_rate_ = Q_ss * (system_pressure_bar_ / p_equilibrium);
      
      // When pump is off, puck is fully saturated, so nozzle flow equals pump flow
      nozzle_flow_rate_ = current_flow_rate_;

      // Accumulate residual volume
      total_volume_ += current_flow_rate_ * dt_s;
      nozzle_total_volume_ += nozzle_flow_rate_ * dt_s;

      // Pressure decays exponentially: τ = internal_volume / nominal_flow
      float tau_decay = (nominal_flow_ > 0.0f) ? internal_volume_ml_ / nominal_flow_ : 1.0f;
      system_pressure_bar_ *= std::exp(-dt_s / tau_decay);

      if (system_pressure_bar_ < 0.01f) {
        system_pressure_bar_ = 0.0f;
        current_flow_rate_ = 0.0f;
        nozzle_flow_rate_ = 0.0f;
        if (rate_sensor_) {
          rate_sensor_->publish_state(0.0f);
        }
        if (pressure_sensor_) {
          pressure_sensor_->publish_state(0.0f);
        }
        if (nozzle_rate_sensor_) {
          nozzle_rate_sensor_->publish_state(0.0f);
        }
      }
    } else {
      // No internal volume (or pressure already gone) — quick decay
      if (current_flow_rate_ > 0.0f) {
        current_flow_rate_ *= 0.9f;
        nozzle_flow_rate_ *= 0.9f;
        if (current_flow_rate_ < 0.01f) {
          current_flow_rate_ = 0.0f;
          nozzle_flow_rate_ = 0.0f;
          if (rate_sensor_) {
            rate_sensor_->publish_state(0.0f);
          }
          if (nozzle_rate_sensor_) {
            nozzle_rate_sensor_->publish_state(0.0f);
          }
        }
      }
    }
    run_time_ = 0.0f;  // Reset run time for next start
  }

  // Push current flow rate to mock heater for thermoblock cooling simulation
  if (flow_observer_) {
    flow_observer_->set_flow_rate(current_flow_rate_);
  }

  // Flow-rate bang-bang: when a target flow rate is set, modulate the pump
  // on/off to maintain the requested rate. This mirrors PumpSwitch::loop() so
  // the mock behaves consistently with real hardware.
  // Note: MockPump has no min_on/min_off hardware constraints, so oscillation
  // can be very fast (every 10 ms tick). This is acceptable for simulation.
  if (target_flow_rate_ > 0.0f) {
    if (current_flow_rate_ < target_flow_rate_ && !running_)
      write_state(true);
    else if (current_flow_rate_ >= target_flow_rate_ && running_)
      write_state(false);
  }

  // Log periodically (every ~5 seconds for debugging)
  static uint32_t last_log_ms = 0;
  if (now - last_log_ms > 5000) {
    last_log_ms = now;
    ESP_LOGD(TAG, "Pump %s%s, D_eff=%.1f, Q=%.2f mL/s, Qnoz=%.2f mL/s, V=%.1f mL, Nozzle=%.1f mL, Absorbed=%.1f mL, t=%.1f s, P_sys=%.2f bar",
             running_ ? "ON" : "OFF",
             open_valve_mode_ ? " (bypass)" : "",
             effective_density, current_flow_rate_, nozzle_flow_rate_, total_volume_,
             nozzle_total_volume_, absorbed_volume_, run_time_, system_pressure_bar_);
  }
}

void MockPump::write_state(bool state) {
  running_ = state;
  this->publish_state(state);

  if (state) {
    // Pump starting — reset run time and system pressure, but NOT total volume
    // (total volume is only reset by explicit reset_flow() call)
    run_time_ = 0.0f;
    system_pressure_bar_ = 0.0f;
    ESP_LOGI(TAG, "Mock pump started");
  } else {
    ESP_LOGI(TAG, "Mock pump stopped (total: %.1f mL, residual pressure: %.2f bar)",
             total_volume_, system_pressure_bar_);
  }
}

void MockPump::reset_flow() {
  total_volume_ = 0.0f;
  nozzle_total_volume_ = 0.0f;
  current_flow_rate_ = 0.0f;
  nozzle_flow_rate_ = 0.0f;
  absorbed_volume_ = 0.0f;
  run_time_ = 0.0f;
  system_pressure_bar_ = 0.0f;
  for (uint8_t i = 0; i < AVG_WINDOW_SIZE; i++)
    avg_buf_[i] = 0.0f;
  avg_buf_idx_ = 0;
  avg_buf_count_ = 0;
  avg_rate_3s_ = 0.0f;

  if (rate_sensor_) {
    rate_sensor_->publish_state(0.0f);
  }
  if (total_sensor_) {
    total_sensor_->publish_state(0.0f);
  }
  if (pressure_sensor_) {
    pressure_sensor_->publish_state(0.0f);
  }
  if (nozzle_rate_sensor_) {
    nozzle_rate_sensor_->publish_state(0.0f);
  }
  if (nozzle_total_sensor_) {
    nozzle_total_sensor_->publish_state(0.0f);
  }
  if (avg_rate_sensor_) {
    avg_rate_sensor_->publish_state(0.0f);
  }

  ESP_LOGD(TAG, "Flow counters reset");
}

}  // namespace espresso_machine_mock_pump
}  // namespace esphome
