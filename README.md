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
[cite_start]VibePulse is a professional-grade, **event-driven embedded system** built on Linux (Raspberry Pi)[cite: 54, 60]. [cite_start]It solves the real-world challenge of physiological state management by continuously monitoring heart-rate signals (PPG), inferring stress/relaxation states, and dynamically adapting music playback in realtime[cite: 49, 53].

## 🧠 Real-Time Implementation & DSP
[cite_start]*In accordance with ENG 5220 marking criteria, this project strictly avoids polling to ensure high responsiveness[cite: 8, 38, 73].*

* [cite_start]**Event-Driven Architecture**: Processing is triggered by hardware events and handled via **asynchronous callbacks** and **timers**[cite: 8, 38, 64].
* [cite_start]**Multithreading**: We employ thread-based event handling (waking up threads) to ensure the system remains responsive, preventing the software from entering an unresponsive wait state[cite: 8, 70, 71].
* **DSP Pipeline**:
    * [cite_start]**High-Pass Filter**: DC removal to eliminate static tissue interference[cite: 38].
    * [cite_start]**Low-Pass Filter**: Removes high-frequency electrical artifacts[cite: 38].
    * [cite_start]**Quantitative Assessment**: Latencies are monitored to ensure data acquisition and music adaptation happen within defined tolerances[cite: 38].

## 💻 Software Structure & Reliability
[cite_start]*Our code is structured using Object-Oriented principles to guarantee high reliability and ease of maintenance[cite: 6, 61].*

* [cite_start]**SOLID Principles**: The choice of classes is guided by SOLID principles to ensure clear encapsulation and rationale.
* **Encapsulation**: Internal data is strictly private. [cite_start]We use safe getters, setters, and callback interfaces to manage data flow between threads without memory leaks.
* [cite_start]**Failsafe Design**: The application is designed to be leak-free, ensuring it can run as a standalone embedded product upon boot-up[cite: 36, 58].

## 📌 Key Features
- [cite_start]📈 **Realtime heart-rate sampling** with event-triggered peak detection[cite: 38].
- [cite_start]🎵 **Adaptive music selection** based on inferred physiological state[cite: 49].
- [cite_start]🧾 **Mood and HR logging** with time-stamped entries for trend analysis[cite: 57].
- [cite_start]⚙️ **Production-level C++** running on Raspberry Pi (Linux)[cite: 60, 63].

## 👥 Project Management & Labor Division
[cite_start]*Managed via GitHub Issues, Projects, and formal Revision Control[cite: 9, 10, 41].*

* **Revision Control**: We use a formal **Branching & Release strategy**. [cite_start]Commit messages are linked to specific Issues to track development history[cite: 9, 11, 41].
* **Labor Division**:
    * [cite_start]**Member 1 (PM)**: Revision control strategy, Release management, and Issue tracking[cite: 10, 41].
    * [cite_start]**Member 2 (DSP Lead)**: Signal filtering algorithms and peak detection logic[cite: 38, 65].
    * [cite_start]**Member 3 (Hardware Lead)**: Sensor integration, I2C protocol optimization, and wiring[cite: 59, 65].
    * [cite_start]**Member 4 (Real-time Lead)**: Callback implementation, thread management, and latency assessment[cite: 8, 38, 65].
    * [cite_start]**Member 5 (PR & Docs)**: Social media strategy, GitHub presentation, and promotional content[cite: 12, 14, 65].

## 📢 Social Media & Promotion
[cite_start]*Creating a "buzz" around VibePulse to engage potential users[cite: 12, 15, 43].*

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
[cite_start]*Designed for full reproducibility[cite: 13, 43].*

1. **Clone & Setup**
   ```bash
   git clone [https://github.com/Zonlywyh/ENG5220-VibePulse.git](https://github.com/Zonlywyh/ENG5220-VibePulse.git)
   cd ENG5220-VibePulse
