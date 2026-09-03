# HoppieDatalinkXP

`HoppieDatalinkXP` 是面向 X-Plane 12.04+ 的通用数据链插件，提供 Hoppie CPDLC、DCL、气象/ATIS 请求、TELEX、飞行计划辅助和周期 ADS-C。插件不绑定特定机型，也不会把许可自动写入 FMS。

> 仅供飞行模拟。CPDLC 不能替代语音守听，不得用于真实航空运行。

## 功能

- 中英文通用 DCDU 浮动窗口，可移动、缩放及弹出；航空报文始终以大写英文发送。
- CPDLC LOGON、LOGOFF、HANDOVER，以及高度、直飞、速度/Mach、WHEN CAN WE EXPECT、位置报告、自由文本和 Oceanic Clearance。
- 按报文响应类型提供 WILCO/UNABLE、AFFIRM/NEGATIVE、ROGER、STANDBY；DCL 许可只显示 ACCEPT，并阻止同一报文重复最终回复。
- Hoppie TELEX DCL，以及 METAR、TAF、SHORT TAF、VATSIM/IVAO/PilotEdge ATIS 和普通 TELEX。
- 可选周期 ADS-C；只接受不短于 60 秒的 `REQUEST PERIODIC n`，不支持事件合约。
- 按 CID 读取 VATSIM 在线航班（找不到时读取 prefile），并按 Pilot ID 读取最新 SimBrief OFP 航路与 navlog。
- 单网络工作线程；所有 XPLM 调用都留在 X-Plane 主线程。HTTPS 超时不会冻结画面。
- Hoppie logon code 使用 Windows Credential Manager 或 macOS Keychain。安全存储失败时只保留到当前运行。

数据优先级为：当前草稿的手动输入 > 在线 VATSIM > VATSIM prefile > SimBrief 补全。纬度、经度和实时 MSL 高度始终来自 X-Plane dataref。
DCL 呼号例外：始终使用当前 Hoppie 会话呼号，不接受草稿或航班计划覆盖。

## 安装

### 0.1.6 回复结果与原生窗口弹出

- 在收到的消息正文下方显示最后一次发送成功的机组回复：WILCO／AFFIRM／ROGER 为绿色，UNABLE／NEGATIVE 为红色，STANDBY 为橙色；DCL 继续显示绿色、不可点击的 ACCEPTED。
- STANDBY 后仍可最终回复，最终回复发送成功后替换 STANDBY 字样。等待发送确认或发送失败不会把新回复显示为成功；已有的成功回复保留。切换消息或页面后状态仍保留，TX 不新增收件行。
- 这些字样表示 Hoppie 已接收机组回复，不代表管制已阅读或再次确认。无需回复的消息仍只读，不凭空添加回复。
- 删除 DCDU 内的 POP OUT／弹出窗口按钮及其窗口模式切换代码。弹出与还原只使用 X-Plane 标题栏的原生入口，避免重复触发自定义切换路径。

升级时先退出 X-Plane，备份旧插件目录，再替换整个 `HoppieDatalinkXP` 目录。
重启后确认顶部显示 `v0.1.6`，无需删除偏好设置或凭据。

### 0.1.5 CPDLC 全量收件与 30 秒轮询

- 正常轮询固定为 30 秒，首次实际请求后启动，取消原来的随机间隔和 20 秒快速轮询；后续发送不会推迟已有收件计划。失败仍按 60／120／240／300 秒退避，恢复后回到 30 秒。
- 修复 `NE` 被解析器拒绝的问题：`NE/LOGON ACCEPTED` 正常更新当前 Station、清除 Pending Station；`MESSAGE NOT SUPPORTED BY THIS ATS UNIT` 等通知也会显示。匹配待定 Station 的 CURRENT ATC UNIT 通知可完成状态更新，其他站的通知不能接管会话。
- 所有收到的 CPDLC 都进入收件显示路径。未知响应类型只读显示；无法解析的旧格式、畸形包按原始报文只读显示，不再静默丢弃。显示 ID、REPLY TO 和 RESPONSE，便于核对服务器记录。
- 去重不再只看发送方和消息 ID；同 ID 的不同正文、响应类型或关联均保留，只合并同一发送方的完全相同重传。删除后去重记录仍保留。TX 继续隐藏，普通合法请求仍可回复，N／NE／未知类型不生成回复按钮。
- 一批 Hoppie 信封中，后续畸形信封不再丢掉前面已完整解析的报文。

