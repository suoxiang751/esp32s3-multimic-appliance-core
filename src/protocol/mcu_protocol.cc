#include "mcu_protocol.h"

#include <algorithm>
#include <array>
#include <utility>

#include <esp_log.h>

#include "uart1_communicator.h"

namespace {

constexpr char kTag[] = "McuProtocol";
constexpr uint8_t kFrameHeader1 = 0x55;
constexpr uint8_t kFrameHeader2 = 0xAA;
constexpr uint8_t kAiModuleVersion = 0x00;
constexpr uint8_t kMcuVersion = 0x03;
constexpr size_t kFrameFixedSize = 7;
constexpr size_t kFunctionHeaderSize = 4;
constexpr size_t kMaxRxDataSize = 512;
constexpr size_t kMaxTxDataSize = 8;
constexpr size_t kMaxTxFrameSize = kFrameFixedSize + kMaxTxDataSize;

uint16_t ReadBigEndian16(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) |
           static_cast<uint16_t>(data[1]);
}

uint32_t ReadBigEndian32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint8_t CalculateChecksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

}  // namespace

McuProtocol& McuProtocol::GetInstance() {
    static McuProtocol instance;
    return instance;
}

size_t McuProtocol::ParseMcuData(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return 0;
    }

    rx_buffer_.insert(rx_buffer_.end(), data, data + length);
    size_t parsed_frames = 0;

    while (true) {
        size_t header_index = 0;
        while (header_index + 1 < rx_buffer_.size() &&
               (rx_buffer_[header_index] != kFrameHeader1 ||
                rx_buffer_[header_index + 1] != kFrameHeader2)) {
            ++header_index;
        }

        if (header_index + 1 >= rx_buffer_.size()) {
            const bool keep_header =
                !rx_buffer_.empty() && rx_buffer_.back() == kFrameHeader1;
            rx_buffer_.clear();
            if (keep_header) {
                rx_buffer_.push_back(kFrameHeader1);
            }
            break;
        }

        if (header_index > 0) {
            ESP_LOGW(kTag, "Discarding %u byte(s) before frame header",
                     static_cast<unsigned>(header_index));
            rx_buffer_.erase(rx_buffer_.begin(),
                             rx_buffer_.begin() + header_index);
        }

        if (rx_buffer_.size() < kFrameFixedSize) {
            break;
        }

        if (rx_buffer_[2] != kMcuVersion) {
            ESP_LOGW(kTag, "Unexpected MCU protocol version: 0x%02X",
                     rx_buffer_[2]);
            rx_buffer_.erase(rx_buffer_.begin());
            continue;
        }

        const size_t data_length = ReadBigEndian16(rx_buffer_.data() + 4);
        if (data_length > kMaxRxDataSize) {
            ESP_LOGW(kTag, "Invalid MCU frame data length: %u",
                     static_cast<unsigned>(data_length));
            rx_buffer_.erase(rx_buffer_.begin());
            continue;
        }

        const size_t frame_length = kFrameFixedSize + data_length;
        if (rx_buffer_.size() < frame_length) {
            break;
        }

        const uint8_t expected_checksum =
            CalculateChecksum(rx_buffer_.data(), frame_length - 1);
        const uint8_t received_checksum = rx_buffer_[frame_length - 1];
        if (expected_checksum != received_checksum) {
            ESP_LOGW(kTag,
                     "MCU frame checksum mismatch: expected 0x%02X, got 0x%02X",
                     expected_checksum, received_checksum);
            rx_buffer_.erase(rx_buffer_.begin());
            continue;
        }

        ProcessFrame(rx_buffer_.data(), frame_length);
        rx_buffer_.erase(rx_buffer_.begin(),
                         rx_buffer_.begin() + frame_length);
        ++parsed_frames;
    }

    return parsed_frames;
}

void McuProtocol::ResetParser() {
    rx_buffer_.clear();
}

bool McuProtocol::SendBool(uint8_t function_id, bool value) {
    const uint8_t data = value ? 0x01 : 0x00;
    return SendFunctionData(function_id, McuDataType::kBool, &data,
                            sizeof(data));
}

bool McuProtocol::SendEnum(uint8_t function_id, uint8_t value) {
    return SendFunctionData(function_id, McuDataType::kEnum, &value,
                            sizeof(value));
}

bool McuProtocol::SendValue(uint8_t function_id, uint32_t value) {
    const std::array<uint8_t, 4> data = {
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF),
    };
    return SendFunctionData(function_id, McuDataType::kValue, data.data(),
                            data.size());
}

bool McuProtocol::SendHeartbeat() {
    return SendFrame(McuCommand::kHeartbeat, nullptr, 0);
}

bool McuProtocol::QueryAllStatus() {
    return SendFrame(McuCommand::kQueryAllStatus, nullptr, 0);
}

void McuProtocol::SetFunctionDataCallback(FunctionDataCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    function_data_callback_ = std::move(callback);
}

void McuProtocol::SetHeartbeatCallback(HeartbeatCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    heartbeat_callback_ = std::move(callback);
}

