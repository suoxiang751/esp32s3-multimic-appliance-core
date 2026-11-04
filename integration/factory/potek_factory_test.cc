#include "potek_factory_test.h"
#include "application.h"
#include "config.h"
#include "assets/lang_config.h"
#include "potek_single_led.h"
#include "display/display.h"
#include <ssid_manager.h>
#include <wifi_station.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>

#define FACTORY_TEST_NVS_NAMESPACE    "FACTORYTEST"      // 存储空间
#define FACTORY_TEST_FLAGT            "TEST_FLAG"        // flg int
#define TAG "Factory_Test"



bool FactoryTest::mic_test_ack_flg_ = false;
int FactoryTest::test_pass_cnt_flg_ = false;

extern void SetReferenceInputParam(bool enabled, int channels);

FactoryTest::FactoryTest() {
  ESP_LOGI(TAG, "FactoryTest initialized");
  m_sta_netif = nullptr;

  // 初始化产测状态结构体
  factory_test_state_.test_state_ = FACTORY_TEST_OFF;
  factory_test_state_.mic_test = false;
  factory_test_state_.mic_left_test = false;
  factory_test_state_.mic_right_test = false;
  factory_test_state_.button_test = false;
}

FactoryTest::~FactoryTest() { 
  CleanupTasks();
  WifiScanDeinit();
  ESP_LOGI(TAG, "FactoryTest destroyed");
}

// 产测进行中
bool FactoryTest::IsFactoryTesting(){
    return (factory_test_state_.test_state_ != FACTORY_TEST_OFF);
}

bool FactoryTest::ShouldEnterFactoryTest() {
#if FACTORY_TEST_ALWAYS_RUN
    return true;
#else
    return !IsFactoryTested();
#endif
}

void FactoryTest::FactoryTestInit(){
    // 初始化状态值
    factory_test_state_.test_state_ = FACTORY_TEST_OFF;
    factory_test_state_.mic_test = false;
    factory_test_state_.mic_left_test = false;
    factory_test_state_.mic_right_test = false;
    factory_test_state_.button_test = false;

    if (ShouldEnterFactoryTest()) {
        ESP_LOGI(TAG, "产测模式: 关闭输入参考，启用双麦输入");
        SetReferenceInputParam(false, 2);
    } else {
        ESP_LOGI(TAG, "正常模式: 启用输入参考");
        SetReferenceInputParam(true, 2);
    }
}

void FactoryTest::Start() {
  factory_test_state_.test_state_ = FACTORY_TEST_OFF;
  button_test_triggered_ = false;
  exit_triggered_ = false;
  
#if !FACTORY_TEST_ALWAYS_RUN
  // 检查是否已完成产测
  if (!ShouldEnterFactoryTest()) {
    ESP_LOGI(TAG, "Factory test already completed, skipping");
    return;
  }
#endif
  
  auto& sig_led = PotekSingleLed::GetInstance();
  auto& app = Application::GetInstance();
  auto& board = Board::GetInstance();
  auto codec = board.GetAudioCodec();
  ESP_LOGI(TAG, "Starting factory test");
  codec->SetOutputVolume(TEST_AUDIO_VOLUME);
  sig_led.SingleLedrgbBlink(LED_BRIGHTNESS_NORMAL, LED_BRIGHTNESS_NORMAL, LED_BRIGHTNESS_NORMAL, 200);

  ESP_LOGW(TAG, "开始产测");
  app.PlaySound(Lang::Sounds::OGG_USER_PTEST_START);
  vTaskDelay(pdMS_TO_TICKS(500));
  
  // 初始化并扫描WiFi
  vTaskDelay(pdMS_TO_TICKS(500));
  WifiScanInit();
  int scan_result = -1;
  for (size_t i = 0; i < WIFI_SCAN_MAX_RETRY; i++) {
    // app.PlaySound(Lang::Sounds::OGG_SUNSHINEGUITAR);
    scan_result = WifiScanOnce();
    ESP_LOGI(TAG, "WiFi scan attempt %d, result: %d", i + 1, scan_result);
    if (scan_result == 1) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(DELAY_WIFI_SCAN_RETRY_MS));
  }
  vTaskDelay(pdMS_TO_TICKS(500));
  
    WifiScanDeinit();
    // 未扫到WiFi，退出产测
    if (scan_result != 1) {
        ESP_LOGW(TAG, "wifi失败");
        app.PlaySound(Lang::Sounds::OGG_USER_WIFI_LOW);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }
    ESP_LOGW(TAG, "wifi扫描成功");

