#include "config.h"

#include "logging.h"
#include "yaml-cpp/yaml.h"

#include <fstream>

namespace fs = std::filesystem;

namespace vrperfkit {
	UpscaleMethod MethodFromString(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		if (s == "fsr") {
			return UpscaleMethod::FSR;
		}
		if (s == "nis") {
			return UpscaleMethod::NIS;
		}
		if (s == "cas") {
			return UpscaleMethod::CAS;
		}
		LOG_INFO << "Unknown upscaling method " << s << ", defaulting to NIS";
		return UpscaleMethod::NIS;
	}

	std::string MethodToString(UpscaleMethod method) {
		switch (method) {
		case UpscaleMethod::FSR:
			return "FSR";
		case UpscaleMethod::NIS:
			return "NIS";
		case UpscaleMethod::CAS:
			return "CAS";
		}
	}

	FixedFoveatedMethod FFRMethodFromString(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		if (s == "vrs") {
			return FixedFoveatedMethod::VRS;
		}
		LOG_INFO << "Unknown fixed foveated method " << s << ", defaulting to VRS";
		return FixedFoveatedMethod::VRS;
	}

	std::string FFRMethodToString(FixedFoveatedMethod method) {
		switch (method) {
		case FixedFoveatedMethod::VRS:
			return "VRS";
		}
	}

	GameMode GameModeFromString(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		if (s == "auto") {
			return GameMode::AUTO;
		}
		if (s == "single") {
			return GameMode::GENERIC_SINGLE;
		}
		if (s == "left") {
			return GameMode::LEFT_EYE_FIRST;
		}
		if (s == "right") {
			return GameMode::RIGHT_EYE_FIRST;
		}
		LOG_INFO << "Unknown HRM Mode " << s << ", defaulting to auto";
		return GameMode::AUTO;
	}

	std::string GameModeToString(GameMode mode) {
		switch (mode) {
		case GameMode::AUTO:
			return "auto";
		case GameMode::GENERIC_SINGLE:
			return "single";
		case GameMode::LEFT_EYE_FIRST:
			return "left";
		case GameMode::RIGHT_EYE_FIRST:
			return "right";
		}
	}

	std::string PrintToggle(bool toggle) {
		return toggle ? "enabled" : "disabled";
	}

	Config g_config;