30 秒是按本次需求采用的频率，快于 [Hoppie 官方推荐的 45–75 秒](https://www.hoppie.nl/acars/system/tech.html)，并非官方推荐值。
旧版已从服务器提取、但在本机丢弃的报文不会自动恢复；升级后请重新 CONNECT／LOGON，并请管制重发需要查看的消息。

### 0.1.4 DCL 高亮与接受状态

- MESSAGES 正文隐藏 `@` 标记，成对 `@...@` 内的文本显示为橙色。保留正常换行和窗口缩放排版；原始报文不改写。落单 `@` 隐藏，但之后的文字不会丢失或整段误高亮。
- 支持截图中的 `CLD ... PDC ... CLRD TO ...` 等离场许可格式，只显示一个 ACCEPT 按钮；普通 CPDLC 指令仍保留原有回复选项。
- 点击后显示 SENDING REPLY...；Hoppie 确认发送成功后变为绿色、不可点击的 ACCEPTED。失败不显示 ACCEPTED，允许核实服务器记录后手动重试，不自动重发。
- ACCEPT 根据响应类型发送 WILCO（WU）、AFFIRMATIVE（AN）或 ROGER（R/Y），并引用原消息 ID。ACCEPTED 表示本机的接受回复已被 Hoppie 接收，不代表收到管制的二次确认。
- 无需回复的 N 消息及不含 CPDLC 关联字段的普通 TELEX 仍只读，不凭空生成协议回复。

### 0.1.3 设置、DCL 呼号与提示音

- SETTINGS 的 DEFAULT ATIS 显示 `VATSIM`、`IVAO`、`PilotEdge`，底层请求编码和已保存的设置保持兼容。
- 新配置默认英文；已保存的中文或英文选择不会被覆盖，可在 SETTINGS → LANGUAGE 修改并 SAVE。
- 界面中的 ATSU 统一显示为 Station，包括 DCL 接收站和当前／待定站状态。
- DCL CALLSIGN 为只读，跟随当前 Hoppie 会话；发送时再次使用会话呼号，换呼号重连后不会沿用旧草稿呼号。
- 来信提示音改为约 1.35 秒的三连“嘟”：每声 350 ms，间隔 150 ms。保留 SOUND 开关；同一段提示音内的新来信不会叠加音量，静音或禁用插件会停止播放。

### 0.1.2 收件与消息管理修复

- 收件轮询固定使用 `to=SERVER`，修复 `{no to address}` 导致 DCL／CPDLC 未被提取的问题；兼容带数字前缀的 Hoppie 信封。
- MESSAGES 只显示 RX。TX 保留为内部协议关联，不显示在收件箱，也不能对自己发出的消息回复。
- 报文列表右键 **DELETE／删除** 删除单条；**CLEAR ALL／清空全部** 一键清空收件箱。删除不可撤销，不会断开连接或取消已经发送的请求；新的来信仍会显示。CPDLC 去重记录保留。
- Hoppie 返回成功后显示绿色发送提示；成功指服务器接收，不是管制确认。超时或失败显示红色提示，不自动重发；核实服务器记录后才手动重试。
- 回复发送期间阻止重复点击，收到服务器确认后才标为完成。失败时解除发送锁；删除正在回复的消息不会导致崩溃或使其重新出现。
- STATUS 显示下次轮询及最近一次成功收件时间。暂时的网络／服务器错误保留重试状态；呼号更改、主动断开或禁用插件会停止该会话的轮询。
- DCL 草稿呼号必须与已连接呼号相同，避免许可发往另一个呼号。LOGON 接受／拒绝仅由待定 ATSU 更新会话；普通 `CONTACT ...` 语音指令不再误当作 ATSU 移交。

### 0.1.1 窗口坐标修复

修正高 DPI／界面缩放下 DCDU 内容绘制在窗口框外、可见控件与点击区域不一致的问题。
布局和鼠标事件统一使用窗口内的逻辑坐标，绘制保留 X-Plane 当前窗口的投影与视口；
同时处理悬停坐标、弹出／还原状态同步和未读报文点击后的样式恢复。

自动坐标测试已覆盖 100%／150%／200% 缩放、多屏负坐标和弹出窗口；模拟器内仍需实际复测。

### 安装步骤

1. 从发布包解压 `HoppieDatalinkXP` 整个目录。
2. 将其复制到 `X-Plane 12/Resources/plugins/`。
3. Windows 应包含 `HoppieDatalinkXP/win_x64/HoppieDatalinkXP.xpl`；macOS 应包含 `HoppieDatalinkXP/mac_x64/HoppieDatalinkXP.xpl`。
4. 启动 X-Plane，在 `Plugins > HoppieDatalinkXP > Toggle DCDU` 打开窗口，或绑定命令 `hoppiedatalinkxp/toggle_window`。
5. 输入呼号及 Hoppie logon code，先按 CONNECT 完成 ping，再向 Station LOGON。

非敏感设置保存到 `X-Plane 12/Output/preferences/HoppieDatalinkXP.json`。报文正文只保存在内存中，模拟器退出后不会保留。

## DCDU 页面

- **STATUS**：Hoppie 连接、当前/待定 Station、LOGON/LOGOFF、VATSIM 与 SimBrief 状态。
- **MESSAGES**：收件列表、未读提示、报文状态、合法回复按钮和彩色机组回复结果；支持右键删除与一键清空，TX 不显示。
- **ATC**：常用 CPDLC 请求、位置报告、自由文本和 Oceanic Clearance。
- **REQUEST**：DCL 表单与发送；呼号只读并跟随 Hoppie 会话，许可只显示供人工复核。
- **WX / TELEX**：气象、ATIS 与 TELEX。
- **SETTINGS**：语言、数据源 ID、默认 ATIS、提示音和 ADS-C 总开关。

## 从源码构建

构建会下载并校验以下固定依赖：X-Plane SDK 4.3.0、Dear ImGui 1.91.8、nlohmann/json 3.11.3 和 Noto Sans CJK 2.004。也可用 `HOPPIE_DEPENDENCY_CACHE` 指向包含归档的离线缓存目录。

Windows（在 x64 Native Tools 命令提示符中）：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build/windows-release --prefix build/stage
```

macOS（Xcode Command Line Tools、CMake 和 Ninja）：

```bash
cmake --preset macos-release
cmake --build --preset macos-release
ctest --preset macos-release
lipo -archs build/macos-release/HoppieDatalinkXP/mac_x64/HoppieDatalinkXP.xpl
cmake --install build/macos-release --prefix build/stage
```

macOS 目标固定为 `x86_64;arm64`，默认执行 ad-hoc 签名。CI 可通过 Developer ID secrets 覆盖签名。

## 排障

- **插件未出现在菜单**：检查平台目录和 `Log.txt` 中的加载错误；不要把 `.xpl` 直接放在插件根目录。
- **内容与点击区域错位**：确认已退出模拟器并更新为 `v0.1.1` 或更新版本；检查是否存在重复安装。若仍异常，请提供窗口截图、X-Plane 版本、系统及模拟器界面缩放比例，以及是否使用弹出窗口／多显示器。
- **CONNECT 失败**：确认呼号、Hoppie code、系统时间、代理/防火墙及 `https://www.hoppie.nl/` 可访问。插件不会降级到 HTTP。
- **一直没有新报文**：CONNECT 只做 ping；插件在第一条实际报文发出后才开始每 30 秒轮询，网络失败时会退避。v0.1.5 已修复 NE／未知 CPDLC 被丢弃的问题，旧版已 relayed 的丢失报文需要管制重发。
- **`no to address` 或官网没有 relayed 时间**：确认已升级 `v0.1.2`；旧版 poll 的收件地址为空。新版在 STATUS 显示轮询时间。检查连接呼号是否与管制发送的收件呼号一致，以及是否有其他客户端使用同一呼号收件。
- **官网一段时间后显示离线**：Hoppie 使用轮询而非永久连接。旧版轮询失败会导致在线状态无法续期；暂时的网络错误也可能造成这一现象。新版会退避重试，最近成功轮询时间可用于诊断，无法保证网络不可用时仍保持服务器在线状态。
- **删除后还在等待管制**：清空只作用于收件箱，不撤回已发出的请求，不清除待定 LOGON。发送提示中的“成功”也不代表管制已同意。
- **呼号更改后停止**：这是预期行为。为避免旧呼号轮询和 ADS-C 泄漏，必须重新 CONNECT/LOGON。
- **没有中文字符**：确认 `HoppieDatalinkXP/Resources/NotoSansCJKsc-Regular.otf` 存在且未被安全软件隔离。
- **macOS 提示无法验证**：测试包使用 ad-hoc 签名。可在系统隐私与安全设置中明确允许运行，正式分发建议使用 Developer ID 重新签名。
- **ADS-C 被拒绝**：先在 SETTINGS 开启总开关；周期必须至少 60 秒，事件合约不会被接受。

## 隐私与日志

插件不创建遥测、不保存长期报文数据库。日志只应记录生命周期、状态、错误类别和消息 ID，不记录 Hoppie code 或报文正文。Windows 凭据目标为 `com.hoppiedatalinkxp.plugin/hoppie-logon`；macOS Keychain service 为 `com.hoppiedatalinkxp.plugin`。

## 致谢与许可证

操作流程和协议实践参考了 [EasyCPDLC](https://github.com/quassbutreally/EasyCPDLC)，感谢其作者与贡献者。HoppieDatalinkXP 是针对 X-Plane SDK、系统网络 API 和通用 DCDU 界面重新实现的独立代码。

本项目按 GNU GPL v3 授权。第三方组件见 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。
