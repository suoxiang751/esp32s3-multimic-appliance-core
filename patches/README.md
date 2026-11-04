# 音频改动补丁

上游小智源码未打包进此仓库。补丁保留必要上下文，完整 MIT 许可见 [UPSTREAM_MIT_LICENSE.txt](../UPSTREAM_MIT_LICENSE.txt)。精确基线提交、补丁 SHA-256 和检查结果见 [PATCH_BASES.json](PATCH_BASES.json)。

| 文件前缀 | 上游基线 | 改动重点 |
| --- | --- | --- |
| `626a56cc0__` | `v2.2.3` | ES7210 参考通道与多通道采集、RMNM、双麦 SE/BSS 与 WakeNet 协同、音频任务状态/代次、PSRAM 栈、ESP-SR 依赖 |
| `76ed7e282__` | `v2.5.0` | 选择已有 NSNet 模型、增益配置、模型资源打包 |

检查方式：在对应上游 tag 的副本中，执行 `git apply --check` 并传入同一前缀的全部 `.patch` 文件。两组均已在导出的精确基线文件上检查通过；检查没有修改原工程。

补丁组聚焦音频。恢复完整产品仍需要板级 GPIO/I2S、模型分区、Kconfig、工具/设备适配和对应 IDF/组件版本。补丁能干净应用只证明文本基线匹配，不证明固件编译、链接或板上声学性能。