	void LoadConfig(const fs::path &configPath) {
		g_config = Config();

		if (!exists(configPath)) {
			LOG_ERROR << "Config file not found, falling back to defaults";
			return;
		}

		try {
			std::ifstream cfgFile (configPath);
			YAML::Node cfg = YAML::Load(cfgFile);

			YAML::Node upscaleCfg = cfg["upscaling"];
			UpscaleConfig &upscaling= g_config.upscaling;
			upscaling.enabled = upscaleCfg["enabled"].as<bool>(upscaling.enabled);
			upscaling.method = MethodFromString(upscaleCfg["method"].as<std::string>(MethodToString(upscaling.method)));
			upscaling.renderScale = sqrt(upscaleCfg["renderScale"].as<float>(upscaling.renderScale) / 100.00f);
			if (upscaling.renderScale < 0.5f) {
				LOG_INFO << "Setting render scale to minimum value of 25%";
				upscaling.renderScale = 0.5f;
			}
			upscaling.sharpness = std::max(0.f, upscaleCfg["sharpness"].as<float>(upscaling.sharpness));
			upscaling.radius = std::max(0.f, upscaleCfg["radius"].as<float>(upscaling.radius));
			upscaling.applyMipBias = upscaleCfg["applyMipBias"].as<bool>(upscaling.applyMipBias);

			YAML::Node dxvkCfg = cfg["dxvk"];
			DxvkConfig &dxvk = g_config.dxvk;
			dxvk.enabled = dxvkCfg["enabled"].as<bool>(dxvk.enabled);
			dxvk.dxgiDllPath = dxvkCfg["dxgiDllPath"].as<std::string>(dxvk.dxgiDllPath);
			dxvk.d3d11DllPath = dxvkCfg["d3d11DllPath"].as<std::string>(dxvk.d3d11DllPath);

			YAML::Node ffrCfg = cfg["fixedFoveated"];
			FixedFoveatedConfig &ffr = g_config.ffr;
			ffr.enabled = ffrCfg["enabled"].as<bool>(ffr.enabled);
			ffr.apply = ffr.enabled;
			ffr.favorHorizontal = ffrCfg["favorHorizontal"].as<bool>(ffr.favorHorizontal);
			ffr.innerRadius = ffrCfg["innerRadius"].as<float>(ffr.innerRadius);
			ffr.midRadius = ffrCfg["midRadius"].as<float>(ffr.midRadius);
			ffr.outerRadius = ffrCfg["outerRadius"].as<float>(ffr.outerRadius);
			ffr.preciseResolution = ffrCfg["preciseResolution"].as<bool>(ffr.preciseResolution);
			ffr.ignoreFirstTargetRenders = ffrCfg["ignoreFirstTargetRenders"].as<int>(ffr.ignoreFirstTargetRenders);
			ffr.maxRadius = ffr.innerRadius;
			ffr.overrideSingleEyeOrder = ffrCfg["overrideSingleEyeOrder"].as<std::string>(ffr.overrideSingleEyeOrder);
			ffr.fastMode = ffrCfg["fastMode"].as<bool>(ffr.fastMode);
			ffr.dynamic = ffrCfg["dynamic"].as<bool>(ffr.dynamic);
			ffr.targetFrameTime = 1.f / ffrCfg["targetFPS"].as<float>(ffr.targetFrameTime);
			ffr.marginFrameTime = 1.f / ffrCfg["marginFPS"].as<float>(ffr.marginFrameTime);
			ffr.dynamicChangeRadius= ffrCfg["dynamicChangeRadius"].as<bool>(ffr.dynamicChangeRadius);
			ffr.minRadius = ffrCfg["minRadius"].as<float>(ffr.minRadius);
			ffr.increaseRadiusStep = ffrCfg["increaseRadiusStep"].as<float>(ffr.increaseRadiusStep);
			ffr.decreaseRadiusStep = ffrCfg["decreaseRadiusStep"].as<float>(ffr.decreaseRadiusStep);

			YAML::Node hiddenMaskCfg = cfg["hiddenMask"];
			HiddenRadialMask &hiddenMask= g_config.hiddenMask;
			hiddenMask.enabled = hiddenMaskCfg["enabled"].as<bool>(hiddenMask.enabled);
			hiddenMask.radius = std::max(0.f, hiddenMaskCfg["radius"].as<float>(hiddenMask.radius));
			hiddenMask.maxRadius = hiddenMask.radius;
			hiddenMask.preciseResolution = hiddenMaskCfg["preciseResolution"].as<bool>(hiddenMask.preciseResolution);
			hiddenMask.ignoreFirstTargetRenders = hiddenMaskCfg["ignoreFirstTargetRenders"].as<int>(hiddenMask.ignoreFirstTargetRenders);
			hiddenMask.dynamic = hiddenMaskCfg["dynamic"].as<bool>(hiddenMask.dynamic);
			hiddenMask.targetFrameTime = 1.f / hiddenMaskCfg["targetFPS"].as<float>(hiddenMask.targetFrameTime);
			hiddenMask.marginFrameTime = 1.f / hiddenMaskCfg["marginFPS"].as<float>(hiddenMask.marginFrameTime);
			hiddenMask.dynamicChangeRadius = hiddenMaskCfg["dynamicChangeRadius"].as<bool>(hiddenMask.dynamicChangeRadius);
			hiddenMask.minRadius = hiddenMaskCfg["minRadius"].as<float>(hiddenMask.minRadius);
			hiddenMask.increaseRadiusStep = hiddenMaskCfg["increaseRadiusStep"].as<float>(hiddenMask.increaseRadiusStep);
			hiddenMask.decreaseRadiusStep = hiddenMaskCfg["decreaseRadiusStep"].as<float>(hiddenMask.decreaseRadiusStep);

			g_config.debugMode = cfg["debugMode"].as<bool>(g_config.debugMode);

			g_config.dllLoadPath = cfg["dllLoadPath"].as<std::string>(g_config.dllLoadPath);

			g_config.gameMode = GameModeFromString(cfg["gameMode"].as<std::string>(GameModeToString(g_config.gameMode)));
			g_config.dynamicFramesCheck = cfg["dynamicFramesCheck"].as<int>(g_config.dynamicFramesCheck);
			if (g_config.dynamicFramesCheck < 1) {
				g_config.dynamicFramesCheck = 1;
			}
		}
		catch (const YAML::Exception &e) {
			LOG_ERROR << "Failed to load configuration file: " << e.msg;
		}
	}

