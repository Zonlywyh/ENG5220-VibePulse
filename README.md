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
VibePulse is a professional-grade, **event-driven embedded system** built on Linux (Raspberry Pi). [cite_start]It solves the real-world challenge [cite: 53, 55] [cite_start]of physiological state management by continuously monitoring heart-rate signals (PPG), inferring stress/relaxation states, and dynamically adapting music playback in realtime[cite: 47, 49].

## 🧠 Real-Time Implementation & DSP
[cite_start]*In accordance with ENG 5220 marking criteria[cite: 7, 8], this project strictly avoids polling.*

* [cite_start]**Event-Driven Architecture**: Processing is triggered by hardware interrupts and handled via **asynchronous callbacks** and **timers**[cite: 8, 38].
* [cite_start]**Multithreading**: We employ thread-based event handling to ensure the system remains responsive, preventing the software from entering an unresponsive wait state[cite: 38, 70].
* **DSP Pipeline**:
    * [cite_start]**High-Pass Filter**: DC removal to eliminate static tissue interference[cite: 38].
    * **Low-Pass Filter**: Removes high-frequency electrical artifacts.
    * [cite_start]**Quantitative Assessment**: Latencies are monitored to ensure data acquisition and music adaptation happen within physiological constraints[cite: 38].

## 💻 Software Structure & Reliability
[cite_start]*Our code is structured to guarantee high reliability and ease of maintenance[cite: 6].*

* [cite_start]**SOLID Principles**: The choice of classes is guided by SOLID principles to ensure modularity and clear rationale[cite: 36].
* **Encapsulation**: Internal data is strictly private. [cite_start]We use safe getters, setters, and callback interfaces to manage data flow between threads without memory leaks[cite: 36].
* [cite_start]**Failsafe Design**: The application is tested to be memory-leak free, ensuring it can run as a standalone embedded product upon boot-up[cite: 36, 58].

## 📌 Key Features
- [cite_start]📈 **Realtime heart-rate sampling** with event-triggered peak detection[cite: 49].
- 🎵 **Adaptive music selection** based on inferred physiological state.
- 🧾 **Mood and HR logging** with time-stamped entries for trend analysis.
- [cite_start]⚙️ **Production-level C++** utilizing Linux kernel-space or userspace interrupt handling[cite: 38, 50].

## 👥 Project Management & Labor Division
[cite_start]*Managed via GitHub Issues, Projects, and formal Revision Control[cite: 9, 11, 41].*

* **Revision Control**: We use a formal **Branching & Release strategy** (v1.0.0). [cite_start]Commit messages are linked to specific Issues[cite: 41].
* [cite_start]**Labor Division [cite: 10, 23, 65]**:
    * **Member 1 (PM)**: Project planning, Issue tracking, Release management.
    * **Member 2 (DSP Lead)**: Filtering algorithms and peak detection logic.
    * **Member 3 (Hardware Lead)**: Sensor integration and I2C protocol optimization.
    * **Member 4 (Real-time Specialist)**: Callback handler and thread synchronization.
    * **Member 5 (PR & Docs)**: Social media strategy and eye-catching presentation.

## 📢 Social Media & Promotion
[cite_start]*Creating a "buzz" around VibePulse to engage potential users[cite: 12, 14, 15].*

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
[cite_start]*Designed for reproducibility[cite: 13, 43].*

1. **Clone & Setup**
   ```bash
   git clone [https://github.com/Zonlywyh/ENG5220-VibePulse.git](https://github.com/Zonlywyh/ENG5220-VibePulse.git)
   cd ENG5220-VibePulse
