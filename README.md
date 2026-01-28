# ENG5220-VibePulse
## Realtime Heart-Rate–Driven Adaptive Music System

University of Glasgow — ENG5220: Real Time Embedded Programming Team Project

<!-- PROJECT LOGO -->
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


## 🚀 Introduction
A realtime, event-driven embedded system on Raspberry Pi that continuously monitors heart-rate signals, infers physiological state (e.g., stress/relaxation), logs mood trends, and dynamically adapts music playback to support user state.

## 🧠 Motivation
Humans’ heart rate changes with physical and emotional states. Adapting music in realtime based on heart-rate inferred mood can enhance focus, relaxation, or performance in daily activities.

## 📌 Key Features
- 📈 **Realtime heart-rate sampling** with event-triggered peak detection  
- 🎵 **Adaptive music selection** based on inferred state  
- 🧾 **Mood and HR logging** with time-stamped entries  
- ⚙️ **Runs on Raspberry Pi using event-driven C++**

## Social Media

<p align="center">
  <!-- Instagram -->
  <a href="https://www.instagram.com/vibepulse2026">
    <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/a/a5/Instagram_icon.png/128px-Instagram_icon.png"
         width="52" height="52" alt="Instagram">
  </a>
  &nbsp;&nbsp;&nbsp;

  <!-- YouTube (PNG, stable) -->
  <a href="https://www.youtube.com/">
    <img src="https://upload.wikimedia.org/wikipedia/commons/0/09/YouTube_full-color_icon_%282017%29.svg"
         width="72" height="52" alt="YouTube">
  </a>
  &nbsp;&nbsp;&nbsp;

  <!-- Tiktok (PNG, stable) -->
  <a href="https://www.douyin.com/">
    <img src="https://upload.wikimedia.org/wikipedia/commons/a/a6/Tiktok_icon.svg"
         width="52" height="52" alt="Douyin">
  </a>
  &nbsp;&nbsp;&nbsp;

  <!-- Xiaohongshu (PNG, stable) -->
  <a href="https://www.xiaohongshu.com/">
    <img src="https://upload.wikimedia.org/wikipedia/commons/a/a4/Xiaohongshu_logo%26slogan.png"
         width="152" height="52" alt="Xiaohongshu">
  </a>
</p>

## Project Management

Development is managed using GitHub Issues and Projects.

- Tasks are tracked as GitHub Issues with clear ownership.
- Progress is visualised using a project board (To do / In progress / Done).
- Milestones are used to structure development phases.
- All code changes are linked to issues via commit messages.


## 🛠️ Hardware & Software
### Hardware
- Raspberry Pi (Linux)
- Heart-rate sensor (e.g., PPG, pulse oximeter)
- Audio output device (speaker/headphones)

### Software
- Core realtime system in **C++**  
- Event-driven architecture with threads/timers/callbacks  
- Logging and async analysis (optional external AI summary)

## 📦 Installation
1. **Clone the repository**
   ```bash
   git clone https://github.com/<your-org>/<your-repo>.git