//   bool connect_result = WifiConnectTest(TEST_MODE_WIFI_SSID, TEST_MODE_WIFI_PASSWORD, WIFI_CONNECT_TIMEOUT_MS);
//   WifiScanDeinit();
//     if (!connect_result) {
//         ESP_LOGW(TAG, "wifi连接失败");
//         app.PlaySound(Lang::Sounds::OGG_USER_WIFI_ERR);
//         vTaskDelay(pdMS_TO_TICKS(3000));
//         return;
//     }
//     ESP_LOGW(TAG, "wifi连接通过");
    app.PlaySound(Lang::Sounds::OGG_USER_WIFI_PASS);
    vTaskDelay(pdMS_TO_TICKS(1000));


  // 启动产测任务
  factory_test_state_.test_state_ = FACTORY_TEST_INIT;
  mic_test_ack_flg_ = false;
  test_pass_cnt_flg_ = 0;
  
  BaseType_t ret = xTaskCreate(
      [](void* arg) {
        FactoryTest* factory_test = static_cast<FactoryTest*>(arg);
        factory_test->FactoryTestTask();
        vTaskDelete(NULL);
      },
      "factory_test_task", 8000, this, 3, &factory_test_task_handle_);
  
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create factory test task");
    factory_test_state_.test_state_ = FACTORY_TEST_OFF;
    return;
  }
  
  vTaskDelay(pdMS_TO_TICKS(DELAY_TASK_START_MS));

  // 等待产测任务完成
  while(factory_test_state_.test_state_ != FACTORY_TEST_OFF) {
    vTaskDelay(pdMS_TO_TICKS(DELAY_TASK_LOOP_MS));
  }
  
  vTaskDelay(pdMS_TO_TICKS(DELAY_TASK_CLEANUP_MS));
  ESP_LOGI(TAG, "Factory test completed");
}

void FactoryTest::FactoryTestTask() {
  auto& board = Board::GetInstance();
  auto codec = board.GetAudioCodec();
  auto& sig_led = PotekSingleLed::GetInstance();
  auto& app = Application::GetInstance();

  uint16_t timeout_counter = 0;
  char notice_str[100] = {0};
  FACTORY_TEST_E previous_state = FACTORY_TEST_OFF;

  while(1) {
    // 外部退出触发
    if (exit_triggered_) {
      exit_triggered_ = false;

        app.PlaySound(Lang::Sounds::OGG_USER_PTEST_END);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGW(TAG, "退出产测");

      factory_test_state_.test_state_ = FACTORY_TEST_EXIT;
    }

    // 状态变化时重置计数器
    if(previous_state != factory_test_state_.test_state_) {
      previous_state = factory_test_state_.test_state_;
      timeout_counter = 0;
      if (factory_test_state_.test_state_ != FACTORY_TEST_MIC_TEST) {
        test_pass_cnt_flg_ = 0;
      }
    }

    switch(factory_test_state_.test_state_) {
      case FACTORY_TEST_INIT:
        HandleInitState(codec, notice_str);
        break;
        
      case FACTORY_TEST_SPEAKER_TEST:
        HandleSpeakerTest(codec, sig_led, timeout_counter, notice_str);
        break;
        
      case FACTORY_TEST_MIC_TEST:
        HandleMicTest(sig_led, timeout_counter, notice_str);
        break;
        
      case FACTORY_TEST_BUTTON_TEST:
        HandleButtonTest(sig_led, notice_str);
        break;
        
      case FACTORY_TEST_EXIT:
        HandleExitState(sig_led);
        return;  // 直接返回，任务会自我删除
        
      default:
        ESP_LOGW(TAG, "Unknown factory test state: %d", factory_test_state_.test_state_);
        factory_test_state_.test_state_ = FACTORY_TEST_EXIT;
        break;
    }

    FactoryTestShowNotification(notice_str);
    vTaskDelay(pdMS_TO_TICKS(DELAY_TASK_LOOP_MS));
  }
}

