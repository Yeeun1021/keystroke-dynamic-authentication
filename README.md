# Keystroke Dynamic Authentication on nRF54L15

A biometric authentication system that identifies users by their unique typing rhythm on the 4 buttons of the Nordic nRF54L15 Development Kit. Built with Zephyr RTOS and LiteRT for Microcontrollers (TFLite Micro).

---

## What it does

The system records how long each button is held (hold time) and how fast the user moves between buttons (flight time) while entering a fixed PIN sequence. These 7 timing features are fed into a quantized neural network running on the nRF54L15 to identify which user is typing. Identified users are printed to the serial terminal. Unknown users are rejected.

---

## Hardware Target
- **Board**: nRF54L15 DK (Nordic Semiconductor)
- **SoC**: nRF54L15 (Arm Cortex-M33, 256 KB RAM, 1.5 MB Flash)
- **Board target string for west/CMake**: `nrf54l15dk/nrf54l15/cpuapp`
- **Reference docs**: `documentation/nRF54L15_DK_HW_User_Guide_v1.0.0.pdf`

## Software

- nRF Connect SDK v3.2.4 with Zephyr RTOS v4.2.99
- Python 3.x with `pyserial` (data collection on PC)
- Google Colab (model training)
- LiteRT for Microcontrollers / TFLite Micro (on-device inference)

---

## Repository structure

```
keystroke_dynamic-repo/
├── README.md
├── collect_datase.py
├── data_collection/      
│   ├── CMakeLists.txt
│   ├── prj.conf
|   ├── build
│   └── src/main.c
├── training/              
│   └── keystroke_dynamic_authentication.ipynb
├── dataset/              
|   ├── data_user0.csv  raw dataset, too many zeros, not used for training  
|   ├── data_user1.csv  raw dataset, too many zeros, not used for training
|   ├── data_user2.csv  raw dataset, too many zeros, not used for training
│   ├── data_user3.csv  raw dataset, no zeros, used for training
│   ├── data_user4.csv  raw dataset, no zeros, used for training
│   ├── data_user5.csv  raw dataset, no zeros, used for training
│   └── data_user6.csv  raw dataset, no zeros, used for training
├── inference/            
│   ├── CMakeLists.txt
│   ├── .vscode
│   ├── build
|   ├── prj.conf
│   └── src/
│       ├── main.cpp
│       └── kda_model.h
└── model/
    ├── confusion_matrix.png
    └── kda_model.tflite
```
---

## Button and LED assignments

| Hardware | Assignment |
|---|---|
| BTN0 (SW0) | PIN key 1 — press first |
| BTN1 (SW1) | PIN key 2 — press second |
| BTN2 (SW2) | PIN key 3 — press third |
| BTN3 (SW3) | PIN key 4 — press fourth |
| LED0 (green) | User authenticated |
| LED1 (red) | User rejected or unknown |

The PIN sequence is always **BTN0 → BTN1 → BTN2 → BTN3**. Press and release each button fully before pressing the next.

user3: short duration 1 | short interval 1 | short duration 2 | short interval 2 | short duration 3 | short interval 3 | short duration 4

user4: long  duration 1 | short interval 1 | long duration  2 | short interval 2 | long  duration 3 | short interval 3 | long  duration 4

user5: short duration 1 | long  interval 1 | short duration 2 | long  interval 2 | short duration 3 | long  interval 3 | short duration 4

user6: short duration 1 | short interval 1 | long  duration 2 | long  interval 2 | long  duration 3 | long  interval 3 | short duration 4

Each user's typing patten follows the table above.

---

## How to build and flash

### Prerequisites

1. Install [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop)
2. Install nRF Connect SDK v3.2.4 via the Toolchain Manager
3. Install the nRF Connect for VS Code extension pack

### Build the data collection firmware

1. Open VS Code and add `data_collection/` as an application in the nRF Connect panel
2. Set board target: `nrf54l15dk/nrf54l15/cpuapp`
3. Click **Build**, then **Flash**

### Build the inference firmware

1. Add `inference/` as an application in the nRF Connect panel
2. Set board target: `nrf54l15dk/nrf54l15/cpuapp`
3. Click **Build**, then **Flash**

> Make sure `kda_model.h` is present in `inference/src/` before building. See the retraining section below if you need to regenerate it.

---

## How to collect data

1. Flash the data collection firmware
2. Close the nRF serial monitor in VS Code (it must not hold the COM port)
3. Open a new terminal in VS Code and run:

```bash
pip install pyserial
python collect_dataset.py
```

4. Edit `collect_dataset.py` to set the correct COM port and user ID:

```python
PORT = "COM7"    # Windows: COMx  |  Linux: /dev/ttyACM0
USER_ID = 3      # Change per user
```

5. Press BTN0→BTN1→BTN2→BTN3 repeatedly (~50 times) in your natural rhythm
6. Press Ctrl+C to stop. The CSV is saved as `data_userX.csv`
7. Repeat with a different USER_ID for each additional user

---

## How to retrain the model

