# Product Requirements Document (PRD)
## Project Name: VibePulse  
**Real-time Heart-Rate–Driven Adaptive Music System**

---

## 1. Background

In daily activities such as studying, working, or exercising, human physiological states (e.g., heart rate) can change rapidly due to stress, fatigue, or activity intensity.  
Music has a strong influence on emotional regulation and concentration; however, most existing music playback systems rely on static playlists and cannot adapt to users’ real-time physiological changes.

VibePulse aims to design and implement a **real-time embedded system** that transforms physiological signals into immediate and perceivable music feedback under explicit timing constraints.

---

## 2. Objectives

- Acquire heart-rate data in real time  
- Analyse physiological changes under low-latency constraints  
- Dynamically adapt music playback based on physiological state  
- Demonstrate the feasibility of an **event-driven real-time embedded system** on the Raspberry Pi platform  

---

## 3. Use Cases

- Adaptive music playback during studying or working to support focus or relaxation  
- Light exercise scenarios where music tempo adjusts to heart-rate zones  
- A teaching and demonstration prototype for a real-time embedded systems course  

---

## 4. Users and Stakeholders

- End users wearing a heart-rate sensor and using the music playback system  
- Developers and course assessors focusing on system architecture and real-time behaviour  

---

## 5. Functional Requirements

### 5.1 Heart-Rate Data Acquisition
- The system shall sample heart-rate data at a fixed frequency (e.g., 50–100 Hz)  
- Sampling shall be driven by timers rather than polling loops  

### 5.2 Heart-Rate Event Detection
- The system shall detect heartbeat events or significant heart-rate changes  
- When meaningful changes occur, a **HeartRateEvent** shall be generated  

### 5.3 Physiological State Analysis
- The system shall compute BPM or trend information from heart-rate data  
- Heart-rate values shall be mapped to a small set of physiological states (e.g., calm, active)  

### 5.4 Adaptive Music Control
- When a physiological state change is detected, a music adaptation event shall be triggered  
- Music playback parameters (e.g., BPM matching or playlist selection) shall be adjusted dynamically  

### 5.5 User Feedback
- Users shall perceive music adaptation as system feedback  
- Music transitions shall be smooth and free from noticeable glitches  

---

## 6. Real-Time Requirements

- Heart-rate sampling shall maintain a stable and consistent period  
- The end-to-end latency from heart-rate change detection to music adaptation trigger shall be kept **below 200 ms**  
- Excessive latency may lead to BPM mismatch and degraded user experience  

---

## 7. Non-Functional Requirements

### 7.1 System Architecture
- The system shall adopt an **event-driven architecture**  
- Core modules shall run in independent threads to avoid blocking  

### 7.2 Platform and Language
- Target platform: Raspberry Pi running Linux  
- Core implementation language: **C++**  

### 7.3 Maintainability
- Modules shall have clear responsibilities and well-defined interfaces  
- The system shall support future extensions (e.g., logging or analysis features)  

---

## 8. Non-Real-Time Tasks (Out of Scope for the Real-Time Path)

The following tasks are not part of the real-time execution path and shall be handled asynchronously:
- Heart-rate and state logging  
- Data statistics and post-analysis  
- AI-based or high-level semantic analysis  

---

## 9. Success Criteria

- The system runs stably and acquires heart-rate data in real time  
- Music playback responds to detected physiological state changes  
- Measured end-to-end latency satisfies the defined real-time constraints  
- The system architecture meets the requirements of the real-time systems course  

---

## 10. Constraints

- Limited hardware resources on an embedded platform  
- Restricted development time due to course schedule  
- The system is a prototype and does not aim for medical-grade accuracy  

---

## 11. Version Information

- Current version: PRD v1.0  
- Status: Initial requirement definition to guide system design and implementation  