void FactoryTest::HandleInitState(AudioCodec* codec, char* notice_str) {
  codec->SetOutputVolume(TEST_AUDIO_VOLUME);
  snprintf(notice_str, 100, "factory test");
  factory_test_state_.test_state_ = FACTORY_TEST_SPEAKER_TEST;
  ESP_LOGI(TAG, "Factory test initialized, starting speaker test");
}

void FactoryTest::HandleSpeakerTest(AudioCodec* codec, PotekSingleLed& sig_led, 
                                     uint16_t& timeout_counter, char* notice_str) {
  if (++timeout_counter < TEST_TIMEOUT_SPEAK) {
    PlaySoundSpeakTest();
    sig_led.SingleLedrgbSet(LED_BRIGHTNESS_NORMAL, LED_BRIGHTNESS_NORMAL, LED_BRIGHTNESS_NORMAL);
    snprintf(notice_str, 100, "factory test");
  } else {
    timeout_counter = 0;
    factory_test_state_.test_state_ = FACTORY_TEST_MIC_TEST;
    ESP_LOGI(TAG, "Speaker test completed, starting mic test");
  }
}

void FactoryTest::HandleMicTest(PotekSingleLed& sig_led, uint16_t& timeout_counter, char* notice_str) {
  struct MicDetectResult {
    bool left_detected;
    bool right_detected;
  };

  static uint8_t left_detect_streak = 0;
  static uint8_t right_detect_streak = 0;
  constexpr int kMicStartupDiscardFrames = 20;
  const int samples_per_frame = AUDIO_SAMPLE_RATE * AUDIO_FRAME_DURATION_MS / 1000;

  // 单次检查内连续读取多帧音频，避免每秒只采到 1 帧导致漏检
  auto detect_sound = [&]() -> MicDetectResult {
    auto& app = Application::GetInstance();
    auto& audio_service = app.GetAudioService();
    auto calculate_stats = [](const std::vector<int16_t>& data) -> std::tuple<int16_t, int16_t, float> {
      if (data.empty()) return {0, 0, 0.0f};
      int16_t min_val = INT16_MAX;
      int16_t max_val = INT16_MIN;
      int64_t sum_abs = 0;
      for (int16_t val : data) {
        min_val = std::min(min_val, val);
        max_val = std::max(max_val, val);
        sum_abs += abs(val);
      }
      float avg_abs = static_cast<float>(sum_abs) / data.size();
      return {min_val, max_val, avg_abs};
    };

    auto calculate_rms = [](const std::vector<int16_t>& data) -> int32_t {
      if (data.empty()) return 0;
      int64_t sum_sq = 0;
      for (const auto& sample : data) {
        sum_sq += static_cast<int64_t>(sample) * sample;
      }
      return static_cast<int32_t>(sqrt(static_cast<double>(sum_sq) / data.size()));
    };

    int left_hit_frames = 0;
    int right_hit_frames = 0;
    int32_t mic1_peak_rms = 0;
    int32_t mic2_peak_rms = 0;
    int16_t mic1_last_min = 0;
    int16_t mic1_last_max = 0;
    int16_t mic2_last_min = 0;
    int16_t mic2_last_max = 0;
    float mic1_last_avg_abs = 0.0f;
    float mic2_last_avg_abs = 0.0f;
    std::vector<int16_t> mic1_last_data;
    std::vector<int16_t> mic2_last_data;

    for (int frame_index = 0; frame_index < MIC_DETECT_FRAMES_PER_CHECK; ++frame_index) {
      std::vector<int16_t> audio_data;
      if (!audio_service.ReadAudioData(audio_data, AUDIO_SAMPLE_RATE, samples_per_frame) || audio_data.empty()) {
        continue;
      }

      std::vector<int16_t> mic1_data, mic2_data;
      for (size_t i = 0; i < audio_data.size(); i += 2) {
        mic1_data.push_back(audio_data[i]);
        if (i + 1 < audio_data.size()) {
          mic2_data.push_back(audio_data[i + 1]);
        }
      }
      if (mic1_data.empty() || mic2_data.empty()) {
        continue;
      }

      auto [mic1_min, mic1_max, mic1_avg_abs] = calculate_stats(mic1_data);
      auto [mic2_min, mic2_max, mic2_avg_abs] = calculate_stats(mic2_data);
      int32_t mic1_rms = calculate_rms(mic1_data);
      int32_t mic2_rms = calculate_rms(mic2_data);

      if (mic1_rms > AUDIO_RMS_THRESHOLD) {
        left_hit_frames++;
      }
      if (mic2_rms > AUDIO_RMS_THRESHOLD) {
        right_hit_frames++;
      }

      mic1_peak_rms = std::max(mic1_peak_rms, mic1_rms);
      mic2_peak_rms = std::max(mic2_peak_rms, mic2_rms);
      mic1_last_min = mic1_min;
      mic1_last_max = mic1_max;
      mic2_last_min = mic2_min;
      mic2_last_max = mic2_max;
      mic1_last_avg_abs = mic1_avg_abs;
      mic2_last_avg_abs = mic2_avg_abs;
      mic1_last_data = std::move(mic1_data);
      mic2_last_data = std::move(mic2_data);
    }

    static int sample_print_count = 0;
    sample_print_count++;

    ESP_LOGI(TAG, "=== MIC SAMPLE #%d ===", sample_print_count);
    ESP_LOGI(TAG, "Check frames: %d, Mic1 hit: %d/%d, Mic2 hit: %d/%d, threshold:%d",
             MIC_DETECT_FRAMES_PER_CHECK, left_hit_frames, MIC_DETECT_HIT_FRAMES,
             right_hit_frames, MIC_DETECT_HIT_FRAMES, AUDIO_RMS_THRESHOLD);
    ESP_LOGI(TAG, "MIC1 peak RMS: %d, Min: %d, Max: %d, Avg|Abs|: %.1f",
             mic1_peak_rms, mic1_last_min, mic1_last_max, mic1_last_avg_abs);
    ESP_LOGI(TAG, "MIC2 peak RMS: %d, Min: %d, Max: %d, Avg|Abs|: %.1f",
             mic2_peak_rms, mic2_last_min, mic2_last_max, mic2_last_avg_abs);

    if (mic1_last_data.size() >= 10) {
      char mic1_buf[128] = {0};
      sprintf(mic1_buf, "MIC1[0-9]: ");
      for (int i = 0; i < 10; i++) {
        sprintf(mic1_buf + strlen(mic1_buf), "%d,", mic1_last_data[i]);
      }
      ESP_LOGI(TAG, "%s", mic1_buf);
    }

    if (mic2_last_data.size() >= 10) {
      char mic2_buf[128] = {0};
      sprintf(mic2_buf, "MIC2[0-9]: ");
      for (int i = 0; i < 10; i++) {
        sprintf(mic2_buf + strlen(mic2_buf), "%d,", mic2_last_data[i]);
      }
      ESP_LOGI(TAG, "%s", mic2_buf);
    }

    bool mic1_frame_detected = left_hit_frames >= MIC_DETECT_HIT_FRAMES;
    bool mic2_frame_detected = right_hit_frames >= MIC_DETECT_HIT_FRAMES;

    left_detect_streak = mic1_frame_detected ? std::min<uint8_t>(left_detect_streak + 1, MIC_DETECT_CONSECUTIVE_FRAMES) : 0;
    right_detect_streak = mic2_frame_detected ? std::min<uint8_t>(right_detect_streak + 1, MIC_DETECT_CONSECUTIVE_FRAMES) : 0;

    bool mic1_detected = left_detect_streak >= MIC_DETECT_CONSECUTIVE_FRAMES;
    bool mic2_detected = right_detect_streak >= MIC_DETECT_CONSECUTIVE_FRAMES;

    ESP_LOGI(TAG, "MIC1 check: %s streak:%u/%d, MIC2 check: %s streak:%u/%d",
             mic1_frame_detected ? "YES" : "NO", left_detect_streak, MIC_DETECT_CONSECUTIVE_FRAMES,
             mic2_frame_detected ? "YES" : "NO", right_detect_streak, MIC_DETECT_CONSECUTIVE_FRAMES);
    ESP_LOGI(TAG, "MIC1 detected: %s, MIC2 detected: %s",
             mic1_detected ? "YES" : "NO", mic2_detected ? "YES" : "NO");

    return {mic1_detected, mic2_detected};
  };

  if (timeout_counter == 0) {
    left_detect_streak = 0;
    right_detect_streak = 0;

    auto& app = Application::GetInstance();
    auto& audio_service = app.GetAudioService();
    std::vector<int16_t> discard_data;
    for (int frame_index = 0; frame_index < kMicStartupDiscardFrames; ++frame_index) {
      discard_data.clear();
      audio_service.ReadAudioData(discard_data, AUDIO_SAMPLE_RATE, samples_per_frame);
    }
    ESP_LOGI(TAG, "Discarded %d mic startup frames before detection", kMicStartupDiscardFrames);
  }

  auto mic_result = detect_sound();
  factory_test_state_.mic_left_test = mic_result.left_detected;
  factory_test_state_.mic_right_test = mic_result.right_detected;
  factory_test_state_.mic_test = mic_result.left_detected && mic_result.right_detected;// 正确调试时屏蔽
//   factory_test_state_.mic_test = mic_result.left_detected || mic_result.right_detected;// 错误 

  if (factory_test_state_.mic_test) {
    sig_led.SingleLedrgbSet(0, LED_BRIGHTNESS_NORMAL, 0);
    mic_test_ack_flg_ = true;
  } else {
    sig_led.SingleLedrgbSet(LED_BRIGHTNESS_NORMAL, 0, 0);
    mic_test_ack_flg_ = false;
  }

  auto& app = Application::GetInstance();
  // 麦克风通过次数大于0 按下按键进入麦克风测试
  if (button_test_triggered_ && test_pass_cnt_flg_ > 0) {
    // button_test_triggered_ = false;
    factory_test_state_.test_state_ = FACTORY_TEST_BUTTON_TEST;
    factory_test_state_.button_test = true;
    // FactoryTestFlagWrite();
    // // PlaySoundButtonTestAck();
    // app.PlaySound(Lang::Sounds::OGG_USER_PTEST_PASS);
    // vTaskDelay(pdMS_TO_TICKS(1000));
    test_pass_cnt_flg_ = 1;
    snprintf(notice_str, 100, "key OK %d", test_pass_cnt_flg_);
    ESP_LOGI(TAG, "Transitioned to button test");
    return;
  }
  else{
    button_test_triggered_ = false;
  }

  if (++timeout_counter < TEST_TIMEOUT_MIC) {
    if (mic_test_ack_flg_) {
      PlaySoundMicTestAck();
      vTaskDelay(pdMS_TO_TICKS(DELAY_MIC_ACK_MS));
    //   timeout_counter++;
      mic_test_ack_flg_ = false;
      test_pass_cnt_flg_++;
      snprintf(notice_str, 100, "MIC LOK ROK %d", test_pass_cnt_flg_);
      ESP_LOGI(TAG, "Mic test passed, count: %d", test_pass_cnt_flg_);
    } else {
      if (factory_test_state_.mic_left_test && !factory_test_state_.mic_right_test) {
        snprintf(notice_str, 100, "MIC LOK RFAIL");
      } else if (!factory_test_state_.mic_left_test && factory_test_state_.mic_right_test) {
        snprintf(notice_str, 100, "MIC LFAIL ROK");
      } else if (!factory_test_state_.mic_left_test && !factory_test_state_.mic_right_test) {
        snprintf(notice_str, 100, "MIC LFAIL RFAIL");
      } else if (test_pass_cnt_flg_ > 0) {
        snprintf(notice_str, 100, "MIC LOK ROK %d", test_pass_cnt_flg_);
      } else {
        snprintf(notice_str, 100, "MIC test %d/%d", timeout_counter, TEST_TIMEOUT_MIC);
      }
    }
  } else {

    auto& app = Application::GetInstance();
    app.PlaySound(Lang::Sounds::OGG_USER_MIC_ERR);
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGW(TAG, "麦克风测试超时");

    // timeout_counter = 0;
    factory_test_state_.test_state_ = FACTORY_TEST_EXIT;
    if (factory_test_state_.mic_left_test && !factory_test_state_.mic_right_test) {
      snprintf(notice_str, 100, "MIC timeout RFAIL");
    } else if (!factory_test_state_.mic_left_test && factory_test_state_.mic_right_test) {
      snprintf(notice_str, 100, "MIC timeout LFAIL");
    } else if (!factory_test_state_.mic_left_test && !factory_test_state_.mic_right_test) {
      snprintf(notice_str, 100, "MIC timeout LRFAIL");
    } else {
      snprintf(notice_str, 100, "MIC timeout");
    }
    ESP_LOGW(TAG, "Mic test timeout");
  }
}