	void PrintCurrentConfig() {
		LOG_INFO << "Current configuration:";
		LOG_INFO << "  Upscaling is " << PrintToggle(g_config.upscaling.enabled);
		if (g_config.upscaling.enabled) {
			LOG_INFO << "    * Method:        " << MethodToString(g_config.upscaling.method);
			LOG_INFO << "    * Render scale:  " << std::setprecision(6) << g_config.upscaling.renderScale * g_config.upscaling.renderScale * 100 << "%";
			LOG_INFO << "    * Render factor: " << std::setprecision(6) << g_config.upscaling.renderScale;
			LOG_INFO << "    * Sharpness:     " << std::setprecision(6) << g_config.upscaling.sharpness;
			LOG_INFO << "    * Radius:        " << std::setprecision(6) << g_config.upscaling.radius;
			LOG_INFO << "    * MIP bias:      " << PrintToggle(g_config.upscaling.applyMipBias);
		}
		LOG_INFO << "  Game Mode:         " << GameModeToString(g_config.gameMode);
		if ((g_config.ffr.enabled && g_config.ffr.dynamic) || (g_config.hiddenMask.enabled && g_config.hiddenMask.dynamic)) {
			LOG_INFO << "  Dynamic Frames Check:  " << std::setprecision(6) << g_config.dynamicFramesCheck;
		}
		LOG_INFO << "  Fixed foveated rendering is " << PrintToggle(g_config.ffr.enabled);
		if (g_config.ffr.enabled) {
			LOG_INFO << "    * Method:        " << FFRMethodToString(g_config.ffr.method);
			LOG_INFO << "    * Inner radius:  " << std::setprecision(6) << g_config.ffr.innerRadius;
			LOG_INFO << "    * Mid radius:    " << std::setprecision(6) << g_config.ffr.midRadius;
			LOG_INFO << "    * Outer radius:  " << std::setprecision(6) << g_config.ffr.outerRadius;
			LOG_INFO << "    * Precise res:   " << PrintToggle(g_config.ffr.preciseResolution);
			LOG_INFO << "    * No renders:    " << std::setprecision(6) << g_config.ffr.ignoreFirstTargetRenders;
			if (!g_config.ffr.overrideSingleEyeOrder.empty()) {
				LOG_INFO << "    * Eye order:     " << g_config.ffr.overrideSingleEyeOrder;
			}
			LOG_INFO << "    * Fast mode:     " << PrintToggle(g_config.ffr.fastMode);
			LOG_INFO << "    * Dynamic:       " << PrintToggle(g_config.ffr.dynamic);
			if (g_config.ffr.dynamic) {
				LOG_INFO << "      * Target FPS:  " << std::setprecision(6) << (1.f / g_config.ffr.targetFrameTime);
				LOG_INFO << "      * Target FT:   " << std::setprecision(6) << (g_config.ffr.targetFrameTime * 1000.f) << "ms";
				LOG_INFO << "      * Margin FPS:  " << std::setprecision(6) << (1.f / g_config.ffr.marginFrameTime);
				LOG_INFO << "      * Margin FT:   " << std::setprecision(6) << (g_config.ffr.marginFrameTime * 1000.f) << "ms";
				LOG_INFO << "      * Change radius is " << PrintToggle(g_config.ffr.dynamicChangeRadius);
				if (g_config.ffr.dynamicChangeRadius) {
					LOG_INFO << "      * Min radius: " << std::setprecision(6) << g_config.ffr.minRadius;
					LOG_INFO << "      * Inc radius: " << std::setprecision(6) << g_config.ffr.increaseRadiusStep;
					LOG_INFO << "      * Dec radius: " << std::setprecision(6) << g_config.ffr.decreaseRadiusStep;
				}
			}
		} else {
			g_config.ffr.dynamic = false;
		}
		LOG_INFO << "  Hidden radial mask is " << PrintToggle(g_config.hiddenMask.enabled);
		if (g_config.hiddenMask.enabled) {
			LOG_INFO << "    * Radius:        " << std::setprecision(6) << g_config.hiddenMask.radius;
			LOG_INFO << "    * Precise res:   " << PrintToggle(g_config.hiddenMask.preciseResolution);
			LOG_INFO << "    * No renders:    " << std::setprecision(6) << g_config.hiddenMask.ignoreFirstTargetRenders;
			LOG_INFO << "    * Dynamic:       " << PrintToggle(g_config.hiddenMask.dynamic);
			if (g_config.hiddenMask.dynamic) {
				LOG_INFO << "      * Target FPS:  " << std::setprecision(6) << (1.f / g_config.hiddenMask.targetFrameTime);
				LOG_INFO << "      * Target FT:   " << std::setprecision(6) << (g_config.hiddenMask.targetFrameTime * 1000.f) << "ms";
				LOG_INFO << "      * Margin FPS:  " << std::setprecision(6) << (1.f / g_config.hiddenMask.marginFrameTime);
				LOG_INFO << "      * Margin FT:   " << std::setprecision(6) << (g_config.hiddenMask.marginFrameTime * 1000.f) << "ms";
				LOG_INFO << "      * Change radius is " << PrintToggle(g_config.hiddenMask.dynamicChangeRadius);
				if (g_config.hiddenMask.dynamicChangeRadius) {
					LOG_INFO << "       - Min radius: " << std::setprecision(6) << g_config.hiddenMask.minRadius;
					LOG_INFO << "       - Inc radius: " << std::setprecision(6) << g_config.hiddenMask.increaseRadiusStep;
					LOG_INFO << "       - Dec radius: " << std::setprecision(6) << g_config.hiddenMask.decreaseRadiusStep;
				}
			}
		} else {
			g_config.hiddenMask.dynamic = false;
		}
		LOG_INFO << "  Debug mode is " << PrintToggle(g_config.debugMode);
		FlushLog();
	}
}
