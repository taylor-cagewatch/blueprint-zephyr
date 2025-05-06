# 1NCE Zephyr blueprint

## 🧭 Overview

The **1NCE Zephyr Blueprint** is a reference application that showcases how to integrate and use various 1NCE OS features with [Zephyr RTOS](https://zephyrproject.org/), including:

- ✅ **Device Authenticator**
- 📡 **IoT Integrator**
- 🔋 **Energy Saver**
- 📥 **Device Controller** for UDP and CoAP-based downlink and real-time remote interaction
- 🧩 **Plugin Integrations** with partners like [Mender](https://mender.io) for FOTA and [Memfault](https://memfault.com) for device observability

It is based on the **1NCE IoT C SDK** and runs on Nordic Semiconductor boards.

The Zephyr OS is a scalable, secure, real-time operating system designed for resource-constrained embedded devices — from smart sensors to full-featured gateways.

---

## 🧱 Supported Boards

The demo supports the following Nordic boards:

- [nRF9151 Development Kit](https://www.nordicsemi.com/Products/Development-hardware/nrf9151-dk)
- [nRF9160 Development Kit](https://www.nordicsemi.com/Products/Development-hardware/nrf9160-dk)
- [Thingy:91](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91)

---

## 🚀 Getting Started

This guide walks you through:

- Setting up the **1NCE IoT C SDK**
- Getting the source code
- Building and flashing the blueprint demo

---

### 📦 Prerequisites

- [nRF Connect SDK v2.8.0](https://docs.nordicsemi.com/bundle/ncs-2.8.0/page/nrf/installation/install_ncs.html)
- [Visual Studio Code](https://code.visualstudio.com/)
- [West tool](https://docs.zephyrproject.org/3.1.0/develop/west/install.html)

---

## 🧩 Integrating 1NCE IoT C SDK

The [1NCE IoT C SDK](https://github.com/1NCE-GmbH/1nce-iot-c-sdk) provides C-based modules to easily use 1NCE OS services.

Follow these steps:

1. **Open `west.yml`:**

   ```bash
   %HOMEPATH%\ncs\v2.8.0\nrf\west.yml
   ```

2. **Add the module to `name-allowlist`:**  
   Ensure `nce-sdk` is listed in alphabetical order.

3. **Activate the SDK via submanifest:**

   Rename and edit the file:
   ```bash
   %HOMEPATH%\ncs\v2.8.0\zephyr\submanifests\example.yaml
   ```

   ```yaml
   manifest:
     projects:
       - name: nce-sdk
         url: https://github.com/1NCE-GmbH/1nce-iot-c-sdk
         revision: main
   ```

4. **Run West update:**

   Open `cmd.exe` and run:

   ```bash
   cd %HOMEPATH%\ncs\v2.8.0
   west update
   ```

---

## ▶️ Running the Demo

1. **Clone the Blueprint Repository:**

   ```bash
   git clone https://github.com/1NCE-GmbH/blueprint-zephyr.git
   ```

2. **Open VS Code and Launch nRF Connect Extension**

3. **Add the project:**
   - Click **Add Existing Application**
   - Choose the folder where the blueprint was cloned

4. **Create a Build Configuration:**
   - Select your board target, e.g.:
     - `nrf9160dk/nrf9160/ns`
     - `nrf9151dk/nrf9151/ns`
     - `thingy91/nrf9160/ns`

5. **Flash the board:**
   - Connect your board via USB
   - Click **Flash** or **Debug** to deploy the firmware

📖 Need help with board connection?  
👉 [Nordic Docs: Connect Using Serial Port](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/bd_work_with_boards.html#how-to-connect-using-serial-port)

---

## 🧪 Testing Instructions for Thingy:91

To easily test the default setup on the **Thingy:91**, follow these steps using the provided binaries for the specific demo you'd like to run:

1. **Remove the plastic cover** from the Thingy:91.
2. **Connect the device to your computer** using a micro-USB cable.
3. **Enter DFU mode**:
   - Power off the Thingy:91.
   - Hold down the **black button** while switching the power back to **ON**.
4. **Open [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop)** and launch the **Programmer** tool.
5. Click **SELECT DEVICE** and choose **Thingy:91** from the dropdown list.
6. In the left panel, go to **File > Add file > Browse** and choose the appropriate `.hex` file from the `thingy_binaries` folder of your desired demo.
7. Scroll down to **Enable MCUboot** and ensure it is checked.
8. Click **Write** on the left panel, then confirm again in the **MCUboot DFU** pop-up by pressing **Write**.
9. Wait for the update to finish. A message saying **"Completed successfully"** will confirm a successful flash.

### 📂 Available Firmware for Thingy:91

- [🔐 CoAP Demo Firmware](./nce_coap_demo/thingy_binaries)
- [📡 UDP Demo Firmware](./nce_udp_demo/thingy_binaries)
- [📥 FOTA with Mender Firmware](./plugin_system/nce_fota_mender_demo/thingy_binaries)
- [🛠️ Debug with Memfault Firmware](./plugin_system/nce_debug_memfault_demo/thingy_binaries)

> 💡 **Note:** For Memfault diagnostics and debugging, you should upload [zephyr.elf](./plugin_system/nce_debug_memfault_demo/thingy_binaries/zephyr.elf) file to the Memfault portal.  
> Refer to the [Memfault documentation](https://docs.memfault.com) for instructions on setting up symbol files and debugging integration.
> For a faster getting started experience, you can directly use the documentation under [`plugin_system/nce_debug_memfault_demo`](./plugin_system/nce_debug_memfault_demo).


📘 For more detailed device guidance, check the official [Thingy:91 Getting Started Guide](https://docs.nordicsemi.com/bundle/ncs-2.6.1/page/nrf/device_guides/working_with_nrf/nrf91/thingy91_gsg.html)

---

## 📚 Demos in This Blueprint

The blueprint includes the following applications:

| Demo Path                               | Summary                                                                                   |
|----------------------------------------|-------------------------------------------------------------------------------------------|
| `nce_coap_demo`                         | Secure CoAP communication using DTLS from Device Authenticator, with Energy Saver & Device Controller support |
| `nce_udp_demo`                          | Lightweight UDP communication with compressed payloads and Device Controller |
| `nce_lwm2m_demo`                        | LwM2M client for LED, buzzer, and sensor control over CoAP/DTLS with support for 1NCE Action API |
| `plugin_system/nce_debug_memfault_demo`| Device diagnostics and crash reporting using Memfault over the 1NCE CoAP Proxy           |
| `plugin_system/nce_fota_mender_demo`   | Firmware-over-the-air updates via Mender.io using the 1NCE CoAP Proxy and secure onboarding |


---

## 🆘 Need Help?

Open an issue on GitHub for:

- ❗ Bug reports  
- 🚀 Feature requests  
- 📝 Documentation issues  
- ❓ General questions  

👉 [Create a new issue](https://github.com/1NCE-GmbH/blueprint-zephyr/issues/new/choose)

---

Made with 💙 by the 1NCE Team.