void FactoryTest::HandleButtonTest(PotekSingleLed& sig_led, char* notice_str) {
    auto& app = Application::GetInstance();
    // 按键触发标志用于计数
    if (button_test_triggered_) {
        button_test_triggered_ = false;
        // 播放 产测通过
        PlaySoundButtonTestAck();
        FactoryTestFlagWrite();
        vTaskDelay(pdMS_TO_TICKS(1000));
        // 播放 产测结束
        app.PlaySound(Lang::Sounds::OGG_USER_PTEST_END);
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP_LOGW(TAG, "退出产测");
        ESP_LOGW(TAG, "重启设备");
        esp_restart();
        vTaskDelay(pdMS_TO_TICKS(200));

        // test_pass_cnt_flg_++;
        // ESP_LOGI(TAG, "Button test count: %d", test_pass_cnt_flg_);
    }

    // if(test_pass_cnt_flg_ > 0) {
    //     sig_led.SingleLedrgbSet(0, LED_BRIGHTNESS_NORMAL, 0);
    //     snprintf(notice_str, 100, "key OK %d", test_pass_cnt_flg_);
    // } else {
    //     sig_led.SingleLedrgbSet(0, LED_BRIGHTNESS_DIM, 0);
    //     snprintf(notice_str, 100, "key test");
    // }
}

