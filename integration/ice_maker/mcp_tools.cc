#include "mcp.h"

#include <stdexcept>
#include <string>

#include <esp_log.h>

#include "mcp_server.h"
#include "mcu_dp.h"
#include "application.h"

namespace {

constexpr char kTag[] = "IceMakerMcp";

ReturnValue SetIceMakerMode(uint8_t work_mode, const char* mode_name) {
    auto& mcu_dp = McuDp::GetInstance();
    bool sent = false;
    switch (work_mode) {
        case 1:  // 快速冰
            sent = mcu_dp.SetQuickIce();
            break;
        case 2:  // 透明冰
            sent = mcu_dp.SetClearIce();
            break;
        case 3:  // 清洗
            sent = mcu_dp.StartClean();
            break;
        case 0:  // 关机
            sent = mcu_dp.PowerOff();
            break;
        default:
            throw std::runtime_error(std::string("不支持的模式：") +
                                     mode_name);
    }
    if (!sent) {
        throw std::runtime_error(std::string("切换到") + mode_name +
                                 "指令发送失败");
    }
    return std::string("好的，正在") + mode_name;
}

ReturnValue SetIceMakerUvLight(bool on) {
    if (!McuDp::GetInstance().SetUvLight(on)) {
        throw std::runtime_error(on ? "开启灯光指令发送失败"
                                    : "关闭灯光指令发送失败");
    }
    return std::string(on ? "好的，灯光已开启" : "好的，灯光已关闭");
}

ReturnValue SetIceMakerDelayShutdown(int hours) {
    if (hours < 1 || hours > 24) {
        throw std::runtime_error("预约关机小时数必须在 1 到 24 之间");
    }
    if (!McuDp::GetInstance().SetDelayShutdown(
            static_cast<uint8_t>(hours))) {
        throw std::runtime_error("预约关机指令发送失败");
    }
    return std::string("好的，已设置") + std::to_string(hours) +
           "小时后关机";
}

ReturnValue SetIceMakerDelayPowerOn(int hours) {
    if (hours < 1 || hours > 24) {
        throw std::runtime_error("预约开机小时数必须在 1 到 24 之间");
    }
    if (!McuDp::GetInstance().SetDelayPowerOn(
            static_cast<uint8_t>(hours))) {
        throw std::runtime_error("预约开机指令发送失败");
    }
    return std::string("好的，已设置") + std::to_string(hours) +
           "小时后开机";
}

ReturnValue SetIceMakerThickness(int level) {
    if (level < -5 || level > 5) {
        throw std::runtime_error("冰厚档位必须在 -5 到 5 之间");
    }
    if (!McuDp::GetInstance().SetIceThickness(
            static_cast<int8_t>(level))) {
        throw std::runtime_error("冰厚档位指令发送失败");
    }
    return std::string("好的，已设置冰厚档位为") + std::to_string(level);
}

const char* WorkModeChineseName(uint8_t mode) {
    switch (mode) {
        case 0:
            return "关机";
        case 1:
            return "快速冰";
        case 2:
            return "透明冰";
        case 3:
            return "清洗";
        default:
            return "未知";
    }
}

const char* FaultChineseName(uint8_t fault_flags) {
    // bit3-7 为 E1~E5，取故障码中最高的一个。
    const uint8_t code = (fault_flags >> 2) & 0x1F;
    if (code == 0) {
        return "无";
    }
    for (int i = 5; i >= 1; --i) {
        if ((code & (1 << (i - 1))) != 0) {
            static const char* kNames[] = {"E1", "E2", "E3", "E4", "E5"};
            return kNames[i - 1];
        }
    }
    return "未知";
}

void AddBooleanStatus(cJSON* json, const char* name,
                      const McuDpState& state, McuDpId dp_id,
                      bool value) {
    if (state.HasValue(dp_id)) {
        cJSON_AddBoolToObject(json, name, value);
    } else {
        cJSON_AddNullToObject(json, name);
    }
}

void AddNumberStatus(cJSON* json, const char* name,
                     const McuDpState& state, McuDpId dp_id,
                     uint32_t value) {
    if (state.HasValue(dp_id)) {
        cJSON_AddNumberToObject(json, name, value);
    } else {
        cJSON_AddNullToObject(json, name);
    }
}

void AddStringStatus(cJSON* json, const char* name,
                     const McuDpState& state, McuDpId dp_id,
                     const char* value) {
    if (state.HasValue(dp_id)) {
        cJSON_AddStringToObject(json, name, value);
    } else {
        cJSON_AddNullToObject(json, name);
    }
}

ReturnValue GetIceMakerStatus() {
    auto& mcu_dp = McuDp::GetInstance();
    // 主动触发一次全属性查询，让 MCU 刷新状态；本次返回当前缓存。
    mcu_dp.QueryAllStatus();

    const McuDpState state = mcu_dp.GetState();
    cJSON* status = cJSON_CreateObject();
    if (status == nullptr) {
        throw std::runtime_error("创建制冰机状态结果失败");
    }

    AddStringStatus(status, "work_mode", state, McuDpId::kWorkMode,
                    WorkModeChineseName(state.work_mode));
    AddBooleanStatus(status, "uv_on", state, McuDpId::kUvLight,
                     state.uv_on);
    AddNumberStatus(status, "delay_shutdown_hours", state,
                    McuDpId::kDelayShutdown, state.delay_shutdown_hours);
    AddNumberStatus(status, "delay_power_on_hours", state,
                    McuDpId::kDelayPowerOn, state.delay_power_on_hours);
    AddNumberStatus(status, "tds", state, McuDpId::kTds, state.tds);
    AddNumberStatus(status, "remaining_ice_minutes", state,
                    McuDpId::kRemainingIceTime, state.remaining_ice_minutes);
    AddNumberStatus(status, "remaining_clean_minutes", state,
                    McuDpId::kRemainingCleanTime,
                    state.remaining_clean_minutes);
    AddNumberStatus(status, "ice_count", state, McuDpId::kIceCount,
                    state.ice_count);
    AddNumberStatus(status, "ice_thickness", state,
                    McuDpId::kIceThickness,
                    static_cast<uint32_t>(state.ice_thickness));
    AddBooleanStatus(status, "ice_full", state, McuDpId::kIceFull,
                     state.ice_full);
    AddBooleanStatus(status, "water_shortage", state,
                     McuDpId::kWaterShortage, state.water_shortage);
    AddStringStatus(status, "fault", state, McuDpId::kFault,
                    FaultChineseName(state.fault_flags));
    return status;
}

ReturnValue SetConversationModeByName(const std::string& mode_name) {
    ListeningMode mode;
    if (mode_name == "auto_stop" || mode_name == "连续对话") {
        mode = kListeningModeAutoStop;
    } else if (mode_name == "realtime" || mode_name == "实时对话") {
        mode = kListeningModeRealtime;
    } else {
        throw std::runtime_error("不支持的对话模式：" + mode_name);
    }

    // Module-local feature: only switches the local conversation mode,
    // nothing is sent to the MCU.
    Application::GetInstance().SetConversationMode(mode);
    return std::string("已切换到") +
           (mode == kListeningModeRealtime ? "实时对话" : "连续对话") + "模式";
}

ReturnValue GetConversationModeStatus() {
    const ListeningMode mode = Application::GetInstance().GetConversationMode();
    cJSON* status = cJSON_CreateObject();
    if (status == nullptr) {
        throw std::runtime_error("创建对话模式状态结果失败");
    }
    cJSON_AddStringToObject(
        status, "mode",
        mode == kListeningModeRealtime ? "realtime" : "auto_stop");
    cJSON_AddStringToObject(
        status, "mode_name",
        mode == kListeningModeRealtime ? "实时对话" : "连续对话");
    return status;
}

}  // namespace

