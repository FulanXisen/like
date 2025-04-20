# DeepSeek Chat Client

DeepSeek Chat Client is a cross-platform GUI application built with Rust and `egui`. It allows users to interact with the DeepSeek API, providing a seamless chat experience with AI, including support for chat history and real-time updates.

## Features

- **Cross-Platform**: Runs on Windows, macOS, and Linux.
- **Modern GUI**: Built with `egui` for a lightweight and responsive interface.
- **Chat History**: View and manage previous conversations.
- **Real-Time Updates**: Background tasks ensure the UI stays updated.
- **Chinese Language Support**: Fully supports Chinese characters in input and display.

## Screenshots

![DeepSeek Chat Client Screenshot](assets/screenshot.png)

## Installation

### Prerequisites

- [Rust](https://www.rust-lang.org/tools/install) (latest stable version)
- [Cargo](https://doc.rust-lang.org/cargo/) (comes with Rust)
- DeepSeek API Key (set as an environment variable)

### Clone the Repository

```bash
git clone https://github.com/yourusername/deepseek_client.git
cd deepseek_client
```

### Build and Run

1. Install dependencies:
   ```bash
   cargo build --release
   ```

2. Run the application:
   ```bash
   cargo run
   ```

## Usage

1. Launch the application.
2. Enter your message in the input box and click "Send".
3. View responses from DeepSeek in real-time.
4. Switch between chat histories using the left-side panel.

## Environment Variables

The application requires the following environment variables to be set:

- `DEEPSEEK_API_KEY`: Your DeepSeek API key.
- `DEEPSEEK_API_BASE` (optional): The base URL for the DeepSeek API. Defaults to `https://api.deepseek.com/v1`.

You can set these variables in a `.env` file in the project root:

```env
DEEPSEEK_API_KEY=your_api_key_here
DEEPSEEK_API_BASE=https://api.deepseek.com/v1
```

## Troubleshooting

### Common Issues

1. **Missing API Key**:
   - Ensure the `DEEPSEEK_API_KEY` environment variable is set.
   - Verify the `.env` file is in the project root and properly formatted.

2. **Connection Errors**:
   - Check your internet connection.
   - Verify the `DEEPSEEK_API_BASE` URL is correct and reachable.

3. **Build Errors**:
   - Ensure you have the latest stable version of Rust installed.
   - Run `cargo clean` and try rebuilding the project.

4. **Real-Time Updates Not Working**:
   - Ensure the WebSocket connection to the DeepSeek server is active.
   - Check the logs for any errors.

## Contributing

Contributions are welcome! To contribute:

1. Fork the repository.
2. Create a new branch for your feature or bugfix:
   ```bash
   git checkout -b feature-name
   ```
3. Commit your changes:
   ```bash
   git commit -m "Add feature-name"
   ```
4. Push to your fork:
   ```bash
   git push origin feature-name
   ```
5. Open a pull request.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [egui](https://github.com/emilk/egui) for the GUI framework.
- [async-openai](https://github.com/64bit/async-openai) for OpenAI API integration.
- [poll-promise](https://github.com/emilk/poll_promise) for handling asynchronous tasks in the UI.
- [dotenv](https://github.com/dotenv-rs/dotenv) for environment variable management.