void FactoryTest::HandleExitState(PotekSingleLed& sig_led) {
  sig_led.OnStateChanged();
  ESP_LOGI(TAG, "Factory test task exiting, mic_test: %s, mic_left_test: %s, mic_right_test: %s, button_test: %s",
           factory_test_state_.mic_test ? "PASS" : "FAIL",
           factory_test_state_.mic_left_test ? "PASS" : "FAIL",
           factory_test_state_.mic_right_test ? "PASS" : "FAIL",
           factory_test_state_.button_test ? "PASS" : "FAIL");
  factory_test_task_handle_ = nullptr;
  factory_test_state_.test_state_ = FACTORY_TEST_OFF;
  vTaskDelete(NULL);
}

void FactoryTest::CleanupTasks() {
  if (factory_test_task_handle_ != nullptr) {
    vTaskDelete(factory_test_task_handle_);
    factory_test_task_handle_ = nullptr;
  }
}

// 喇叭测试
void FactoryTest::PlaySoundSpeakTest() {
    auto& app = Application::GetInstance();
    app.PlaySound(Lang::Sounds::OGG_POPUP);
}
// 麦克风测试应答
void FactoryTest::PlaySoundMicTestAck() {
    auto& app = Application::GetInstance();
    app.PlaySound(Lang::Sounds::OGG_USER_MIC_PASS);
}
// 按键测试 按键按下提示音
void FactoryTest::PlaySoundButtonTestAck() {
    auto& app = Application::GetInstance();
    app.PlaySound(Lang::Sounds::OGG_USER_PTEST_PASS);
}