1. Open `training/KDA_Training.ipynb` in [Google Colab](https://colab.research.google.com)
2. Upload your CSV files when prompted
3. Run all cells in order
4. Cell 3 prints scaler mean and std values — copy these into `inference/src/main.cpp`:

```cpp
static const float SCALER_MEAN[] = { /* paste values here */ };
static const float SCALER_STD[]  = { /* paste values here */ };
```

5. Cell 6 downloads `kda_model.h` — place it in `inference/src/`
6. Clean Build and reflash the inference firmware

---

## Serial output (inference firmware)

Open a serial terminal at **115200 baud** after flashing. Example output:

```
KDA Inference ready.
Enter PIN: BTN0 -> BTN1 -> BTN2 -> BTN3
Scores: U3=99% U4=0% U5=0% U6=1%
IDENTIFIED as User3
Scores: U3=0% U4=93% U5=1% U6=6%
IDENTIFIED as User4
Scores: U3=0% U4=0% U5=100% U6=0%
IDENTIFIED as User5
Scores: U3=0% U4=0% U5=0% U6=100%
IDENTIFIED as User6
Scores: U3=12%  U4=11%  U5=38%  U6=39%
UNKNOWN USER - rejected
```

---

## Model details

| Property | Value |
|---|---|
| Architecture | Fully connected neural network (MLP) |
| Input | 7 timing features (4 hold times + 3 flight times), z-score normalised |
| Layers | Dense(16, ReLU) → Dense(8, ReLU) → Dense(N, Softmax) |
| Parameters | ~350 |
| Quantisation | INT8 post-training quantisation via TFLite converter |
| Model size | ~4 KB |
| Confidence threshold | 70% — below this the user is rejected as unknown |

---

## Known limitations

- **Small dataset**: ~50 samples per user. A production system would require hundreds of samples collected across multiple sessions and days.
- **Session variability**: A user's typing rhythm can vary with fatigue, stress, or unfamiliar hardware. The model was trained and tested in a single session which may overfit to that session's rhythm.
- **No liveness detection**: The system cannot detect if someone is deliberately mimicking another user's timing pattern.
- **Fixed PIN**: The PIN sequence is hardcoded as BTN0→BTN1→BTN2→BTN3. A real system would support configurable PINs.

---

## Ethical considerations

- **False positives** (impostors authenticated) are a security risk. The 70% confidence threshold prioritises security over convenience by rejecting uncertain inputs.
- **False negatives** (legitimate users rejected) are an inconvenience but not a security breach. In safety-critical deployments the threshold should be tuned using an ROC curve on a larger dataset.
- Biometric data (typing rhythm) is personal data under GDPR and similar regulations. In a production system, all timing data must be stored securely, and users must be informed and give consent.
- The system should not be used as the sole authentication factor for high-security applications without additional verification.

## Build System & Toolchain
- **RTOS**: Zephyr RTOS (via nRF Connect SDK)
- **Build tool**: CMake 3.20+ with `west`
- **Build command**: `west build -b nrf54l15dk/nrf54l15/cpuapp kda/keystroke_dynamic`
- **Flash command**: `west flash`
- **SDK**: nRF Connect SDK (NCS). Assume the latest stable version.
- Every example is a standalone Zephyr application with its own `CMakeLists.txt` and `prj.conf`.

## AI/ML Framework
- **Framework**: TensorFlow Lite Micro (TFLM)
- **Kconfig**: Enable via `CONFIG_TENSORFLOW_LITE_MICRO=y` in `prj.conf`
- **C++ standard**: C++17 required (`CONFIG_STD_CPP17=y`, `CONFIG_CPP=y`)
- **Stack size**: Set `CONFIG_MAIN_STACK_SIZE=8192` or higher for ML inference
- **Constraints**:
  - No dynamic memory allocation (`new`/`malloc`) — TFLM uses a static arena
  - Models must be quantized (int8) to fit in flash and run without FPU overhead
  - Tensor arena size must be tuned per model (start at 8 KB, adjust down)

## Coding Conventions
- **Language**: C for drivers/peripherals, C++ for ML inference code
- **Style**: Follow Zephyr coding guidelines (kernel API, devicetree macros)
- **Devicetree**: Use DT macros (`DT_NODELABEL`, `DT_ALIAS`) — never hardcode peripheral addresses
- **Logging**: Use Zephyr `LOG_MODULE_REGISTER()` and `LOG_INF()` / `LOG_ERR()`, not `printf`
- **GPIO/Peripheral access**: Always use Zephyr's device driver API (`gpio_pin_configure()`, `pwm_set_pulse_dt()`, etc.)
- **Pin mapping**: Always refer to the board's devicetree overlay (`.overlay` files), not raw register addresses

## Common Kconfig Options (prj.conf)
When creating or modifying a `prj.conf`, use these patterns:
```
CONFIG_GPIO=y              # For any GPIO usage
CONFIG_PWM=y               # For PWM (servos, buzzers)
CONFIG_ADC=y               # For analog input (microphone)
CONFIG_SPI=y               # For SPI peripherals (camera, TFT)
CONFIG_DISPLAY=y           # For display subsystem
CONFIG_CPP=y               # Required for TFLM
CONFIG_STD_CPP17=y         # Required for TFLM
CONFIG_TENSORFLOW_LITE_MICRO=y
CONFIG_MAIN_STACK_SIZE=8192
