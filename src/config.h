#pragma once
#include "types.h"

#include <filesystem>

namespace vrperfkit {
	struct UpscaleConfig {
		bool enabled = false;
		UpscaleMethod method = UpscaleMethod::NIS;
		float renderScale = 0.866f;
		float sharpness = 0.20f;
		float radius = 1.00f;
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
		FixedFoveatedMethod method = FixedFoveatedMethod::VRS;
		float innerRadius = 0.70f;
		float midRadius = 0.85f;
		float outerRadius = 1.00f;
		bool favorHorizontal = true;
		std::string overrideSingleEyeOrder;
	};

	struct Config {
		UpscaleConfig upscaling;
		DxvkConfig dxvk;
		FixedFoveatedConfig ffr;
		bool debugMode = false;
		std::string dllLoadPath = "";
	};

	extern Config g_config;

	void LoadConfig(const std::filesystem::path &configPath);
	void PrintCurrentConfig();
}