void FactoryTest::FactoryTestFlagWrite() { 
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(FACTORY_TEST_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
    return;
  }

  int32_t flag_value = 0x55555555;
  ret = nvs_set_i32(nvs_handle, FACTORY_TEST_FLAGT, flag_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error writing factory test flag: %s", esp_err_to_name(ret));
    nvs_close(nvs_handle);
    return;
  }

  ret = nvs_commit(nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error committing NVS changes: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Factory test flag written successfully");
  }

  nvs_close(nvs_handle);
}

bool FactoryTest::IsFactoryTested() {
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(FACTORY_TEST_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGD(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
    return false;
  }

  int32_t flag_value = 0;
  ret = nvs_get_i32(nvs_handle, FACTORY_TEST_FLAGT, &flag_value);
  nvs_close(nvs_handle);

  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGD(TAG, "Factory test flag not found in NVS");
    return false;
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error reading factory test flag: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_LOGI(TAG, "flag_value= %d", flag_value);

  return (flag_value == 0x55555555);
}

void FactoryTest::FactoryTestFlagButtonWrite() {
   // 检查标志位
  if (!IsFactoryTested()) {
    FactoryTestFlagWrite();
  }
}

void FactoryTest::FactoryTestFlagClear() { 
  nvs_handle_t nvs_handle;
  esp_err_t ret = nvs_open(FACTORY_TEST_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
    return;
  }

  ret = nvs_set_i32(nvs_handle, FACTORY_TEST_FLAGT, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error clearing factory test flag: %s", esp_err_to_name(ret));
    nvs_close(nvs_handle);
    return;
  }

  ret = nvs_commit(nvs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error committing NVS changes: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Factory test flag cleared successfully");
  }

  nvs_close(nvs_handle);
}


/******************************************************************
 * wifi相关
 ******************************************************************
 */
/**
 * wifi扫描初始化
 *  */ 
void FactoryTest::WifiScanInit(){
  ESP_LOGI(TAG, "正在初始化WiFi扫描资源");

  // 1. 初始化网络接口
  ESP_ERROR_CHECK(esp_netif_init());

  // 2. 创建默认事件循环
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // 3. 初始化WiFi驱动
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // 4. 创建STA模式网络接口
  m_sta_netif = esp_netif_create_default_wifi_sta();

  // 5. 设置WiFi模式并启动
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi扫描资源初始化完成");
}
/**
 * 扫描产测wifi一次
 * 返回值 未扫到 -1  扫到1 信号弱 2
 */
int FactoryTest::WifiScanOnce() {
  int scan_result = -1;
  wifi_scan_config_t scan_config = {
      .channel = 0,
      .show_hidden = false,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE
  };
  
  ESP_LOGI(TAG, "Starting WiFi scan...");
  esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(ret));
    return -1;
  }
  
  uint16_t ap_count = 0;
  ret = esp_wifi_scan_get_ap_num(&ap_count);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
    return -1;
  }
  
  ESP_LOGI(TAG, "WiFi scan completed, found %d APs", ap_count);

  if (ap_count > 0) {
    wifi_ap_record_t* ap_list = 
        static_cast<wifi_ap_record_t*>(malloc(sizeof(wifi_ap_record_t) * ap_count));
    
    if (ap_list == nullptr) {
      ESP_LOGE(TAG, "Failed to allocate memory for AP list");
      return -1;
    }
    
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
      free(ap_list);
      return -1;
    }

    // 查找目标SSID
    for (int i = 0; i < ap_count; i++) {
      if (strcmp(reinterpret_cast<const char*>(ap_list[i].ssid), TEST_MODE_WIFI_SSID) == 0) {
        if(ap_list[i].rssi > WIFI_RSSI_THRESHOLD_STRONG) {
          ESP_LOGI(TAG, "Found target SSID: %s (RSSI: %d dBm)", TEST_MODE_WIFI_SSID, ap_list[i].rssi);
          scan_result = 1;
        } else {
          ESP_LOGI(TAG, "Found target SSID: %s (weak signal: %d dBm)", TEST_MODE_WIFI_SSID, ap_list[i].rssi);
          scan_result = 2;
        }
        break;
      }
    }
    
    free(ap_list);
  }
  
  return scan_result;
}

