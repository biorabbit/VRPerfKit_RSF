#pragma once
#include "types.h"

#include <filesystem>

namespace vrperfkit {
	struct UpscaleConfig {
		bool enabled = false;
		UpscaleMethod method = UpscaleMethod::NIS;
		float renderScale = 0.83667f;
		float sharpness = 0.30f;
		float radius = 0.95f;
		bool applyMipBias = true;
	};

	struct DxvkConfig {
		bool enabled = false;
		std::string dxgiDllPath = "dxvk\\dxgi.dll";
		std::string d3d11DllPath = "dxvk\\d3d11.dll";

		// not actually a config option, but a real-time toggle hack...
		bool shouldUseDxvk = true;
	};

	struct FixedFoveatedConfig {
		bool enabled = false;
		bool apply = false;
		FixedFoveatedMethod method = FixedFoveatedMethod::VRS;
		float innerRadius = 0.50f;
		float midRadius = 0.65f;
		float outerRadius = 0.80f;
		bool favorHorizontal = true;
		std::string overrideSingleEyeOrder;
		bool fastMode = false;
		bool dynamic = false;
		bool dynamicChangeRadius = false;
		float targetFrameTime = 0.0167f;
		float marginFrameTime = 0.f;
		float minRadius = 0.30f;
		float maxRadius = 1.15f;
		float decreaseRadiusStep = 0.01f;
		float increaseRadiusStep = 0.03f;
		bool preciseResolution = true;
		int ignoreFirstTargetRenders = 0;
		bool radiusChanged[2] = { true, true };
	};

	struct HiddenRadialMask {
		bool enabled = false;
		float radius = 1.15f;
		bool dynamic = false;
		bool dynamicChangeRadius = false;
		float targetFrameTime = 0.0167f;
		float marginFrameTime = 0.f;
		float minRadius = 0.8f;
		float maxRadius = 1.15f;
		float decreaseRadiusStep = 0.01f;
		float increaseRadiusStep = 0.03f;
		bool preciseResolution = true;
		int ignoreFirstTargetRenders = 0;
	};

	struct Config {
		UpscaleConfig upscaling;
		DxvkConfig dxvk;
		GameMode gameMode = GameMode::AUTO;
		bool renderingSecondEye = false;
		int ffrDepthClearCount = 0;
		FixedFoveatedConfig ffr;
		HiddenRadialMask hiddenMask;
		bool debugMode = false;
		std::string dllLoadPath = "";
		int dynamicFramesCheck = 1;
	};

	extern Config g_config;

	void LoadConfig(const std::filesystem::path &configPath);
	void PrintCurrentConfig();
}
