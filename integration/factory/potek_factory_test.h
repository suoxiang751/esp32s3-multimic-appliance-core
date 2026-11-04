#ifndef __U_FACTORY_TEST_H__
#define __U_FACTORY_TEST_H__

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string>
#include <vector>
#include "audio_service.h"
#include "esp_wifi.h"



#define TEST_MODE_WIFI_SSID "PORTFOLIO_FACTORY"                    // 产测SSID 
// #define TEST_MODE_WIFI_PASSWORD "configure-at-build-time"              // 产测密码
#define TEST_MODE_WIFI_PASSWORD "configure-at-build-time"
#define WIFI_CONNECT_TIMEOUT_MS        10000
#define TEST_AUDIO_VOLUME 100
#define TEST_TIMEOUT_SPEAK 2                                // 扬声器测试超时时间单位秒
#define TEST_TIMEOUT_MIC 30                                 // 麦克风测试超时时间单位秒
#define FACTORY_TEST_ALWAYS_RUN 0                           // 1: 每次启动都进入产测；0: 按标志位决定


// 常量定义
#define WIFI_RSSI_THRESHOLD_STRONG    -60                // WiFi信号强度阈值
#define LED_BRIGHTNESS_NORMAL          50                // LED正常亮度
#define LED_BRIGHTNESS_DIM             5                 // LED低亮度
#define DELAY_TASK_START_MS            2000              // 任务启动延迟
#define DELAY_TASK_CLEANUP_MS          1000              // 任务清理延迟
#define DELAY_MIC_ACK_MS               1000              // 麦克风应答延迟
#define DELAY_WIFI_SCAN_RETRY_MS       500               // WiFi扫描重试延迟
#define DELAY_TASK_LOOP_MS             1000              // 任务循环延迟
#define AUDIO_RMS_THRESHOLD            400               // 音频RMS阈值
#define MIC_DETECT_CONSECUTIVE_FRAMES  1                 // 连续命中检查次数阈值
#define MIC_DETECT_FRAMES_PER_CHECK    10                // 单次检查抓取的音频帧数
#define MIC_DETECT_HIT_FRAMES          3                 // 单次检查内命中帧数阈值
#define AUDIO_SAMPLE_RATE              16000             // 音频采样率
#define AUDIO_FRAME_DURATION_MS        10                // 音频帧时长(ms)
#define WIFI_SCAN_MAX_RETRY            5                 // WiFi扫描最大重试次数


enum FACTORY_TEST_E {
    FACTORY_TEST_OFF = 0,           // 产测模式已结束的状态
    FACTORY_TEST_INIT,                  // 产测初始化状态
    FACTORY_TEST_SPEAKER_TEST,          // 喇叭测试
    FACTORY_TEST_MIC_TEST,              // 麦克风测试
    FACTORY_TEST_BUTTON_TEST,           // 按键测试
    FACTORY_TEST_EXIT,                  // 在退出状态
};

struct FACTORY_TEST_STATE_T {
    FACTORY_TEST_E  test_state_;
    bool mic_test;
    bool mic_left_test;
    bool mic_right_test;
    bool button_test;
};

class FactoryTest {
public:
    static FactoryTest& GetInstance() {
        static FactoryTest instance;
        return instance;
    }

    void FactoryTestInit();                                     // 在Application::Start() 音频初始化之前调用
    void Start();                                               // 开启工厂测试（初始化调用）
    void FactoryTestButtonOnClick();                            // 按键测试触发         （对外接口，用户调用）
    void FactoryTestButtonDoubleClick();                        // 按键双击退出产测      （对外接口，用户调用）
    void FactoryTestFlagButtonWrite();                          // 产测标志位写入       （boot双击事件中调用）
    bool IsFactoryTesting();                                    // 产测中
    bool ShouldEnterFactoryTest();                              // 本次启动是否需要进入产测
    FACTORY_TEST_STATE_T GetFactoryTestStateStruct();           // 获取完整的产测状态结构体
    
    bool IsFactoryTested();                                     // 已产测true
    void FactoryTestFlagWrite();                                // 写产测标志位
    void FactoryTestFlagClear();                                // 擦除产测标志位        (调用后延时3秒重启设备)


    void ReEnterFactoryTest();                                  // 重新进入产测
    void ExitFactoryTest();                        // 结束产测
private:
    esp_event_handler_instance_t instance_any_id_;
    esp_netif_t* m_sta_netif;

    FactoryTest();
    ~FactoryTest();
    FactoryTest(const FactoryTest&) = delete;                   // 禁止拷贝
    FactoryTest& operator=(const FactoryTest&) = delete;        // 禁止赋值

    FACTORY_TEST_STATE_T factory_test_state_;
    // FACTORY_TEST_E  factory_test_state_.test_state_= FACTORY_TEST_INIT;        // 工厂测试状态
    bool wifi_station_was_running_ = false;                         // 测试标志位
    bool button_test_triggered_ = false;                            // 按键测试触发标志
    bool exit_triggered_ = false;                                   // 退出触发标志

    TaskHandle_t factory_test_task_handle_ = nullptr;

    static bool mic_test_ack_flg_;                                     // mic测试应答语音应答标志位
    static int test_pass_cnt_flg_;                                        // 测试通过标志位

    void FactoryTestTask();

    // 状态处理函数
    void HandleInitState(class AudioCodec* codec, char* notice_str);
    void HandleSpeakerTest(class AudioCodec* codec, class PotekSingleLed& sig_led, 
                           uint16_t& timeout_counter, char* notice_str);
    void HandleMicTest(class PotekSingleLed& sig_led, uint16_t& timeout_counter, char* notice_str);
    void HandleButtonTest(class PotekSingleLed& sig_led, char* notice_str);
    void HandleExitState(class PotekSingleLed& sig_led);
    
    // 资源清理函数
    void CleanupTasks();

    // 音频提示音
    void PlaySoundSpeakTest();
    void PlaySoundMicTestAck();
    void PlaySoundButtonTestAck();
    // 屏幕提示
    void FactoryTestShowNotification(const char* str);

    void WifiScanInit();
    int WifiScanOnce();
    bool WifiConnectTest(const char* ssid, const char* password, int timeout_ms);
    void WifiScanDeinit();
};



#endif // 