/**
 * wifi扫描资源释放
 */
bool FactoryTest::WifiConnectTest(const char* ssid, const char* password, int timeout_ms) {
  if (ssid == nullptr || ssid[0] == '\0') {
    ESP_LOGE(TAG, "WiFi connect test failed: empty ssid");
    return false;
  }

  wifi_config_t wifi_config = {};
  strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid, sizeof(wifi_config.sta.ssid) - 1);
  if (password != nullptr && password[0] != '\0') {
    strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  } else {
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi set config failed: %s", esp_err_to_name(ret));
    return false;
  }

  esp_wifi_disconnect();
  ret = esp_wifi_connect();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi connect start failed: %s", esp_err_to_name(ret));
    return false;
  }

  const int wait_step_ms = 200;
  int waited_ms = 0;
  while (waited_ms < timeout_ms) {
    wifi_ap_record_t ap_info = {};
    esp_netif_ip_info_t ip_info = {};

    bool associated = esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
    bool got_ip = m_sta_netif != nullptr &&
                  esp_netif_get_ip_info(m_sta_netif, &ip_info) == ESP_OK &&
                  ip_info.ip.addr != 0;
    if (associated && got_ip) {
      ESP_LOGI(TAG, "WiFi connect test success, ssid:%s, rssi:%d", ssid, ap_info.rssi);
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(wait_step_ms));
    waited_ms += wait_step_ms;
  }

  ESP_LOGW(TAG, "WiFi connect test timeout, ssid:%s", ssid);
  esp_wifi_disconnect();
  return false;
}

