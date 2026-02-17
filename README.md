# Selective Pet Access 🐱

**Smart Pet Feeder & Access Control System**

## 📖 About the Project

A robust, safety-critical firmware implementation for an automated pet door/feeder system. This project demonstrates **Modern C++ (C++20)** practices applied to embedded systems, featuring a decoupled Hardware Abstraction Layer (HAL), deterministic real-time behavior, and a hermetic Docker-based build environment.

This repository hosts the firmware for the **ESP32-S3** microcontroller, built with a focus on industrial-grade software quality and reliability.

## 🛠️ Technology Stack

* **Hardware:** ESP32-S3-N16R8
* **Language:** Modern C++ (C++20)
* **Framework:** ESP-IDF v5.3
* **Operating System:** FreeRTOS (SMP)
* **Tools:** Docker & VS Code Dev Containers (Hermetic Build)

## 📚 Documentation & Setup

We adhere to a Tier-1 documentation standard. Please choose the guide that fits your needs:

### 🚀 Getting Started
* **[Developer Guide](docs/Developer_guide.md):** *Target Audience:* Contributors & Developers.  
  *Content:* How to clone, build, flash, and debug using the existing environment. **Start here.**

### 🏗️ Infrastructure & Architecture
* **[Infrastructure Setup (From Scratch)](docs/Infrastructure_setup_from_scratch.md):** *Target Audience:* Architects & Maintainers.  
  *Content:* Deep dive into *how* the environment is built (Docker, partitions, CMake) and the architectural decisions behind it.

---

**Author:** João Berlese  
*Connect with me on [LinkedIn](https://www.linkedin.com/in/joao-berlese/)*