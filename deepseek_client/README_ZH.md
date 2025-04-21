# DeepSeek 聊天客户端

DeepSeek 聊天客户端是一个使用 Rust 和 `egui` 构建的跨平台 GUI 应用程序。它允许用户与 DeepSeek API 交互，提供无缝的 AI 聊天体验，包括支持聊天历史记录和实时更新。

## 功能

- **跨平台**：支持在 Windows、macOS 和 Linux 上运行。
- **现代化 GUI**：基于 `egui` 构建，提供轻量级且响应迅速的界面。
- **聊天历史**：查看和管理以前的对话记录。
- **实时更新**：后台任务确保界面实时更新。
- **中文支持**：完全支持中文输入和显示。

## 截图

![DeepSeek Chat Client Screenshot](assets/screenshot.png)

## 安装

### 前置条件

- [Rust](https://www.rust-lang.org/tools/install)（最新稳定版本）
- [Cargo](https://doc.rust-lang.org/cargo/)（随 Rust 一起安装）
- DeepSeek API 密钥（设置为环境变量）

### 克隆代码库

```bash
git clone https://github.com/yourusername/deepseek_client.git
cd deepseek_client
```

### 构建和运行

1. 安装依赖：
   ```bash
   cargo build --release
   ```

2. 运行应用程序：
   ```bash
   cargo run
   ```

## 使用方法

1. 启动应用程序。
2. 在输入框中输入您的消息并点击“发送”按钮。
3. 实时查看来自 DeepSeek 的响应。
4. 使用左侧面板切换聊天历史记录。

## 环境变量

应用程序需要设置以下环境变量：

- `DEEPSEEK_API_KEY`：您的 DeepSeek API 密钥。
- `DEEPSEEK_API_BASE`（可选）：DeepSeek API 的基础 URL，默认为 `https://api.deepseek.com/v1`。

您可以在项目根目录下创建一个 `.env` 文件来设置这些变量：

```env
DEEPSEEK_API_KEY=your_api_key_here
DEEPSEEK_API_BASE=https://api.deepseek.com/v1
```

## 常见问题

### 常见问题及解决方法

1. **缺少 API 密钥**：
   - 确保已设置 `DEEPSEEK_API_KEY` 环境变量。
   - 验证 `.env` 文件是否位于项目根目录并格式正确。

2. **连接错误**：
   - 检查您的网络连接。
   - 验证 `DEEPSEEK_API_BASE` URL 是否正确且可访问。

3. **构建错误**：
   - 确保已安装最新稳定版本的 Rust。
   - 运行 `cargo clean` 并尝试重新构建项目。

4. **实时更新不起作用**：
   - 确保与 DeepSeek 服务器的 WebSocket 连接是活动的。
   - 检查日志以获取任何错误信息。

## 贡献

欢迎贡献！贡献步骤如下：

1. Fork 此代码库。
2. 为您的功能或修复创建一个新分支：
   ```bash
   git checkout -b feature-name
   ```
3. 提交您的更改：
   ```bash
   git commit -m "Add feature-name"
   ```
4. 推送到您的 Fork：
   ```bash
   git push origin feature-name
   ```
5. 打开一个 Pull Request。

## 许可证

此项目基于 MIT 许可证。详情请参阅 [LICENSE](LICENSE) 文件。

## 鸣谢

- [egui](https://github.com/emilk/egui)：用于构建 GUI 框架。
- [async-openai](https://github.com/64bit/async-openai)：用于集成 OpenAI API。
- [poll-promise](https://github.com/emilk/poll_promise)：用于在 UI 中处理异步任务。
- [dotenv](https://github.com/dotenv-rs/dotenv)：用于管理环境变量。
