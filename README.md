# TitanAI

TitanAI is a lightweight, local-first AI assistant specifically designed for **Arch Linux** users.

## Project Vision
TitanAI aims to deliver a privacy-respecting, high-performance desktop AI companion integrated tightly into the Arch Linux system environment. It is engineered from the ground up to be fast, efficient, and zero-bloat while providing system automation and local AI capabilities.

## Current Project Status
- **Milestone 1**: Initial project foundation setup (C++20, CMake, Ninja).
- **Status**: Pre-alpha foundation. Advanced features (LLM engine, agent architecture, database, GUI, tools integration) will be introduced in subsequent milestones.

## Technology Stack
- **Language**: C++20
- **Compiler**: GCC
- **Build System**: CMake (>= 3.20)
- **Generator**: Ninja
- **Target OS**: Arch Linux
- **IDE**: Antigravity IDE

## Building the Project

### Prerequisites
Ensure you have CMake, Ninja, and GCC installed on Arch Linux:
```bash
sudo pacman -S cmake ninja gcc
```

### Build Instructions
Configure the build system using CMake and Ninja:
```bash
cmake -S . -B build -G Ninja
```

Build the application target:
```bash
cmake --build build
```

## Running the Project
After building, execute the binary:
```bash
./build/TitanAI
```

## License
This project is licensed under the [MIT License](LICENSE).
