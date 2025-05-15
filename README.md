# EmbedJack
## Team Members
William Gilmore

Nelson Yeh

Michael Chen

## Project Description
This project implements a real-time, table-side assistant for the game of Blackjack that uses reinforcement learning to advise players on the optimal moves for each round. It employs two ESP32-CAM modules controlled by NRF52840dk boards positioned at the dealer and player vantage points respectively to visually detect and identify dealt cards, which continuously updates a running card count. This data is streamed to a TagoIO web dashboard, which also displays recommended actions based on the changing game state. The recommended actions are computed on the base node (Disco L475 IOT01) via reinforcement learning techniques such as Q Learning and DQNs. An onboard speaker provides audio cues (“money cashing out e.g. cha-ching") when the odds are favourable. To ensure accurate operation of the camera and maintain player alertness, the sensor node (Nordic Thingy52) monitors room conditions such as temperature, humidity, light, and CO2 levels which helps preserve the player’s cognitive function and ensure sufficient lighting for the cameras. 

## Project Block Diagram
![image](https://github.com/user-attachments/assets/87cdbfeb-d8ee-4ac2-a332-8b66fef8f37f)

## DIKW Pyramid Abstraction
![image](https://github.com/user-attachments/assets/7fbd11a6-4e95-4919-a335-da528c64c624)

## System Integration
| Component  | Description |
| ------------- | ------------- |
| ESP32-CAM Module  | Takes pictures of player and dealer cards  |
| NRF52840 DK  | Receives and processes pictures taken by ESP32-CAM  |
| Thingy52  | Sensor Node for temperature, light and gas |
| Disco L475 IOT01  | Base node for running ML algorithm and transmitting sensor data to web dashboard   |
| Speaker Module  | Provides audio cue to signal player when to hit   |
| HTS221  | Temperature and Humidity Sensor   |
| LDR Ambient Light Sensor | Light Sensor |
| CO2 and TVOC Sensor | Gas Sensor |

## Wireless Network Communications
![image](https://github.com/user-attachments/assets/e3b56851-1da6-4d8c-a2c2-5fe8069a6313)

Message format: JSON  

Bluetooth low energy (BLE) will be used to communicate between sensor nodes and base node 

Wi-Fi is used to send data packets from base node to the dashboard  

# Deliverables and Key Performance Indicators
| Deliverable  | Description | KPI |
| ------------- | ------------- | ------------- |
| Sensor Data Detection and Processing   | Fully functional embedded node that interfaces with the ESP32-CAM to detect the card value, card suit, the recipient of the card (dealer or player) and the timestamp of when the card was dealt. The Thingy52 detects room ambiance via temperature, light and CO2 gas levels.   | Room sensor readings register every 5 seconds with 1% missing/error rate over 1-hour runtime. Camera readings register in intervals of 1 min with 2% missing/error rate over longer hours of runtime. |
| Player Action Recommendation via Machine Learning   | Real time computation of the player and dealer’s hands using reinforcement learning techniques such as Q-Learning or DQN to learn the optimal betting and playing strategy on the current card count. | System provides optimal betting and playing advice within 8 seconds of the cards being dealt.  |
| Alert System with Speaker   | Prediction of player success rate in the current hand being 85% or higher enables speakers to alert player with custom audio cues that the player should continue.  | Audio alerts generate 1 - 2 seconds after the system has computed the optimal move. |
| Wireless Communication System   | Bluetooth Low Energy (BLE) communication protocol implemented between sensor nodes reading from ESP32-CAM and base node processing the machine learning algorithm with approximately 0.5s latency. Base node communicates with the web dashboard using Wi-Fi protocol to send JSON packets.    | 90 – 95% successful data advertising and scanning in a multi-node network. Minimise packet loss to less than 5%.  |
| Web Dashboard displaying Current and Past Data   | Web Dashboard implemented through TagoIO displays room data on temperature, light and CO2 levels, the live card counts and the recommended move.   | Data updates onto web dashboard in real-time with potential 1-3 second delays. Room factors such as temperature, light and CO2 levels are displayed in respective graphs on the dashboard. |


