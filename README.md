# OpenXilEdu

OpenXilEdu is a free educational project built on [OpenXilEnv](https://github.com/eclipse-openxilenv/openxilenv). It bundles a real EV powertrain simulation (battery, motor, transmission, vehicle dynamics) with an Arduino accelerator pedal and live 3D visualization ([esmini](https://github.com/esmini/esmini)). Turn a potentiometer, and watch it drive a car around an oval track. It is intended for students, for parents looking for a weekend project to build with their kid, and for anyone who has ever wondered what more you could do with an Arduino than blink an LED.

<video src="https://github.com/user-attachments/assets/ddbf42f4-78ef-4cbf-aabf-934dc002f94f" controls width="640"></video>

**No Arduino? Start with Stage 0 anyway.** It runs on your PC alone, and once you've seen the car move, you'll know whether the Arduino is worth ordering.

The project is structured in three stages:

- **Stage 0 — Drive the car, no hardware needed.** Download the ZIP, unzip it, double-click one file, and you're driving. Nothing to build, nothing to install, about five minutes.
- **Stage 1 — Add the pedal.** The heart of the project. Swap the on-screen slider for a real potentiometer wired to an Arduino, and drive the same simulation with your hand. The only new work is flashing one small sketch — no soldering, the parts just plug together.
- **Stage 2 — Make it yours.** Add a brake pedal, wire up a real ignition switch, or open the powertrain model and change how the car drives. Everything here is yours to take apart.

> **Windows x64 only for now.** A Linux release is planned and will follow.

> OpenXilEdu is an independent community project. It is not an official Eclipse Foundation or ZF Friedrichshafen AG deliverable.

## Stage 0 — Drive the car, no hardware needed

### What you need

**Hardware:**
- A PC running Windows (64-bit)

### Download and unzip

Grab [`OpenXilEdu-Windows-x64-v1.0.0.zip`](https://github.com/eddy-brde/openxiledu/releases/download/v1.0.0/OpenXilEdu-Windows-x64-v1.0.0.zip).

Unzip it to `C:\OpenXilEdu\`. Another folder is fine, but avoid `C:\Program Files\` (Windows blocks regular apps from writing there) and your Desktop or Documents folder if it's backed up by OneDrive, which many newer Windows setups turn on by default — the simulation writes your window layout back into its own configuration files every time it closes, and both can silently get in the way of that.

### Start the simulation

Double-click **`start_stage0.bat`** in the `OpenXilEdu` folder.

Windows will probably show you a blue **"Windows protected your PC"** box. That's only because I haven't paid for a code-signing certificate — click **More info**, then **Run anyway**. You'll see it once and never again.

You should get two windows: the **OpenXilEnv GUI**, full of sliders and gauges, and the **esmini 3D viewer**, with a white car sitting on an oval track.

### Drive

The car is switched off, so the accelerator won't do anything yet. Find the small **Enum Window** in the top left and set **`FireUp`** to **On** — that's the ignition.

Now drag the **`AcceleratorPosition`** slider up.

Watch the car pull away, and keep half an eye on the gauges while you do it. Nothing there is animation. The speed comes out of a real force balance — drag, rolling resistance, motor torque — and the battery is genuinely being drained to produce it. `BrakePedal` brings you back down again.

## Stage 1 — Add the pedal

### What you need

**No soldering, and no electronics experience.** Everything plugs together with jumper wires, and you can take it all apart again afterwards.

**Hardware:**
- A PC running Windows (64-bit)
- An Arduino Uno, or a compatible clone
- A potentiometer (any linear pot, e.g. 10k, works)
- A few jumper wires
- A USB cable

**Software:**
- The Arduino IDE, to flash the sketch
- If you're using a low-cost clone board built on the **ATmega328PB** chip (like the JOY-IT ARD-ONE-C-MC), you'll also need the **MiniCore** board package by MCUdude — the standard Arduino AVR core doesn't know this chip and will refuse to flash it. A genuine Arduino Uno, which is what I used to build and film this project, doesn't need this extra step.

### Wire the potentiometer

A potentiometer has three pins. Push a jumper wire onto each one and connect them like this:

| Pot pin | Connect to |
|---|---|
| One outer pin | **5V** |
| Other outer pin | **GND** |
| Middle pin (the wiper) | **A0** |

Which outer pin goes to 5V and which to GND doesn't matter — swapping them just flips which way you turn the knob for more throttle. Just don't let the 5V and GND wires touch each other directly; that's a short circuit.

That's the only wiring you need. The ignition indicator uses the Arduino's own built-in LED (the one labeled "L", next to pin 13) — the simulation sends the `FireUp` state back down to the Arduino over the same USB cable, so no extra parts or wiring are needed for it to light up.

<!-- TODO: photo or Fritzing diagram of the wiring -->

### Flash the sketch

Open `SerialBridge/SerialBridge.ino` (it's in the same ZIP) in the Arduino IDE, pick your board and port, and upload.

If you get an `avrdude` signature mismatch error, first double-check you've selected the right board and port in the Arduino IDE's Tools menu. If that's already correct and you're on a low-cost clone board, it's likely built on the ATmega328PB chip, which the standard Arduino core doesn't recognize — if that is the case, install MiniCore (Boards Manager → search "MiniCore"), then choose **MiniCore → ATmega328** and set **Variant** to **328PB**.

### Plug in the Arduino and start the simulation

1. Connect the Arduino to your PC by USB.
2. Double-click **`start_stage1.bat`**.
3. Set **`FireUp`** to **On** in the Enum Window, same as in Stage 0. The Arduino's built-in LED lights up now.
4. Turn the pot — and off you go.

You don't have to pick a COM port. I've made the bridge scan your serial ports and find the Arduino by itself, and it keeps retrying if the board isn't plugged in yet, so you can connect it before or after starting the simulation. If it ever picks the wrong device, set the environment variable `XILEDU_ARDUINO_PORT` to the port you want (`COM5`, for example) before starting.

Here's the part worth stopping on. The powertrain model is exactly the same as it was in Stage 0 — not recompiled, not reconfigured. It reads the same `AcceleratorPosition` signal it always did, and it has no idea that number now comes from your hand instead of a slider. That's the whole lesson of this stage, and it's how real vehicle development works: swap a simulated input for a real one, and the model never notices.

## Stage 2 — Make it yours

### What you need

This stage is open-ended — what you need depends on what you want to change. At minimum:

- A C++ compiler, CMake, and a generator such as Ninja, to rebuild whichever piece you modify
- The [OpenXilEnv](https://github.com/eclipse-openxilenv/openxilenv) source checked out alongside this repo (`ExtProc_3DViewer` and `ExtProc_ArduinoSerialBridge` both expect a sibling `openxilenv` checkout — see `OPENXILENV_SRC_DIR` at the top of each `CMakeLists.txt`)
- Whatever extra hardware your idea needs — a second potentiometer, a switch, another LED

<!-- TODO: a proper build guide (BUILDING.md?) belongs here once one exists - for
     now, the CMakeLists.txt in ExtProc_3DViewer/ and ExtProc_ArduinoSerialBridge/
     are the source of truth for how to configure and build each one. -->

### Where to look

- **The powertrain itself** (battery, motor, transmission, vehicle dynamics) isn't in this repo — it lives upstream in [OpenXilEnv](https://github.com/eclipse-openxilenv/openxilenv), under `Samples/ExternalProcesses/`. `ExtProc_VehicleModel/ExtProc_VehicleModel.c` is the one that turns pedal input into `Speed`, so start there if you want to change the car's physics.
- **The Arduino sketch**: `SerialBridge/SerialBridge.ino` in this repo. Right now it reads one potentiometer and drives the board's built-in LED.
- **The PC-side bridge**: `ExtProc_ArduinoSerialBridge/ExtProc_ArduinoSerialBridge.cpp`. It parses what the sketch sends and hands it to OpenXilEnv as blackboard signals like `AcceleratorPosition`.
- **The 3D viewer**: `ExtProc_3DViewer/ExtProc_3DViewer.cpp`, if you'd rather change how the car looks than how it drives.

### Ideas to get started

- **Add a brake pedal on a second pot.** Read a second analog pin in the sketch, send it alongside `PedalPos:`, and have the bridge expose it as `BrakePedal`. That signal is already a real input to the vehicle model — it's just only ever been driven from the GUI slider so far.
- **Replace the `FireUp` GUI button with a physical switch.** This one's more interesting than it sounds. Today `FireUp` only travels *from* the PC *to* the Arduino, to light the LED. A real ignition switch means the sketch has to read a digital pin and report it back, so you'll be extending the protocol in the other direction too.
- **Change how the car handles.** Try a different `CarWeight` or `WheelRadius` in `ExtProc_VehicleModel.c` and see what it does to the feel of it.

## Troubleshooting

**Windows says it protected your PC.**
The binaries aren't code-signed. Click **More info** → **Run anyway**.

**The car won't move.**
Check that `FireUp` is set to **On** in the Enum Window. Without it, the accelerator does nothing.

**Turning the pot does nothing (Stage 1).**
Make sure the board is flashed and connected. Open the Arduino IDE's Serial Monitor at 115200 baud — you should see a stream of `PedalPos:` lines that change as you turn the pot. Close the Serial Monitor again before starting the simulation, since only one program can hold the port at a time.

**The windows are in a mess and I can't get them back.**
The simulation saves your window layout when it closes. To start over, copy the matching file from `Configurations\original\` over the one in `Configurations\`.

**Something else going wrong?**
[Open an issue](https://github.com/eddy-brde/openxiledu/issues) — tell me what stage you're on and what you're seeing, and I'll try to help sort it out.

## License

OpenXilEdu's own code is licensed under the Apache License 2.0 — see [LICENSE](LICENSE).

The release package redistributes OpenXilEnv (Apache-2.0), esmini (MPL-2.0), Qt (LGPLv3), Boost (BSL-1.0) and the MinGW-w64/GCC runtime. See `THIRD-PARTY-NOTICES.txt` and the `licenses/` folder in the package for full details.
