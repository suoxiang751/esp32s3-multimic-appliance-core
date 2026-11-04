# 验证记录

2026-11-03：G++ 主机编译通过，`-std=c++20 -Wall -Wextra -Werror`；`mcu_protocol` 测试通过。

直接编译 `src/protocol/mcu_protocol.cc`，覆盖每一个双段分片位置、带噪声帧头续接、校验错误后恢复、超长声明后恢复、连续两帧、心跳、畸形/未知类型 TLV、大端 value 编解码和发送短写。

7 个 `v2.2.3` 补丁及 2 个 `v2.5.0` 补丁分别通过 `git apply --check`。精确基线见 `patches/PATCH_BASES.json`。

DP/MCP/产测及音频补丁未做完整固件构建或板上运行。主机测试不验证 UART 电气特性、并发调度、MCP 云端互通、声学效果或产测合格率；补丁应用检查不等于编译通过。
