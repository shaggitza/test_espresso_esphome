#include "mock_heater.h"
#include <algorithm>
#include <cmath>
#include "esphome/core/hal.h"

namespace esphome {
namespace espresso_machine_mock_heater {

static const char *const TAG = "mock_heater";

// ---------------------------------------------------------------------------
// Aluminium material constants (physical, not tunable)
// ---------------------------------------------------------------------------
static constexpr float K_AL = 200.0f;      // Thermal conductivity [W/(m·K)]
static constexpr float RHO_AL = 2700.0f;   // Density [kg/m³]
static constexpr float CP_AL = 897.0f;     // Specific heat [J/(kg·K)]
// Thermal diffusivity: α = K_AL/(RHO_AL×CP_AL) ≈ 8.26×10⁻⁵ m²/s = 82.6 mm²/s
// Reference cross-section of the Al path (1 cm², represents a typical thermoblock
// inter-node conduction area)
static constexpr float A_REF = 1e-4f;      // m² (1 cm²)

// ---------------------------------------------------------------------------
// integrate_chain — update N_THERMAL_SEGS-1 intermediate diffusion nodes.
//
// Parameters:
//   nodes  — array of N_THERMAL_SEGS-1 intermediate node temperatures [°C]
//   T_A    — fixed temperature at the source endpoint [°C]
//   T_B    — fixed temperature at the sink endpoint [°C]
//   d_m    — path length [m] (d_mm × 1e-3)
//   dt_s   — outer time step [s]
//
// Models 1D heat conduction through an aluminium rod of length d_m between
// two endpoint temperatures T_A and T_B. Uses explicit Euler with automatic
// sub-stepping to satisfy the diffusion stability criterion
//   dt ≤ 0.4 × Δx² / α_Al   (Δx = d_m / N_THERMAL_SEGS)
//
// This produces a HIGHER-ORDER step response that approximates the true 1D
// heat diffusion Green's function:
//   — Short-time dead zone: no response until heat diffuses from T_A to the node
//   — Rising phase: Gaussian-shaped front arrives and heats the chain
//   — Long-time convergence: exponential approach to steady state
//
// A simple RC coupling (1st-order, G×ΔT) would give an immediate exponential
// response with no dead time. The chain gives proper spatial averaging.
//
// nodes[] is updated in-place. Call with the CURRENT endpoint temperatures
// (before updating them) to implement the correct explicit coupling scheme.
// ---------------------------------------------------------------------------
static void integrate_chain(float *nodes, float T_A, float T_B, float d_m, float dt_s) {
  const float dx = d_m / N_THERMAL_SEGS;
  const float G_seg = K_AL * A_REF / dx;
  const float C_seg = RHO_AL * CP_AL * A_REF * dx;

  // Stability criterion: dt_sub ≤ 0.4 × C_seg/G_seg = 0.4 × dx²/α_Al
  const float tau_seg = C_seg / G_seg;  // = dx² / α_Al
  const int n_sub = (dt_s > 0.4f * tau_seg) ? (int)(dt_s / (0.4f * tau_seg)) + 1 : 1;
  const float dt_sub = dt_s / n_sub;
  const float coeff = dt_sub * G_seg / C_seg;  // = dt_sub × α_Al / dx²

  const int n = N_THERMAL_SEGS - 1;  // number of intermediate nodes
  float dT[N_THERMAL_SEGS - 1];

  for (int s = 0; s < n_sub; s++) {
    // Explicit Euler: update all nodes simultaneously from old state
    for (int i = 0; i < n; i++) {
      float T_left = (i == 0) ? T_A : nodes[i - 1];
      float T_right = (i == n - 1) ? T_B : nodes[i + 1];
      dT[i] = coeff * (T_left + T_right - 2.0f * nodes[i]);
    }
    for (int i = 0; i < n; i++) nodes[i] += dT[i];
  }
}

// ---------------------------------------------------------------------------
// MockHeaterOutput
// ---------------------------------------------------------------------------
void MockHeaterOutput::write_state(float state) {
  // Clamp duty cycle to [0, 1]
  duty_ = std::max(0.0f, std::min(1.0f, state));
}

// ---------------------------------------------------------------------------
// MockHeaterTempSensor
// ---------------------------------------------------------------------------
void MockHeaterTempSensor::update() {
  if (parent_) {
    float temp = parent_->get_sensor_temperature();
    this->publish_state(temp);
  }
}

// ---------------------------------------------------------------------------
// MockHeaterNumber
// ---------------------------------------------------------------------------
void MockHeaterNumber::setup() {
  if (!parent_) return;

  // Publish the initial value from the parent
  float initial_value = 0.0f;
  switch (param_type_) {
    case ParamType::POWER:
      initial_value = parent_->get_power_watts();
      break;
    case ParamType::THERMAL_MASS:
      initial_value = parent_->get_thermal_mass();
      break;
    case ParamType::HEAT_LOSS:
      initial_value = parent_->get_heat_loss();
      break;
    case ParamType::AMBIENT:
      initial_value = parent_->get_ambient_temp();
      break;
    case ParamType::HEAT_TRANSFER_K:
      initial_value = parent_->get_heat_transfer_k();
      break;
    case ParamType::DIST_WATER_TO_HEATER:
      initial_value = parent_->get_dist_water_to_heater();
      break;
    case ParamType::DIST_WATER_TO_SENSOR:
      initial_value = parent_->get_dist_water_to_sensor();
      break;
    case ParamType::DIST_SENSOR_TO_HEATER:
      initial_value = parent_->get_dist_sensor_to_heater();
      break;
  }
  this->publish_state(initial_value);
}

void MockHeaterNumber::control(float value) {
  if (!parent_) return;

  switch (param_type_) {
    case ParamType::POWER:
      parent_->update_power_watts(value);
      ESP_LOGD(TAG, "Power updated to %.1f W", value);
      break;
    case ParamType::THERMAL_MASS:
      parent_->update_thermal_mass(value);
      ESP_LOGD(TAG, "Thermal mass updated to %.1f J/°C", value);
      break;
    case ParamType::HEAT_LOSS:
      parent_->update_heat_loss(value);
      ESP_LOGD(TAG, "Heat loss updated to %.2f W/°C", value);
      break;
    case ParamType::AMBIENT:
      parent_->update_ambient_temp(value);
      ESP_LOGD(TAG, "Ambient temperature updated to %.1f °C", value);
      break;
    case ParamType::HEAT_TRANSFER_K:
      parent_->update_heat_transfer_k(value);
      ESP_LOGD(TAG, "Heat transfer k updated to %.2f mL/s", value);
      break;
    case ParamType::DIST_WATER_TO_HEATER:
      parent_->update_dist_water_to_heater(value);
      ESP_LOGD(TAG, "Thermal distance water-to-heater updated to %.1f mm", value);
      break;
    case ParamType::DIST_WATER_TO_SENSOR:
      parent_->update_dist_water_to_sensor(value);
      ESP_LOGD(TAG, "Thermal distance water-to-sensor updated to %.1f mm", value);
      break;
    case ParamType::DIST_SENSOR_TO_HEATER:
      parent_->update_dist_sensor_to_heater(value);
      ESP_LOGD(TAG, "Thermal distance sensor-to-heater updated to %.1f mm", value);
      break;
  }
  this->publish_state(value);
}

// ---------------------------------------------------------------------------
// MockHeater
// ---------------------------------------------------------------------------
void MockHeater::set_target_temperature(float t) {
  target_temperature_ = t;
  ESP_LOGD(TAG, "IHeater::set_target_temperature(%.1f°C) — stored (use espresso_machine_heater to control PID)", t);
}

void MockHeater::setup() {
  // Initialize all temperature nodes and chain intermediate nodes to the
  // same starting temperature (zero internal gradient = no initial heat flow)
  sensor_temperature_ = temperature_;
  heater_temperature_ = temperature_;
  for (int i = 0; i < N_THERMAL_SEGS - 1; i++) {
    hw_nodes_[i] = temperature_;
    ws_nodes_[i] = temperature_;
    hs_nodes_[i] = temperature_;
  }
  last_update_ms_ = millis();
  ESP_LOGI(TAG, "Mock heater initialized:");
  ESP_LOGI(TAG, "  Initial temp: %.1f °C", temperature_);
  ESP_LOGI(TAG, "  Ambient temp: %.1f °C", ambient_temp_);
  ESP_LOGI(TAG, "  Power: %.0f W", power_watts_);
  ESP_LOGI(TAG, "  Thermal mass: %.0f J/°C (Al block + water)", thermal_mass_);
  ESP_LOGI(TAG, "  Heat loss: %.2f W/°C", heat_loss_);
  ESP_LOGI(TAG, "  Water inlet temp: %.1f °C", water_inlet_temp_);
  ESP_LOGI(TAG, "  Heat transfer k: %.2f mL/s (ε≈63%% at Q=k)", heat_transfer_k_);
  ESP_LOGI(TAG, "  Dist water-heater: %.1f mm (%d-seg diffusion chain)",
           dist_water_to_heater_mm_, N_THERMAL_SEGS);
  ESP_LOGI(TAG, "  Dist water-sensor: %.1f mm (%d-seg diffusion chain)",
           dist_water_to_sensor_mm_, N_THERMAL_SEGS);
  ESP_LOGI(TAG, "  Dist sensor-heater: %.1f mm (%d-seg diffusion chain)",
           dist_sensor_to_heater_mm_, N_THERMAL_SEGS);
}

void MockHeater::loop() {
  uint32_t now = millis();
  uint32_t dt_ms = now - last_update_ms_;

  // Update every ~10 ms for smooth simulation
  if (dt_ms < 10) {
    return;
  }
  last_update_ms_ = now;

  float dt_s = dt_ms / 1000.0f;

  // Get current duty cycle from PID output
  float duty = output_ ? output_->get_duty() : 0.0f;

  // ---------------------------------------------------------------------------
  // Thermal model — 3-node + diffusion chains
  //
  // Nodes (when distances > 0):
  //   H — heater element (receives electrical power), C_H = 10% of C
  //   W — water contact / bulk block (flow cooling acts here), C_W = 90% of C
  //   S — sensor probe (what PID reads), C_S = 5% of C
  //
  // Each non-zero distance d_mm activates a finite-difference diffusion chain
  // of N_THERMAL_SEGS segments between the two endpoint nodes. The chain:
  //   - Has segment conductance G_seg = K_Al × A_ref × N / d_m [W/K]
  //   - Has segment thermal mass C_seg = ρ_Al × Cp_Al × A_ref × d_m / N [J/K]
  //   - Is sub-stepped to satisfy stability: dt_sub ≤ 0.4 × dx²/α_Al
  //   - Produces a higher-order diffusion response (dead time + spatial averaging)
  //     instead of the simple 1st-order exponential of a single RC coupling.
  //
  // Energy conservation:
  //   The heat flux INTO endpoint B from the chain = G_seg × (last_node - T_B).
  //   The heat flux OUT OF endpoint A into the chain = G_seg × (T_A - first_node).
  //   These differ during transients (chain stores/releases energy), which is
  //   exactly the averaging effect. In steady state they are equal.
  //
  // When all distances = 0: no chains, no separate nodes → 1-node model.
  // ---------------------------------------------------------------------------
  static constexpr float CP_WATER = 4.186f;  // J/(mL·°C)

  // Heat transfer effectiveness for flow-based cooling
  float effectiveness = 1.0f;
  if (flow_rate_ > 0.01f) {
    effectiveness = 1.0f - std::exp(-heat_transfer_k_ / flow_rate_);
  }
  float Q_flow = flow_rate_ * CP_WATER * effectiveness * (temperature_ - water_inlet_temp_);
  float Q_loss = heat_loss_ * (temperature_ - ambient_temp_);
  float Q_elec = duty * power_watts_;

  // Which paths are active?
  const bool has_HW = (dist_water_to_heater_mm_ > 0.0f);
  const bool has_WS = (dist_water_to_sensor_mm_ > 0.0f);
  const bool has_HS = (dist_sensor_to_heater_mm_ > 0.0f);
  const bool has_heater_node = has_HW || has_HS;
  const bool has_sensor_node = has_WS || has_HS;

  // ---------------------------------------------------------------------------
  // Step 1: Capture all inter-node heat fluxes [W] using the CURRENT chain
  // state (pre-integration). This is the explicit coupling scheme: endpoint
  // temperatures are held fixed while the chain is sub-stepped, then the
  // endpoint ODEs use the fluxes captured here.
  //
  // For a path from A to B through N_THERMAL_SEGS segments:
  //   Q_from_A   = G_seg × (T_A − nodes[0])         — flux leaving A into chain
  //   Q_into_B   = G_seg × (nodes[N-2] − T_B)        — flux entering B from chain
  // ---------------------------------------------------------------------------
  float Q_H_to_chain = 0.0f;   // total flux leaving H (into HW chain + HS chain)
  float Q_HWchain_to_W = 0.0f; // flux entering W from HW chain
  float Q_W_to_WSchain = 0.0f; // flux leaving W into WS chain
  float Q_S_from_chains = 0.0f; // total flux entering S (from WS chain + HS chain)

  if (has_HW) {
    float d_m = dist_water_to_heater_mm_ * 1e-3f;
    float G_s = K_AL * A_REF * N_THERMAL_SEGS / d_m;  // segment conductance
    Q_H_to_chain += G_s * (heater_temperature_ - hw_nodes_[0]);
    Q_HWchain_to_W = G_s * (hw_nodes_[N_THERMAL_SEGS - 2] - temperature_);
  }
  if (has_WS) {
    float d_m = dist_water_to_sensor_mm_ * 1e-3f;
    float G_s = K_AL * A_REF * N_THERMAL_SEGS / d_m;
    Q_W_to_WSchain = G_s * (temperature_ - ws_nodes_[0]);
    Q_S_from_chains += G_s * (ws_nodes_[N_THERMAL_SEGS - 2] - sensor_temperature_);
  }
  if (has_HS) {
    float d_m = dist_sensor_to_heater_mm_ * 1e-3f;
    float G_s = K_AL * A_REF * N_THERMAL_SEGS / d_m;
    Q_H_to_chain += G_s * (heater_temperature_ - hs_nodes_[0]);
    Q_S_from_chains += G_s * (hs_nodes_[N_THERMAL_SEGS - 2] - sensor_temperature_);
  }

  // ---------------------------------------------------------------------------
  // Step 2: Integrate diffusion chain intermediate nodes (sub-stepped).
  // Endpoint temperatures T_A and T_B are held at their START-OF-STEP values.
  // ---------------------------------------------------------------------------
  if (has_HW) {
    integrate_chain(hw_nodes_, heater_temperature_, temperature_,
                    dist_water_to_heater_mm_ * 1e-3f, dt_s);
  }
  if (has_WS) {
    integrate_chain(ws_nodes_, temperature_, sensor_temperature_,
                    dist_water_to_sensor_mm_ * 1e-3f, dt_s);
  }
  if (has_HS) {
    integrate_chain(hs_nodes_, heater_temperature_, sensor_temperature_,
                    dist_sensor_to_heater_mm_ * 1e-3f, dt_s);
  }

  // ---------------------------------------------------------------------------
  // Step 3: Update endpoint node temperatures using captured fluxes.
  // ---------------------------------------------------------------------------
  if (has_heater_node) {
    // 3-node: heater element is thermally separate from the block.
    // C_H = 10% of total thermal mass; C_W = 90% of total thermal mass.
    float C_H = 0.10f * thermal_mass_;
    float C_W = 0.90f * thermal_mass_;
    heater_temperature_ += dt_s * (Q_elec - Q_H_to_chain) / C_H;
    temperature_ += dt_s * (Q_HWchain_to_W - Q_loss - Q_flow - Q_W_to_WSchain) / C_W;
  } else {
    // 1-node: all electrical power goes directly to block with full thermal mass.
    temperature_ += dt_s * (Q_elec - Q_loss - Q_flow - Q_W_to_WSchain) / thermal_mass_;
    heater_temperature_ = temperature_;
  }

  if (has_sensor_node) {
    // Sensor node: receives heat from WS and/or HS diffusion chains.
    // C_S = 5% of total thermal mass.
    float C_S = 0.05f * thermal_mass_;
    sensor_temperature_ += dt_s * Q_S_from_chains / C_S;
  } else {
    // No sensor lag: sensor instantly reads block temperature
    sensor_temperature_ = temperature_;
  }

  // ---------------------------------------------------------------------------
  // Clamp all nodes to physical bounds
  // Floor: 10°C below ambient (can't cool below ambient naturally in this model)
  // Ceiling: 300°C safety cap for simulation (prevents runaway in degenerate cases)
  // ---------------------------------------------------------------------------
  auto clamp = [this](float &t) {
    if (t < ambient_temp_ - 10.0f) t = ambient_temp_ - 10.0f;
    if (t > 300.0f) t = 300.0f;
  };
  clamp(temperature_);
  clamp(heater_temperature_);
  clamp(sensor_temperature_);
  for (int i = 0; i < N_THERMAL_SEGS - 1; i++) {
    clamp(hw_nodes_[i]);
    clamp(ws_nodes_[i]);
    clamp(hs_nodes_[i]);
  }

  // Log periodically (every ~5 seconds for debugging)
  static uint32_t last_log_ms = 0;
  if (now - last_log_ms > 5000) {
    last_log_ms = now;
    ESP_LOGD(TAG, "T_block=%.1f°C T_sensor=%.1f°C T_heater=%.1f°C duty=%.1f%% Q=%.2f mL/s ε=%.0f%%",
             temperature_, sensor_temperature_, heater_temperature_,
             duty * 100.0f, flow_rate_, effectiveness * 100.0f);
  }

  // Publish duty cycle sensor at ~4 Hz so HA can show SSR switching intensity
  static uint32_t last_duty_publish_ms = 0;
  if (now - last_duty_publish_ms > 250) {
    last_duty_publish_ms = now;
    if (duty_sensor_) {
      duty_sensor_->publish_state(duty * 100.0f);
    }
  }
}

}  // namespace espresso_machine_mock_heater
}  // namespace esphome