bool McuProtocol::SendFunctionData(uint8_t function_id,
                                   McuDataType data_type,
                                   const uint8_t* data, size_t length) {
    if (function_id == 0 || data == nullptr) {
        ESP_LOGE(kTag, "Invalid function data");
        return false;
    }

    const bool valid_length =
        ((data_type == McuDataType::kBool ||
          data_type == McuDataType::kEnum) &&
         length == 1) ||
        (data_type == McuDataType::kValue && length == 4);
    if (!valid_length) {
        ESP_LOGE(kTag, "Invalid function data length: %u",
                 static_cast<unsigned>(length));
        return false;
    }

    std::array<uint8_t, kMaxTxDataSize> payload = {};
    payload[0] = function_id;
    payload[1] = static_cast<uint8_t>(data_type);
    payload[2] = static_cast<uint8_t>((length >> 8) & 0xFF);
    payload[3] = static_cast<uint8_t>(length & 0xFF);
    std::copy(data, data + length, payload.begin() + kFunctionHeaderSize);

    return SendFrame(McuCommand::kFunctionControl, payload.data(),
                     kFunctionHeaderSize + length);
}

bool McuProtocol::SendFrame(McuCommand command, const uint8_t* data,
                            size_t length) {
    if (length > kMaxTxDataSize || (length > 0 && data == nullptr)) {
        ESP_LOGE(kTag, "Invalid frame data length: %u",
                 static_cast<unsigned>(length));
        return false;
    }

    std::array<uint8_t, kMaxTxFrameSize> frame = {};
    frame[0] = kFrameHeader1;
    frame[1] = kFrameHeader2;
    frame[2] = kAiModuleVersion;
    frame[3] = static_cast<uint8_t>(command);
    frame[4] = static_cast<uint8_t>((length >> 8) & 0xFF);
    frame[5] = static_cast<uint8_t>(length & 0xFF);
    if (length > 0) {
        std::copy(data, data + length, frame.begin() + 6);
    }

    const size_t frame_length = kFrameFixedSize + length;
    frame[frame_length - 1] =
        CalculateChecksum(frame.data(), frame_length - 1);

    std::lock_guard<std::mutex> lock(send_mutex_);
    const int written =
        Uart1Communicator::GetInstance().Uart1Send(frame.data(), frame_length);
    if (written != static_cast<int>(frame_length)) {
        ESP_LOGE(kTag, "UART1 frame send failed: %d/%u byte(s)", written,
                 static_cast<unsigned>(frame_length));
        return false;
    }

    ESP_LOGI(kTag, "Sent command 0x%02X through UART1, %u byte(s)",
             static_cast<uint8_t>(command),
             static_cast<unsigned>(frame_length));
    return true;
}

void McuProtocol::ProcessFrame(const uint8_t* frame, size_t length) {
    if (frame == nullptr || length < kFrameFixedSize) {
        return;
    }

    const McuCommand command = static_cast<McuCommand>(frame[3]);
    const size_t data_length = ReadBigEndian16(frame + 4);
    const uint8_t* data = frame + 6;

    switch (command) {
        case McuCommand::kHeartbeat:
            if (data_length == 1) {
                NotifyHeartbeat(data[0]);
            } else {
                ESP_LOGW(kTag, "Invalid heartbeat response length: %u",
                         static_cast<unsigned>(data_length));
            }
            break;

        case McuCommand::kFunctionStatus:
            if (!ProcessFunctionData(data, data_length)) {
                ESP_LOGW(kTag, "Invalid MCU function status payload");
            }
            break;

        default:
            ESP_LOGW(kTag, "Unsupported MCU command: 0x%02X", frame[3]);
            break;
    }
}

bool McuProtocol::ProcessFunctionData(const uint8_t* data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        if (length - offset < kFunctionHeaderSize) {
            return false;
        }

        const uint8_t function_id = data[offset];
        const McuDataType data_type =
            static_cast<McuDataType>(data[offset + 1]);
        const size_t function_length = ReadBigEndian16(data + offset + 2);
        offset += kFunctionHeaderSize;

        if (function_length > length - offset) {
            return false;
        }

        uint32_t value = 0;
        bool supported = true;
        switch (data_type) {
            case McuDataType::kBool:
                if (function_length != 1) {
                    supported = false;
                } else {
                    value = data[offset];
                    if (value > 1) {
                        ESP_LOGW(kTag,
                                 "Non-boolean value %u for function 0x%02X",
                                 static_cast<unsigned>(value), function_id);
                    }
                }
                break;

            case McuDataType::kEnum:
                if (function_length != 1) {
                    supported = false;
                } else {
                    value = data[offset];
                }
                break;

            case McuDataType::kValue:
                if (function_length != 4) {
                    supported = false;
                } else {
                    value = ReadBigEndian32(data + offset);
                }
                break;

            default:
                supported = false;
                break;
        }

        if (supported) {
            NotifyFunctionData({
                .function_id = static_cast<McuFunctionId>(function_id),
                .data_type = data_type,
                .value = value,
            });
        } else {
            ESP_LOGW(kTag,
                     "Skipping function 0x%02X: type 0x%02X, length %u",
                     function_id, static_cast<uint8_t>(data_type),
                     static_cast<unsigned>(function_length));
        }

        offset += function_length;
    }

    return true;
}

void McuProtocol::NotifyFunctionData(const McuFunctionData& data) {
    ESP_LOGI(kTag, "MCU function 0x%02X, type 0x%02X, value %lu",
             static_cast<uint8_t>(data.function_id),
             static_cast<uint8_t>(data.data_type),
             static_cast<unsigned long>(data.value));

    FunctionDataCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = function_data_callback_;
    }
    if (callback) {
        callback(data);
    }
}

void McuProtocol::NotifyHeartbeat(uint8_t heartbeat_state) {
    ESP_LOGI(kTag, "MCU heartbeat state: 0x%02X", heartbeat_state);

    HeartbeatCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = heartbeat_callback_;
    }
    if (callback) {
        callback(heartbeat_state);
    }
}
