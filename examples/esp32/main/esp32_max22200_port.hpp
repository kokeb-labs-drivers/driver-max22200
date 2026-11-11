/**
 * @file esp32_max22200_port.hpp
 * @brief ESP-IDF port for the MAX22200 driver: spi_master for the bus, GPIO for CMD, ENABLE and
 * FAULT, esp_rom delays and ESP_LOG for the log hook.
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32Max22200Pins; the defaults
 * match the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1).
 */
#ifndef MAX22200_ESP32_MAX22200_PORT_HPP
#define MAX22200_ESP32_MAX22200_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "max22200.hpp"

/// The port's default SPI clock: 1 MHz for bring-up on jumper wires.
inline constexpr int kEsp32Max22200ClockHz = 1'000'000;
/// The port's SPI mode.
inline constexpr uint8_t kEsp32Max22200SpiMode = 0;
// The bus requirements of the driver (handbook KB-CPP-123), checked at compile time.
static_assert(kEsp32Max22200ClockHz <= static_cast<int>(max22200::kMaxClockHz),
              "the default clock is above the MAX22200's tCLK limit");
static_assert(kEsp32Max22200SpiMode == max22200::kSpiMode, "the MAX22200 works in SPI mode 0 only");
static_assert(max22200::kMsbFirst, "this port sends MSB first (no SPI_DEVICE_BIT_LSBFIRST)");
static_assert(max22200::kTransferBytes[1] <= 16U,
              "max_transfer_sz below covers the longest transfer");

/// Pins and bus settings of the ESP-IDF port.
struct Esp32Max22200Pins {
  spi_host_device_t host = SPI2_HOST;
  int sclk = 6;
  int mosi = 7;   ///< To SDI.
  int miso = 2;   ///< From SDO: open drain, needs a pull-up (the internal one is too weak above
                  ///< about 1 MHz; fit an external one).
  int cs = 10;    ///< CSB.
  int cmd = 11;   ///< CMD.
  int enable = 3; ///< ENABLE; -1 when tied high on the board.
  int fault = 4;  ///< FAULT (open drain, active low); -1 when not wired.
  int clock_hz = kEsp32Max22200ClockHz; ///< Up to `max22200::kMaxClockHz`; checked at start.
};

/// The MAX22200 SPI port on ESP-IDF, with every optional hook.
class Esp32Max22200Port : public max22200::SpiInterface<Esp32Max22200Port> {
public:
  explicit Esp32Max22200Port(const Esp32Max22200Pins& pins = {}) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (device_ != nullptr) {
      return true;
    }
    if (pins_.clock_hz <= 0 || pins_.clock_hz > static_cast<int>(max22200::kMaxClockHz)) {
      ESP_LOGE(kTag, "clock outside the MAX22200's bus requirements");
      return false;
    }
    // Idle levels before the outputs are enabled (handbook KB-HW-018): CMD high, ENABLE low.
    if (!outputAt(pins_.cmd, 1) || (pins_.enable >= 0 && !outputAt(pins_.enable, 0))) {
      return false;
    }
    if (pins_.fault >= 0) {
      gpio_config_t in{};
      in.pin_bit_mask = 1ULL << static_cast<unsigned>(pins_.fault);
      in.mode = GPIO_MODE_INPUT;
      in.pull_up_en = GPIO_PULLUP_ENABLE;
      if (gpio_config(&in) != ESP_OK) {
        return false;
      }
    }
    spi_bus_config_t bus{};
    bus.mosi_io_num = pins_.mosi;
    bus.miso_io_num = pins_.miso;
    bus.sclk_io_num = pins_.sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 16;
    const esp_err_t bus_err = spi_bus_initialize(pins_.host, &bus, SPI_DMA_DISABLED);
    if (bus_err != ESP_OK && bus_err != ESP_ERR_INVALID_STATE) { // INVALID_STATE: bus already up
      ESP_LOGE(kTag, "spi_bus_initialize: %s", esp_err_to_name(bus_err));
      return false;
    }
    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = pins_.clock_hz;
    dev.mode = kEsp32Max22200SpiMode; // CPOL = 0, CPHA = 0
    dev.spics_io_num = pins_.cs;
    dev.queue_size = 1;
    if (spi_bus_add_device(pins_.host, &dev, &device_) != ESP_OK) {
      device_ = nullptr;
      return false;
    }
    return true;
  }

  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    if (device_ == nullptr || len == 0) {
      return false;
    }
    spi_transaction_t t{};
    t.length = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_polling_transmit(device_, &t) == ESP_OK;
  }

  bool GpioSet(max22200::CtrlPin pin, max22200::GpioSignal signal) noexcept {
    const uint32_t level = signal == max22200::GpioSignal::ACTIVE ? 1 : 0; // both active high
    switch (pin) {
    case max22200::CtrlPin::Cmd:
      return gpio_set_level(static_cast<gpio_num_t>(pins_.cmd), level) == ESP_OK;
    case max22200::CtrlPin::Enable:
      return pins_.enable < 0 ||
             gpio_set_level(static_cast<gpio_num_t>(pins_.enable), level) == ESP_OK;
    case max22200::CtrlPin::Fault: // an input: never driven
    default:
      break;
    }
    return false;
  }

  bool GpioRead(max22200::CtrlPin pin, max22200::GpioSignal& signal) noexcept {
    if (pin != max22200::CtrlPin::Fault || pins_.fault < 0) {
      return false;
    }
    // Active low: a low level is a fault.
    const bool low = gpio_get_level(static_cast<gpio_num_t>(pins_.fault)) == 0;
    signal = low ? max22200::GpioSignal::ACTIVE : max22200::GpioSignal::INACTIVE;
    return true;
  }

  void DelayUs(uint32_t us) noexcept {
    esp_rom_delay_us(us);
  }

  void Log(max22200::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = level == max22200::LogLevel::Error     ? ESP_LOG_ERROR
                                      : level == max22200::LogLevel::Warning ? ESP_LOG_WARN
                                      : level == max22200::LogLevel::Info    ? ESP_LOG_INFO
                                                                             : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  /// Set the level first, then enable the output driver, so the pin never shows another level.
  static bool outputAt(int gpio, uint32_t level) noexcept {
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << static_cast<unsigned>(gpio);
    io.mode = GPIO_MODE_OUTPUT;
    return gpio_set_level(static_cast<gpio_num_t>(gpio), level) == ESP_OK &&
           gpio_config(&io) == ESP_OK;
  }

  static constexpr const char* kTag = "max22200";
  Esp32Max22200Pins pins_;
  spi_device_handle_t device_ = nullptr;
};

#endif // MAX22200_ESP32_MAX22200_PORT_HPP
