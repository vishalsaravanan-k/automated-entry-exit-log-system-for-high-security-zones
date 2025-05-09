
# Automated Entry-Exit Log System For High-Security Zones

A real-time, multi-factor access control system using biometrics, RFID, MQTT, and Flask for secure environments like courts, banks and government buildings.


## 🔍 PROBLEM STATEMENT

Traditional security systems in high-security environments like courtrooms rely heavily on manual verification, CCTV surveillance, and standalone RFID access control. These methods suffer from critical limitations:

- Human Error & Forgery: Manual ID checks and paper-based logging are prone to errors and fake credentials.

- Reactive Monitoring: CCTV systems lack real-time alerts and integration with authentication mechanisms.

- No Multi-Factor Assurance: Basic RFID systems allow unauthorized access if tags are stolen or duplicated.

- Fragmented Data: Logs, images, and identity verification operate in silos, complicating audits and incident response.

This project addresses these gaps by designing an automated, multi-layered security system that combines biometric authentication, RFID tracking, real-time image capture, and centralized logging to ensure only authorized individuals gain access while maintaining tamper-proof, auditable records.
## 🚀 INNOVATION

- Performs fingerprint matching using a CSV-based Aadhar-like database instead of relying on sensor's internal storage.
- Triggers ESP32-CAM remotely via MQTT for real-time image capture.
- Links RFID data with biometric and visual identity for full traceability.
- Uses Flask + Socket.IO for a live dashboard with entry/exit tracking.
## ✨ FEATURES

- Biometric Authentication: Fingerprint verification using the R307 sensor.
- RFID Tracking: Unique RFID tags linked to user identities for entry/exit logging.
- Real-Time Image Capture: ESP32-CAM captures and uploads images via HTTP.
- Centralized Dashboard: Live monitoring using Flask, Socket.IO, and a React-based UI.
- Offline Logging: CSV logs stored on a microSD card for audit trails.
- MQTT Communication: Real-time data synchronization between devices.


## 🛠️ HARDWARE COMPONENTS

- ESP32-WROOM-32D
- ESP32-CAM 
- R307 FINGERPRINT SENSOR
- RC522 RFID MODULE
- microSD CARD MODULE
- OLED DISPLAY
## 📦 SOFTWARE COMPONENTS

- ARDUINO IDE
- PYTHON IDE
- VIM (TEXT EDITOR)
## 🧠 SYSTEM ARCHITECTURE

ESP32-WROOM-32D handles:

    - Fingerprint matching against a local CSV database

    - Communicating with RFID module and OLED display

    - Sending MQTT triggers to ESP32-CAM

    - Writing logs to microSD card

ESP32-CAM:

    - Receives MQTT command

    - Captures a real-time image

    - Sends image via HTTP to Flask server

Flask Server (app.py):

    - Receives and stores uploaded images

    - Handles MQTT messages (entry, exit, camera metadata)

    - Pushes data to dashboard using Socket.IO

Web Dashboard (index.html):

    - Displays real-time entry/exit logs

    - Shows user information with photos

    - Offers a responsive, modern UI for security staff
## ⚙️ WORKING PRINCIPLE

    Entry Process:

        Scan fingerprint → Capture image → Issue RFID tag → Log entry.

    Exit Process:

        Scan RFID tag → Update exit time → Log exit.

    Dashboard:

        Real-time updates for entries/exits.

        View historical logs and images.
## 📂 REPOSITORY STRUCTURE FOR FLASK SERVER
 
├── app.py                 # Flask server + MQTT handler  
├── templates/index.html             # Web dashboard UI  
├── images/      # Stores registered user images  
└── uploads/                # Stores esp32-cam captured user images  
## 🔧 TROUBLESHOOTING

- Wi-Fi/MQTT Connection Failed: Verify IP addresses and network settings in the code.

- Fingerprint Mismatch: Re-enroll fingerprints and ensure a_d.csv entries match.

- Image Upload Failure: Check ESP32-CAM HTTP endpoint and server connectivity.


## 📜 LICENSE

This project is open-source under the MIT License.
