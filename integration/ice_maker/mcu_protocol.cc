#include "mcu_protocol.h"

#include <array>
#include <utility>

#include <esp_log.h>

#include "uart1_communicator.h"

namespace {

constexpr char kTag[] = "McuProtocol";
constexpr uint8_t kFrameHeader1 = 0x55;
constexpr uint8_t kFrameHeader2 = 0xAA;
constexpr uint8_t kFrameTail = 0xA5;

// 短帧：55 AA 功能码 数据 校验 A5 = 6 字节。
constexpr size_t kShortFrameSize = 6;
// 全属性上报：55 AA 03 + 13 字节数据 + 校验 + A5 = 18 字节。
constexpr size_t kAllStatusFrameSize = 18;
// 全属性数据区字节数（byte4 ~ byte16）。
constexpr size_t kAllStatusDataSize = 13;
// 全属性数据区在帧内的偏移。
constexpr size_t kAllStatusDataOffset = 3;
// 全属性校验字节位置（byte17，从 1 计数）。
constexpr size_t kAllStatusChecksumIndex = 16;
// 全属性上报中 TDS 高低字节偏移（相对数据区）。
constexpr size_t kAllStatusTdsHighOffset = 4;
constexpr size_t kAllStatusTdsLowOffset = 5;
// 全属性上报中制冰数量高低字节偏移（相对数据区）。
constexpr size_t kAllStatusIceCountHighOffset = 8;
constexpr size_t kAllStatusIceCountLowOffset = 9;
// 全属性上报中故障状态 1/2 偏移（相对数据区）。
constexpr size_t kAllStatusFault1Offset = 10;
constexpr size_t kAllStatusFault2Offset = 11;
// 全属性上报中冰厚档位偏移（相对数据区）。
constexpr size_t kAllStatusIceThicknessOffset = 12;

uint8_t CalculateChecksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

// 根据功能码确定接收帧长度；未知功能码返回 0。
size_t FrameLengthFor(McuCommand command) {
    switch (command) {
        case McuCommand::kControl:
        case McuCommand::kEvent:
        case McuCommand::kWifi:
            return kShortFrameSize;
        case McuCommand::kQueryAll:
            return kAllStatusFrameSize;
        default:
            return 0;
    }
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
        // 寻找帧头 55 AA。
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

        const auto command = static_cast<McuCommand>(rx_buffer_[2]);
        const size_t frame_length = FrameLengthFor(command);
        if (frame_length == 0) {
            ESP_LOGW(kTag, "Unexpected MCU command: 0x%02X", rx_buffer_[2]);
            rx_buffer_.erase(rx_buffer_.begin());
            continue;
        }

        if (rx_buffer_.size() < frame_length) {
            break;  // 等待更多数据。
        }

        // ADD8 校验：仅对功能码 + 数据区求和（短帧=2字节，长帧=14字节）。
        const uint8_t expected_checksum =
            CalculateChecksum(rx_buffer_.data() + 2, frame_length - 4);
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

bool McuProtocol::SendControl(McuControlCode code) {
    return SendFrame(McuCommand::kControl, static_cast<uint8_t>(code));
}

bool McuProtocol::SendWifi(McuWifiCode code) {
    return SendFrame(McuCommand::kWifi, static_cast<uint8_t>(code));
}

bool McuProtocol::QueryAllStatus() {
    return SendFrame(McuCommand::kQueryAll, 0x00);
}

void McuProtocol::SetEventCallback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = std::move(callback);
}

void McuProtocol::SetWifiCallback(WifiCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    wifi_callback_ = std::move(callback);
}

void McuProtocol::SetAllStatusCallback(AllStatusCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    all_status_callback_ = std::move(callback);
}

bool McuProtocol::SendFrame(McuCommand command, uint8_t data) {
    std::array<uint8_t, kShortFrameSize> frame = {
        kFrameHeader1,
        kFrameHeader2,
        static_cast<uint8_t>(command),
        data,
        0,  // 校验，下方计算。
        kFrameTail,
    };
    // ADD8 校验：仅对功能码 + 数据求和（不含帧头 55 AA 与帧尾 A5）。
    frame[kShortFrameSize - 2] =
        CalculateChecksum(frame.data() + 2, kShortFrameSize - 4);

    std::lock_guard<std::mutex> lock(send_mutex_);
    const int written =
        Uart1Communicator::GetInstance().Uart1Send(frame.data(), frame.size());
    if (written != static_cast<int>(frame.size())) {
        ESP_LOGE(kTag, "UART1 frame send failed: %d/%u byte(s)", written,
                 static_cast<unsigned>(frame.size()));
        return false;
    }

    ESP_LOGI(kTag, "Sent 55 AA %02X %02X %02X A5 through UART1",
             static_cast<uint8_t>(command), data, frame[kShortFrameSize - 2]);
    return true;
}

void McuProtocol::ProcessFrame(const uint8_t* frame, size_t length) {
    if (frame == nullptr || length < kShortFrameSize) {
        return;
    }

    const auto command = static_cast<McuCommand>(frame[2]);

    switch (command) {
        case McuCommand::kEvent:
            NotifyEvent(static_cast<McuEventCode>(frame[3]));
            break;

        case McuCommand::kWifi:
            NotifyWifi(static_cast<McuWifiCode>(frame[3]));
            break;

        case McuCommand::kQueryAll:
            if (length == kAllStatusFrameSize) {
                ProcessAllStatus(frame + kAllStatusDataOffset,
                                 kAllStatusDataSize);
            } else {
                ESP_LOGW(kTag, "Unexpected all-status frame length: %u",
                         static_cast<unsigned>(length));
            }
            break;

        default:
            ESP_LOGW(kTag, "Unsupported MCU command: 0x%02X", frame[2]);
            break;
    }
}

void McuProtocol::ProcessAllStatus(const uint8_t* data, size_t length) {
    if (data == nullptr || length < kAllStatusDataSize) {
        return;
    }

    McuAllStatus status;
    status.work_mode = data[0];
    status.uv_on = data[1] != 0;
    status.delay_shutdown_hours = data[2];
    status.delay_power_on_hours = data[3];
    status.tds = (static_cast<uint16_t>(data[kAllStatusTdsHighOffset]) << 8) |
                 static_cast<uint16_t>(data[kAllStatusTdsLowOffset]);
    status.remaining_ice_minutes = data[6];
    status.remaining_clean_minutes = data[7];
    status.ice_count =
        (static_cast<uint16_t>(data[kAllStatusIceCountHighOffset]) << 8) |
        static_cast<uint16_t>(data[kAllStatusIceCountLowOffset]);
    status.fault_flags = data[kAllStatusFault1Offset];
    status.ice_thickness_index = data[kAllStatusIceThicknessOffset];

    (void)data[kAllStatusFault2Offset];  // 故障状态 2 为预留位。

    NotifyAllStatus(status);
}

void McuProtocol::NotifyEvent(McuEventCode event) {
    ESP_LOGI(kTag, "MCU event 0x%02X", static_cast<uint8_t>(event));

    EventCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = event_callback_;
    }
    if (callback) {
        callback(event);
    }
}

void McuProtocol::NotifyWifi(McuWifiCode code) {
    ESP_LOGI(kTag, "MCU wifi command 0x%02X", static_cast<uint8_t>(code));

    WifiCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = wifi_callback_;
    }
    if (callback) {
        callback(code);
    }
}

void McuProtocol::NotifyAllStatus(const McuAllStatus& status) {
    ESP_LOGI(kTag,
             "MCU all status: mode=%u uv=%u delayOff=%u delayOn=%u tds=%u "
             "iceLeft=%u cleanLeft=%u iceCount=%u fault=0x%02X thick=%u",
             status.work_mode, status.uv_on ? 1U : 0U,
             status.delay_shutdown_hours, status.delay_power_on_hours,
             status.tds, status.remaining_ice_minutes,
             status.remaining_clean_minutes, status.ice_count,
             status.fault_flags, status.ice_thickness_index);

    AllStatusCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = all_status_callback_;
    }
    if (callback) {
        callback(status);
    }
}