void PotekHiconMcpInitialize() {
    static bool initialized = false;
    if (initialized) {
        ESP_LOGW(kTag, "MCP tools are already initialized");
        return;
    }

    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.ice_maker.turn_on_clear_ice",
        "Turn on the Hicon ice maker in clear-ice (透明冰) mode. After the "
        "command succeeds, speak the returned Chinese confirmation to the "
        "user verbatim.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return SetIceMakerMode(2, "启动透明冰模式");
        });

    mcp_server.AddTool(
        "self.ice_maker.turn_on_quick_ice",
        "Turn on the Hicon ice maker in quick-ice (快速冰) mode. After the "
        "command succeeds, speak the returned Chinese confirmation to the "
        "user verbatim.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return SetIceMakerMode(1, "启动快速冰模式");
        });

    mcp_server.AddTool(
        "self.ice_maker.start_clean",
        "Start the cleaning (清洗) cycle of the Hicon ice maker. Cleaning "
        "takes about 15 minutes. After the command succeeds, speak the "
        "returned Chinese confirmation to the user verbatim.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return SetIceMakerMode(3, "启动清洗模式");
        });

    mcp_server.AddTool(
        "self.ice_maker.turn_off",
        "Turn off the Hicon ice maker. After the command succeeds, speak "
        "the returned Chinese confirmation to the user verbatim.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return SetIceMakerMode(0, "关机");
        });

    mcp_server.AddTool(
        "self.ice_maker.set_uv_light",
        "Turn the UV/light (灯光/UV 消毒) of the Hicon ice maker on or off. "
        "on=true enables the light, on=false disables it. After the command "
        "succeeds, speak the returned Chinese confirmation to the user "
        "verbatim.",
        PropertyList({Property("on", kPropertyTypeBoolean)}),
        [](const PropertyList& properties) -> ReturnValue {
            return SetIceMakerUvLight(properties["on"].value<bool>());
        });

    mcp_server.AddTool(
        "self.ice_maker.set_delayed_shutdown",
        "Set a delayed shutdown (预约关机) of the Hicon ice maker. hours "
        "must be 1 to 24 and means the device shuts down that many hours "
        "from now. After the command succeeds, speak the returned Chinese "
        "confirmation to the user verbatim.",
        PropertyList({Property("hours", kPropertyTypeInteger, 1, 24)}),
        [](const PropertyList& properties) -> ReturnValue {
            return SetIceMakerDelayShutdown(
                properties["hours"].value<int>());
        });

    mcp_server.AddTool(
        "self.ice_maker.set_delayed_power_on",
        "Set a delayed power-on (预约开机) of the Hicon ice maker. hours "
        "must be 1 to 24 and means the device starts that many hours from "
        "now. After the command succeeds, speak the returned Chinese "
        "confirmation to the user verbatim.",
        PropertyList({Property("hours", kPropertyTypeInteger, 1, 24)}),
        [](const PropertyList& properties) -> ReturnValue {
            return SetIceMakerDelayPowerOn(
                properties["hours"].value<int>());
        });

    mcp_server.AddTool(
        "self.ice_maker.set_ice_thickness",
        "Set the ice thickness level (冰厚档位) of the Hicon ice maker. "
        "level must be -5 to 5, where a larger value means thicker ice. "
        "After the command succeeds, speak the returned Chinese "
        "confirmation to the user verbatim.",
        PropertyList({Property("level", kPropertyTypeInteger, -5, 5)}),
        [](const PropertyList& properties) -> ReturnValue {
            return SetIceMakerThickness(properties["level"].value<int>());
        });

    mcp_server.AddTool(
        "self.ice_maker.get_status",
        "Get the latest Hicon ice-maker status. It returns work mode "
        "(关机/快速冰/透明冰/清洗), UV light, delayed shutdown/power-on hours, "
        "TDS water quality, remaining ice/clean minutes, ice count, ice "
        "thickness, ice-full and water-shortage flags, and fault codes "
        "(E1~E5). Always call this read-only tool for questions about "
        "device status; never answer from conversation memory. A null field "
        "has not yet been reported by the MCU. This tool never changes "
        "settings.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return GetIceMakerStatus();
        });

    mcp_server.AddTool(
        "self.conversation.set_mode",
        "Switch the local conversation mode of the AI module (module-local "
        "feature; nothing is sent to the MCU). mode must be \"auto_stop\" "
        "(连续对话模式, kListeningModeAutoStop) or \"realtime\" (实时对话模式, "
        "kListeningModeRealtime). Call this tool when the user asks to switch "
        "between continuous conversation and real-time conversation. After "
        "the command succeeds, speak the returned Chinese confirmation to "
        "the user verbatim.",
        PropertyList({
            Property("mode", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            return SetConversationModeByName(
                properties["mode"].value<std::string>());
        });

    mcp_server.AddTool(
        "self.conversation.get_mode",
        "Get the current local conversation mode of the AI module "
        "(module-local feature; nothing is read from the MCU). Returns "
        "\"auto_stop\" (连续对话模式, kListeningModeAutoStop) or \"realtime\" "
        "(实时对话模式, kListeningModeRealtime). Always call this read-only "
        "tool when the user asks about the current conversation mode; never "
        "answer from conversation memory.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return GetConversationModeStatus();
        });

    initialized = true;
    ESP_LOGI(kTag, "Ice-maker MCP tools registered");
}