void FactoryTest::WifiScanDeinit() {
  ESP_LOGI(TAG, "正在释放WiFi资源");

  // 1. 停止WiFi扫描（如果正在进行）
  esp_wifi_scan_stop();

  // 2. 断开WiFi连接
  esp_wifi_disconnect();

  // 3. 停止WiFi
  esp_wifi_stop();

  // 4. 销毁网络接口
  if (m_sta_netif) {
    esp_netif_destroy_default_wifi(m_sta_netif);
    m_sta_netif = nullptr;
  }

  // 5. 反初始化WiFi驱动
  esp_wifi_deinit();

  ESP_LOGI(TAG, "WiFi资源释放完成");
}

void FactoryTest::FactoryTestButtonOnClick() {
  if (factory_test_state_.test_state_ == FACTORY_TEST_OFF) {
    return;
  }
  // 仅设置触发标志，具体动作在任务中完成
  button_test_triggered_ = true;
}

void FactoryTest::FactoryTestButtonDoubleClick() {
  if (factory_test_state_.test_state_ == FACTORY_TEST_OFF) {
    return;
  }
  exit_triggered_ = true;
  ESP_LOGI(TAG, "Exit trigger received (double click)");
}

//
void FactoryTest::FactoryTestShowNotification(const char* str)
{
  auto& board = Board::GetInstance();
  // board.GetDisplay()->ShowNotification(str, 500);
  board.GetDisplay()->ShowNotification(str);
}

// 获取产测状态结构体方法实现
FACTORY_TEST_STATE_T FactoryTest::GetFactoryTestStateStruct() {
    return factory_test_state_;
}


/**
 * 重新进入产测
 */
void FactoryTest::ReEnterFactoryTest() {
    FactoryTestFlagClear();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    vTaskDelay(pdMS_TO_TICKS(200));
}


/**
 * 结束产测
 */
void FactoryTest::ExitFactoryTest() {
  if (factory_test_state_.test_state_ == FACTORY_TEST_OFF) {
    return;
  }
  // 仅设置触发标志，具体动作在任务中完成
  button_test_triggered_ = true;
}
