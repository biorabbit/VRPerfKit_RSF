An Updated Branch from VRPerfKit_RSF optimized for VaM VR
=========================================

This is an updated fork from [VRPerfKit_RSF](https://github.com/RavenSystem/VRPerfKit_RSF). The original repo hasn't been updated since Dec 2023.

This fork mainly focuses on the following improvements:

* Adding the vertical offset feature provided by [erbarratt](https://github.com/erbarratt).
* Fixing functionality problem due to an update of Meta Quest Link app (provided by [mledour](https://github.com/mledour) [issue](https://github.com/RavenSystem/VRPerfKit_RSF/pull/8))
* Fixing project building problem.

Additionally, the default configuration file has been optimized for VaM, when using the following system:
* Meta Quest 3 headset
* RTX 4080 Super GPU
* Virtual Desktop connection (GodLike quality at 72-90 fps).

Specifically, the following default options were set:
* upscaling: false for no loss of graphic quality.
* verticalOffset: 0.1 to improve render resolution at the center.
* innerRadius: 0.3 for a balance between performance and quality.
* overrideSingleEyeOrder: RL: Don't change this if you are VaM user using VD!
  
You can play with the config file if you have other system specs or play with other games.

## Installation (VaM)

Extract `dxgi.dll` and `vrperfkit_RSF.yml` next to the game's main executable (VaM.exe).

(Optional) Edit the `vrperfkit_RSF.yml` config file to your heart's content. 

## Build instructions

Clone the repository and init all submodules.

```
git clone https://github.com/biorabbit/VRPerfKit_RSF.git
cd VRPerfKit_RSF
git submodule init
git submodule update --recursive
```

Download the [Oculus SDK](https://developer.oculus.com/downloads/package/oculus-sdk-for-windows)
and extract `LibOVR` from the downloaded archive to the `ThirdParty` folder.

Download [NVAPI](https://github.com/NVIDIA/nvapi/tree/main) and extract the contents to `ThirdParty\nvapi`.

Run cmake to generate Visual Studio solution files. Build with Visual Studio. Note: Ninja does not work,
due to the included shaders that need to be compiled. This is only supported with VS solutions.
