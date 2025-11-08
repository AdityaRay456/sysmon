# SysMon – System Monitor in C++

A lightweight **terminal-based system monitor** built in C++ using the **ncurses** library.  
It displays real-time **CPU usage**, **memory usage**, and the list of top active processes — similar to `top` or `htop`.

---

## ⚙️ Features
- Real-time CPU and Memory monitoring  
- Displays top active processes with PID, name, CPU%, and memory usage  
- Toggle sorting between CPU and Memory  
- Lightweight, written entirely in modern C++ (C++17)  
- Compatible with **Ubuntu / WSL** environments  

---

## 🧩 Controls
| Key | Action |
|-----|--------|
| **q** | Quit the monitor |
| **s** | Toggle sort between CPU% and Memory usage |

---

## 🛠 Requirements
- Ubuntu / WSL environment  
- `g++` (C++17 or higher)  
- `libncurses5-dev` or `libncursesw5-dev`

Install dependencies:
```bash
sudo apt update
sudo apt install g++ libncurses5-dev -y
