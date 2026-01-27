# ENG5220-VibePulse
# Realtime Heart-Rate–Driven Adaptive Music System

University of Glasgow — ENG5220: Real Time Embedded Programming Team Project

Live demo · Report Bug · Feature Request

## 🚀 Introduction
A realtime, event-driven embedded system on Raspberry Pi that continuously monitors heart-rate signals, infers physiological state (e.g., stress/relaxation), logs mood trends, and dynamically adapts music playback to support user state.

## 🧠 Motivation
Humans’ heart rate changes with physical and emotional states. Adapting music in realtime based on heart-rate inferred mood can enhance focus, relaxation, or performance in daily activities.

## 📌 Key Features
- 📈 **Realtime heart-rate sampling** with event-triggered peak detection  
- 🎵 **Adaptive music selection** based on inferred state  
- 🧾 **Mood and HR logging** with time-stamped entries  
- ⚙️ **Runs on Raspberry Pi using event-driven C++**

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
