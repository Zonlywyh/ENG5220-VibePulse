# ENG5220-VibePulse
## Realtime Heart-Rate–Driven Adaptive Music System

University of Glasgow — ENG 5220: Real Time Embedded Programming Team Project

<br />
<p align="center">
  <a href="https://github.com/Zonlywyh/ENG5220-VibePulse">
    <img src="https://raw.githubusercontent.com/Zonlywyh/logo/main/vibepulse-logo.jpg"
         alt="VibePulse Logo"
         width="120"
         height="120">
  </a>

  <h3 align="center">VibePulse</h3>

  <p align="center">
    Realtime Heart-Rate–Driven Adaptive Music System
    <br />
    <a href="https://github.com/Zonlywyh/ENG5220-VibePulse">
      <strong>Explore the project »</strong>
    </a>
    <br />
    <br />
    <a href="https://github.com/Zonlywyh/ENG5220-VibePulse">Demo</a>
    ·
    <a href="https://github.com/Zonlywyh/ENG5220-VibePulse/issues">Report Bug</a>
    ·
    <a href="https://github.com/Zonlywyh/ENG5220-VibePulse/issues">Request Feature</a>
  </p>
</p>

---

## 🚀 Introduction
VibePulse is a professional-grade, **event-driven embedded system** built on Linux (Raspberry Pi). It solves the real-world challenge of physiological state management by continuously monitoring heart-rate signals (PPG), inferring stress/relaxation states, and dynamically adapting music playback in realtime.

## 🧠 Real-Time Implementation & DSP
*In accordance with ENG 5220 marking criteria, this project strictly avoids polling to ensure high responsiveness.*

* **Event-Driven Architecture**: Processing is triggered by hardware events and handled via **asynchronous callbacks** and **timers**.
* **Multithreading**: We employ thread-based event handling (waking up threads) to ensure the system remains responsive, preventing the software from entering an unresponsive wait state.
* **DSP Pipeline**:
    * **High-Pass Filter**: DC removal to eliminate static tissue interference.
    * **Low-Pass Filter**: Removes high-frequency electrical artifacts.
    * **Quantitative Assessment**: Latencies are monitored to ensure data acquisition and music adaptation happen within defined tolerances.

## 💻 Software Structure & Reliability
*Our code is structured using Object-Oriented principles to guarantee high reliability and ease of maintenance.*

* **SOLID Principles**: The choice of classes is guided by SOLID principles to ensure clear encapsulation and rationale.
* **Encapsulation**: Internal data is strictly private. We use safe getters, setters, and callback interfaces to manage data flow between threads without memory leaks.
* **Failsafe Design**: The application is designed to be leak-free, ensuring it can run as a standalone embedded product upon boot-up.

## 📌 Key Features
- 📈 **Realtime heart-rate sampling** with event-triggered peak detection.
- 🎵 **Adaptive music selection** based on inferred physiological state.
- 🧾 **Mood and HR logging** with time-stamped entries for trend analysis.
- ⚙️ **Production-level C++** running on Raspberry Pi (Linux).

## 👥 Project Management & Labor Division
*Managed via GitHub Issues, Projects, and formal Revision Control.*

* **Revision Control**: We use a formal **Branching & Release strategy**. Commit messages are linked to specific Issues to track development history.
* **Labor Division**:
  * **Member 1 (PM)**: Revision control strategy, Release management, and Issue tracking.
  * **Member 2 (DSP Lead)**: Signal filtering algorithms and peak detection logic.
  * **Member 3 (Hardware Lead)**: Sensor integration, I2C protocol optimization, and wiring.
  * **Member 4 (Real-time Lead)**: Callback implementation, thread management, and latency assessment.
  * **Member 5 (PR & Docs)**: Social media strategy, GitHub presentation, and promotional content.

## 📢 Social Media & Promotion
*Creating a "buzz" around VibePulse to engage potential users.*

<p align="center">
  <a href="https://www.instagram.com/vibepulse2026">
    <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/a/a5/Instagram_icon.png/128px-Instagram_icon.png" width="52" height="52" alt="Instagram">
  </a>
  &nbsp;&nbsp;&nbsp;
  <a href="https://www.youtube.com/">
    <img src="https://upload.wikimedia.org/wikipedia/commons/0/09/YouTube_full-color_icon_%282017%29.svg" width="72" height="52" alt="YouTube">
  </a>
  &nbsp;&nbsp;&nbsp;
  <a href="https://www.douyin.com/">
    <img src="https://upload.wikimedia.org/wikipedia/commons/a/a6/Tiktok_icon.svg" width="52" height="52" alt="Douyin">
  </a>
</p>

## 🛠️ Installation & Build
*Designed for full reproducibility.*

1. **Clone & Setup**
   ```bash
   git clone [https://github.com/Zonlywyh/ENG5220-VibePulse.git](https://github.com/Zonlywyh/ENG5220-VibePulse.git)
   cd ENG5220-VibePulse
   mkdir build && cd build
2. Build (CMake)
    ```bash
   mkdir build && cd build
   cmake ..
   make -j4 # Production build with unit tests
